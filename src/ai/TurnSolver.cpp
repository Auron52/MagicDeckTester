#include "../core/EnvFlags.h"
#include "TurnSolver.h"
#include "ManaPayment.h"
#include "PlanContext.h"
#include "LandPlay.h"
#include "Combat.h"
#include "EngineFlags.h"
#include "TranspositionTable.h"
#include "KeepModel.h"              // MidGameEvaluator / ExtractMidGameFeatures (learned d0 eval)
#include "Profiler.h"
#include "../core/ManaPool.h"
#include "../core/EffectHandler.h"
#include "../core/SpellEffects.h"
#include "../core/Trace.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <iostream>
#include <sstream>
#include <fstream>
#include <mutex>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// When true, SolveWithLookahead prints per-pass per-candidate win turn estimates
// for top-level T1 pre-combat decisions.  Set via TurnSolver::SetTraceSolve().
static bool s_trace_solve = false;

// Fidelity of the full-depth search's BEYOND-HORIZON leaf estimate (FSLineWin's
// `depth<=0` tail). This is the policy that ranks plans whose payoff lies past the
// searched horizon -- e.g. a combo deck (Treasure Hunt) whose lethal turn is several
// turns out. A greedy depth-0 rollout (the original value) develops such lines too
// weakly: it mis-ranked TH's setup turn and predicted a ~turn-19 win where baseline
// (whose own leaf rolls out WITH lookahead, sub_depth up to depth-1) reached turn 5.
// A 1-ply lookahead leaf fixes this -- it matches baseline's rollout fidelity closely
// enough to rank combo setup correctly, and across slivers/burn/TH at d3 and d5 it
// makes the committed-line search BEAT baseline. We cap it at 1 rather than the full
// depth-1: a deeper leaf is run at EVERY node of the B^depth tree (not just the root
// like baseline) and so blows the per-decision budget -- a depth-2 leaf measured ~11x
// slower for only a marginal further gain. Env-overridable as the active tuning lever
// for the leaf estimator (it is the slot a learned eval will eventually replace -- see
// project-search-distillation). See project-full-depth-search (TH leaf-depth finding).
static const char* s_fd_leaf_depth_env = std::getenv("MTG_FD_LEAF_DEPTH");
static const int   s_fd_leaf_depth     = s_fd_leaf_depth_env ? std::atoi(s_fd_leaf_depth_env) : 1;

// Deterministic rollout-cost telemetry (MTG_ROLLOUT_STATS): total SimulateToEnd calls + simulated
// turn-steps this process. A CONTENTION-PROOF measure of rollout work (wall-clock is not, under shared
// machine load), so truncated-rollout (MTG_ROLLOUT_HORIZON) speedups can be read as a step-count ratio.
// Flag-gated so the shared counters never touch the rollout hot loop (cross-thread atomic contention) when off.
static const bool             s_rollout_stats = EnvOn("MTG_ROLLOUT_STATS");
static std::atomic<long long> g_rollout_calls{0};
static std::atomic<long long> g_rollout_steps{0};
// Interior-node telemetry (same MTG_ROLLOUT_STATS gate): the number of full-depth search
// interior nodes = plans applied (EnumeratePlansWithLand + ApplyPlanDirect + a GameState
// copy per node). Reported next to turn_steps so the interior fraction
// = interior / (interior + turn_steps) bounds how much any interior-reuse cache could
// save on the escalation. See docs/design/escalation-interior-reuse.md.
static std::atomic<long long> g_interior_nodes{0};
static std::atomic<long long> g_interior_nodes_esc{0};   // interior nodes done under the heuristic escalation
// Matched-depth escalation measurement (MTG_ESC_MEASURE): per escalation, the ladder's work units
// (budget->Used() delta) vs a COLD single pass at the ladder's ACTUAL committed depth (fresh caches).
// This is the apples-to-apples "skip earlier depths" test -- same target depth, ladder vs single-pass.
static const bool             s_esc_measure = EnvOn("MTG_ESC_MEASURE");
static std::atomic<long long> g_esc_ladder_units{0};
static std::atomic<long long> g_esc_single_units{0};
static std::atomic<long long> g_esc_measure_n{0};
// LOSSLESS audit (MTG_ESC_PREDICT_AUDIT): per escalation, shadow-run the ladder and compare its committed
// depth to the predictor's -- count LOSSY (predict shallower = quality risk) vs deeper vs exact, and dump the
// first few lossy cases so we can see WHY the estimate under-shot. Doubles escalation work (audit only).
static const bool             s_esc_predict_audit = EnvOn("MTG_ESC_PREDICT_AUDIT");
static std::atomic<long long> g_pred_audit_n{0}, g_pred_lossy{0}, g_pred_deeper{0}, g_pred_lossy_dumped{0};
namespace
{
    struct RolloutStatsReporter
    {
        ~RolloutStatsReporter()
        {
            if (!s_rollout_stats) { return; }
            const long long c = g_rollout_calls.load(), s = g_rollout_steps.load();
            const long long in = g_interior_nodes.load(), ine = g_interior_nodes_esc.load();
            const long long tot = in + s;
            std::cerr << "[rollout-stats] calls=" << c << " turn_steps=" << s
                      << " steps_per_call=" << (c ? static_cast<double>(s) / c : 0.0) << "\n";
            std::cerr << "[rollout-stats] interior_nodes=" << in
                      << " interior_frac=" << (tot ? static_cast<double>(in) / tot : 0.0)
                      << " (of interior+turn_steps)\n";
            std::cerr << "[rollout-stats] interior_esc=" << ine
                      << " esc_share=" << (in ? static_cast<double>(ine) / in : 0.0)
                      << " (interior nodes re-traversed by the heuristic escalation)\n";
        }
    };
    RolloutStatsReporter g_rollout_stats_reporter;

    struct EscPredictReporter
    {
        ~EscPredictReporter()
        {
            if (!s_esc_predict_audit) { return; }
            if (g_pred_audit_n.load() > 0)
            {
                const long long an = g_pred_audit_n.load(), lo = g_pred_lossy.load(), dp = g_pred_deeper.load();
                std::cerr << "[esc-predict] AUDIT vs ladder: n=" << an
                          << " lossy(shallower)=" << lo << " (" << (100.0 * lo / an) << "%)"
                          << " deeper=" << dp << " exact=" << (an - lo - dp) << "\n";
            }
        }
    };
    EscPredictReporter g_esc_predict_reporter;

    struct EscMeasureReporter
    {
        ~EscMeasureReporter()
        {
            if (!s_esc_measure) { return; }
            const long long n = g_esc_measure_n.load();
            const long long L = g_esc_ladder_units.load(), S = g_esc_single_units.load();
            std::cerr << "[esc-measure] escalations=" << n
                      << " ladder_units=" << L << " single_at_committed_units=" << S
                      << " single/ladder=" << (L ? static_cast<double>(S) / static_cast<double>(L) : 0.0)
                      << " (matched-depth: cold single pass vs the d1..K ladder)\n";
        }
    };
    EscMeasureReporter g_esc_measure_reporter;
}

// Mana-dork ramp value (EvalCard). A 0-power mana dork (Ignoble Hierarch, Birds) scores 0 in the
// per-turn combat eval, so the greedy Solve rollout NEVER deploys one (casting it ties the
// do-nothing plan and loses the smallest-mask tie-break). Ramp is invisible to a per-turn eval, so
// combo/ramp lines got evaluated a turn slow -- the search couldn't see that a turn-1 dork unlocks
// an earlier kill (Anti-Lifegain s23: real T4 win searched as T5). Crediting a modest ramp value
// makes the rollout develop accelerants early so the search surfaces the faster line. The value is
// turns-scaled (a dork is worthless on the last turn, valuable early). Env-gated for A/B; default
// off (MTG_DORK_RAMP=0) restores the old byte-identical behaviour for A/B. Default ON (100): a
// dork is worth up to ~4 damage-equivalents early (100 * min(remaining_attacks,4)), decaying to
// ~1 late. See heuristic-optimization + rollout-policy investigation.
static const char* s_dork_ramp_env = std::getenv("MTG_DORK_RAMP");
static const int   s_dork_ramp     = s_dork_ramp_env ? std::atoi(s_dork_ramp_env) : 100;

// Gates SPECULATIVE free safe-alt enumeration (a Remedy-flip Invigorate offered as a real burn) so
// the SEARCH can assemble multi-payload lethal bursts the single-shot auto-fire cannot -- WITHOUT
// the greedy path (Solve: the d0 decision AND every rollout leaf) casting it early and wasting its
// pump/option value. Default TRUE (a real rollout-evaluated search node rejects a wasteful early
// cast); Solve() flips it FALSE for its own enumeration so the greedy path keeps the tuned auto-fire
// and stays byte-identical. thread_local so parallel rollouts don't race.
static thread_local bool g_search_candidate_enum = true;

// Move-ordering for the full-depth branch-and-bound (FSLineWin / FSLineTail). Each B&B loop
// returns at the FIRST verified in-horizon win; trying the plans that statically look lethal
// (then higher-value) first makes that win surface after fewer simulated plans, so WINNING
// nodes search less. Result-preserving by construction: in an iterative-deepening pass every
// in-horizon win sits at the same horizon-edge turn, so reordering changes only WHICH tied
// line commits, not the win turn -- and the rollout and executor share FSLineWin, so they
// stay in lockstep. A stable sort keeps the original order within ties to minimise
// committed-line churn (a different tied line could realise differently only under
// commit-the-line non-convergence; verified against GT). Default ON; MTG_NO_MOVE_ORDER opts
// out for the with/without A/B. Cheap: one O(n log n) sort per interior node.
static const bool s_move_order = !EnvOn("MTG_NO_MOVE_ORDER");

static void MoveOrderPlans(std::vector<TurnSolver::Plan>& plans)
{
    if (!s_move_order || plans.size() < 2) { return; }
    std::stable_sort(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn; }
            return a.value > b.value;
        });
}

void TurnSolver::SetTraceSolve(bool enable) { s_trace_solve = enable; }
bool TurnSolver::GetTraceSolve() { return s_trace_solve; }

static std::string PlanDesc(const TurnSolver::Plan& p)
{
    std::ostringstream os;
    auto list_kind = [&](Action::Kind kind, const char* label)
    {
        bool first = true;
        for (const Action& a : p.actions)
        {
            if (a.kind != kind) { continue; }
            if (first) { if (os.tellp() > 0) os << ' '; os << label << '['; first = false; }
            else       { os << ','; }
            os << a.card_name;
            if (kind == Action::Kind::DiscardToLandsEdge) { os << "x" << a.discard_lands; }
        }
        if (!first) { os << ']'; }
    };
    list_kind(Action::Kind::ActivateVial,       "vial");
    list_kind(Action::Kind::CastFromHand,       "spells");
    list_kind(Action::Kind::CastFromGraveyard,  "retrace");
    list_kind(Action::Kind::DiscardToLandsEdge, "le");
    if (p.empty()) os << "<pass>";
    return os.str();
}

// ---- Indifference probe (MTG_TRACE=tie) ----------------------------------
//
// Finds the OFFER/PRUNE class of design gap: actions the search takes because it is
// INDIFFERENT rather than because they help. See
// docs/design/searched-design-audit-blind-spots.md (blind spot 1).
//
// Mechanism it watches: FSLineWin keeps a plan only when it is STRICTLY better, and
// MoveOrderPlans has already sorted candidates by static value -- so among plans the search
// cannot tell apart, the FIRST scanned wins, which is the highest-STATIC-value one. Whenever a
// scored plan Q is a strict SUBSET of the chosen plan P and reached the SAME win turn, the extra
// actions P\Q bought nothing measurable and were taken on static value alone. That is the
// signature of the duplicate-legend misplay (109 casts per 600 games, -0.0417 once pruned) -- a
// correct rule plus an indifferent search.
//
// WEAK IN PRACTICE: at a bounded horizon most plays do not move the projected win turn either
// (67.5% of Goblins decisions contain such an action), so prefer the board-nullity probe below.
// Reports the difference, not the plan, because the difference is the candidate for a prune.
// Off unless MTG_TRACE lists `tie`; the keys are only built when it is on.
static std::vector<std::string> PlanActionKeys(const TurnSolver::Plan& p)
{
    std::vector<std::string> keys;
    keys.reserve(p.actions.size());
    for (const Action& a : p.actions)
    {
        // Disambiguate the plan VARIANTS that share a card name (X value, splice count, tutor
        // target, alt cost, chosen colour); without this an X=3 plan would read as a subset of
        // an X=5 plan and the probe would report a difference that is really a substitution.
        // Card names contain spaces, so the trace joins keys with '|' -- never whitespace.
        std::string k = a.card_name;
        k += "#" + std::to_string(static_cast<int>(a.kind));
        if (a.chosen_x)               { k += "/x" + std::to_string(a.chosen_x); }
        if (a.splice_count)           { k += "/s" + std::to_string(a.splice_count); }
        if (a.discard_lands)          { k += "/d" + std::to_string(a.discard_lands); }
        if (a.alt_cost)               { k += "/alt"; }
        if (!a.tutor_target.empty())  { k += "/t" + a.tutor_target; }
        if (!a.chosen_float_color.empty()) { k += "/c" + a.chosen_float_color; }
        keys.push_back(std::move(k));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

// ---- Board-nullity probe (MTG_TRACE=nil) ---------------------------------
//
// The SHARP lens for the offer/prune audit. The `tie` probe above asks "did this action move the
// projected win turn", which at a bounded horizon is dominated by noise -- a lord cast on turn 2
// does not move a turn-4 win either, yet every player casts it. What actually characterised the
// duplicate-legend misplay is stronger and horizon-INDEPENDENT: applying the action left the BOARD
// exactly as it was. The copy resolved, the legend rule killed it, and the only trace was a card
// gone from hand and mana spent -- neither of which the leaf evaluator prices, which is precisely
// why the search could not see the loss.
//
// So: signature = the multiset of permanents (name + tapped + sickness + counters) plus both life
// totals. An action whose presence does not change it produced NO board effect.
//
// This is a SHORTLIST generator, not a verdict. Cantrips, rituals and tutors are board-null by
// construction and are all perfectly good plays -- their payoff is in hand, library or mana. Only
// an action that is board-null AND converts nothing is the duplicate-legend shape. The classifying
// question stays human: would a player ever consider this play?
static std::string BoardSignature(const GameState& s)
{
    std::vector<std::string> perms;
    perms.reserve(s.battlefield.size());
    for (const Permanent& p : s.battlefield)
    {
        std::string e = p.card.m_name;
        e += p.tapped ? "/T" : "/U";
        e += p.entered_this_turn ? "/S" : "/R";     // summoning sickness
        e += "/c" + std::to_string(p.counters.size());
        e += "/ch" + std::to_string(p.charge_counters) + "," + std::to_string(p.storage_counters);
        e += "/p" + std::to_string(p.temp_power_bonus) + "," + std::to_string(p.temp_tough_bonus);
        e += "/d" + std::to_string(p.damage);
        e += "/a" + std::to_string(p.aura_attached_to);
        e += "/o" + std::to_string(p.controller_index);
        perms.push_back(std::move(e));
    }
    std::sort(perms.begin(), perms.end());
    std::string sig;
    for (const std::string& e : perms) { sig += e; sig += ";"; }
    for (const Player& pl : s.players) { sig += "L" + std::to_string(pl.life) + ";"; }
    return sig;
}

// ---- Local helpers -------------------------------------------------------

// A colored_creature_only source (Cavern of Souls / Unclaimed Territory / Sliver Hive / Secluded
// Courtyard) is PARTIALLY creature-only: "{T}: Add {C}" is unrestricted, but its coloured mana can
// only pay for a creature spell. AddSourceToPool sees six produces (W/U/B/R/G/C) and books it as one
// WILD, which satisfies any single coloured pip -- so in the NON-creature pool it wrongly paid for a
// coloured noncreature spell. The payment path already models this correctly (ProducesForPayment,
// honoured in ManaPayment.cpp), so the enumerator OFFERED casts the executor then could not pay and
// which strand as a silent no-op -- exactly the failure the colour-producibility gate below exists
// to prevent. Found by the board-nullity probe (MTG_TRACE=nil): 25/60 Goblins games committed a plan
// containing a Lightning Bolt that never resolved, every one of them off a Cavern of Souls.
// Restricted to this NON-creature pool, so the creature-cast path keeps the full colour list.
//
// DEFAULT OFF (opt in with MTG_CCO_NONCREATURE_POOL=1). It was briefly adopted and then WITHDRAWN
// (user-directed), because it does not clear the bar for keeping a change that perturbs results:
//   * It is NOT "we allowed invalid behaviour and now disallow it". The evaluator already refused
//     the Bolt off a Cavern -- MTG_CCO_AUDIT reports 0 illegal taps on the unfixed arm. The cast
//     merely STRANDED; no illegal play was ever made. So the regressing cases cannot be charged to
//     correctness, and the change has to earn its place on measurement like any heuristic.
//   * On measurement it is EXACTLY neutral: 0.0000 delta on all 12 jobs, 6600 fresh games/arm
//     (seeds 9001-9006, d3/10ms + d5/20ms). Every digest differs, so play IS perturbed -- the
//     outcome simply is not. An earlier +0.0024t read on one train seed pair was sampling noise.
// The reason it cancels is worth keeping: the fixed arm GAINS the Bolt's damage but PAYS more land
// branching, because playing the Mountain first leaves the singleton Cavern in hand and every later
// turn must keep branching over two distinct land choices. Measured on same-outcome games:
// +18% interior nodes (gi28), +97% (gi166), -3% (gi90). That search-economy effect is the live
// question -- see the land-order heuristic in GoblinsProvider -- not this pool.
static const bool s_cco_noncreature_pool = EnvOn("MTG_CCO_NONCREATURE_POOL");

// Pool excluding creature-only mana sources (e.g. Ancient Ziggurat).
// Used to verify that non-creature spells are payable without those sources.
static ManaPool BuildNonCreaturePool(const GameState& state)
{
    ManaPool pool;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.creature_mana_only) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap()) || def->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        if (s_cco_noncreature_pool && def->params.colored_creature_only)
        {
            // Only the unrestricted "{T}: Add {C}" mode is legal for a noncreature spell. Book it as
            // colourless (pays generic pips, never a coloured one) instead of as wild. Yield
            // resolution mirrors AddSourceToPool's own yield_override handling.
            const int yield = PermanentManaYield(p, *def);
            pool.Add(Color::Colorless, yield >= 0 ? yield : ManaProducedPerTap(*def));
            continue;
        }
        AddSourceToPool(pool, state, *def, PermanentManaYield(p, *def));
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }  // see AvailableManaPool
    return pool;
}

static int CountLands(const GameState& state)
{
    int n = 0;
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index && p.card.IsLand()) { ++n; }
    }
    return n;
}

// Color-producibility gate for a chosen action subset. ManaPool::CanPay stores every
// multi-color land as one "wild" mana that satisfies ANY single colored pip — an
// over-approximation for a land that produces only a SUBSET of colors (e.g. Tournament
// Grounds = W/R/B cannot pay {U}). That let the enumerator OFFER hard-casts the executor
// then cannot pay (a silent no-op; found by the Knights claude-play sweep). This gate
// rejects a subset only when it requires a colored pip of a color that NO untapped source
// can produce at all. That is the maximally-conservative necessary condition: it can never
// reject a plan the real payment would allow whenever the controller has at least one
// source of each needed color (true for burn/slivers/TH on their own seeds), so those
// decks stay byte-identical; it prunes exactly the restricted-color phantom (a {U} cost
// with zero blue sources). It deliberately does NOT model count/contention/filter-yield
// (those mis-estimate amounts and would false-reject payable RR/filter-chain plans), so a
// rarer "needs 2 of a color, only 1 source" phantom is left to the rollout's real payment,
// which already no-ops it. Cheap and deterministic.
// Colors at least one untapped source can produce (W,U,B,R,G). State-only -- it does not depend
// on which subset is being tested -- so callers compute it ONCE before the subset-enumeration
// loop and pass the result to SubsetPayable, instead of re-scanning the battlefield per subset.
static void ComputeAvailableColors(const GameState& state, bool have[5])
{
    have[0] = have[1] = have[2] = have[3] = have[4] = false;
    int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        bool is_src = (def->tmpl == CardTemplate::BasicLand)
                   || (def->tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def->params.mana_rock;
        if (!is_src) { continue; }
        for (Color c : EffectiveProduces(state, active, *def))   // RP -> union of other lands
        {
            switch (c)
            {
                case Color::White: have[0] = true; break;
                case Color::Blue:  have[1] = true; break;
                case Color::Black: have[2] = true; break;
                case Color::Red:   have[3] = true; break;
                case Color::Green: have[4] = true; break;
                default: break;
            }
        }
        // Three Tree City scaled ability yields a SEARCH-CHOSEN colour (any of W/U/B/R/G), so when it
        // can fire (a feeder pays the {2} and its net beats the basic {C}) every colour is producible.
        // Empty subtype on every board without the land -> byte-identical.
        if (ScaledManaNetYield(state, *def) > 0)
        { have[0] = have[1] = have[2] = have[3] = have[4] = true; }
    }
    // Floating mana (turn-scoped reserve) also satisfies colored pips: a floated {U} pays a {U}
    // pip even when no untapped land produces blue. AvailableManaPool already credits floating into the
    // count pool, so without this the per-color gate would false-reject an otherwise-payable cast
    // (e.g. a second Treasure Hunt {1}{U} off a floating {U} plus a colorless land). A wild
    // floating mana (multi-color ritual float) can pay any single pip. Empty floating ->
    // byte-identical for every non-floating deck/seed.
    if (FloatLeftoverManaEnabled())
    {
        const ManaPool& f = state.floating_mana;
        if (f.white > 0) { have[0] = true; }
        if (f.blue  > 0) { have[1] = true; }
        if (f.black > 0) { have[2] = true; }
        if (f.red   > 0) { have[3] = true; }
        if (f.green > 0) { have[4] = true; }
        if (f.wild  > 0) { have[0] = have[1] = have[2] = have[3] = have[4] = true; }
    }
}

static bool SubsetPayable(const bool have[5], const std::vector<Action>& cands,
                          const std::vector<int>& sel)
{
    // Colors required by the chosen casts (Vial deploys cost no mana).
    bool need[5] = {false,false,false,false,false};  // W,U,B,R,G ({C}/generic via CanPay)
    bool any = false;
    for (int j : sel)
    {
        const Action& a = cands[j];
        if (a.kind == Action::Kind::ActivateVial) { continue; }
        if (a.cost.white > 0) { need[0] = true; any = true; }
        if (a.cost.blue  > 0) { need[1] = true; any = true; }
        if (a.cost.black > 0) { need[2] = true; any = true; }
        if (a.cost.red   > 0) { need[3] = true; any = true; }
        if (a.cost.green > 0) { need[4] = true; any = true; }
    }
    if (!any) { return true; }

    for (int i = 0; i < 5; ++i) { if (need[i] && !have[i]) { return false; } }
    return true;
}

// Same-turn affinity generic credit (Thrumming Hivepool: "Affinity for Slivers"). An affinity card's
// per-Action cost only credited matching permanents ALREADY in play (EffectiveCost on the base
// state). When the same plan also casts matching-subtype creatures, those are on the battlefield
// when the affinity card resolves (CastOrderRank 10 creatures precede the rank-20 artifact, so the
// executor's apply_one casts them first and recomputes the cheaper EffectiveCost), so they further
// reduce its generic. Returns the extra generic discount to subtract from the subset's combined cost
// so an affordable "deploy slivers + Hivepool" line is enumerated rather than dropped as too costly.
// Only creatures cast BEFORE the affinity card (lower CastOrderRank) count -- matching the order the
// executor realises. Capped at each affinity card's remaining generic. Returns 0 unless the subset
// holds an affinity card, so every non-affinity deck/seed is byte-identical.
// MTG_NO_COST_TRICKS (default: tricks ON): disables the per-deck same-turn cost-credit patches
// (SameTurnReducerGenericCredit + SameTurnAffinityGenericCredit). Set to TEST whether the general
// cost-reframe recovers their value on an EXISTING deck stripped of its hand-patches -- a proxy for
// onboarding a NEW deck patch-free. See docs/design/enumeration-feasibility-via-executor.md.
static bool CostTricksEnabled()
{
    static const bool on = []{ const char* e = std::getenv("MTG_NO_COST_TRICKS"); return !(e && std::string(e) == "1"); }();
    return on;
}
// MTG_COST_REFRAME (default OFF -> byte-identical): the deck-agnostic over-optimistic enumeration
// relaxation. In EnumeratePlans, offer an INTERACTING subset the flat-pool aggregate rejects -- assume its
// generic is same-turn-coverable (what reducers/affinity cut, or rituals float), require only the COLOURED
// pips to be really payable -- and let the scoring apply (SolveWithLookahead 9446/9467) validate for real.
// Replaces per-deck credit patches: a new deck's cost mechanic is offered without one.
static bool CostReframeEnabled()
{
    static const bool on = []{ const char* e = std::getenv("MTG_COST_REFRAME"); return e && std::string(e) == "1"; }();
    return on;
}

static int SameTurnAffinityGenericCredit(const GameState& state, const std::vector<Action>& cands,
                                         const std::vector<int>& sel)
{
    int credit = 0;
    for (int j : sel)
    {
        const CardDefinition* dj = cands[j].def;
        if (!dj || !dj->params.affinity_for_subtype || dj->params.subtypes_affected.empty()) { continue; }
        const int j_rank = ResolveProvider(state).CastOrderRank(state, *dj);
        int same_turn = 0;
        for (int k : sel)
        {
            if (k == j) { continue; }
            const CardDefinition* dk = cands[k].def;
            if (!dk || !dk->card.IsCreature()) { continue; }
            if (ResolveProvider(state).CastOrderRank(state, *dk) >= j_rank) { continue; }  // cast after -> no credit
            bool match = false;
            for (const std::string& sub : dj->params.subtypes_affected)
            {
                for (const std::string& cs : dk->card.m_subtypes) { if (cs == sub) { match = true; break; } }
                if (match) { break; }
            }
            if (match) { ++same_turn; }
        }
        credit += std::min(same_turn, cands[j].cost.generic);
    }
    return credit;
}

// Same-turn cost-reducer generic credit (Ruby Medallion: "Red spells you cast cost {1} less"). A
// spell's per-Action cost (EffectiveCost) only credits reducers ALREADY in play; a reducer cast in
// the SAME subset is deferred here (the ManaPruneBound bail already disables the scalar prune for
// such subsets). When the plan casts a reducer, the executor casts it before the spells it discounts
// (Apex-containing sets are OrderingOpaque -> plan-action order; a same-turn Medallion resolves
// ahead of the later red casts, whose per-cast EffectiveCost then sees it in play). Returns the extra
// generic discount to subtract from the subset's combined cost so an affordable
// "Medallion + discounted rituals + Apex" line is ENUMERATED instead of dropped as too costly. Each
// matching same-colour spell's generic drops by 1 per same-turn reducer (floored at its generic;
// colour pips never reduce -- matching "cost {1} less"). The credit is OPTIMISTIC (it assumes the
// reducer precedes every discounted cast); that is SOUND because it can only ADD plans -- a plan the
// executor's realised order cannot actually pay rolls out to a non-win and the search discards it, so
// over-crediting never picks an unpayable line. Returns 0 unless the subset holds a reducer, so every
// non-reducer deck/seed is byte-identical. Only Ruby Medallion sets reduces_spell_color today, so the
// whole path is Dragonstorm-scoped.
static int SameTurnReducerGenericCredit(const GameState& state, const std::vector<Action>& cands,
                                        const std::vector<int>& sel)
{
    (void)state;
    // Count same-turn reducers per colour in the subset (only "R" exists today, but generalise).
    int red = 0, white = 0, blue = 0, black = 0, green = 0;
    // Count same-turn SUBTYPE reducers (Goblin Warchief) per subtype string.
    std::unordered_map<std::string, int> sub_reducers;
    for (int j : sel)
    {
        const CardDefinition* d = cands[j].def;
        if (!d) { continue; }
        if (!d->params.reduces_spell_subtype.empty()) { ++sub_reducers[d->params.reduces_spell_subtype]; }
        if (d->params.reduces_spell_color.empty()) { continue; }
        const std::string& rc = d->params.reduces_spell_color;
        if      (rc == "R") { ++red; }   else if (rc == "W") { ++white; }
        else if (rc == "U") { ++blue; }  else if (rc == "B") { ++black; }
        else if (rc == "G") { ++green; }
    }
    if (red + white + blue + black + green == 0 && sub_reducers.empty()) { return 0; }
    int credit = 0;
    for (int j : sel)
    {
        const CardDefinition* dj = cands[j].def;
        if (!dj || cands[j].cost.generic <= 0) { continue; }   // nothing to discount
        const ManaCost& mc = dj->card.m_mana_cost;             // printed pips (colour set by the card)
        int reducers = 0;
        if (mc.red   > 0) { reducers += red; }
        if (mc.white > 0) { reducers += white; }
        if (mc.blue  > 0) { reducers += blue; }
        if (mc.black > 0) { reducers += black; }
        if (mc.green > 0) { reducers += green; }
        // Subtype reducers (Warchief): a Goblin spell gets -1 per OTHER same-turn Warchief in the
        // subset. Warchief is itself a Goblin, so exclude the spell's own reducer copy (a lone
        // Warchief never discounts itself; two Warchiefs each discount the other).
        for (const std::string& cs : dj->card.m_subtypes)
        {
            auto it = sub_reducers.find(cs);
            if (it == sub_reducers.end()) { continue; }
            int n = it->second;
            if (dj->params.reduces_spell_subtype == cs) { --n; }   // exclude self
            reducers += n;
        }
        // A colour reducer never discounts itself (a Ruby Medallion is {2}, colourless); Warchief's
        // own subtype match is excluded above.
        credit += std::min(reducers, cands[j].cost.generic);
    }
    return credit;
}

// Forward decl of the real backtracking mana payment (defined below); used by the filter
// affordability fallback so enumeration can recognize filter/depletion/ramp-land lines.
bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature);

// Real-payment affordability fallback for filter / ramp lands. AvailableManaPool models a filter land
// (Cascade Bluffs) as a single wild, which cannot express its color conversion (feed {U} -> {R}{R}),
// so a filter-payable subset (Land's Edge {1}{R}{R} off Saprazzan Skerry + Cascade Bluffs) fails the
// flat CanPay even though the executor's TapForCostDirect can pay it. When the flat check fails and a
// filter/ramp land is present, retry the subset by actually tapping real sources on a copy. Returns
// true iff every selected cast can be paid for real. Only reached when the flat check already failed
// AND such a land exists, so non-filter decks never run it (byte-identical) and the cost is bounded.
static bool SubsetPayableWithFilters(const GameState& state, const std::vector<Action>& cands,
                                     const std::vector<int>& sel)
{
    GameState cp = state;
    // Pay each selected cast's mana cost with real sources; taps persist across casts in cp, so a
    // filter consumed by one cast is unavailable to the next. Mana producers (rocks) pay first and
    // join the board so their mana is online for later casts in the subset.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool want_rock = (pass == 0);
        for (int j : sel)
        {
            const Action& a = cands[j];
            if (a.kind == Action::Kind::ActivateVial) { continue; }   // Vial deploys cost no mana
            const CardDefinition* def = a.def;
            const bool is_rock = def && def->params.mana_rock && !def->card.IsCreature();
            if (is_rock != want_rock) { continue; }
            const bool for_creature = def && def->card.IsCreature();
            if (!TapForCostDirect(cp, a.cost, for_creature)) { return false; }
            if (is_rock && def)   // freshly-cast rock joins the board so its mana funds later casts
            {
                Permanent perm;
                perm.card             = def->card;
                perm.controller_index = cp.active_player_index;
                perm.owner_index      = cp.active_player_index;
                cp.battlefield.push_back(perm);
            }
        }
    }
    return true;
}

// True if the active player controls an untapped filter / ramp-filter mana source, whose color
// conversion the flat AvailableManaPool cannot model (so the affordability fallback above is needed).
static bool HasUntappedFilterSource(const GameState& state)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        // Three Tree City's scaled tap is a colour conversion (feed {2} -> N chosen colour) the flat
        // pool cannot fully express, so it too needs the real-payment affordability retry.
        if (d && (d->params.is_filter || d->params.ramp_filter || IsScaledManaLand(*d))) { return true; }
    }
    return false;
}

static int PendingAttackDamage(const GameState& state)
{
    int dmg = 0;
    int active = state.active_player_index;
    std::vector<const Permanent*> attackers;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!ResolveProvider(state).ShouldAttackWith(state, p)) { continue; }
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, active, animated, &p);
        bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active));
        int base_pw = p.EffectivePower() + lord_pb;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        base_pw += AuraBonusFor(p, state).first;                 // Bogles: attached auras + Kor self-buff
        dmg += base_pw * (ds ? 2 : 1);
        attackers.push_back(&p);
    }
    dmg += CountAttackTriggerLifeLoss(state.battlefield, active, attackers);

    // Exalted (Ignoble Hierarch): a creature attacking ALONE gets +1/+1 per Exalted ability.
    if (static_cast<int>(attackers.size()) == 1)
    { dmg += CountExalted(state.battlefield, active); }

    // Estimate attack-trigger tokens (Adeline) for this turn only: const path cannot create
    // them, so add their immediate damage (token base power + anthem bonus) if attacking.
    if (!attackers.empty())
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d || d->params.attack_creates_tokens <= 0) { continue; }
            Card tok;
            tok.AddType(CardType::Creature);
            tok.m_subtypes = d->params.attack_token_subtypes;
            tok.m_power    = d->params.attack_token_power;
            auto [tpb, ttb] = ComputeLordBonus(tok, state.battlefield, active, false, nullptr);
            dmg += d->params.attack_creates_tokens
                 * (d->params.attack_token_power + tpb);
        }
    }
    return dmg;
}

// Pre-computed per-permanent on-cast trigger: damage dealt to the caster when they
// cast a spell with MV <= max_mv (e.g. Eidolon of the Great Revel).
struct TriggerSource
{
    int max_mv;
    int damage;
};

static std::vector<TriggerSource> CollectTriggerSources(const GameState& state)
{
    std::vector<TriggerSource> sources;
    for (const Permanent& p : state.battlefield)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.on_cast_trigger_max_mv <= 0) { continue; }
        sources.push_back({def->params.on_cast_trigger_max_mv,
                           def->params.on_cast_trigger_damage});
    }
    return sources;
}

// Returns the number of Prowess creatures the active player controls that can attack.
// Each noncreature spell cast this turn adds 1 to each of their powers, so the
// prowess bonus to combat damage = noncreature_spell_count * CountProwessAttackers().
static int CountProwessAttackers(const GameState& state)
{
    int count = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index
            && CanAttackFull(p, state.battlefield, state.active_player_index)
            && ResolveProvider(state).ShouldAttackWith(state, p)
            && p.card.HasKeyword(Keyword::Prowess))
        {
            ++count;
        }
    }
    return count;
}

static bool HasLegalCreatureTarget(const GameState& state)
{
    const Player& opp = state.Opponent();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index && p.card.IsCreature()) { return true; }
    }
    return false;
}

// The "prowess line" is available when: no opponent creature (the burn is otherwise dead), a prowess
// attacker exists to benefit from the +1/+1, and an own creature SURVIVES the burn to still attack.
// FindSurvivingOwnCreature (SpellEffects.h) picks that target; here we add the prowess-payoff gate.
// Shared by the enumeration gate, both executors, and the rollout so all agree the line is available.
static int FindOwnProwessBurnTarget(const GameState& state, const CardDefinition& def)
{
    if (CountProwessAttackers(state) <= 0) { return -1; }   // no prowess payoff -> never self-harm
    return FindSurvivingOwnCreature(state, state.active_player_index, CreatureBurnDamage(def, state));
}

static ManaCost EffectiveCost(const CardDefinition& def, const GameState& state, int copies = 1)
{
    // Delegates to the UNIFIED EffectiveSpellCost (ManaPayment.cpp) -- formerly a byte-identical
    // twin of AIEngine::EffectiveCost kept in lockstep by comment discipline.
    // (Goblin Warchief's reduces_spell_subtype reduction lives inside EffectiveSpellCost.)
    return EffectiveSpellCost(def, state, copies);
}

// Estimate how many times a creature placed NOW will attack before the game ends.
// Uses the current turn number against an assumed 6-turn average game length for
// aggressive decks, so creatures placed early score higher than late-game drops.
// Callers subtract 1 for non-haste creatures (they miss the current attack step).
static int ExpectedAttacks(const GameState& state)
{
    // +1: include the current turn's attack step (haste creatures attack now).
    int remaining = 6 - state.turn_number + 1;
    return std::max(1, std::min(remaining, 5));
}

// Evaluate a card's contribution to winning, accounting for tempo.
// Uses a fixed unit of 100 per damage-equivalent so integer arithmetic
// stays precise even with multipliers.
static int EvalCard(const CardDefinition& def, const GameState& state)
{
    constexpr int DMG = 100;  // points per damage-equivalent

    if (def.tmpl == CardTemplate::DirectDamage)
    {
        // Penalise spells with additional costs so equivalent-damage alternatives
        // are preferred.  The penalty must be less than DMG so a sacrifice-land
        // spell is still selected when it extends the total subset (e.g. Shard
        // Volley + Bolt beats Bolt alone).
        int penalty = def.params.sacrifice_land ? (DMG / 2) : 0;
        return def.params.damage * DMG - penalty;
    }

    if (def.card.IsCreature())
    {
        auto [lord_pb, lord_tb] = ComputeLordBonus(def.card, state.battlefield, state.active_player_index);
        bool ds = def.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(def.card, state.battlefield, state.active_player_index);
        // Adeline (power = creatures you control): estimate as the current creature count
        // plus 1 for herself entering. Her printed power is 0, so without this she scores 0.
        int dyn = def.params.power_equals_creature_count
                  ? CreatureCount(state, state.active_player_index) + 1 : 0;
        int power = (def.card.m_power.value_or(0) + dyn + lord_pb) * (ds ? 2 : 1);
        if (power <= 0)
        {
            // Mana-dork ramp: a 0-power accelerant contributes future mana the per-turn combat
            // eval can't see. Credit a turns-scaled ramp value (worthless late, valuable early) so
            // the greedy rollout deploys it instead of passing. Env-gated; s_dork_ramp==0 -> old 0.
            if (s_dork_ramp > 0 && def.tmpl == CardTemplate::ManaDork)
            {
                int remaining = std::max(1, ExpectedAttacks(state));
                return s_dork_ramp * std::min(remaining, 4);
            }
            return 0;
        }

        // Haste (from the card or from a lord already on board) attacks this turn;
        // others start next turn.
        bool haste = def.card.HasKeyword(Keyword::Haste)
                  || HasHasteFromLords(def.card, state.battlefield, state.active_player_index);
        int  attacks = ExpectedAttacks(state);
        if (!haste && attacks > 0) { --attacks; }
        return power * attacks * DMG;
    }

    if (def.tmpl == CardTemplate::DrawSpell)
    {
        // 1 damage-equivalent per card: draw spells trail burn and creatures,
        // but are still worth casting when other options are exhausted.
        return def.params.draw * DMG;
    }

    if (def.tmpl == CardTemplate::DrawX)
    {
        return DMG;  // minimal X=1 estimate
    }

    // Archetype-specific card value (Treasure Hunt / Land's Edge combo): provider-owned
    // (ArchetypeCardValue), so the clairvoyant + combo assumptions live in the per-deck file. A deck
    // without such a model returns false here and falls through to the generic estimates.
    {
        int archetype_value = 0;
        if (ResolveProvider(state).ArchetypeCardValue(state, def, DMG, archetype_value))
        {
            return archetype_value;
        }
    }

    // Cascade spells: value = free spell drawn (assume ~3 damage-equivalents on average).
    if (def.params.cascade_max_mv > 0)
    {
        return 3 * DMG;
    }

    // Aether Vial and similar: deploys a creature from hand for free each turn once charged.
    // Score as the best matching creature in hand. Lords boost all existing attackers
    // immediately on deployment (continuous effect, not blocked by summoning sickness);
    // haste creatures (via card or lord) attack the same turn deployed.
    // Apply a 1-attack penalty vs direct cast: Vial must reach target charge first.
    if (def.params.upkeep_adds_charge)
    {
        int best = 0;
        int target_mv = state.vial_target_mv;
        for (const Card& c : state.ActivePlayer().hand)
        {
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
            if (!cdef || !cdef->card.IsCreature()) { continue; }
            if (target_mv > 0 && cdef->card.m_mana_cost.ManaValue() != target_mv) { continue; }
            auto [lord_pb, lord_tb] = ComputeLordBonus(cdef->card, state.battlefield,
                                                        state.active_player_index);
            bool ds = cdef->card.HasKeyword(Keyword::DoubleStrike)
                   || HasDoubleStrikeFromLords(cdef->card, state.battlefield,
                                               state.active_player_index);
            int power = (cdef->card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
            bool haste = cdef->card.HasKeyword(Keyword::Haste)
                      || HasHasteFromLords(cdef->card, state.battlefield,
                                           state.active_player_index);
            int attacks = std::max(1, ExpectedAttacks(state) - (haste ? 1 : 2));
            best = std::max(best, power * attacks * DMG);
        }
        if (best > 0) { return best; }
        return std::max(3 * DMG / 4, ExpectedAttacks(state) * DMG - state.turn_number * 30);
    }

    return DMG;  // fallback for other spell types
}

// True if the active player controls no opponent creature to exile (used by the Swords gate).
static bool HasOpponentCreature(const GameState& state, int active)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active && p.card.IsCreature()) { return true; }
    }
    return false;
}

// True if a lifegain->loss enabler (Tainted Remedy / Plague Drone) is on the battlefield OR in the
// active player's hand. The Swords-to-Plowshares enumeration gate uses this so it also OFFERS the
// same-turn "enabler -> Swords" combo: on the turn the enabler lands it is not yet on the
// battlefield at enumeration (turn start), so the strict RemedyActive gate would defer Swords a
// turn. Emitting it here is safe because plan validity (SubsetHasUnbackedLifegainRemoval) still
// requires the enabler to actually be in the plan, and the rollout casts it enabler-first, so
// Swords never resolves without a live enabler (which would just hand the passive opponent life).
static bool RemedyActiveOrInHand(const GameState& state, int active)
{
    if (RemedyActive(state, active)) { return true; }
    for (const Card& c : state.players[active].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.lifegain_to_loss) { return true; }
    }
    return false;
}

// Plan-validity gate for Swords to Plowshares (controller_lifegain_equals_power): its exile-
// lifegain rider only helps once a lifegain->loss enabler is live when it resolves. A subset that
// casts Swords is valid ONLY if an enabler is already in play OR the SAME subset casts one
// (enabler-first ordering resolves it before Swords). This rejects the myopic "Swords with no
// enabler" line (which would hand a passive opponent life) at BOTH the d0 greedy (Solve::consider)
// and the search (EnumeratePlans), so the enumeration gate can offer the same-turn combo without
// the enabler being on the battlefield at turn start. Inert for every deck without Swords.
static bool SubsetHasUnbackedLifegainRemoval(const GameState& state,
                                             const std::vector<Action>& cands,
                                             const std::vector<int>& sel)
{
    bool has_removal = false, has_enabler = false;
    for (int j : sel)
    {
        const CardDefinition* d = cands[j].def;
        if (!d) { continue; }
        if (d->params.controller_lifegain_equals_power) { has_removal = true; }
        if (d->params.lifegain_to_loss)                 { has_enabler = true; }
    }
    if (!has_removal)                                   { return false; }
    if (RemedyActive(state, state.active_player_index)) { return false; }
    return !has_enabler;   // removal present, no enabler live and none cast this turn -> unbacked
}

// Payoff-prune (DecisionProvider::PrunesAcceleratorWithoutPayoff -- the ritual-guard's search-side analog;
// the user's spec). A mana ritual is a ONE-TURN accelerant: its float empties at end of turn (identical to
// Reality Spasm's untap) and storm count does not carry across turns, so a subset that casts a ritual
// (ritual_float > 0) but no PAYOFF -- a Dragon (creature), Dragonstorm (tutor_to_battlefield), or Apex of
// Power (impulse_exile) -- burns the ritual for nothing (no same-turn sink). Callers gate this on
// ResolveProvider(state).PrunesAcceleratorWithoutPayoff() so ONLY Dragonstorm prunes; Hinata (whose ritual is
// a useful cantrip/dig accelerant) is untouched. A ritual-only subset deals no damage, so it can never be a
// winning line -> pruning it never drops a lethal plan. Inert (no ritual) -> unchanged. Storm-hold rule
// (ADOPTED 2026-07-23; default ON, off-switch MTG_NO_STORM_HOLD). REFINES the UNCONDITIONAL slow-dragon rule
// that was REJECTED (making a fair Dragon never a payoff improved BLIND d0 ~-0.73 but WORSENED the shipped d5
// search ~+0.37: a blind leaf that always holds its ritual durdles, never reaching the storm it can't
// foresee). The fix, per the deck's human pilot: only hold when a storm payoff (Dragonstorm/Apex) is ALREADY
// IN HAND -- then the leaf CAN see the payoff it is saving the ritual for, so it doesn't durdle. A subset
// that spends a ritual on a FAIR Dragon is pruned ONLY when a storm is in hand (the Dragon can still be cast
// off lands -- a ritual-free subset never trips this). Measured (train 4004/5005): blind d0 -0.60, shipped d5
// -0.005 (NEUTRAL, no search regression -- the storm-in-hand gate is what makes an option-prune safe for the
// search). Applied to the greedy/rollout POLICY only; the search's root branch list is untouched (see the 2nd
// call site). See docs/design/dragonstorm-d0-divergence-digest.md.
static const bool s_storm_hold = !EnvOn("MTG_NO_STORM_HOLD");

static bool SubsetWastesAccelerant(const std::vector<Action>& cands, const std::vector<int>& sel,
                                   bool storm_in_hand)
{
    // A fair creature justifies a ritual by default; under the storm-hold rule, when a storm payoff is
    // in hand, it does NOT -- only the storm finishers (Dragonstorm/Apex) do, so the ritual is held.
    const bool creature_pays = !(s_storm_hold && storm_in_hand);
    bool has_ritual = false, has_payoff = false;
    for (int j : sel)
    {
        if (cands[j].ritual_float > 0) { has_ritual = true; }
        const CardDefinition* d = cands[j].def;
        if (!d) { continue; }
        if (d->params.tutor_to_battlefield || d->params.impulse_exile > 0
            || (creature_pays && d->card.IsCreature()))
        {
            has_payoff = true;
        }
    }
    return has_ritual && !has_payoff;
}

// Reject a subset that selects two SacForMana actions for the SAME source (its colour variants are
// mutually exclusive -- a given Lotus can be tapped+sacrificed for exactly one colour, once). The
// colour variants are enumerated as independent actions, so this is the analogue of the Vial per-charge
// capacity cap. Inert (returns false) for every deck without a SacForMana action -> byte-identical.
static bool SubsetHasDuplicateSacSource(const std::vector<Action>& cands, const std::vector<int>& sel)
{
    for (size_t a = 0; a < sel.size(); ++a)
    {
        if (cands[sel[a]].kind != Action::Kind::SacForMana) { continue; }
        for (size_t b = a + 1; b < sel.size(); ++b)
        {
            if (cands[sel[b]].kind == Action::Kind::SacForMana
                && cands[sel[b]].sac_source_id == cands[sel[a]].sac_source_id) { return true; }
        }
    }
    return false;
}

// Reject a plan that activates a CREATURE sac-for-mana outlet (Skirk Prospector, "Sacrifice a Goblin:
// Add {R}") while the subset spends NO mana at all. Such a plan is strictly DOMINATED by the same plan
// without the activation: floated mana empties at end of phase, so it buys nothing, while the
// sacrificed body is gone permanently -- a lost creature and lost tempo for exactly zero gain. The
// no-sac variant is always enumerated alongside it (the powerset visits the same subset minus this
// index, and no other reject keys on the sac), so dropping the dominated one never hides a line.
//
// Solve already refuses this via the rituals-for-payoff guard, which rejects a SURPLUS ritual on any
// non-winning subset -- that is precisely why d0/d1 never make the play and only the searched depths
// do. EnumeratePlans deliberately does NOT run that guard, and for a HAND ritual that is right: "cast
// it now or keep the card for a later turn" is a real branch the search should arbitrate. An IN-PLAY
// sac outlet has no such trade -- declining leaves both the outlet and the body on the battlefield --
// so the branch carries no upside to weigh against the loss. Goblins gi44 is the case that exposed it:
// the search sacrificed Skirk Prospector in T1 main 2 for an unspendable {R} and so could not pay for
// the T2 Goblin Matron that same body funds, sliding the whole line a turn.
//
// NOT dominated when a death TRIGGER pays for the body: Pashalik Mons (1 damage per Goblin death),
// Rundvelt Hordemaster (impulse exile) and Mogg War Marshal (a token on its own death) all turn an
// "unspent" sacrifice into real value, and the mana is then incidental. Any death-watcher on the
// controller's battlefield suppresses the prune -- conservatively, whether or not it would actually
// match this victim's subtype. The victim itself is covered by that scan (it must be on the
// battlefield to be sacrificed). A subset carrying direct damage is likewise left alone, so the prune
// can never touch a reach-to-lethal line. Inert (false) for every deck without a creature mana outlet
// -> byte-identical.
static const bool s_sac_waste_prune = !EnvOn("MTG_NO_SAC_WASTE_PRUNE");

static bool SubsetWastesCreatureSacMana(const GameState& state,
                                        const std::vector<Action>& cands,
                                        const std::vector<int>& sel)
{
    if (!s_sac_waste_prune) { return false; }
    bool      has_creature_sac = false;
    long long spend            = 0;
    for (int j : sel)
    {
        const Action& a = cands[j];
        spend += a.cost.ManaValue();
        if (a.direct_damage > 0) { return false; }          // never touch a reach-to-lethal subset
        if (a.kind != Action::Kind::SacForMana || a.sac_source_id == 0) { continue; }
        for (const Permanent& p : state.battlefield)        // is the source a CREATURE sac outlet?
        {
            if (p.controller_index != state.active_player_index
                || p.card.m_number != a.sac_source_id) { continue; }
            const CardDefinition* sd = CardDatabase::Instance().LookupCached(p.card);
            if (sd && sd->params.sac_creature_outlet) { has_creature_sac = true; }
            break;
        }
    }
    if (!has_creature_sac || spend > 0) { return false; }   // mana IS spent -> the sac may be paying for it
    for (const Permanent& p : state.battlefield)            // a death payoff makes the body worth spending
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (d->params.dies_trigger_damage > 0 || d->params.dies_trigger_creates_tokens > 0
            || d->params.dies_trigger_impulse_exile) { return false; }
    }
    return true;
}

// Reject a subset that over-splices Desperate Ritual (splice_onto_arcane). Splicing k copies onto a
// base cast REVEALS k OTHER copies that must still be IN HAND at that moment; a revealed copy stays in
// hand and may later be cast as its own base (and/or spliced again). So casting m bases of the SAME
// card with splice counts k (sorted DESCENDING) is physically legal iff the j-th base (0-indexed,
// largest first) splices at most N-1-j other copies, where N = copies of that card in hand at
// enumeration time. The maximal legal chain is triangular (N-1, N-2, ..., 0). Any k exceeding its slot
// (e.g. two bases both claiming to splice the full N-1 others, when the first cast leaves hand) is
// impossible -> reject. Grouped by card name (only same-named Arcane copies can be each other's splice
// targets in this deck). Inert (false) without a splice base selected -> byte-identical for every
// non-splice deck.
static bool SubsetHasIllegalSplice(const GameState& state,
                                   const std::vector<Action>& cands,
                                   const std::vector<int>& sel)
{
    auto is_splice_base = [&](int j) -> bool {
        const CardDefinition* d = cands[j].def;
        return d && d->params.splice_onto_arcane && cands[j].kind == Action::Kind::CastFromHand;
    };
    std::vector<std::string> names;
    for (int j : sel)
    {
        if (!is_splice_base(j)) { continue; }
        if (std::find(names.begin(), names.end(), cands[j].card_name) == names.end())
        { names.push_back(cands[j].card_name); }
    }
    if (names.empty()) { return false; }
    const Player& ap = state.ActivePlayer();
    for (const std::string& nm : names)
    {
        std::vector<int> ks;
        for (int j : sel)
        {
            if (is_splice_base(j) && cands[j].card_name == nm) { ks.push_back(cands[j].splice_count); }
        }
        int N = 0;
        for (const Card& c : ap.hand) { if (c.m_name == nm) { ++N; } }
        std::sort(ks.begin(), ks.end(), std::greater<int>());
        for (size_t j = 0; j < ks.size(); ++j)
        {
            if (ks[j] > N - 1 - static_cast<int>(j)) { return true; }
        }
    }
    return false;
}

// Fill a scaled divided-damage cast (Magma Opus) UP from a plan's LEFTOVER mana (user directive: "spend all
// available mana in the plan"; "fit the scalars to the proposed plan" rather than enumerate every cost). The
// cast is emitted as ONE cheap candidate (min face); after a plan's other costs are paid -- including the
// searched Crackle {X}, which takes its 3-mana chunks first -- this pours the surplus generic into Magma's
// face, up to its max, pricing each face via the provider's cost-per-face ladder. So a plan spends all its
// mana: Crackle (searched) first, Magma the sub-chunk remainder. Keeps the plan enumeration NARROW (a single
// Magma candidate) -- a wide face ladder bloated the group product and the breadth cap dropped the winning
// subset (see the CollectActions note + docs). Lockstep: apply_one / CastSpellFromHand recompute the cost
// from crackle_targets via the SAME ladder, and resolution deals crackle_targets, so the filled face is
// consistent end-to-end. Mutates `a` in place (cost, crackle_targets, direct_damage, eval); returns the
// EXTRA face damage added (0 if not a fillable scaled cast, or no affordable bump). Off-switch
// MTG_NO_MAGMA_RESERVE keeps the emitted (min) face -- the A/B baseline. Inert for every non-scaled cast.
static int FillScaledCastFace(const GameState& state, Action& a, int surplus_generic)
{
    static const bool fill_enabled = !EnvOn("MTG_NO_MAGMA_RESERVE");
    if (!fill_enabled) { return 0; }
    const CardDefinition* d = a.def;
    if (!d || a.kind != Action::Kind::CastFromHand) { return 0; }
    if (!d->params.damage_divided || a.crackle_targets < 0) { return 0; }
    if (surplus_generic <= 0) { return 0; }
    std::vector<ScaledCastVariant> ladder = ResolveProvider(state).ScaledCastVariants(state, *d);
    if (ladder.empty()) { return 0; }
    const int base_gen  = a.cost.generic;      // cost of the currently-committed (cheap) face
    const int cur_face  = a.crackle_targets;
    const ScaledCastVariant* best = nullptr;   // highest face whose extra generic fits the surplus
    for (const ScaledCastVariant& v : ladder)
    {
        if (v.face <= cur_face)                              { continue; }
        if (v.cost.generic - base_gen > surplus_generic)    { continue; }
        if (!best || v.face > best->face)                   { best = &v; }
    }
    if (!best) { return 0; }
    const int extra   = best->face - cur_face;
    a.cost            = best->cost;
    a.crackle_targets = best->face;
    a.direct_damage   = best->face;
    a.eval            = best->face * 100;
    return extra;
}

// Choice-INDEPENDENT precompute shared by the two splice predicates below (sibling of
// BuildAccelPrefixOrder; built ONCE per plan enumeration, right before the odometer).
//
// Both predicates used to redo three things at EVERY odometer position: group the selected splice
// bases by card NAME via std::string compares, rescan the active player's hand to count that name's
// copies (N), and rescan it again to find each base's copy POSITION. None of that depends on the
// odometer's `choice` -- it is fixed for the whole enumeration -- yet it dominated the Dragonstorm
// combo search (GroupChoiceOverSplices 12.2% + SpliceCollapseViolated 10.8% of total runtime,
// plus ~4% of libc memcmp from the name compares; perf, d5 seed 1001). Hoisting it here turns both
// predicates into a walk over dense int arrays.
//
// Name identity is keyed on Action::def rather than card_name: CardDatabase::LookupCached returns
// ONE stable CardDefinition* per name and CollectActions resolves `def` from `card_name`, so
// def-pointer equality == name equality for every candidate these predicates look at (a candidate
// with a null def is skipped by the splice_onto_arcane test either way). The hand scans below still
// compare by name -- they run once, so the string compare is free there.
struct SpliceOdometerIndex
{
    std::vector<int> cand_name_id;    // per cand: dense splice-base name id, or -1 (not a splice base)
    std::vector<int> cand_pos;        // per cand: # same-named copies in hand BEFORE its hand_index
    std::vector<int> cand_k;          // per cand: splice_count (hoisted out of the fat Action struct)
    std::vector<int> name_hand_count; // per name id: copies of that name in hand (the old N)
    std::vector<int> splice_groups;   // group indices holding a splice-base option -- the ONLY digits
                                      // the predicate reads, so it walks this instead of every group.
                                      // SORTED by (name id, copy pos), which is what lets the canonical
                                      // -form check run with NO sort at all: groups are keyed by
                                      // hand_index, so every option in a group is the SAME hand card and
                                      // its name/pos are group CONSTANTS.
    std::vector<int> group_name_id;   // per group: dense splice-base name id, or -1
    std::vector<int> group_pos;       // per group: that hand card's copy position among same-named copies
    int              num_names = 0;   // 0 == no splice base among the candidates (predicate inert)
};

static void BuildSpliceOdometerIndex(const GameState& state, const std::vector<Action>& cands,
                                     const std::vector<std::vector<int>>& groups,
                                     SpliceOdometerIndex& out)
{
    const int m = static_cast<int>(cands.size());
    out.cand_name_id.assign(m, -1);
    out.cand_pos.assign(m, 0);
    out.cand_k.assign(m, 0);
    out.name_hand_count.clear();
    out.splice_groups.clear();
    out.group_name_id.assign(groups.size(), -1);
    out.group_pos.assign(groups.size(), 0);
    out.num_names = 0;

    static thread_local std::vector<int> reps;   // name id -> representative candidate index
    reps.clear();
    for (int j = 0; j < m; ++j)
    {
        const CardDefinition* d = cands[j].def;
        if (!d || !d->params.splice_onto_arcane || cands[j].kind != Action::Kind::CastFromHand)
        { continue; }
        int id = -1;
        for (size_t k = 0; k < reps.size(); ++k)
        { if (cands[reps[k]].def == d) { id = static_cast<int>(k); break; } }
        if (id < 0) { id = static_cast<int>(reps.size()); reps.push_back(j); }
        out.cand_name_id[j] = id;
        out.cand_k[j]       = cands[j].splice_count;
    }
    out.num_names = static_cast<int>(reps.size());
    if (out.num_names == 0) { return; }

    const Player& ap = state.ActivePlayer();
    const int     hs = static_cast<int>(ap.hand.size());
    out.name_hand_count.assign(out.num_names, 0);
    for (int h = 0; h < hs; ++h)
    {
        for (int id = 0; id < out.num_names; ++id)
        { if (ap.hand[h].m_name == cands[reps[id]].card_name) { ++out.name_hand_count[id]; break; } }
    }
    for (int j = 0; j < m; ++j)
    {
        const int id = out.cand_name_id[j];
        if (id < 0) { continue; }
        const std::string& nm = cands[reps[id]].card_name;
        int pos = 0;
        for (int h = 0; h < cands[j].hand_index && h < hs; ++h)
        { if (ap.hand[h].m_name == nm) { ++pos; } }
        out.cand_pos[j] = pos;
    }
    // The digits the predicate actually reads. Every other group is a payoff/land card whose choice it
    // ignores, so walking only these turns the per-position cost from O(groups) into O(splice groups)
    // -- on a Dragonstorm go-off hand a handful instead of the whole hand. Name and pos are group
    // constants (one hand card per group), so we cache them and pre-sort by (name, pos): the walk then
    // visits each name's copies in ascending position order, which IS the order the canonical-form
    // check needs -- so it needs no sort of its own.
    for (size_t g = 0; g < groups.size(); ++g)
    {
        for (int j : groups[g])
        {
            if (out.cand_name_id[j] < 0) { continue; }
            out.group_name_id[g] = out.cand_name_id[j];
            out.group_pos[g]     = out.cand_pos[j];
            out.splice_groups.push_back(static_cast<int>(g));
            break;
        }
    }
    std::sort(out.splice_groups.begin(), out.splice_groups.end(),
              [&out](int a, int b) {
                  if (out.group_name_id[a] != out.group_name_id[b])
                  { return out.group_name_id[a] < out.group_name_id[b]; }
                  return out.group_pos[a] < out.group_pos[b];
              });
}

// Shared read-only stand-in for a deck with no splice cards. Namespace scope, NOT a function-local
// static: `Solve` is the rollout leaf (one call per search node), and a function-local static costs a
// thread-safe-init guard check on every one of those calls.
static const SpliceOdometerIndex g_no_splice_index{};

// Generate-time, BYTE-IDENTICAL analogue of SubsetHasIllegalSplice applied to the odometer's GROUP
// selection (Solve's default enumeration). Splice bases are CastFromHand actions (hand_index >= 0), so
// they always live in a group -- never in the independent 2^num_ind set -- which means the group choice
// ALONE determines splice legality (an imask extension adds only non-splice-base actions, so it can
// never make an over-splice legal). Rejecting an over-splicing group choice before its inner imask loop
// therefore skips exactly the subsets consider()'s SubsetHasIllegalSplice already discards -> identical
// `best`. This prunes the illegal-over-splice majority the Dragonstorm go-off hand generates (N copies
// of Desperate Ritual -> N groups x (N options) -> most k-assignments violate the triangular N-1-j
// bound) before paying the per-subset cost. Mirrors SubsetHasIllegalSplice exactly; gate the call on a
// one-time any_splice flag so every non-splice deck skips it entirely (byte-identical, zero overhead).
//
// MERGED with the Desperate Ritual SPLICE-COUNT COLLAPSE into ONE pass; `check_collapse` selects
// whether that half runs (HEURISTIC -- DragonstormProvider::UseSpliceCollapse() +
// MTG_UNPRUNED(SpliceCollapse)). With the collapse on, CollectActions emits only two splice variants
// per copy -- BARE (k=0) and ONE-SPLICE (k=1) -- so the splice count is CAPPED at 1 (the user's "one
// splice per" model: the front-loaded max chain needs ~8 mana up front and overshoots this deck's
// typical lines; a k=1 splice needs only 4 mana and each self-funds the next). Because same-named
// copies are identical, a selection is fully described by (m = bases cast, s = how many splice one
// other), so only the canonical form is kept: the cast bases occupy positions {0,1,...,m-1} AND the k
// values, read in position order, are non-increasing. That collapses every symmetric/duplicate {0,1}
// mix -- e.g. {0,1,1,0} -> canonical {1,1,0,0} -- to one representative per (m,s), while still letting
// the SEARCH pick the affordable (m,s) by ROLLING each out (a splice the mana can't front simply fails
// in the rollout) -- i.e. mana-derived within the k<=1 bound, no deterministic mana fill needed. The
// triangular legality (s <= N-1: the last cast copy has no other to splice) is the OTHER half of this
// function, which still runs alongside.
//
// Both halves used to do their own gather + std::sort over the same projection of `choice`; both now
// read the precomputed SpliceOdometerIndex instead of re-deriving names, hand counts and copy
// positions from strings at every odometer position. Byte-identical: each half tests exactly the same
// conditions, and the result is an OR over them, so evaluating them interleaved per name (rather than
// one predicate fully, then the other) cannot change the boolean.
static bool SpliceGroupChoiceRejected(const SpliceOdometerIndex& idx,
                                      const std::vector<std::vector<int>>& groups,
                                      const std::vector<int>& choice,
                                      bool check_collapse)
{
    if (idx.num_names <= 0) { return false; }
    // One walk of the (name, pos)-sorted splice groups serves BOTH checks. Because the walk already
    // visits a name's copies in ascending position order, the canonical-form test is a running
    // comparison (no sort), and only the triangular test needs the run's k values ordered -- a tiny
    // descending insertion sort over at most (copies in hand) ints. thread_local scratch keeps the
    // batch/gen worker pool race-free (non-re-entrant leaf -> one buffer per thread is safe).
    static thread_local std::vector<int> ks;   // current name run's selected splice_counts

    // Triangular legality for one name's run: casting m bases with splice counts k (DESCENDING) is
    // physically possible iff the j-th splices at most N-1-j others, N = copies of that name in hand.
    auto run_violates = [&](int name_id) -> bool
    {
        const int n = static_cast<int>(ks.size());
        if (n == 0) { return false; }
        for (int i = 1; i < n; ++i)          // insertion sort, DESCENDING
        {
            const int v = ks[i];
            int q = i - 1;
            while (q >= 0 && ks[q] < v) { ks[q + 1] = ks[q]; --q; }
            ks[q + 1] = v;
        }
        const int N = idx.name_hand_count[name_id];
        for (int t = 0; t < n; ++t) { if (ks[t] > N - 1 - t) { return true; } }
        return false;
    };

    int cur_name = -1;      // name id of the run being walked
    int cast_cnt = 0;       // cast bases seen so far in this run (== the expected copy position)
    int last_k   = 0;       // previous cast base's k, for the non-increasing canonical form
    ks.clear();
    for (int g : idx.splice_groups)
    {
        const int name = idx.group_name_id[g];
        if (name != cur_name)
        {
            if (cur_name >= 0 && run_violates(cur_name)) { return true; }
            cur_name = name; cast_cnt = 0; last_k = 0; ks.clear();
        }
        if (choice[g] <= 0) { continue; }
        const int j = groups[g][choice[g] - 1];
        if (idx.cand_name_id[j] < 0) { continue; }   // selected option is not a splice base
        const int k = idx.cand_k[j];
        if (check_collapse)
        {
            // Cast bases must occupy positions {0,1,...,m-1} (a prefix; dedup of identical copies)...
            if (idx.group_pos[g] != cast_cnt) { return true; }
            // ...and the splicers must be the FIRST s of that prefix -> k non-increasing in position
            // order, so each (m,s) is enumerated once (rejects e.g. {0,1} == canonical {1,0}).
            if (cast_cnt > 0 && k > last_k) { return true; }
        }
        last_k = k; ks.push_back(k); ++cast_cnt;
    }
    return cur_name >= 0 && run_violates(cur_name);
}

// Dragonstorm acceleration-prefix collapse (HEURISTIC -- unlike GroupChoiceOverSplices this changes WHICH
// action masks are enumerated, so it is NOT byte-identical: callers gate it behind
// DragonstormProvider::UseAccelPrefixCollapse() && !DecisionUnpruned(UnprunedGate::AccelPrefix)). On a
// non-lethal go-off hand the odometer powersets the K ritual accelerants (ritual_floating_mana > 0: Rite
// of Flame, Pyretic/Desperate Ritual, Seething Song, Irencrag Feat) into 2^K positions, and this fires at
// EVERY node of a full-depth rollout -> a combinatorial straggler (docs/design/dragonstorm-search-pruning.md
// Step 2). A ritual chain funds itself cheapest-first (each cheap ritual pays for the next), so for any
// storm count j the CHEAPEST j accelerants dominate every other size-j accelerant subset (identical storm
// count, >= mana); enumerating only the K+1 cheapest-first PREFIXES therefore preserves the reachable
// (storm, mana) frontier while collapsing 2^K -> K+1. This returns true (skip) when the CAST accelerant
// groups are NOT a cheapest-first prefix: order accelerant groups by (base cast ManaValue, hand_index)
// ascending and reject any position that casts an accelerant sitting AFTER an un-cast cheaper one. Only
// WHICH accelerant groups are cast is constrained -- the splice-k option WITHIN a cast Desperate Ritual
// stays free (storm-vs-mana is still a search choice; step 1's over-splice skip handles its legality), and
// the payoff / Irencrag "one more spell" (max_casts_after) legality is enforced elsewhere. The ordering key
// is choice-INDEPENDENT (min effective cost over the group's ritual-cast options + the group's unique
// hand_index as tiebreak) so the sort is stable across every odometer position. Callers gate on a one-time
// any_accel flag so a deck with no ritual accelerant pays nothing.
// Choice-INDEPENDENT precompute for the collapse: order the accelerant GROUPS cheapest-first by
// (min ritual-cast ManaValue over the group, group hand_index). Depends only on cands/groups/
// group_hand_index -- all fixed across a Solve's odometer -- so build it ONCE per plan enumeration
// instead of the old per-choice recompute+sort (profiled ~21% of rollout even after the alloc fix,
// dominated by this choice-independent work). Fills order_out with the accelerant group indices in
// that order; < 2 groups means the prefix rule can never fire (leave order_out empty).
static void BuildAccelPrefixOrder(const std::vector<Action>& cands,
                                  const std::vector<std::vector<int>>& groups,
                                  const std::vector<int>& group_hand_index,
                                  std::vector<int>& order_out)
{
    struct AccelG { int base_mv; int hand_index; int group; };
    static thread_local std::vector<AccelG> accel;   // synchronous (completes before the odometer) -> safe
    accel.clear();
    for (size_t g = 0; g < groups.size(); ++g)
    {
        int base_mv = -1;   // min effective MV over this group's ritual-cast options (choice-independent)
        for (int j : groups[g])
        {
            const CardDefinition* d = cands[j].def;
            if (d && d->params.ritual_floating_mana > 0 && cands[j].kind == Action::Kind::CastFromHand)
            {
                int mv = cands[j].cost.ManaValue();
                if (base_mv < 0 || mv < base_mv) { base_mv = mv; }
            }
        }
        if (base_mv < 0) { continue; }   // not an accelerant group
        accel.push_back({ base_mv, group_hand_index[g], static_cast<int>(g) });
    }
    order_out.clear();
    if (accel.size() < 2) { return; }   // 0/1 accelerant -> every selection is trivially a prefix
    std::sort(accel.begin(), accel.end(), [](const AccelG& a, const AccelG& b) {
        if (a.base_mv != b.base_mv) { return a.base_mv < b.base_mv; }
        return a.hand_index < b.hand_index;
    });
    for (const AccelG& a : accel) { order_out.push_back(a.group); }
}

// Lowest odometer DIGIT (group index) whose selection can change any of the three group predicates:
// a group holding a splice base, or an accelerant group in accel_order. All three read choice[] ONLY
// for splice-base / accelerant groups, so when the odometer's carry stops BELOW this digit the
// predicate projection is unchanged and the previous result can be reused -- which is what turns the
// per-plan-position predicate cost into a per-accelerant-position one (the payoff groups sitting
// below the lowest ritual group stop multiplying it). Returns groups.size() when nothing is relevant
// (compute once, never again); 0 reproduces the old recompute-every-position behaviour.
static int MinPredicateDigit(const std::vector<std::vector<int>>& groups,
                             const SpliceOdometerIndex& sidx,
                             const std::vector<int>& accel_order)
{
    int lo = static_cast<int>(groups.size());
    for (int g : sidx.splice_groups) { if (g < lo) { lo = g; } }
    for (int g : accel_order)        { if (g < lo) { lo = g; } }
    return lo;
}

// Per-CHOICE check (cheap -- just a walk of the precomputed order): the CAST accelerant groups are NOT a
// cheapest-first prefix iff a cast group sits AFTER an un-cast cheaper one. Empty order (< 2 accelerant
// groups) never fires. Byte-identical to the old GroupChoiceNonPrefixAccel (same order, same cast test).
static inline bool NonPrefixAccelViolated(const std::vector<int>& accel_order, const std::vector<int>& choice)
{
    bool saw_uncast = false;
    for (int g : accel_order)
    {
        if (choice[g] > 0) { if (saw_uncast) { return true; } }
        else               { saw_uncast = true; }
    }
    return false;
}

// Forward decl so the storm go-off short-circuit in Solve/EnumeratePlans can VERIFY a projected win by
// actually simulating the line (ApplyPlanDirect is defined later). Default arg lives here (the earliest
// declaration) so those callsites can omit out_breakpoint.
static void ApplyPlanDirect(GameState& state, const TurnSolver::Plan& plan, bool is_pre_combat,
                            std::vector<Action>* out_breakpoint = nullptr);

// Forward decl: ApplyPlanDirect's SEARCHED breakpoint continuation (Plan::bp_choice) picks its
// candidate from the same land-folded plan set the outer search ranks. Defined later.
static std::vector<TurnSolver::Plan> EnumeratePlansWithLand(const GameState& state,
                                                            bool is_pre_combat);

// ---- Searched mid-turn breakpoints (docs/design/post-breakpoint-search.md) -------------------
// MTG_BP_SEARCH=<W> turns the post-breakpoint continuation into a real search node: the land
// enumeration emits W extra Plan variants (bp_choice = 0..W-1) for every plan that opens a
// breakpoint, and the OUTER rollout scores each one. ADOPTED default W=2 -- measured the knee of
// the curve: it takes essentially all of the available quality (TH -0.05..-0.09 avg, Dragonstorm
// -0.02..-0.044, burn -0.002, Hinata neutral-to-better) where W=4 adds only -0.002..-0.01 more for
// +30-35% nodes. **MTG_BP_SEARCH=0 restores the old greedy engine byte-identically** and is the
// A/B hatch. Read once so the hot paths pay one load.
static int BpSearchWidth()
{
    static const int w = []() -> int
    {
        const char* v = std::getenv("MTG_BP_SEARCH");
        if (v == nullptr || *v == '\0') { return 2; }
        int n = std::atoi(v);
        return n < 0 ? 0 : n;
    }();
    return w;
}

// How many breakpoints deep WAVE 0's fan-out reaches (MTG_BP_DEPTH). Breakpoint i in [0, depth)
// gets its own W variants via Plan::bp_at, so wave 0 emits depth*W variants per base plan.
//
// DEFAULT 1 -- and that is now a COST prune, not a quality one. A constant depth here was tried
// twice as the whole answer and rejected both times: TRAIN (smoke + regression) went 10 better / 0
// worse and the suite got FASTER (59s -> 33s, most depth-2 variants dedup away), but the HELD-OUT
// overnight seeds reversed it (3 better, 8 worse, net +0.00020 avg). Every held-out slowdown
// RECOVERS at depth 9 / unbounded, so the cause was budget DILUTION -- doubling wave 0's candidate
// set makes every rollout shallower at a fixed per-decision budget -- not a defect.
//
// So nesting takes the SAME shape the rank axis already took (see DEFERRED CONTINUATION WAVES):
// defer, don't cap. Wave 0 stays narrow (depth 1) so a node whose budget the search already spent
// is byte-identical, and the wave phase walks bp_at = 1, 2, ... afterwards on whatever budget is
// left, LEARNING how many breakpoints the apply actually reached (g_bp_seen_last) rather than
// assuming a number. Unbounded budget => every nested breakpoint is reached => coverage is
// structural. Raising this promotes nesting into wave 0 for an A/B.
// See docs/design/post-breakpoint-search.md.
static bool SearchedPlayActive();   // defined with the searched-play depth gate, below

// Searched land-ETB scry/surveil disposition (MTG_SCRY_SEARCH, default ON; MTG_SCRY_WIDTH caps how
// many candidates the enumeration emits, heuristic-first). Off => the provider heuristic decides
// every look at resolution, byte-identical to before the branch existed.
//
// DEPTH-GATED like cantrip-first and the Ponder branch: at depth 0 there is no rollout to score the
// variants, so an extra plan variant would not be a search -- just enumeration order picking a
// different fixed rule. Held-out overnight: 72 games faster, 5 slower (all five budget churn --
// each recovers at 4x and stays recovered at 16x), all 8 Treasure Hunt cases improved, -0.0690
// summed, and no measurable wall-time cost. See docs/design/searched-scry-disposition.md.
static bool ScrySearchEnabled()
{
    static const bool on = EnvOn("MTG_SCRY_SEARCH", true);
    return on && SearchedPlayActive();
}
// NOT under-measured -- SATURATED, which is why there is no sweep to point at. Both modelled ETB
// look cards examine exactly ONE card (Temple of Epiphany etb_scry 1, Thundering Falls etb_surveil 1,
// Treasure Hunt the only deck running either), and for a 1-card look EnumerateTopDispositions yields
// 2^1 = 2 dispositions, so TopDispositionCandidates returns 2 and `min(k, width)` is already the
// whole legal space at width 2. Raising this is a provable no-op at the current card pool.
// It starts to matter the moment a multi-card look is modelled: k becomes 2^m ordered subsets
// (4 at scry 2, 8 at scry 3), and only THEN is the prune real and a sweep meaningful.
static std::size_t ScrySearchWidth()
{
    static const std::size_t w = []() -> std::size_t
    {
        const char* v = std::getenv("MTG_SCRY_WIDTH");
        if (v == nullptr || *v == '\0') { return 2; }
        const int n = std::atoi(v);
        return n < 1 ? 1 : static_cast<std::size_t>(n);
    }();
    return w;
}

// ---- Cost-neutral action sub-decisions as a post-dedup AXIS -----------------------------------
// A sub-decision that does not change what the plan can AFFORD (a tutor target, Ponder's
// keep-vs-shuffle) produces variants with an identical cast-NAME set -- and EnumeratePlans'
// autonomous plan_signature keys only on names, so the dedup silently kept the first and threw the
// rest away. The decision was therefore never searched at all; it was the provider's ordering
// wearing a search's clothes. (The signature comment says so outright: "NOT a correctness property
// -- an efficiency shortcut that DELEGATES the sub-decision to the heuristic".)
//
// The fix is the shape the engine already uses for every other inline sub-decision (fetch_target,
// land_face, scry_choice, bp_choice): emit ONE option inside the odometer, then fan the alternatives
// out AFTER the dedup as a second axis. Cost becomes P+W instead of P*k, and the options are really
// scored. MTG_TUTOR_AXIS=0 restores the old collapse-to-heuristic behaviour for the A/B.
static bool TutorAxisEnabled()
{
    static const bool on = EnvOn("MTG_TUTOR_AXIS", true);
    return on;
}
// MTG_TUTOR_AXIS_RESOLVE=1 (default off): bind the searched tutor pick by INDEX resolved at the
// TRUE per-plan state, instead of by NAME ranked at the shared pre-land turn-start state. This is
// the honest form of the located axis defect (see the fan-out note in EnumeratePlansWithLand):
// collection emits ONE cast with an empty target, the fan-out emits Plan::tutor_choice variants,
// and the ranking runs inside PerformTutor at each plan's own resolution state -- land played,
// prefix casts applied and PAID FOR, the source already on the battlefield. Every prior state fix
// (POSTLAND, PLAN_AWARE, PENDING_LAND) approximated parts of that state from the outside; none of
// them saw the spent mana, which is what makes "castable this turn" an honest read at resolution
// (a to-hand fetch can never be cast this turn, and at the resolution state the mana that made
// t=0 look plausible is genuinely gone). Same index-binding architecture as scry_choice /
// etbdig_choice / ponder_choice. Base plans keep tutor_choice = -1 == the provider's front at
// that same state, so base and variants come from ONE ranking at ONE state by construction --
// the incoherence AXIS_REBASE existed to patch cannot arise.
static bool TutorAxisResolveMode()
{
    return TutorAxisResolveEnabled();   // shared reader (EngineFlags.h) -- providers branch on it too
}
// How many tutor targets the axis scores, INCLUDING the provider's best (so 1 == the old
// heuristic-only behaviour). The provider orders candidates best-first, so this is a pure cost
// prune in preference order, exactly like MTG_SCRY_WIDTH.
//
// The width is PROVIDER-OWNED (DecisionProvider::TutorSearchWidth) rather than one global constant,
// because the per-deck optima genuinely diverge rather than scatter around a shared value. Goblins
// keeps improving to W=12 (Matron's ~16 Goblin names are not close substitutes); antilife's pool
// IS 2 (its only enchantments are Tainted Remedy and Aria of Flame, and it runs no artifacts), so
// everything above 2 was enumeration cost for a provably empty gain. Held-out confirmation of the
// finished config vs this global 6: goblins -0.0620 (8/8 cases), antilife 0.0000 with identical
// play, d0 unmoved. Hinata was ALSO trained to 2 and did NOT survive the holdout (+0.0009, and no
// cheaper), so it keeps this base -- see docs/design/searched-action-subdecisions.md.
//
// MTG_TUTOR_WIDTH overrides every provider for the A/B; unset (or 0) defers to the provider.
static std::size_t TutorAxisWidthOverride()
{
    static const std::size_t w = []() -> std::size_t
    {
        const char* v = std::getenv("MTG_TUTOR_WIDTH");
        if (v == nullptr || *v == '\0') { return 0; }   // 0 == not set == defer to the provider
        const int n = std::atoi(v);
        return n < 1 ? 1 : static_cast<std::size_t>(n);
    }();
    return w;
}
static std::size_t TutorAxisWidth(const GameState& s)
{
    const std::size_t ovr = TutorAxisWidthOverride();
    if (ovr > 0) { return ovr; }
    const int w = ResolveProvider(s).TutorSearchWidth();
    return w < 1 ? 1 : static_cast<std::size_t>(w);
}

// SEARCHED ETB-DIG PICK (Acclaimed Contender). Same additive post-dedup axis as the tutor target:
// the dug card goes to HAND and cannot be cast this turn, so the pick does not change what the plan
// affords and every variant shares a cast-name signature -- which is exactly why the dedup inside
// EnumeratePlans used to discard all but the provider's first pick. DEFAULT ON; =0 restores the
// pure heuristic.
static bool EtbDigAxisEnabled()
{
    static const bool on = EnvOn("MTG_ETBDIG_AXIS", true);
    return on;
}
// How many dig candidates the axis scores, INCLUDING the provider's best (so 1 == heuristic only).
//
// DEFAULT 3 = the measured knee, 300 games x 7 seeds spanning all three seed sets:
//     W3 - W2 = -0.0099   (3 seeds improve, 4 tie, none worse)
//     W5 - W3 = +0.0000   (identical on every seed)
// The first sweep of this width was run on seed 4004 ALONE, where W2 and W3 tie, and that single
// seed made W=2 look like the whole gain -- the mirror of the standing "sum all seed sets before
// calling a small delta a win" rule, applied to calling one a non-win. The distribution
// (MTG_ETBDIG_TRACE: 5.9% one legal match, 60.0% two, 6.9% three, 23.4% four, 3.7% five, mean 2.6)
// explains the shape: past 3 the extra candidates are ones the provider already ranked last.
static std::size_t EtbDigAxisWidth()
{
    static const std::size_t w = []() -> std::size_t
    {
        const char* v = std::getenv("MTG_ETBDIG_WIDTH");
        if (v == nullptr || *v == '\0') { return 3; }
        const int n = std::atoi(v);
        return n < 1 ? 1 : static_cast<std::size_t>(n);
    }();
    return w;
}

// SEARCHED PONDER KEEP-vs-SHUFFLE. The decision has never actually been searched: the variants were
// emitted inside CollectActions where the name-only dedup deleted them (see the CORRECTION at that
// site). This is the post-dedup AXIS that makes it real, the same shape as the tutor target.
//
// UNLIKE the tutor and ETB-dig axes this is NOT free of same-turn interaction: Ponder DRAWS, and a
// plain cantrip is one of the five breakpoint sites, so the keep-vs-shuffle choice changes which card
// arrives and therefore what the breakpoint re-solve can cast this turn. Each variant carries a
// nested re-solve rather than one cheap rollout -- the cost shape that lost the budget race for the
// cantrip-first class (+113% interior nodes for +2% rollout calls). DEFAULT OFF pending measurement.
//
// `partial` mode fans out only when the heuristic's call is CLOSE -- the looked-at set is mixed
// (at least one card the provider wants and at least one it does not). A set that is all-wanted or
// all-unwanted is not a real decision, so spending a variant on it is pure cost.
static bool PonderAxisEnabled()
{
    static const bool on = EnvOn("MTG_PONDER_AXIS", true);
    return on;
}
static bool PonderAxisPartial()
{
    static const bool on = EnvOn("MTG_PONDER_PARTIAL");
    return on;
}
// ORDER axis: branch on the disposition (which card ends up on TOP, plus shuffle) rather than only
// keep-vs-shuffle. Ponder draws immediately, so the top card is the one received now.
static bool PonderOrderAxis()
{
    static const bool on = EnvOn("MTG_PONDER_ORDER");
    return on;
}
static std::size_t PonderOrderWidth()
{
    static const std::size_t w = []() -> std::size_t
    {
        const char* v = std::getenv("MTG_PONDER_ORDER_WIDTH");
        if (v == nullptr || *v == '\0') { return 4; }   // heuristic + shuffle + 2 top-card variants
        const int n = std::atoi(v);
        return n < 1 ? 1 : static_cast<std::size_t>(n);
    }();
    return w;
}

// Is the top `n` of the library a MIXED set (some wanted, some not)? Used only to decide whether the
// partial axis bothers emitting a variant; read off the library as it stands at enumeration time,
// which an earlier cantrip in the same plan can shift -- that staleness costs a wasted variant or a
// missed one, never correctness (both ponder_keep values are always legal).
static bool PonderSetIsMixed(const GameState& state, int n)
{
    const Player& ap = state.players[state.active_player_index];
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 0) { return false; }
    int wanted = 0;
    for (int i = 0; i < look; ++i)
    { if (ResolveProvider(state).ScryKeepOnTop(state, ap.library[i])) { ++wanted; } }
    return wanted > 0 && wanted < look;
}

// How many legal dig matches the top `pp.etb_dig_count` of the library holds RIGHT NOW. Used only to
// SIZE the axis at enumeration time; the real candidate list is rebuilt at resolution (an earlier
// cantrip in the same plan can shift the top N), and the pin clamps if this over-counts.
static std::size_t EtbDigCandidateCountNow(const GameState& state, const CardParams& pp)
{
    const Player& ap = state.players[state.active_player_index];
    const int n = std::min(pp.etb_dig_count, static_cast<int>(ap.library.size()));
    std::size_t matches = 0;
    for (int i = 0; i < n; ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
        const SubtypeSet& subs = d ? d->card.m_subtypes : ap.library[i].m_subtypes;
        for (const std::string& want : pp.etb_dig_subtypes)
        {
            bool match = false;
            for (const std::string& cs : subs) { if (cs == want) { match = true; break; } }
            if (match) { ++matches; break; }
        }
    }
    return matches;
}

// SEARCHED GOBLIN LACKEY PUT. The put is free and lands after attackers are declared, so like the
// tutor target and the ETB dig it is cost-neutral and its variants all share a cast-name signature
// -- the dedup inside EnumeratePlans kept only the provider's top pick.
//
// DEFAULT ON; =0 restores the pure heuristic. This is the case that justifies the "a heuristic is a
// branch's DEFAULT, not a substitute for branching" rule: highest-MV was measured BEST of four
// rankings (a bad rule costs 1.47 turns, every sensible one lands within 0.06 --
// docs/design/lackey-put-ranking.md), and the search STILL finds -0.0499 on top of it, 7/7 seed
// sets. A good heuristic and a live branch are not substitutes.
static bool LackeyAxisEnabled()
{
    static const bool on = EnvOn("MTG_LACKEY_AXIS", true);
    return on;
}
// Width 2 -- the ranked top two. Measured identical to W=3 and W=4 on every held-out seed, which
// says the search's whole contribution is "occasionally the provider's #2 is better than its #1",
// not a deep re-ranking. Costs +12% makespan on goblins; no other deck has a cheat source, so no
// other deck pays anything.
static std::size_t LackeyAxisWidth()
{
    static const std::size_t w = []() -> std::size_t
    {
        const char* v = std::getenv("MTG_LACKEY_WIDTH");
        if (v == nullptr || *v == '\0') { return 2; }
        const int n = std::atoi(v);
        return n < 1 ? 1 : static_cast<std::size_t>(n);
    }();
    return w;
}

// How many matching permanents a cheat trigger could choose among RIGHT NOW, and whether a source
// that can attack is even on the board. Used only to SIZE the axis at enumeration time; the real
// list is rebuilt at resolution and the pin clamps if this over-counts.
static std::size_t LackeyCandidateCountNow(const GameState& state)
{
    const int active = state.active_player_index;
    bool have_source = false;
    std::vector<std::string> want;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || d->params.combat_damage_puts_subtype_from_hand.empty()) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        have_source = true;
        want = d->params.combat_damage_puts_subtype_from_hand;
        break;
    }
    if (!have_source) { return 0; }
    std::size_t n = 0;
    for (const Card& c : state.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(c);
        const Card& hc = hd ? hd->card : c;
        if (hc.IsInstant() || hc.IsSorcery()) { continue; }
        for (const std::string& sub : want) { if (CardHasSubtype(hc, sub)) { ++n; break; } }
    }
    return n;
}

static int BpSearchDepth()
{
    static const int d = []() -> int
    {
        const char* v = std::getenv("MTG_BP_DEPTH");
        if (v == nullptr || *v == '\0') { return 1; }
        const int n = std::atoi(v);
        return n < 1 ? 1 : n;
    }();
    return d;
}

// Per-SITE enable mask (MTG_BP_SITES, default all five). Bit i == site i of kBpSiteName:
//   0 stages/EI (Light Up the Stage, Expressive Iteration)   1 DrawUntilNonland (Treasure Hunt)
//   2 impulse_exile (Apex of Power)                          3 plain cantrip (Ponder / Preordain)
//   4 dig-through-lands (cycle / sacrifice)
//
// This mask is the SEARCHABILITY of a class and therefore also its NUMBERING: it decides which
// breakpoints `bp_at` counts, in the apply (ApplyPlanDirect) and in the executor's replay
// (AIEngine::resolve_draw_breakpoint) alike. The two MUST read the same mask or a committed
// continuation replays at the wrong breakpoint, so this is deliberately one global value and not
// something a plan or a wave carries.
//
// DEFAULT 0x17 = every class EXCEPT bit 3 (plain cantrip), and that exclusion is an ADMITTED
// QUALITY PRUNE, not a design: it makes Ponder/Preordain continuations unreachable at any budget,
// which is exactly what the wave mechanism exists to eliminate everywhere else.
//
// It stays off because it is MEASURED to cost, on held-out (overnight) seeds, isolated 2026-07-31
// by running the two axes separately on Hinata:
//     nesting only  (MTG_BP_NEST_DISCOVER=1, this mask)  ->  0.0000  (every score identical)
//     cantrip only  (MTG_BP_NEST_DISCOVER=0, mask 0x1F)  -> +0.0392  (avg win turn, WORSE)
// so the plain-cantrip class is the whole of the Hinata regression and nesting is free. It also
// does NOT respond to the obvious cost knob: deferring the class out of wave 0
// (MTG_BP_W0_SITES=0x17 with this mask at 0x1F) made Hinata WORSE still (+0.0666), so the wave-
// reached cantrip continuations are being mis-RANKED, not merely mis-afforded. That points at the
// continuation ranking / leaf scoring, which is the open work -- do not retry the W0 knob blind.
//
// MTG_BP_SITES=31 turns it on and is the benchmark this prune must eventually justify itself
// against. See docs/design/post-breakpoint-search.md.
static int BpSiteMask()
{
    static const int m = []() -> int
    {
        const char* v = std::getenv("MTG_BP_SITES");
        if (v == nullptr || *v == '\0') { return 0x17; }
        return std::atoi(v) & 0x1F;
    }();
    return m;
}

// A/B hatch for the NESTING axis alone (MTG_BP_NEST_DISCOVER=0). The wave walker learns how many
// breakpoints an apply reached and opens slots for bp_at = 1, 2, ...; setting this to 0 keeps only
// the bp_at < BpSearchDepth() slots, i.e. the pre-nesting engine, while leaving the site mask and
// the rank waves alone. Exists so the two axes can be attributed separately -- MTG_BP_W0_SITES
// isolates the class axis, this one isolates nesting.
static bool BpNestDiscover()
{
    static const bool on = []() -> bool
    {
        const char* v = std::getenv("MTG_BP_NEST_DISCOVER");
        if (v == nullptr || *v == '\0') { return true; }
        return std::atoi(v) != 0;
    }();
    return on;
}

// Which classes get a WAVE-0 fan-out (MTG_BP_W0_SITES), a subset of BpSiteMask. PURE COST PRUNE:
// a class dropped here is still fully searchable -- the deferred wave phase picks its plans up at
// rank 0, exactly as it does for a plan MTG_BP_MAXBASE dropped. Default = the full mask (no prune);
// set e.g. 0x17 to keep the plain-cantrip class out of wave 0 while leaving it reachable.
static int BpWave0SiteMask()
{
    static const int m = []() -> int
    {
        const char* v = std::getenv("MTG_BP_W0_SITES");
        if (v == nullptr || *v == '\0') { return BpSiteMask(); }
        return std::atoi(v) & BpSiteMask();
    }();
    return m;
}

// Re-entrancy guard: the breakpoint's OWN enumeration must not emit further bp_choice variants.
// Only the first breakpoint of an apply is searched (deeper ones stay greedy), so nested variants
// would be pure duplicates burning nodes. Thread-local -- the search is single-threaded per game.
static thread_local int g_bp_enum_depth = 0;

// ---- How many continuations does this breakpoint actually HAVE? ------------------------------
// bp_choice = k indexes cands[k] of a HEURISTICALLY RANKED list, so a continuation ranked >= W is
// unreachable at ANY depth and ANY budget -- the width cap is a QUALITY prune, not a cost prune
// (proof: Hinata seed 4259 gi255 at --budget-ms 0 is T6 at W=2 and T5 at W=16). The fix (deferred
// fallback waves, docs/design/post-breakpoint-search.md "The width cap is itself a quality prune")
// must not hard-code a wider number -- it has to KNOW the list length. That length is only knowable
// at APPLY time, because the breakpoint state depends on the base plan's own casts, so it is
// recorded here by the apply that computed it and read back by the decision node afterwards:
//
//     g_bp_cands_last = 0;
//     ApplyPlanDirect(copy, plan, ...);      // wave 0, bp_choice = 0..W-1
//     const int n = g_bp_cands_last;         // this plan's continuation count (0 = no breakpoint)
//
// Exact per-plan attribution with no map and no extra enumeration: at most one breakpoint per apply
// is eligible (the one at bp_at), so there is at most one write. Thread_local -- the search is
// single-threaded per game, and the batch runner plays games concurrently.
static thread_local int g_bp_cands_last = 0;

// The same trick for the OTHER axis: how many breakpoints of a searchable class did this apply
// actually reach? `bp_at` indexes them, and wave 0 only ever emits bp_at < BpSearchDepth(), so a
// 2nd/3rd breakpoint in the same turn would be permanently greedy if the count were assumed rather
// than measured. It cannot be known before the apply (each continuation changes what comes after
// it), so the apply records it here and BpWaveWalker reads it back to OPEN slots for bp_at = 1, 2,
// ... as they are discovered -- the same defer-don't-cap treatment the rank axis gets.
//
// Counts breakpoints of an ENABLED class only, matching the indexing `bp_at` uses.
static thread_local int g_bp_seen_last = 0;

// Lockstep trace arming flag (MTG_BP_TRACE, diagnosis only). ApplyPlanDirect runs millions of times
// inside rollouts, so an unconditional print is useless; this is set ONLY around the fd-trace's
// replay of the COMMITTED line, which is the exact apply the executor is supposed to reproduce.
// Its [bp-apply] lines then diff line-for-line against the executor's [bp-exec] lines.
static thread_local bool g_bp_trace_arm = false;

// Fan out at EVERY ply, or only at the turn's COMMITTED decision? MEASURED: root-only is nearly
// free but recovers NONE of the gain (TH stays 4.1333, Dragonstorm stays 4.6267). The greedy
// continuation's real damage is to the LEAF EVALUATOR: it makes the rollouts mis-score lines, so
// the root ranks its candidates on bad estimates. Fixing only the root therefore fixes nothing.
// Every ply is the default; MTG_NO_BP_SEARCH_ROLLOUT restores root-only for the A/B (it is also
// the cheap fallback if a future deck turns out to be budget-starved by the fan-out).
// See docs/design/post-breakpoint-search.md.
static thread_local bool g_bp_root_enum = false;
static bool BpSearchInRollouts()
{
    static const bool on = !EnvOn("MTG_NO_BP_SEARCH_ROLLOUT");
    return on;
}

// Commit-the-line recursion depth. The committed decision is the OUTERMOST FSLineWin node of a
// pass (its `state` is the real game state); everything below it is lookahead. Nesting is the
// robust discriminator -- iterative deepening runs many passes, and each pass's root re-enters at
// nest 0. Mirrors g_bp_root_enum for the SolveWithLookahead path (enforce_budget).
static thread_local int g_fsline_nest = 0;

// A no-win is a genuine refutation only if the node actually finished looking. These are the events
// that make it provisional instead: a budget overrun/exhaustion abort, and a beam that stopped the
// plan loop short. A node snapshots this counter before its loop and compares after, so a truncation
// ANYWHERE in the subtree propagates up and suppresses the no-win store at every ancestor.
// thread_local: each worker searches independently. Wins are unaffected (a win found is a win).
inline thread_local unsigned long long g_fs_trunc_events = 0;
struct FsLineNestGuard
{
    FsLineNestGuard()  { ++g_fsline_nest; }
    ~FsLineNestGuard() { --g_fsline_nest; }
};

// MTG_NO_GOFF_SHORTCIRCUIT: isolation toggle for the Dragonstorm storm go-off short-circuit (below);
// default on, disables ONLY that cut for a clean perf/GT A/B (the full search still finds the same win
// via the go-off lethal model, just after paying for the ritual/Lotus powerset).
static const bool s_no_goff_shortcircuit = EnvOn("MTG_NO_GOFF_SHORTCIRCUIT");
// MTG_NO_LETHAL_CUT: isolation toggle for the board-lethal short-circuit (Solve + EnumeratePlans). When
// the current board's attack-all damage already kills the opponent this turn, attacking with no casts
// wins now, so the cast-subset powerset is skippable (a turn-winning plan dominates every plan this turn).
// Default ON (the cut fires); set to disable it for a clean GT/perf A/B. GT-fingerprint-invariant: which
// winning plan is chosen never changes the win TURN. Generic (all decks); shares MTG_UNPRUNED(ComboLine).
static const bool s_no_lethal_cut = EnvOn("MTG_NO_LETHAL_CUT");
// Lotus-independent accel-prefix collapse is OPT-IN (default OFF, enable with MTG_LOTUS_PREFIX). The Lotus
// sacs are fungible in mana, but the plan signature keys on sac_source_id, so collapsing their branches
// churns the budget-limited search (measured a few searched slowdowns) -- so it is held behind a flag
// pending a tractability-vs-GT decision, while the byte-identical go-off short-circuit ships by default.
static const bool s_lotus_prefix         = EnvOn("MTG_LOTUS_PREFIX");

// Independent-accelerant (Lotus Bloom SacForMana) prefix collapse -- the fungible sibling of
// NonPrefixAccelViolated for the odometer's 2^num_ind independent mask. The identical Lotus sacs
// (ritual_float>0, +N of ONE colour, NO storm count) are interchangeable, so any k-subset produces the
// same (mana, storm) as the first k -- keep ONLY the lowest-index prefix and drop the rest. Returns true
// (skip) when a Lotus bit is set while an earlier Lotus bit is unset; non-accelerant independents
// (graveyard retrace) are unconstrained. Collapses 2^L -> L+1. Same family as the ritual accel-prefix
// collapse (here the subsets are effect-identical, not merely dominated), gated with it (accel_prefix_on)
// + MTG_NO_LOTUS_PREFIX. Note: SacColor variants of one source are already mutually exclusive
// (SubsetHasDuplicateSacSource / sac_source_id), so within the default red-only float each Lotus is one
// independent -> this collapses the multi-Lotus fan-out the go-off short-circuit does not cover (non-win
// building turns, and the Apex re-solve the short-circuit defers).
static inline bool IndependentAccelPrefixViolated(const std::vector<Action>& cands,
                                                  const std::vector<int>& independent, int imask)
{
    bool saw_uncast = false;
    for (int b = 0; b < static_cast<int>(independent.size()); ++b)
    {
        if (cands[independent[b]].ritual_float <= 0) { continue; }   // only Lotus-style accelerants
        if (imask & (1 << b)) { if (saw_uncast) { return true; } }
        else                  { saw_uncast = true; }
    }
    return false;
}

// Construct the canonical Dragonstorm storm go-off line for the lethal short-circuit (see the callsites in
// Solve / EnumeratePlans): one representative per mana-ritual hand card (plain cast, splice_count==0) + one
// SacForMana per distinct Lotus source + exactly one Dragonstorm (tutor_to_battlefield) payoff. This is the
// "cast every accelerant, then storm" line -- every ritual is +1 storm count and net-nonnegative mana, the
// Lotus sacs fund it (no storm), and Irencrag Feat rides along (CastOrderRank sequences it right before the
// payoff, and consider()/eval_and_push enforce its one-more-spell legality). Fills `combo` with cand indices
// and returns the Dragonstorm cand index, or -1 if no Dragonstorm is castable (no line to try). De-dups by
// hand_index / sac_source_id so a card is never listed twice (a double-listing would over-project the storm
// count and mis-flag a false lethal). We do NOT add hard-cast Dragons / other non-ritual spells: 3 Dragons
// is lethal and >=2 rituals already reach storm >=3, so the minimal line wins on its own; if it does not
// (too few rituals, or the library is short on Dragons) the callsite falls through to the full search, which
// still finds the pad-with-a-spell line. Pure over cands (called before the odometer builds its groups).
static int BuildStormGoffLine(const std::vector<Action>& cands, std::vector<int>& combo)
{
    combo.clear();
    int payoff = -1;
    for (int j = 0; j < static_cast<int>(cands.size()); ++j)
    {
        const Action& c = cands[j];
        const CardDefinition* d = c.def;
        if (!d) { continue; }
        if (d->params.tutor_to_battlefield && c.kind == Action::Kind::CastFromHand)
        {
            if (payoff < 0) { payoff = j; }   // one Dragonstorm suffices for lethal (3 Dragons)
            continue;
        }
        if (c.ritual_float <= 0) { continue; }   // accelerants only (cast rituals + Lotus sac)
        if (c.kind == Action::Kind::CastFromHand)
        {
            if (c.splice_count != 0) { continue; }   // the plain-cast representative of this ritual card
            bool dup = false;
            for (int k : combo) { if (cands[k].hand_index == c.hand_index) { dup = true; break; } }
            if (!dup) { combo.push_back(j); }
        }
        else if (c.kind == Action::Kind::SacForMana)
        {
            bool dup = false;
            for (int k : combo)
            {
                if (cands[k].kind == Action::Kind::SacForMana
                    && cands[k].sac_source_id == c.sac_source_id) { dup = true; break; }
            }
            if (!dup) { combo.push_back(j); }
        }
    }
    if (payoff >= 0) { combo.push_back(payoff); }
    return payoff;
}

// Candidate single COLOURS for a "add N mana of ONE chosen colour" float (Lotus Bloom's SacForMana
// and Apex of Power's 10-of-one-colour). The set = every colour appearing in the active player's
// NONLAND spell costs across hand / library / graveyard / battlefield (so an off-colour combo line
// stays reachable), or ALL FIVE under MTG_UNPRUNED(SacColor) so the full-search oracle can open every
// colour. Never empty (defensive red fallback for a degenerate all-colourless deck).
//
// ORDER = descending coloured-pip DEMAND (how many pips of that colour the active player's spells
// want), tiebroken W,U,B,R,G. So the colour the deck most needs is FIRST -- and "add N of any one
// colour" defaults to it: that colour WEAKLY DOMINATES every other (a coloured mana pays generic pips
// just as well, PLUS its own pips), so the search picks it in every value-tie and the human-play
// enumeration collapses to it. This is why a mono-red deck floats RED, never a dead off-colour (the
// old W,U,B,R,G order defaulted ties to White). Shared so both float sources enumerate the identical
// ordered set. NOT hardcoded red -- a blue deck floats blue, etc.
static std::vector<std::string> ChosenFloatColorCandidates(const GameState& state)
{
    const Player& ap = state.players[state.active_player_index];
    const bool open_all = DecisionUnpruned(UnprunedGate::SacColor);
    static const char* kColorLetters[5] = { "W", "U", "B", "R", "G" };
    // Provider float-colour collapse (Lotus Bloom): RED by default -- add an off-colour ONLY when a HASTE
    // creature castable THIS turn demands it. ap.hand includes Apex-staged exile cards (m_is_staged), so
    // this covers both "haste Dragon in hand" and "haste Dragon castable from an Apex exile". A floated
    // colour empties end of turn, so a haste creature is the only sink worth an off-colour sac (a non-haste
    // Dragon can wait for lands / a Dragonstorm tutor-to-battlefield). Red WEAKLY DOMINATES (pays generic +
    // red pips), so it is always kept and listed first. Collapses the per-colour Lotus fan-out to RED on
    // the vast majority of turns. HEURISTIC (not byte-identical); off under MTG_UNPRUNED(SacColor).
    if (!open_all && ResolveProvider(state).RestrictSacColorsToHasteAndRed())
    {
        bool need[5] = { false, false, false, true, false };   // R (index 3) always on
        for (const Card& c : ap.hand)
        {
            const CardDefinition* cd = CardDatabase::Instance().LookupCached(c);
            if (!cd || !cd->card.IsCreature() || !cd->card.HasKeyword(Keyword::Haste)) { continue; }
            const ManaCost& mc = cd->card.m_mana_cost;
            if (mc.white) { need[0] = true; } if (mc.blue)  { need[1] = true; }
            if (mc.black) { need[2] = true; } if (mc.green) { need[4] = true; }
        }
        std::vector<std::string> colors;
        colors.push_back("R");   // dominant default, first
        for (int c : { 0, 1, 2, 4 }) { if (need[c]) { colors.push_back(kColorLetters[c]); } }
        return colors;
    }
    int demand[5] = { 0, 0, 0, 0, 0 };   // W,U,B,R,G : total coloured pips across AP nonland spell costs
    auto scan = [&](const Card& card)
    {
        const CardDefinition* cd = CardDatabase::Instance().LookupCached(card);
        if (!cd || cd->card.IsLand()) { return; }
        const ManaCost& mc = cd->card.m_mana_cost;
        demand[0] += mc.white; demand[1] += mc.blue; demand[2] += mc.black;
        demand[3] += mc.red;   demand[4] += mc.green;
    };
    for (const Card& c : ap.hand)      { scan(c); }
    for (const Card& c : ap.library)   { scan(c); }
    for (const Card& c : ap.graveyard) { scan(c); }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        scan(p.card);
    }
    std::vector<int> idx;                                      // candidate colour indices, W,U,B,R,G order
    for (int c = 0; c < 5; ++c) { if (open_all || demand[c] > 0) { idx.push_back(c); } }
    // Stable sort by DESCENDING demand -> equal-demand colours keep their W,U,B,R,G tiebreak order.
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return demand[a] > demand[b]; });
    std::vector<std::string> colors;
    for (int c : idx) { colors.push_back(kColorLetters[c]); }
    if (colors.empty()) { colors.push_back("R"); }
    return colors;
}

// ---- Cantrip-first ordering (docs/design/cantrip-first-collapse.md) --------------------------
// Casting a cantrip EARLIER is strictly more information for every decision that follows it this
// turn, and it moves the re-solve breakpoint to where the continuation still has the whole turn
// left to decide -- a cantrip cast LAST opens its breakpoint at the end of the turn, where the
// searched continuations are near-duplicates that burn wave ranks without being able to differ.
// That is the measured failure of the plain-cantrip class (+0.0392 Hinata held-out, and WORSE when
// deferred out of wave 0, i.e. mis-RANKED rather than mis-afforded).
//
// So a plan containing a cantrip is reordered to:  [may-precede] -> [cantrips] -> [rest],
// each group keeping its relative order (stable).
//
// This is NOT applied via CastOrderRank: a set containing a cantrip is OrderingOpaque
// (params.draw > 0), which bypasses that hook entirely. Reordering the PLAN's action vector works
// because both the rollout (ApplyPlanDirect) and the executor (AIEngine::TakeTurn) follow plan
// order for opaque sets -- so the two stay in lockstep BY CONSTRUCTION rather than by twin edits.
//
// DEPTH-GATED: searched play only (see TurnSolver::SetSearchedPlay). MTG_NO_CANTRIP_FIRST=1 is the
// off-switch for the A/B; MTG_CANTRIP_FIRST=1 forces it on even at depth 0 (measurement only --
// d0 measures worse, which is the reason for the gate).
// THREAD-LOCAL, not a shared global. BatchRunner::RunManifest flattens every game of every job into
// ONE work list, so a depth-0 game and a depth-3 game of different jobs run CONCURRENTLY in the same
// process -- and this value is written by each AIEngine constructor. As a shared global, a d0 game
// starting mid-flight would flip the flag under a d3 game's search, making play depend on thread
// interleaving and breaking the harness's determinism/thread-invariance contract. Each worker builds
// and uses its own engines, so thread_local is exactly the right scope.
// See docs/design/searched-play-flag-thread-scope.md.
static thread_local bool g_searched_play = false;
void TurnSolver::SetSearchedPlay(bool enable)
{
    g_searched_play = enable;
}

// Is this run's play SEARCHED (lookahead depth > 0)? Gates the branches that only mean something
// when there is a rollout to score them -- at depth 0 an extra plan variant is not a search, it is
// just a different fixed rule chosen by enumeration order.
static bool SearchedPlayActive()
{
    return g_searched_play;
}

static bool CantripFirstEnabled()
{
    static const bool off   = EnvOn("MTG_NO_CANTRIP_FIRST");
    static const bool force = EnvOn("MTG_CANTRIP_FIRST");
    if (off) { return false; }
    return force || g_searched_play;
}

// A plain cantrip: the breakpoint class this rule targets (site 3 of PlanOpensBreakpoint -- a
// DrawSpell that is neither a staging card nor Expressive Iteration, both of which re-solve inline).
static bool IsPlainCantrip(const Action& a)
{
    if (a.kind != Action::Kind::CastFromHand) { return false; }
    const CardDefinition* d = a.def;
    if (d == nullptr) { return false; }
    return d->tmpl == CardTemplate::DrawSpell
        && !d->params.stages_cards && !d->params.expressive_iteration;
}

// May this action legitimately precede a cantrip? Two reasons, both derived from CARD PARAMS (never
// a name list -- an omission there would silently lose a real line):
//   AFFORDABILITY -- it funds the cantrip: mana acceleration (ritual float / same-turn rock /
//                    sac-for-mana) or cost reduction (Hinata, Goblin Warchief).
//   VALUE         -- it is a cast-triggered PAYOFF whose trigger the cantrip's own cast would then
//                    fire (Guttersnipe / Vivi). NOT YET REPRESENTABLE: the existing
//                    on_cast_trigger_* pair models Eidolon of the Great Revel, which damages the
//                    CASTER (a punisher, opposite polarity), and no suite deck has a payoff card --
//                    so this category is currently EMPTY, not mis-modelled. When such a card is
//                    implemented it needs a payoff-polarity param, and the check goes here.
// Where neither applies, cantrip-first is a strict dominance improvement.
static bool MayPrecedeCantrip(const Action& a)
{
    if (a.ritual_float > 0)                  { return true; }   // ritual / accelerant float
    if (a.rock_mana.Total() > 0)             { return true; }   // same-turn mana rock
    if (a.kind == Action::Kind::SacForMana)  { return true; }   // Lotus Bloom sac
    const CardDefinition* d = a.def;
    if (d == nullptr) { return false; }
    if (!d->params.reduces_spell_color.empty())   { return true; }   // cost reduction (Hinata)
    if (!d->params.reduces_spell_subtype.empty()) { return true; }   // (Goblin Warchief)
    return false;
}

static void ApplyCantripFirstOrder(std::vector<Action>& acts)
{
    if (!CantripFirstEnabled() || acts.size() < 2) { return; }
    bool any = false;
    for (const Action& a : acts) { if (IsPlainCantrip(a)) { any = true; break; } }
    if (!any) { return; }   // no cantrip in this plan -> untouched
    std::vector<Action> pre, cantrips, rest;
    pre.reserve(acts.size()); cantrips.reserve(acts.size()); rest.reserve(acts.size());
    for (Action& a : acts)
    {
        if      (IsPlainCantrip(a))    { cantrips.push_back(std::move(a)); }
        else if (MayPrecedeCantrip(a)) { pre.push_back(std::move(a)); }
        else                           { rest.push_back(std::move(a)); }
    }
    acts.clear();
    for (Action& a : pre)      { acts.push_back(std::move(a)); }
    for (Action& a : cantrips) { acts.push_back(std::move(a)); }
    for (Action& a : rest)     { acts.push_back(std::move(a)); }
}

// ---- CollectActions ------------------------------------------------------
//
// The single enumeration of action SOURCES, shared by Solve and EnumeratePlans.
// Returns every candidate play available this main phase as an Action:
//   - CastFromHand        : each legally-castable non-land spell in hand
//   - ActivateVial        : each (Aether Vial, matching-MV creature in hand) pair
//   - CastFromGraveyard   : each retrace card in the graveyard with a land to discard
// Land's Edge discards are generated as plan-level count variants by the callers,
// since they depend on the rest of the chosen subset (lands left after retrace).
// The per-Action valuation scalars are read by each caller's subset evaluator.
// The creature-sac OUTLET pre-combat deferral heuristic (Siege-Gang / Pashalik / Skirk / the multi-sac
// burst) lives in GoblinsProvider::DeferSacOutletPreCombat (ADOPTED default-ON, off-switch
// MTG_NO_GOBLIN_SAC_2ND); the guard in the sac-outlet loop below calls it. GenericProvider returns
// false, so every non-Goblins deck enumerates sac outlets pre-combat byte-identically.

static std::vector<Action> CollectActions(const GameState& state, bool is_pre_combat)
{
    const Player& ap = state.ActivePlayer();
    bool has_creature_target = HasLegalCreatureTarget(state);
    int  n = static_cast<int>(ap.hand.size());

    std::vector<Action> actions;

    // --- Hand casts ---
    for (int i = 0; i < n; ++i)
    {
        auto opt = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!opt || opt->card.IsLand()) { continue; }
        const CardDefinition& def = *opt;
        // Goldfish-inert cards (counterspells / "tap target creature" / "bounce target
        // permanent" against a passive opponent that never casts, attacks or blocks): they
        // have no useful target, so the deck genuinely cannot productively cast them. Never
        // offered as an action -> they sit in hand as faithful dead draws. Gated off for
        // every existing deck.
        if (def.params.goldfish_inert) { continue; }
        // Duplicate legend with nothing on entry: the legend rule kills it the moment it resolves,
        // so the cast buys nothing and costs a card plus this turn's mana. Provider-owned, and a
        // PRUNE rather than a ranking -- it drops a plan variant that cannot be better, which
        // matters because variants share a fixed rollout budget. Default off (MTG_PRUNE_DUP_LEGEND).
        if (!ResolveProvider(state).OfferDuplicateLegendCast(state, state.active_player_index, def))
        { continue; }
        // Suspend-only cards (Lotus Bloom, mana_cost "") have NO normal cost -- they can ONLY be
        // suspended, never hard-cast from hand. Skip the cast enumeration; the {0} Suspend action is
        // emitted in its own block below. Gated on suspend_time_counters -> other decks byte-identical.
        if (def.params.suspend_time_counters > 0) { continue; }
        // Sorceries/non-flash spells require an empty stack and a main phase.
        bool timing_ok = def.card.IsInstant()
                      || def.card.HasKeyword(Keyword::Flash)
                      || state.stack.empty();
        if (!timing_ok) { continue; }

        // Flood-engine gate: a Treasure Hunt (DrawUntilNonland) or a cascade/retrace card
        // that can cascade INTO Treasure Hunt (Throes of Chaos) is only offered when its
        // draw has a payoff this turn (Land's Edge online / castable, or a no-max-hand-size
        // land to keep it) -- otherwise the drawn lands are wasted to cleanup. Asked at this
        // single enumeration choke point so the search AND the bottoming rollouts both honor
        // it. Generic returns true (no gate), so non-Treasure-Hunt decks are byte-identical.
        if ((def.tmpl == CardTemplate::DrawUntilNonland
             || def.params.cascade_max_mv > 0 || def.params.retrace)
            && !ResolveProvider(state).ShouldCastDrawEngine(state, state.active_player_index, def))
        {
            continue;
        }

        // Skip spells that need a creature target when none exists. An own-creature pump
        // (Invigorate) needs one of OUR attackers; other creature-targeting spells need an
        // opponent creature.
        Targeting t = def.params.targeting;
        if (t == Targeting::Creature && def.params.target_own_creature)
        {
            if (FindBestOwnAttacker(state, state.active_player_index) < 0)
            {
                // An own-creature pump (Invigorate) needs one of OUR attackers for the pump. BUT
                // under a Tainted Remedy its free alt payload ("an opponent gains N life" -> N LOSS)
                // is a real face-damage burn independent of the (moot) pump, castable on ANY
                // creature (CR "target creature"). Let it through to the alt-payload block below so
                // it is offered as the lifegain-flip damage source, as long as the alt cost is
                // payable (we control a Forest) and a legal creature target exists. Autonomous stays
                // byte-identical: the alt-payload block only EMITS a safe payload under UNPRUNED /
                // ShouldEmitRiskyAltPayload, so on the goldfish path it still falls through to
                // `continue` (auto-fired) exactly as if skipped here.
                const bool alt_burn_live = def.params.alt_lifegain_cost > 0
                    && RemedyActive(state, state.active_player_index)
                    && ControlsSubtype(state, state.active_player_index, def.params.alt_cost_requires_subtype)
                    && AltPayloadTargetLegal(state, def);
                if (!alt_burn_live) { continue; }
            }
        }
        else if ((t == Targeting::Creature || t == Targeting::Multi) && !has_creature_target
                 && !def.params.controller_lifegain_equals_power)
        {
            // No opponent creature to target. A creature-burn (Searing Blood / Blaze) is normally dead
            // here -- but casting it on our OWN surviving creature triggers prowess and can be lethal.
            // Offer that "prowess line" ONLY when a prowess attacker + a surviving own target exist
            // (FindOwnProwessBurnTarget); otherwise the spell stays uncastable. The search/rollout then
            // keeps it only when it actually helps. (Swords -- controller_lifegain -- has its own gate
            // below and must not take this branch.)
            if (FindOwnProwessBurnTarget(state, def) < 0) { continue; }
        }
        // Goldfishing gate (Swords to Plowshares): its controller-lifegain rider only HELPS us with a
        // lifegain->loss enabler live when it resolves (else it just gives the passive opponent life).
        // Offer the cast when there is an opponent creature to exile AND an enabler is available this
        // turn -- in play, OR in hand (cast enabler-first, so it resolves before Swords). Emitting the
        // in-hand case lets the search find the same-turn "Tainted Remedy, then Swords" combo (a
        // strictly-faster line the old "enabler in play at turn start" gate forwent). Plan validity
        // (SubsetHasUnbackedLifegainRemoval, applied in Solve::consider + EnumeratePlans) still requires
        // the enabler to be in the plan, and ApplyPlanDirect casts it first, so Swords never resolves
        // for the passive opponent's benefit -- protecting the greedy d0 path too. Goldfishing assumption.
        if (def.params.controller_lifegain_equals_power
            && (!HasOpponentCreature(state, state.active_player_index)
                || !RemedyActiveOrInHand(state, state.active_player_index)))
        {
            continue;
        }

        // {X} spells: enumerate candidate X values (provider XCandidates narrows the range,
        // the search picks among the variants -- they share hand_index, so they are mutually
        // exclusive in the plan). Only X-damage (DirectDamage) is modeled today; other X
        // templates (DrawX) stay skipped until their effect is scaled in BOTH cast paths.
        if (def.card.m_mana_cost.has_x)
        {
            // Reality Spasm (untap RITUAL): emit ONE action that floats mana for a same-turn
            // payoff (Crackle). Only productive with Hinata in play (her discount makes the {X}
            // free), so chosen_x = #mana sources -> untap/refloat them ALL. Cost is the fixed
            // {U}{U} (the X discounts away via the same formula as Crackle). It deals no damage;
            // its value is the floating it adds (credited in Solve::consider).
            if (def.params.untap_x_mana_sources)
            {
                if (!ResolveProvider(state).ShouldEmitUntapRitual(state)) { continue; }
                int x = ManaSourceCount(state);
                if (x <= 0) { continue; }
                int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
                ManaCost xcost = EffectiveCost(def, state);
                xcost.generic += x * pips;
                xcost.generic = std::max(0, xcost.generic - HinataGenericDiscount(def, state, x));
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = xcost;
                a.chosen_x       = x;
                a.eval           = 0;                  // ritual deals nothing; value is enabling Crackle
                a.direct_damage  = 0;
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                a.ritual_float   = RitualFloatAmount(state, def, x);   // refloat mana, stamped once
                actions.push_back(std::move(a));
                continue;
            }
            if (def.tmpl != CardTemplate::DirectDamage) { continue; }
            ManaCost base  = EffectiveCost(def, state);   // fixed part; ManaValue() ignores X
            ManaPool xpool = AvailableManaPool(state);
            // Hinata combo: a ritual in hand (Reality Spasm) funds a bigger X this turn. Credit
            // its NET mana so this payoff's max X reaches the combo's lethal value. Over-generates
            // candidates affordable ONLY with the ritual; Solve::consider rejects any subset that
            // does not actually include the ritual (only the ritual+payoff subset passes CanPay).
            // 0 with no Hinata / no ritual in hand -> byte-identical for every other deck.
            xpool.wild += HinataRitualNetBonus(state);
            // Each point of X is paid x_pips times (Crackle {X}{X}{X} = 3), so the max
            // affordable X divides the leftover mana by x_pips.
            int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
            int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }
            // Hinata's discount frees up mana for a larger X. Two cases:
            int max_x;
            int P = xpool.Total(), bmv = base.ManaValue();
            if (def.params.discount_targets_scale_x && HinataInPlay(state))
            {
                // Crackle: discount = min(X, avail) -> cost(X) = pips*X + bmv - min(X, avail),
                // monotonic increasing. Solve for the largest affordable X (piecewise at X=avail).
                int avail = HinataAvailableTargets(def, state);
                int cost_at_avail = (pips - 1) * avail + bmv;
                if (P >= cost_at_avail) { max_x = (P - bmv + avail) / pips; }
                else                    { max_x = (P - bmv) / (pips > 1 ? pips - 1 : 1); }
            }
            else
            {
                // Fixed (non-scaling) discount: a constant addend to the affordable budget.
                int disc = HinataGenericDiscount(def, state, 0);
                max_x = (P - bmv + disc) / pips;
            }
            if (max_x < 0) { max_x = 0; }
            // Crackle with Power (scale_x + Hinata): the DECLARED extra-target COUNT is a searched
            // parameter, exactly like Soulfire's own-target count. The discount DERIVES from it
            // (min(X, 1+count)) and the extras take 5X + die (CrackleHitExtraTargets), so the search
            // weighs a bigger discount against losing its own creatures (Hinata last). Bound the
            // blow-up with the user's observation that only mid X branch: X where 5X is already
            // LETHAL collapses to the max count (creatures die free on the win turn -> cheapest), and
            // count is naturally 0 when there are no extra targets. Gated on scale_x + Hinata, so
            // every other {X} burn keeps the single-variant path below (byte-identical).
            const bool crackle_branch = IsCrackleCountSpell(def.params) && HinataInPlay(state);
            const int  active_ci      = state.active_player_index;
            const int  opp_life       = state.players[1 - active_ci].life;
            for (int x : ResolveProvider(state).XCandidates(state, def, max_x))
            {
                if (x <= 0) { continue; }
                // Declared extra-target count for this X (a single autonomous HEURISTIC; the user's
                // "the AI target heuristic can remain"). GT-NEUTRAL design:
                //   * -1  -> the LEGACY path: auto-max discount (min(X,avail)) and NO faithful kill.
                //           Used for every non-crackle {X} spell AND for a NON-lethal Crackle -- so a
                //           chip Crackle behaves EXACTLY as before (same discount, no creatures die),
                //           never throwing away the Hinata engine for damage that doesn't win.
                //   * cap -> a LETHAL Crackle (5X >= opp life): target the extras for the discount and
                //           faithfully kill them. The max discount equals the old auto-max at that X
                //           (1+min(X-1,E) == min(X,1+E)), and the creatures die on the WIN turn, so the
                //           win/turn is identical -- only now honestly modelled.
                // Net: the autonomous search is unchanged (non-lethal identical; lethal wins the same
                // turn) while the faithful declared-target model is live for human play (Stage B).
                int cnt_lo = -1, cnt_hi = -1;
                if (crackle_branch)
                {
                    const int per_tgt = x * mult;
                    const int cap = std::max(0, std::min(x - 1, CrackleExtraTargetCount(state, active_ci, per_tgt)));
                    if (HumanPlayActive())
                    {
                        // HUMAN PLAY: offer EVERY declared count 0..cap so the player picks how many
                        // extra targets (the discount derives from it, min(X,1+count)) -- Stage B's count
                        // picker (CheckLine emits a `crackle` sub). HumanPlayActive() is true across ALL
                        // human-play enumerations (the plan MENU, CheckLine, and the apply re-run), so the
                        // count variants have consistent plan indices between validate and apply. It is
                        // FALSE in the clairvoyant rollout (HumanPlaySuppress -> single count, fast) and in
                        // the autonomous batch (byte-identical, GT-stable). Same signal the plan_signature
                        // dedup keys on, so distinct counts survive as distinct variants.
                        cnt_lo = 0; cnt_hi = cap;
                    }
                    else
                    {
                        cnt_lo = cnt_hi = (per_tgt >= opp_life) ? cap : -1;   // lethal -> faithful max; else legacy
                    }
                }
                for (int count = cnt_lo; count <= cnt_hi; ++count)
                {
                    ManaCost xcost = base;
                    xcost.generic += x * pips;                // X is paid (x_pips times) as generic
                    // Hinata reduces the whole generic (incl. X) by the target count. For Crackle the
                    // discount derives from `count` (min(X,1+count)); for other X spells (Reality
                    // Spasm) `count` is -1 and it uses the auto formula (unchanged).
                    xcost.generic = std::max(0, xcost.generic - HinataGenericDiscount(def, state, x, count));
                    Action a;
                    a.kind            = Action::Kind::CastFromHand;
                    a.card_name       = ap.hand[i].m_name;
                    a.hand_index      = i;
                    a.cost            = xcost;
                    a.chosen_x        = x;
                    a.crackle_targets = count;   // -1 = legacy (auto-max discount, no faithful kill)
                    a.sacrifice_land  = def.params.sacrifice_land;
                    a.eval            = x * mult * 100;        // EvalCard's DMG unit (dmg-equivalents)
                    // X burn reaches the face only when not creature-only targeted (the creature-
                    // target guard above already dropped Creature/Multi with no opponent creature).
                    // Damage per target = chosen X * x_damage_multiplier (Crackle = 5X).
                    a.direct_damage   = (def.params.targeting != Targeting::Creature) ? x * mult : 0;
                    a.is_noncreature  = !def.card.IsCreature();
                    a.card_mv         = def.card.m_mana_cost.ManaValue();  // X = 0 outside the stack
                    actions.push_back(std::move(a));
                }
            }
            continue;
        }

        // Alternative cost (Invigorate / Skyshroud Cutter / Reverent Silence): "If you control
        // a Forest, rather than pay this spell's mana cost, you may have an opponent gain N
        // life" (-> N damage with a Remedy active). These free payloads split into two kinds:
        //   * SAFE (Invigorate / Skyshroud): firing one is strictly good under a Remedy (free
        //     face damage, no downside), so it is NOT a search choice -- it is AUTO-FIRED
        //     deterministically after the casts (FireSafeAltPayloads, see ApplyPlanDirect /
        //     AIEngine). Keeping them out of the action set avoids a free-subset enumeration
        //     blow-up (free actions are never mana-pruned, so flooded hands exploded the plan
        //     count). They are therefore not emitted here at all.
        //   * RISKY (Reverent Silence: destroy_all_enchantments wipes our OWN Aria/Remedy):
        //     this stays a genuine SEARCH decision, offered as a free action only with a Remedy
        //     already active, so the search weighs the board wipe against keeping the combo.
        if (def.params.alt_lifegain_cost > 0
            && ControlsSubtype(state, state.active_player_index, def.params.alt_cost_requires_subtype))
        {
            // Default (pruned): only the RISKY alt is a search choice; the SAFE ones are auto-fired
            // below (byte-identical). UNPRUNED opens the safe alt to the search too -- firing a free
            // payload is NOT always correct (e.g. Invigorate with no attacker just feeds the
            // opponent life), so the search-primary A/B (and human play) must be able to weigh
            // not-firing it. When opened here, the auto-fire pass is suppressed (gated on the same
            // DecisionUnpruned), so the choice is made exactly once, by the search/human.
            // A targeted alt-payload (Invigorate: "target creature") is uncastable with no legal
            // target (CR 601.2c) -- gate the emission on a target existing, but still `continue`
            // below so a Forest-controlled payload is never re-emitted as a hard-cast.
            //
            // SPECULATIVE search emission: under a live Tainted Remedy a safe alt payload is a free
            // face-damage burn (opp gains N -> loses N), so a REAL search node (g_search_candidate_
            // enum) offers it as a real cast -- letting the search STACK several (+ Aria escalation)
            // into a lethal burst the single-shot auto-fire cannot assemble. Only emitted for the
            // rollout-evaluated search, never the greedy Solve()/leaf (which keeps the auto-fire and
            // would otherwise waste it early -- that is the tuned d0 behaviour). A committed plan
            // that casts it removes it from hand so the auto-fire/redirect skip it (no double).
            const bool spec_search_burn = g_search_candidate_enum
                && !def.params.destroy_all_enchantments
                && RemedyActive(state, state.active_player_index);
            if ((ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, def)
                 || DecisionUnpruned(UnprunedGate::AltPayload) || spec_search_burn)
                && AltPayloadTargetLegal(state, def))
            {
                constexpr int DMG = 100;
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = ManaCost{};             // free (alt cost is the opponent lifegain)
                a.alt_cost       = true;
                a.alt_lifegain   = def.params.alt_lifegain_cost;
                a.eval           = def.params.alt_lifegain_cost * DMG;
                a.direct_damage  = def.params.alt_lifegain_cost;
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                actions.push_back(std::move(a));
            }
            continue;   // safe alts: auto-fired (not enumerated); risky alt handled above
        }

        // Tutor: the heuristic (TutorCandidates) returns a NARROWED candidate set. One
        // candidate = a clear heuristic decision; several = the heuristic pruned the options
        // but cannot distinguish them, so emit one cast variant per candidate (all sharing this
        // hand_index, hence mutually exclusive in the plan enumerator) and let the search pick.
        // Narrowing to the few cards that matter keeps the search's branching factor small --
        // the general "heuristic narrows, search decides the rest" pattern.
        if (def.params.tutor_to_hand || def.params.tutor_to_top)
        {
            // RESOLVE MODE (MTG_TUTOR_AXIS_RESOLVE=1): bind NO name at all. The decision moves to
            // the tutor's own resolution (PerformTutor -> provider at the true mid-plan state), so
            // ranking here -- on a pre-land, pre-cast state the plan will never occupy -- would be
            // both wrong and wasted work. One cast action, empty target; the fan-out in
            // EnumeratePlansWithLand adds Plan::tutor_choice variants. Human play keeps the name
            // path: the human picks among named variants, exactly as before.
            if (TutorAxisResolveMode() && !HumanPlayActive() && TutorAxisEnabled())
            {
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = EffectiveCost(def, state);
                a.eval           = EvalCard(def, state);
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                actions.push_back(std::move(a));
                continue;
            }
            // NO PlanContext here, and it is structural rather than an oversight: this runs during
            // ACTION COLLECTION, before any plan exists, so the base target is necessarily chosen
            // without knowing what else the turn will do. Only the post-dedup axis in
            // EnumeratePlansWithLand can supply one (see PlanContext.h). Anything that wants the
            // base pick to be plan-aware has to move this decision after plan assembly.
            std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, state.active_player_index, def.params);
            if (cands.empty()) { cands.push_back(std::string{}); }  // whiff: castable, fetches nothing
            // Emit ONE target here (the provider's best) and search the rest as a post-dedup AXIS
            // (see the tutor fan-out in EnumeratePlansWithLand). Emitting them all was almost pure
            // WASTE: EnumeratePlans' autonomous plan_signature keys only on cast NAMES, so every
            // tutor variant of one subset shares a signature and the dedup already kept exactly this
            // first one and discarded the rest. Measured (MTG_BRANCH_STATS, Hinata d3): Gamble was
            // 75% of the whole enumeration odometer and 54% of total raw plan work, and capping the
            // emission left sum_final and the play identical there.
            // NOT byte-identical overall, though: TurnSolver::Solve has its own odometer and no
            // signature dedup (it is d0 and every rollout leaf), and its smallest-mask tie-break can
            // land differently with fewer candidates. Measured cost of that churn on the full
            // held-out suite at axis width 1: searched sum 0.0000 (0 slower / 0 faster), d0 sum
            // -0.0015. See docs/design/searched-action-subdecisions.md.
            // Human play keeps every variant: there the signature DOES include the target (the human
            // is the decision-maker and must see all of them), so nothing is discarded downstream.
            if (!HumanPlayActive() && TutorAxisEnabled() && cands.size() > 1) { cands.resize(1); }
            for (const std::string& tgt : cands)
            {
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = EffectiveCost(def, state);
                a.eval           = EvalCard(def, state);
                a.is_noncreature = !def.card.IsCreature();
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                a.tutor_target   = tgt;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Soulfire Eruption: the own-creature target COUNT is a searched parameter. Emit one cast
        // variant per own_targets value 0..K (K = #own creatures). More targets = a deeper dig
        // (face = max MV over more exiled cards) + a bigger Hinata discount, paid for with mana-
        // value damage to those creatures (see SoulfireDig). All share hand_index (mutually
        // exclusive), so the search picks the count by playing each out. own_targets = 0 = 6a.
        if (def.tmpl == CardTemplate::DirectDamage && def.params.damage_equals_top_mv)
        {
            const int active   = state.active_player_index;
            // Own-targeting only earns its keep with Hinata in play: each own target is {1} less
            // (the discount that can ENABLE an otherwise-unaffordable Soulfire) plus a deeper dig.
            // Without her, an own target gives no discount -- only marginal extra dig at the cost of
            // creature damage on a 9-mana spell -- so we don't branch on it (keeps the K+1 variants
            // off every non-combo turn). With her, the search still picks the count 0..K.
            const int K        = ResolveProvider(state).BranchSoulfireOwnTargets(state)
                                     ? SoulfireOwnCreatureCount(state, active) : 0;
            ManaCost base_cost = EffectiveCost(def, state);   // base Hinata discount already applied
            const int eval     = EvalCard(def, state);
            const bool to_face = def.params.targeting != Targeting::Creature;
            for (int ot = 0; ot <= K; ++ot)
            {
                ManaCost c = base_cost;
                c.generic = std::max(0, c.generic - SoulfireOwnTargetDiscount(def, state, active, ot));
                Action a;
                a.kind                 = Action::Kind::CastFromHand;
                a.card_name            = ap.hand[i].m_name;
                a.hand_index           = i;
                a.cost                 = c;
                a.eval                 = eval;
                a.direct_damage        = to_face ? SoulfireFaceDamage(state, active, ot) : 0;
                a.is_noncreature       = !def.card.IsCreature();
                a.card_mv              = def.card.m_mana_cost.ManaValue();
                a.soulfire_own_targets = ot;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Desperate Ritual -- SPLICE onto Arcane. The splice COUNT k (how many OTHER same-named copies
        // in hand are revealed + spliced onto this base cast) is a SEARCH decision: emit one cast
        // variant per k = 0..(count of OTHER copies in hand). All variants share this hand_index, so the
        // group enumerator picks at most one per base copy (mutual exclusion, like the {X}/tutor/Soulfire
        // variants). Cost AND float scale by (k+1) here (EffectiveCost/RitualFloatAmount with copies=k+1)
        // and are re-scaled IDENTICALLY at apply (apply_one) and in the executor (CastSpellFromHand +
        // EffectHandler) off the same k -> lockstep. The k spliced copies STAY IN HAND (never removed).
        // Physically-impossible over-splice across multiple selected bases (a spliced copy must still be
        // in hand when revealed) is rejected per-plan by SubsetHasIllegalSplice; every k stays enumerable
        // (never pruned) so MTG_UNPRUNED opens the full range. STORM (future): the spells_cast_this_turn
        // increment fires ONCE per base cast, NOT per (k+1); a later hard-cast of a leftover copy is its
        // own increment. Inert for every non-splice deck (splice_onto_arcane false) -> byte-identical.
        if (def.params.splice_onto_arcane)
        {
            int others = 0;
            for (const Card& c : ap.hand) { if (c.m_name == ap.hand[i].m_name) { ++others; } }
            --others;   // exclude the base copy itself -> # of OTHER copies available to splice
            // SPLICE-count collapse (HEURISTIC, DragonstormProvider::UseSpliceCollapse() +
            // MTG_UNPRUNED(SpliceCollapse)). Full search emits every k = 0..others; the collapse CAPS the
            // splice at 1, emitting only BARE (k=0) and ONE-SPLICE (k=1) per copy (the user's "one splice
            // per" model -- a k=1 splice needs 4 mana up front vs 8 for the full chain, and self-funds the
            // next). SpliceCollapseViolated then keeps only the canonical (m bases, s single-splices) mixes,
            // so the SEARCH picks the affordable (m,s) by rolling each out (mana-derived within k<=1). The
            // triangular s<=N-1 legality is enforced by GroupChoiceOverSplices. Off/other decks -> full k
            // range (byte-identical).
            const bool splice_collapse = ResolveProvider(state).UseSpliceCollapse()
                                      && !DecisionUnpruned(UnprunedGate::SpliceCollapse);
            std::vector<int> ks;
            if (splice_collapse)
            {
                ks.push_back(0);                          // bare (k=0) -- "fewer than one splice" fallback
                if (others >= 1) { ks.push_back(1); }     // one-splice (k=1); dedup/legality handled downstream
            }
            else
            {
                for (int k = 0; k <= others; ++k) { ks.push_back(k); }
            }
            for (int k : ks)
            {
                Action a;
                a.kind            = Action::Kind::CastFromHand;
                a.card_name       = ap.hand[i].m_name;
                a.hand_index      = i;
                a.cost            = EffectiveCost(def, state, k + 1);
                a.eval            = EvalCard(def, state);
                a.is_noncreature  = !def.card.IsCreature();
                a.card_mv         = def.card.m_mana_cost.ManaValue();
                a.max_casts_after = def.params.max_casts_after;
                a.ritual_float    = RitualFloatAmount(state, def, a.chosen_x, k + 1);
                a.splice_count    = k;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Apex of Power -- the "add ten mana of any one colour" COLOUR is a SEARCH decision. Emit one
        // cast variant per candidate colour (ChosenFloatColorCandidates: the deck's spell-cost colours,
        // or all five under MTG_UNPRUNED(SacColor)); all share this hand_index, so the group enumerator
        // picks at most one per base copy (mutual exclusion, like the {X}/splice/Soulfire variants). The
        // colour rides on chosen_float_color to resolution (apply_one / CastSpellFromHand). We DELIBERATELY
        // do NOT credit the 10 as a.ritual_float here: it materialises only at Apex's resolution and funds
        // the freshly-exiled spells via the draw-breakpoint re-solve (crediting it to the MAIN subset
        // budget would over-project mana that isn't yet available). Inert for every non-impulse deck.
        if (def.params.impulse_exile > 0)
        {
            // Provider float-colour collapse: a red-primary archetype (Dragonstorm) floats RED only for
            // Apex -- its mono-red chain never needs another colour and one colour can't fund a multicolour
            // card anyway. Opened to the full candidate set for other decks and under MTG_UNPRUNED(SacColor).
            std::vector<std::string> apex_colors;
            if (ResolveProvider(state).ImpulseFloatColorRedOnly()
                && !DecisionUnpruned(UnprunedGate::SacColor))
            { apex_colors.push_back("R"); }
            else
            { apex_colors = ChosenFloatColorCandidates(state); }
            for (const std::string& col : apex_colors)
            {
                Action a;
                a.kind               = Action::Kind::CastFromHand;
                a.card_name          = ap.hand[i].m_name;
                a.hand_index         = i;
                a.cost               = EffectiveCost(def, state);
                a.eval               = EvalCard(def, state);
                a.is_noncreature     = !def.card.IsCreature();
                a.card_mv            = def.card.m_mana_cost.ManaValue();
                a.chosen_float_color = col;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Scaled divided-damage spell (Magma Opus): the mana cost scales with how much of the divided
        // damage we commit to the opponent's FACE (more face -> fewer distinct spread/tap targets ->
        // less Hinata discount -> more mana), exactly like paying more for a bigger {X}. The archetype
        // owns the model (ScaledCastVariants: which face levels + what each costs -- all card-specific
        // numbers); the engine emits one mutually-exclusive cast per level and lets the plan enumerator
        // + search pick per affordability + value (allocating spare mana across every scaling spell in
        // hand -- a Crackle {X} and this face). The committed face rides to resolution on crackle_targets
        // (the searched-scalar carrier; Magma is not IsCrackleCountSpell, so no crackle resolution
        // fires) and is dealt to the opponent there. Empty (every other deck/card, and Magma with the
        // model off) -> the generic single-line cast below (byte-identical).
        if (def.tmpl == CardTemplate::DirectDamage && def.params.damage_divided)
        {
            std::vector<ScaledCastVariant> scaled = ResolveProvider(state).ScaledCastVariants(state, def);
            if (!scaled.empty())
            {
                // Emit ONE candidate at the CHEAPEST face (min generic). A single, narrow cast keeps the plan
                // enumeration as small as the over-count's -- a wide face ladder here bloated the group product
                // so the breadth cap dropped the winning ramp+Crackle subset (diagnosed on gi163: the lethal
                // cheap-Magma + big-Crackle combo was enumerated but never offered as a FEASIBLE plan; not a
                // budget issue -- 40x budget didn't help). See docs/design/viewer-magma-opus-modeling.md
                // "session 3d". The face is then FILLED UP from the plan's LEFTOVER mana in the evaluator
                // (FillScaledCastFace in eval_and_push / Solve::consider): spend all mana -- the searched
                // Crackle X takes its 3-mana chunks first, Magma mops up the sub-chunk remainder. Cost per
                // face stays the ladder's (recomputed by crackle_targets at apply_one / CastSpellFromHand), so
                // the fill is lockstep with resolution.
                const ScaledCastVariant* cheapest = &scaled[0];
                for (const ScaledCastVariant& v : scaled)
                {
                    if (v.cost.generic < cheapest->cost.generic) { cheapest = &v; }
                }
                Action a;
                a.kind            = Action::Kind::CastFromHand;
                a.card_name       = ap.hand[i].m_name;
                a.hand_index      = i;
                a.cost            = cheapest->cost;
                a.eval            = cheapest->face * 100;      // DMG unit: face damage reaches the opponent
                a.direct_damage   = cheapest->face;            // (the inert spread is not simulated)
                a.is_noncreature  = !def.card.IsCreature();
                a.card_mv         = def.card.m_mana_cost.ManaValue();
                a.crackle_targets = cheapest->face;            // committed opp-face damage -> resolution
                actions.push_back(std::move(a));
                continue;
            }
        }

        // Aura (Bogles): the enchant TARGET is a SEARCH decision -- which creature carries the aura
        // changes the clock (summoning sickness + Kor Spiritdancer's per-aura self-buff). Emit one
        // CastFromHand variant per legal creature target (sharing hand_index -> mutually exclusive),
        // so the search picks. No legal target -> uncastable (an aura can't enchant nothing,
        // CR 601.2c) -> not emitted, the card stays in hand. Restriction-filtered in
        // LegalEnchantTargets (Daybreak Coronet: a creature already carrying an aura; Lion Umbra: a
        // modified creature). The provider is NOT the narrower here -- every legal target is emitted.
        if (def.params.is_aura)
        {
            for (int tgt_num : LegalEnchantTargets(state, state.active_player_index, def.params))
            {
                Action a;
                a.kind           = Action::Kind::CastFromHand;
                a.card_name      = ap.hand[i].m_name;
                a.hand_index     = i;
                a.cost           = EffectiveCost(def, state);
                a.eval           = EvalCard(def, state);
                a.is_noncreature = true;   // enchantments are noncreature spells
                a.card_mv        = def.card.m_mana_cost.ManaValue();
                a.enchant_target = tgt_num;
                actions.push_back(std::move(a));
            }
            continue;
        }

        // Count damage that actually reaches the opponent's life total. A player/multi-target burn
        // deals its face damage directly (Searing Blaze: 1, or landfall 3). A creature-only burn
        // deals damage to a permanent -- EXCEPT Searing Blood, whose "when that creature dies" rider
        // deals death_trigger_damage (3) to the controller IFF the target dies. Credit that reach
        // when a killable opponent creature exists (EffectiveToughness <= the burn's damage), lockstep
        // with the rollout apply / executor which fire the same rider on the same FindBurnKillTarget
        // pick. Without this the search valued Blood at 0 face and never recognised a Blood-closes-it
        // lethal / under-ranked it as reach.
        int direct = 0;
        if (def.tmpl == CardTemplate::DirectDamage
            && def.params.targeting != Targeting::Creature)
        {
            direct = def.params.damage;
            if (def.params.landfall_damage > 0 && ap.lands_played_this_turn > 0)
            {
                direct = def.params.landfall_damage;
            }
        }
        else if (def.tmpl == CardTemplate::DirectDamage
                 && def.params.targeting == Targeting::Creature
                 && def.params.death_trigger_damage > 0)
        {
            int ti = FindBurnKillTarget(state, state.active_player_index, def.params.damage);
            if (ti >= 0 && state.battlefield[ti].EffectiveToughness() <= def.params.damage)
            {
                direct = def.params.death_trigger_damage;
            }
        }

        Action a;
        a.kind                  = Action::Kind::CastFromHand;
        a.card_name             = ap.hand[i].m_name;
        a.hand_index            = i;
        a.cost                  = EffectiveCost(def, state);
        a.sacrifice_land        = def.params.sacrifice_land;
        a.eval                  = EvalCard(def, state);
        a.direct_damage         = direct;
        a.is_noncreature        = !def.card.IsCreature();
        a.card_mv               = def.card.m_mana_cost.ManaValue();
        a.is_draw               = (def.tmpl == CardTemplate::DrawSpell
                                   || def.tmpl == CardTemplate::DrawX);
        a.has_spectacle         = def.params.spectacle_cost.has_value();
        a.is_draw_until_nonland = (def.tmpl == CardTemplate::DrawUntilNonland);
        a.discard_land_damage   = def.params.discard_land_damage;
        a.max_casts_after       = def.params.max_casts_after;   // Irencrag "one more spell" restriction
        // HARD-CAST HASTE creature: it attacks the turn it arrives, so it belongs in this turn's
        // attack projection exactly like a hasted Vial drop (vial_attack_power). Stamped here, once,
        // because the projection is rebuilt per PLAN and both enumerators share CollectActions.
        // Gated on power > 0, which is also precisely when every provider's ShouldAttackWith returns
        // true (Generic always; the AntiLifegain/Hinata overrides hold back only 0-power no-trigger
        // dorks) -- so the projection cannot claim an attack the real DeclareAttackers won't make.
        if (def.card.IsCreature()
            && (def.card.HasKeyword(Keyword::Haste)
                || HasHasteFromLords(def.card, state.battlefield, state.active_player_index)))
        {
            auto [lord_pb, lord_tb] = ComputeLordBonus(def.card, state.battlefield,
                                                       state.active_player_index);
            (void)lord_tb;
            const bool ds = def.card.HasKeyword(Keyword::DoubleStrike)
                         || HasDoubleStrikeFromLords(def.card, state.battlefield,
                                                     state.active_player_index);
            const int power = (def.card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
            if (power > 0)
            {
                a.haste_attack_power = power;
                a.haste_prowess      = def.card.HasKeyword(Keyword::Prowess);
            }
        }
        if (IsManaRitual(def)) { a.ritual_float = RitualFloatAmount(state, def, a.chosen_x); }  // Irencrag burst
        // Same-turn mana-rock ramp: a non-creature mana rock (Sol Ring) taps the turn it is cast.
        // Stamp the mana it produces (by real colour) so the enumerator can fund the rest of the
        // subset off it. Creatures (mana dorks) are excluded -- they are summoning-sick this turn.
        if (RockRampEnumEnabled() && def.params.mana_rock && !def.card.IsCreature())
        { AddSourceToPool(a.rock_mana, state, def); }
        // Ponder (cast_reorder): keep-vs-shuffle. This was the #1 branching source (MTG_BRANCH_STATS:
        // ~47% of all enumeration) because searching BOTH futures emits two variants that multiply
        // every plan where Ponder is castable. By default we now DECIDE it with the heuristic
        // (ponder_keep = -1 -> ReorderTopOrShuffle shuffles iff none of the top N pass ScryKeepOnTop,
        // which for Hinata is the situational-rank threshold) -- one variant, no branch. MTG_UNPRUNED
        // restores the searched 2-way branch for the standing A/B. The provider still supplies the
        // kept-card ORDER (by situational rank) either way.
        // Searched by default at depth > 0 (MTG_PONDER_SEARCH=0 forces the single-variant heuristic
        // back). DEPTH-GATED like cantrip-first: at depth 0 there is no rollout to score the
        // variants, so the "branch" would just be enumeration order picking a different fixed rule.
        static const bool s_ponder_search = EnvOn("MTG_PONDER_SEARCH", true);
        if (def.params.cast_reorder > 0
            && ((s_ponder_search && SearchedPlayActive()) || DecisionUnpruned(UnprunedGate::Ponder)))
        {
            // HEURISTIC FIRST (ponder_keep = -1), then the two pinned alternatives. The order is the
            // whole point: the search accepts a variant only on a STRICT improvement, so whichever
            // plan is enumerated first owns every tie -- and this decision ties constantly, because
            // the cost of keeping three dead cards on top lands OUTSIDE a depth-3 horizon. With the
            // pinned keep variant first (the old order) the search silently became "always keep": in
            // 48 Hinata Ponder resolutions it shuffled 0 times where the heuristic shuffled 13, and
            // it measured worse at EVERY budget including a converged one (6.2750 -> 6.6000 at 640ms
            // and 2560ms, identical digests). That is a tie-break defect, not a search result.
            // Heuristic first makes the branch free: it can only override on a difference it can
            // actually see. See docs/design/searched-scry-disposition.md.
            //
            // CORRECTION (2026-07-31): the tie-break reasoning above is sound in general but is NOT
            // the mechanism here, and this branch is currently INERT. ponder_keep is cost-neutral,
            // so all three variants carry the same cast-NAME set -- and EnumeratePlans' autonomous
            // plan_signature keys only on names, so the dedup keeps the FIRST and discards the other
            // two. Enumerating keep-first therefore made the engine "always keep" (the 0-vs-13
            // shuffles above); enumerating the heuristic first makes the branch not EXIST rather
            // than free. Verified: MTG_PONDER_SEARCH=0 and =1 give identical play (Hinata 200 games,
            // d3 b10 -> 5.8950 both). The fix that shipped was still the right one; the decision is
            // simply still unsearched. The real fix is the post-dedup AXIS the tutor target now uses
            // (TutorAxisEnabled) -- see docs/design/searched-action-subdecisions.md.
            //
            // NOT in the human-play menu: there the heuristic variant is a duplicate of whichever
            // pinned option it resolves to, so it would show the player a redundant third entry --
            // and it shifted the recorded plan indices of two saved Hinata references (26 -> 35,
            // reported as `repaired`). A human picking the disposition wants the explicit keep and
            // shuffle options; the tie-break argument above is about the SEARCH's strict-improvement
            // rule, which does not apply when a person is choosing.
            // COST FIX (2026-08-01): emit ONE variant for autonomous play, not three. The two pinned
            // alternatives were always discarded by the name-only dedup (see the CORRECTION above),
            // but they were emitted INSIDE CollectActions, so they entered the odometer first: this
            // group cost (1 + 3) = 4 where one variant costs (1 + 1) = 2, i.e. 2x the enumeration
            // per Ponder in hand, compounding across copies -- paid on what MTG_BRANCH_STATS measured
            // as the single largest branching source (~47% of all enumeration), for candidates that
            // could never survive. Byte-identical: the dedup already kept exactly this variant.
            //
            // Human play still gets the explicit keep/shuffle pair -- the sub-decision block is not
            // dedup-gated there, so both options are real, and a person choosing wants them named
            // rather than a heuristic entry that duplicates whichever one it resolves to.
            if (!HumanPlayActive())
            {
                actions.push_back(std::move(a));    // a.ponder_keep stays -1 == resolution heuristic
            }
            else
            {
                Action keep_a = a;            keep_a.ponder_keep = 1;
                Action shuf_a = a;            shuf_a.ponder_keep = 0;
                actions.push_back(std::move(keep_a));
                actions.push_back(std::move(shuf_a));
            }
        }
        else
        {
            // Heuristic decides keep-vs-shuffle at RESOLUTION (ponder_keep = -1, the Action default):
            // ReorderTopOrShuffle shuffles iff none of the top N cards passes ScryKeepOnTop (the
            // situational-rank threshold for Hinata), else keeps them on top ordered by rank. One
            // variant, no branch. Deciding at resolution (vs at enumeration) measured strictly better
            // -- the top of library reflects this turn's earlier cantrips by then -- and at the SAME
            // fd-diverge as the searched 2-variant baseline (no lockstep regression).
            actions.push_back(std::move(a));   // a.ponder_keep stays -1
        }
    }

    // --- Aether Vial activations: one per (Vial, creature) pair ---
    {
        constexpr int DMG = 100;
        int bf_size = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_size; ++vi)
        {
            const Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            const CardDefinition* vdef =
                CardDatabase::Instance().LookupCached(vp.card);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }

            int target_mv = vp.charge_counters;
            for (int i = 0; i < n; ++i)
            {
                const CardDefinition* copt =
                    CardDatabase::Instance().LookupCached(ap.hand[i]);
                if (!copt || !copt->card.IsCreature()) { continue; }
                if (copt->card.m_mana_cost.ManaValue() != target_mv) { continue; }

                auto [lord_pb, lord_tb] = ComputeLordBonus(
                    copt->card, state.battlefield, state.active_player_index);
                bool ds = copt->card.HasKeyword(Keyword::DoubleStrike)
                       || HasDoubleStrikeFromLords(copt->card, state.battlefield,
                                                   state.active_player_index);
                int power = (copt->card.m_power.value_or(0) + lord_pb) * (ds ? 2 : 1);
                bool haste = copt->card.HasKeyword(Keyword::Haste)
                          || HasHasteFromLords(copt->card, state.battlefield,
                                               state.active_player_index);
                int attacks = ExpectedAttacks(state);
                if (!haste && attacks > 0) { --attacks; }

                Action a;
                a.kind              = Action::Kind::ActivateVial;
                a.card_name         = ap.hand[i].m_name;
                a.hand_index        = i;
                a.eval              = power * attacks * DMG;
                a.is_noncreature    = false;
                a.card_mv           = target_mv;
                a.vial_bf_index     = vi;
                a.vial_attack_power = haste ? power : 0;
                actions.push_back(std::move(a));
            }
        }
    }

    // --- Retrace: cast a retrace card from the graveyard (Throes of Chaos) ---
    // Additional cost: discard a land card from hand. The card is not exiled, so it
    // returns to the graveyard and can be retraced again on a later turn. One Action
    // per distinct card name (apply finds the first matching copy in the graveyard).
    {
        int lands_in_hand = 0;
        for (const Card& c : ap.hand)
        {
            auto cdef = CardDatabase::Instance().LookupCached(c);
            if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
        }
        if (lands_in_hand > 0)
        {
            std::unordered_set<std::string> seen_gy;
            for (const Card& gc : ap.graveyard)
            {
                auto gdef = CardDatabase::Instance().LookupCached(gc);
                if (!gdef || !gdef->params.retrace) { continue; }
                bool timing_ok = gdef->card.IsInstant()
                              || gdef->card.HasKeyword(Keyword::Flash)
                              || state.stack.empty();
                if (!timing_ok) { continue; }
                if (gdef->card.m_mana_cost.has_x) { continue; }
                if (!seen_gy.insert(gc.m_name).second) { continue; }
                // Flood-engine gate (same as hand casts): a retrace card here is Throes of
                // Chaos, which cascades into Treasure Hunt -- only recast it from the
                // graveyard when the resulting draw has a payoff this turn. In human play the
                // player owns that call, so the gate is bypassed and the retrace line is always
                // offered (MTG_HUMAN_PLAY); autonomous search keeps the gate (byte-identical).
                const bool s_human_play_retrace = HumanPlayActive();
                if (!s_human_play_retrace &&
                    !ResolveProvider(state).ShouldCastDrawEngine(state, state.active_player_index, *gdef))
                {
                    continue;
                }

                Action a;
                a.kind                  = Action::Kind::CastFromGraveyard;
                a.card_name             = gc.m_name;
                a.hand_index            = -1;            // sourced from graveyard, not hand
                a.cost                  = EffectiveCost(*gdef, state);
                a.discard_lands         = 1;            // discard one land as the retrace cost
                a.eval                  = EvalCard(*gdef, state);
                a.is_noncreature        = !gdef->card.IsCreature();
                a.card_mv               = gdef->card.m_mana_cost.ManaValue();
                a.is_draw_until_nonland = (gdef->tmpl == CardTemplate::DrawUntilNonland);
                a.discard_land_damage   = gdef->params.discard_land_damage;
                actions.push_back(std::move(a));
            }
        }
    }

    // --- Suspend (Lotus Bloom "Suspend 3-{0}") ---
    // A {0} main-phase action: exile the card from hand with suspend_time_counters time counters (it
    // arrives, cast off suspend, turn+N). NOT a cast -- adds no storm and no board THIS turn. eval 0:
    // the future mana is realised by the rollout (SimulateEndAndStartNextTurn brings it into play at
    // the arrival upkeep), so the multi-turn search values the suspend, not a d0 eval. One per copy in
    // hand. Emitted only when suspend_time_counters > 0 -> every other deck is byte-identical.
    for (int i = 0; i < n; ++i)
    {
        auto sdef = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!sdef || sdef->params.suspend_time_counters <= 0) { continue; }
        Action a;
        a.kind           = Action::Kind::Suspend;
        a.card_name      = ap.hand[i].m_name;
        a.hand_index     = i;
        a.cost           = ManaCost{};      // {0}
        a.eval           = 0;
        a.is_noncreature = true;
        actions.push_back(std::move(a));
    }

    // --- SacForMana (Lotus Bloom "{T}, Sacrifice this artifact: Add three mana of any one color") ---
    // A battlefield-activated mana ability: tap + SACRIFICE an untapped source in play, floating N of a
    // single SEARCH-CHOSEN colour. Enumerated as one Action per (source, candidate colour); the colour
    // rides on chosen_float_color and is credited as ritual_float (wild) in the subset math, then floated
    // as the real colour at apply (AddChosenColorFloat). Candidate colours = the colours appearing in the
    // deck's spell costs (so an off-colour line stays reachable), opened to all five under
    // MTG_UNPRUNED(SacColor). The colour variants of ONE source are mutually exclusive (sac_source_id);
    // the subset evaluators reject selecting two of them. Emitted only when sac_for_mana_amount > 0.
    {
        bool have_sac_source = false;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index || p.tapped) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (pd && pd->params.sac_for_mana_amount > 0) { have_sac_source = true; break; }
        }
        if (have_sac_source)
        {
            // Candidate colours: the colours in the active player's nonland spell costs across all zones
            // (order-independent set), or all five under the SacColor unpruned gate. Not hardcoded red.
            // Shared with Apex of Power's colour enumeration (ChosenFloatColorCandidates).
            std::vector<std::string> colors = ChosenFloatColorCandidates(state);

            for (const Permanent& p : state.battlefield)
            {
                if (p.controller_index != state.active_player_index || p.tapped) { continue; }
                const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
                if (!pd || pd->params.sac_for_mana_amount <= 0) { continue; }
                for (const std::string& col : colors)
                {
                    Action a;
                    a.kind              = Action::Kind::SacForMana;
                    a.card_name         = p.card.m_name;
                    a.hand_index        = -1;                       // battlefield-sourced (independent)
                    a.cost              = ManaCost{};               // {0}: the cost is tap + sacrifice
                    a.ritual_float      = pd->params.sac_for_mana_amount;   // credited as wild (Solve)
                    a.chosen_float_color= col;                      // floated as this real colour at apply
                    a.sac_source_id     = p.card.m_number;          // per-instance id (mutual exclusion)
                    a.eval              = 0;
                    a.is_noncreature    = true;
                    actions.push_back(std::move(a));
                }
            }
        }
    }

    // --- Goblins tribal activated outlets (Krenko tap / sac-a-Goblin / Twinshot channel) ---
    {
        constexpr int DMG = 100;
        // Krenko, Mob Boss: one TapForTokens per untapped, non-summoning-sick Krenko. Free ({T} only).
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index || p.tapped) { continue; }
            if (p.entered_this_turn && !p.card.HasKeyword(Keyword::Haste)
                && !HasHasteFromLords(p.card, state.battlefield, state.active_player_index)) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (!pd || pd->params.tap_creates_tokens_per_controlled_subtype.empty()) { continue; }
            const int x = CountControlledSubtype(state, state.active_player_index,
                              pd->params.tap_creates_tokens_per_controlled_subtype);
            if (x <= 0) { continue; }
            Action a;
            a.kind           = Action::Kind::TapForTokens;
            a.card_name      = p.card.m_name;
            a.hand_index     = -1;
            a.cost           = ManaCost{};                 // {0}: the cost is the tap
            a.sac_source_id  = p.card.m_number;
            a.eval           = x * pd->params.tap_created_token_power * DMG;   // X bodies ~ board value
            a.is_noncreature = false;
            actions.push_back(std::move(a));
        }

        // Costed VALUE outlets (Siege-Gang "{1}{R}, Sac a Goblin: 2 damage"; Pashalik "{3}{R}, Sac a
        // Goblin: two 1/1 Goblins"): one action per (outlet source, candidate Goblin victim). The mana
        // cost rides on a.cost so the subset math reserves it; the trailing apply pass pays it (both
        // worlds) and only realises the effect if paid, so a stranded outlet is a no-op, not a phantom.
        // Skirk's MANA outlet (sac_outlet_add_mana_color set) is NOT emitted here -- it floats mana and
        // needs the mana-solver float path (separate follow-up); this pass is the value outlets only.
        for (const Permanent& src : state.battlefield)
        {
            if (src.controller_index != state.active_player_index) { continue; }
            const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
            if (!sd || !sd->params.sac_creature_outlet) { continue; }
            const bool is_mana_outlet = !sd->params.sac_outlet_add_mana_color.empty();
            // Sac-outlet pre-combat deferral (GoblinsProvider::DeferSacOutletPreCombat, ADOPTED default-ON,
            // off-switch MTG_NO_GOBLIN_SAC_2ND): defer the VALUE outlets (Siege-Gang / Pashalik / the multi-
            // sac burst) to the second main and haste-gate Skirk's mana outlet. Vs the passive opponent a
            // value sac is >= as good AFTER attacking, so this is near-lossless while dropping them from the
            // pre-combat O(2^candidates) subset. GenericProvider returns false -> byte-identical for every
            // non-Goblins deck. See the hook doc for the Skirk haste-gate rationale.
            if (is_pre_combat && ResolveProvider(state).DeferSacOutletPreCombat(state, src, is_mana_outlet))
            { continue; }
            const std::string& need_sub = sd->params.sac_creature_requires_subtype;
            // BOUNDED victim selection (heuristic narrowing -- NOT one action per victim, which makes
            // the O(2^candidates) subset search explode on a wide Goblin board / Krenko tokens and
            // hangs). The victims are fungible for a sac outlet, so pick ONE canonical victim via the
            // shared expendability heuristic (tokens/Mogg first, lords/scaling deferred, source last --
            // see CanonicalSacVictim). This emits ONE action per outlet -- linear, not exponential, and
            // keeps the single-sac pick in lockstep with the Skirk multi-sac burst's apply-time picks.
            const int victim_id = CanonicalSacVictim(state, state.active_player_index,
                                                     src.card.m_number, need_sub);
            if (victim_id < 0) { continue; }   // no legal Goblin to sacrifice
            Action a;
            a.card_name      = src.card.m_name;
            a.hand_index     = -1;
            a.sac_source_id  = src.card.m_number;      // the outlet permanent
            a.sac_victim_id  = victim_id;              // the heuristically-chosen sacrificed Goblin
            a.is_noncreature = true;
            if (is_mana_outlet)
            {
                // Skirk Prospector: "Sacrifice a Goblin: Add {R}." A free MANA ability -- emit it as a
                // SacForMana action (kind reused) so it inherits the full mana-solver coupling (subset
                // ritual_float credit, BatchPrepay decline, pre-cast float, plan signature). The source
                // stays; ApplySacForMana sacs the victim when sac_victim_id != 0.
                a.kind               = Action::Kind::SacForMana;
                a.cost               = ManaCost{};                              // no mana cost (no tap)
                a.ritual_float       = sd->params.sac_outlet_add_mana_amount;   // credited by Solve
                a.chosen_float_color = sd->params.sac_outlet_add_mana_color;    // "R"
                a.eval               = 0;
            }
            else
            {
                a.kind           = Action::Kind::SacCreatureOutlet;
                a.cost           = sd->params.sac_creature_cost.value_or(ManaCost{});
                a.direct_damage  = sd->params.sac_outlet_damage;
                a.eval           = (sd->params.sac_outlet_damage
                                    + sd->params.sac_outlet_creates_tokens) * DMG;
            }
            actions.push_back(std::move(a));

            // Multi-sac MANA BURST (Skirk Prospector): "Sacrifice a Goblin: Add {R}" is REPEATABLE, so
            // the single-sac action alone under-models it (1 mana/turn) -- the ramp-into-Muxus/Krenko line
            // is impossible for the search. Emit ONE extra bounded burst: sac k Goblins for k*{R}, where k
            // = the shortfall to the most expensive hand spell newly reachable with the sacs (capped at
            // victims). Demand-driven -> one action, no O(2^victims) blowup. Mutually exclusive with the
            // sac-1 action (same-source SacForMana reject), so the search picks none / sac-1 / sac-k.
            // ritual_float=k*per credits the mana solver; ApplySacForMana derives the sac COUNT from it.
            if (is_mana_outlet)
            {
                const int per = std::max(1, sd->params.sac_outlet_add_mana_amount);
                int V = 0;
                for (const Permanent& v : state.battlefield)
                {
                    if (v.controller_index != state.active_player_index || !v.card.IsCreature()) { continue; }
                    if (!need_sub.empty() && !CardHasSubtype(v.card, need_sub)) { continue; }
                    ++V;
                }
                const int base = AvailableManaPool(state).Total();   // mana this turn WITHOUT Skirk
                int reach_mv = 0;   // most expensive hand spell newly reachable with <= V sacs
                for (const Card& hc : ap.hand)
                {
                    const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                    if (!hd || hd->card.IsLand()) { continue; }
                    const int mv = hd->card.m_mana_cost.ManaValue();
                    if (mv > base && mv <= base + V * per && mv > reach_mv) { reach_mv = mv; }
                }
                int k = (reach_mv > base) ? (reach_mv - base + per - 1) / per : 0;   // sacs to reach it
                if (k > V) { k = V; }
                if (k >= 2)
                {
                    Action b;
                    b.kind               = Action::Kind::SacForMana;
                    b.card_name          = src.card.m_name;
                    b.hand_index         = -1;
                    b.sac_source_id      = src.card.m_number;
                    b.sac_victim_id      = 0;                 // canonical victims chosen at apply
                    b.sac_count          = k;
                    b.cost               = ManaCost{};
                    b.ritual_float       = k * per;           // k*{R}; apply derives the sac count
                    b.chosen_float_color = sd->params.sac_outlet_add_mana_color;
                    b.is_noncreature     = true;
                    b.eval               = 0;
                    actions.push_back(std::move(b));
                }
            }

            // Multi-sac BURST (searched COUNT, bounded to ONE extra action so no O(2^victims) blowup):
            // for a DAMAGE outlet (Siege-Gang) also offer saccing MULTIPLE Goblins in one activation for
            // k*damage -- the swarm-sac burst finish the single-victim action can't reach. k = the fewest
            // sacs that could be lethal (ceil(opp_life / damage)), capped at the number of victims; emitted
            // only when k >= 2 (else the single action already covers it). cost/damage are pre-scaled by
            // k; the trailing apply pass pays k*cost (a stranded burst is a no-op, both worlds).
            if (!is_mana_outlet && sd->params.sac_outlet_damage > 0)
            {
                const int D = sd->params.sac_outlet_damage;
                int V = 0;
                for (const Permanent& v : state.battlefield)
                {
                    if (v.controller_index != state.active_player_index || !v.card.IsCreature()) { continue; }
                    if (!need_sub.empty() && !CardHasSubtype(v.card, need_sub)) { continue; }
                    ++V;
                }
                const int opp_life = state.players[1 - state.active_player_index].life;
                int k = (opp_life + D - 1) / D;   // ceil(opp_life / D): fewest sacs that could be lethal
                if (k > V) { k = V; }             // cap at available victims (sac-all for max reach)
                if (k >= 2)
                {
                    Action b;
                    b.kind           = Action::Kind::SacCreatureOutlet;
                    b.card_name      = src.card.m_name;
                    b.hand_index     = -1;
                    b.sac_source_id  = src.card.m_number;
                    b.sac_victim_id  = 0;    // unused for a burst -- victims are chosen canonically at apply
                    b.sac_count      = k;
                    b.is_noncreature = true;
                    ManaCost c = sd->params.sac_creature_cost.value_or(ManaCost{});
                    c.generic *= k; c.white *= k; c.blue *= k; c.black *= k;
                    c.red *= k; c.green *= k; c.colorless *= k;
                    b.cost           = c;
                    b.direct_damage  = D * k;
                    b.eval           = D * k * DMG;
                    actions.push_back(std::move(b));
                }
            }
        }

        // Twinshot Sniper channel: a from-hand ability (pay channel_cost + discard -> 2 face damage).
        for (int i = 0; i < n; ++i)
        {
            const CardDefinition* cd = CardDatabase::Instance().LookupCached(ap.hand[i]);
            if (!cd || !cd->params.channel_cost.has_value() || cd->params.channel_damage <= 0) { continue; }
            Action a;
            a.kind           = Action::Kind::Channel;
            a.card_name      = ap.hand[i].m_name;
            a.hand_index     = i;
            a.cost           = cd->params.channel_cost.value();
            a.direct_damage  = cd->params.channel_damage;
            a.eval           = cd->params.channel_damage * DMG;
            a.is_noncreature = true;
            actions.push_back(std::move(a));
        }
    }

    // Resolve each action's card definition ONCE so the per-node subset evaluators read the
    // cached pointer instead of re-hashing card_name. Equivalent to Lookup(card_name) at each
    // use site (every kind's card_name is a real DB name: hand-cast/vial creature/dig source).
    // Fast path: a hand-sourced action's card_name IS ap.hand[hand_index].m_name, so reuse that
    // card's memoized def (Card::m_def) instead of re-hashing the name string (the by-name Lookup
    // was ~a third of the string-hash cost in a Hinata gen profile). Byte-identical (same def);
    // graveyard/other actions (hand_index < 0) fall back to the by-name Lookup.
    for (Action& a : actions)
    {
        a.def = (a.hand_index >= 0 && a.hand_index < static_cast<int>(ap.hand.size()))
                    ? CardDatabase::Instance().LookupCached(ap.hand[a.hand_index])
                    : CardDatabase::Instance().Lookup(a.card_name);
    }

    return actions;
}

// ---- Breadth cap shared by Solve and EnumeratePlans -------------------------------------------
// The plan enumerators (Solve's odometer and EnumeratePlans) cost product_g(1+|group_g|) * 2^ind.
// A deep Soulfire/cantrip dig leaves ~20 distinct nonland casts in hand, exploding it -- and these
// run PER rollout node, so a few such turns dominate the whole search (MTG_PROFILE: 2 of 24 games
// held ~90% of wall time at low node counts -> the cost was enumeration, not the rollout). Keep only
// the top-K groups by the provider's SituationalCardRank; the lowest-ranked groups (dig duplicates /
// dead cards the rank already deprioritizes) drop out of THIS turn's enumeration. Lossy, so gated:
// inert for any hand with <= cap groups (the whole suite is byte-identical), and disabled by
// MTG_NO_GROUP_CAP / MTG_UNPRUNED for the standing A/B (ON vs OFF give byte-identical search-node
// counts on Hinata -- the pruned groups never produced the optimal plan). The cap VALUE is now
// provider-owned policy (DecisionProvider::EnumGroupCap, audit A1); MTG_SOLVE_GROUP_CAP still tunes K.
bool GroupCapDisabled()
{
    static const bool v = EnvOn("MTG_NO_GROUP_CAP");
    return v;
}

static void CapGroupsBySituationalRank(const GameState& state, const std::vector<Action>& cands,
                                       std::vector<std::vector<int>>& groups,
                                       std::vector<int>& group_hand_index)
{
    if (GroupCapDisabled() || DecisionUnpruned(UnprunedGate::GroupCap)) { return; }
    // The provider supplies the breadth policy; the env knob is an engine-side A/B override.
    // Cache the env read once (this runs per enumeration -- an uncached getenv here was ~1.2% of
    // rollout time / 212M calls in a Hinata gen profile). -1 = unset. Byte-identical to the read.
    static const int s_group_cap_override = []{ const char* e = std::getenv("MTG_SOLVE_GROUP_CAP");
        if (!e) { return -1; } int x = std::atoi(e); return x < 1 ? 1 : x; }();
    int cap = ResolveProvider(state).EnumGroupCap();
    if (s_group_cap_override >= 0) { cap = s_group_cap_override; }
    if (static_cast<int>(groups.size()) <= cap)    { return; }

    const DecisionProvider& prov = ResolveProvider(state);
    std::vector<std::pair<int, int>> ranked;   // (situational rank, original group index)
    ranked.reserve(groups.size());
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
    {
        int best_r = -1;
        for (int idx : groups[g])
        {
            const CardDefinition* d = cands[idx].def;
            if (!d) { continue; }
            int r = prov.SituationalCardRank(state, d->card);
            if (r > best_r) { best_r = r; }
        }
        ranked.push_back({ best_r, g });
    }
    std::stable_sort(ranked.begin(), ranked.end(),
        [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first > b.first; });
    std::vector<char> keep(groups.size(), 0);
    for (int i = 0; i < cap; ++i) { keep[ranked[i].second] = 1; }
    std::vector<std::vector<int>> kept_groups;
    std::vector<int>              kept_hand_index;
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
    {
        if (!keep[g]) { continue; }
        kept_groups.push_back(std::move(groups[g]));
        kept_hand_index.push_back(group_hand_index[g]);
    }
    groups.swap(kept_groups);
    group_hand_index.swap(kept_hand_index);
}

// Conservative upper bound on this turn's TOTAL available mana: the current pool plus ALL ritual/rock
// ramp the hand could add (both ManaPool::Total()-style scalar totals), credited upfront. A combination
// whose total mana cost (sum of ManaValue()) exceeds this bound can never be paid -- the color-aware
// CanPay in consider()/eval_and_push() would reject it anyway -- so skipping it during enumeration is
// BYTE-IDENTICAL; it just avoids the per-combo scan for the doomed majority on low-mana durdle turns.
// Crediting ALL ramp upfront (not per-subset) means a subset that pays for a ritual/rock which nets
// positive, or contains a free (cost-0) spell (Aether Vial deploy / alt-cost payload, whose Action.cost
// is ManaCost{}), is never wrongly pruned. Filters only convert colour (no total-mana gain), so the
// total-necessary condition holds with them too.
//
// The prune's soundness rests on `Action.cost.ManaValue()` being the TRUE mana a subset needs. That
// holds for every reduction BAKED INTO the action cost at build time (Hinata's generic discount for a
// Hinata ALREADY in play, X=0, Spectacle's alt cost) -- consider() sees the same baked cost, so bound
// and affordability agree. It does NOT hold for a reduction applied PER-SUBSET inside consider() AFTER
// the action costs are summed: there the subset's real cost is BELOW sum(cost.ManaValue()), so pruning
// on the undiscounted sum drops a payable plan -- NOT byte-identical.
//
// AFFINITY is the one such case today (SameTurnAffinityGenericCredit subtracts a per-Sliver generic
// discount that GROWS as the subset grows -- anti-monotonic, so no cheap scalar upfront bound captures
// it; this bit slivers_vial). We bail out: when any candidate is affinity-reducible, disable the prune
// (return INT_MAX). Only affinity decks pay the cost; the durdle-heavy generation decks (burn/th/knights)
// have none, so the speedup is unaffected.
//
// >>> MAINTENANCE BREADCRUMB: any FUTURE cost reducer that is credited per-subset in consider() rather
// than baked into a.cost (e.g. "instants cost {1} less" / "Dragon spells cost {2} less" cast the SAME
// turn as their enabler, or a same-turn-cast Hinata crediting later spells) MUST be added to this bail-out
// too, exactly like affinity -- otherwise it silently breaks byte-identity. A reducer already in play that
// bakes into a.cost needs nothing. MTG_MANA_PRUNE=0 also disables (the byte-identical A/B toggle).
static const bool s_legacy_mana_bound = EnvOn("MTG_LEGACY_MANA_BOUND");

// MEASURED DEAD END (2026-07-30) -- do NOT re-add: gating the mana-gate BUILD on a predicted odometer
// width (prod(1+|group|) * 2^|independent|), i.e. "only bother on complex turns", is strictly WORSE than
// always building. Sweeping the threshold on the three gate-relevant decks (callgrind Ir vs gate-off):
//   Dragonstorm  T=0 -13.42%   T=256 -11.73%   T=4096 -0.55%
//   Hinata2      T=0  -0.34%   T=256  -0.14%   T=4096 +0.05%
//   slivers      T=0  -0.14%   T=256  +0.04%   T=4096 +0.04%
// The gate's setup is one term per candidate with no allocation, so it repays itself even on a narrow
// odometer; thresholding only forfeits wins. The residual cost on decks that never build it is the
// RELEVANCE SCAN, not the build -- so that scan was folded into the group-building loop instead.

// Rite-of-Flame graveyard escalation credit: the Nth same-turn copy floats +(N-1) beyond its base
// ritual_float, which consider() credits as this triangular term. Used by BOTH total-mana bounds.
static inline int ManaGateTriangular(int gy) { return gy > 1 ? gy * (gy - 1) / 2 : 0; }

// ---- Same-turn ritual credit: SEQUENCED vs simultaneous -------------------------------------
// A/B gate (MTG_RITUAL_SEQ_CREDIT=1; default OFF -> byte-identical). The affordability model in
// consider()/eval_and_push credits a selected ritual's GROSS float unconditionally while the same
// ritual's own cost sits in `combined`, i.e. it asks only "pool + Σfloat >= Σcost" SIMULTANEOUSLY --
// so A RITUAL CAN FUND ITS OWN COST. Irencrag Feat ({1}{R}{R}{R}, floats 6) then looks castable off
// one land, and Hinata plans a TURN-ONE Irencrag that the executor must silently drop: 548 of its 570
// Irencrag drops per 400 d0 games are total-mana shortfalls, not colour (MTG_AFFORD_AUDIT).
// The mana-ROCK branch a few lines below already has exactly this guard -- "a rock never funds its own
// cost", `if (sel_rock && pool.CanPay(rock_costs))" -- so this is an inconsistency inside one function,
// not a deliberate asymmetry.
// SEQUENCED credit instead walks the selected rituals in EXECUTION order (CastOrderRank, then
// cheapest-first -- the same comparator CastOrderLess uses, so the enumeration's feasibility model and
// the executor's actual cast order become the same sequence) and credits a ritual's float only once
// the board plus the floats of the rituals BEFORE it can pay for it. Strictly tighter than the
// simultaneous model, hence NOT byte-identical: it removes exactly the lines the executor cannot
// realise. Related but distinct from the note on DropRitualGroupsIfNoPayoff, which rejected this
// sequencing as a *byte-identical prune* -- here it is a deliberate model change, measured as such.
// MODES: 0 = off (byte-identical hatch); 1 = LEAF ONLY (Solve::consider -- the greedy/d0 policy and
// the rollout leaf) = THE DEFAULT; 2 = BOTH (also EnumeratePlans' search branch list).
//
// The asymmetry is the point, and it was MEASURED. Optimistic enumeration is SAFE inside the search,
// which discards unpayable lines by rolling them out -- but at DEPTH 0 there is no search to filter
// them, so the greedy commits to a line the executor then cannot pay. Mode 1 fixes exactly the site
// with no safety net and leaves the search's branch list wide. (This mirrors the existing same-turn
// reducer credit, which is deliberately EnumeratePlans-only for the same reason, in the opposite
// direction.) Held-out overnight, vs the simultaneous model:
//     mode 1 (leaf)  NET -0.1816   12 better /  1 worse   searched 4 slower / 13 faster
//     mode 2 (both)  NET -0.2149   15 better /  7 worse   searched 20 slower / 37 faster
// Mode 2 wins on aggregate and loses on the bar that matters (new, hard-to-recover regressions):
// it puts SEVEN cases in the red including every Dragonstorm searched depth. Mode 1 leaves
// Dragonstorm strictly better with none.
static int SeqRitualCreditMode()
{
    static const int mode = []{
        const char* e = std::getenv("MTG_RITUAL_SEQ_CREDIT");
        if (!e) { return 1; }                     // DEFAULT: honest at the leaf
        const int v = std::atoi(e);
        return v > 0 ? v : 0;                     // MTG_RITUAL_SEQ_CREDIT=0 -> the legacy hatch
    }();
    return mode;
}

// Gross float creditable to `sel`'s accelerants under the sequenced model. Returns the same total as
// the simultaneous sum whenever every selected accelerant is genuinely reachable in some order.
//
// Order = CHEAPEST-FIRST by the action's own cost, which is both what a self-funding chain needs and
// what the executor does (CastOrderLess's within-tier rule). Crucially that puts the FREE accelerants
// first: a Lotus Bloom SacForMana carries ritual_float with cost {0} (tap + sacrifice), so it is always
// reachable and bootstraps the rest. Ordering by CastOrderRank instead was WRONG and cost a measured
// Dragonstorm regression -- Lotus Bloom ranks as a plain noncreature (20) behind Seething Song (15), so
// the sort tested Seething Song against a lone Mountain, gave up, and threw away 6 free mana; the
// dragonstorm d3 gi129 T4 win (sac both Blooms -> 7 -> Seething Song -> 9 -> Dragonstorm) became a T6.
// The greedy runs to a FIXPOINT rather than stopping at the first unaffordable accelerant, because
// CanPay is colour-aware: a cheaper accelerant needing an absent colour must not discard the dearer
// ones behind it. Sets are tiny (a handful of accelerants), so the extra passes are free.
// `exclude` (a cands index, -1 = none) drops one accelerant from the set, so the surplus-ritual check
// downstream can ask "what would this subset credit WITHOUT r?" under the same sequenced model.
static int SequencedRitualCredit(const ManaPool& pool, const std::vector<Action>& cands,
                                 const std::vector<int>& sel, int exclude = -1)
{
    // thread_local so the hot path never allocates.
    thread_local std::vector<int>  rit;
    thread_local std::vector<char> credited_flag;
    rit.clear();
    for (int j : sel) { if (j != exclude && cands[j].ritual_float > 0) { rit.push_back(j); } }
    if (rit.empty()) { return 0; }
    if (rit.size() == 1)
    {
        // A single accelerant still has to be payable on its own -- that IS the self-funding case.
        return pool.CanPay(cands[rit[0]].cost) ? cands[rit[0]].ritual_float : 0;
    }
    std::stable_sort(rit.begin(), rit.end(), [&](int x, int y)
    { return cands[x].cost.ManaValue() < cands[y].cost.ManaValue(); });

    // FAST PATH: if the board alone pays for EVERY selected accelerant, the sequence is trivially
    // realisable and the answer is just the simultaneous sum -- one CanPay instead of the walk below.
    // This is the common case on ordinary (non-go-off) turns, which is where the hot path lives.
    ManaCost all;
    int sum_float = 0, sum_gy = 0;
    for (int j : rit)
    {
        const ManaCost& c = cands[j].cost;
        all.white += c.white; all.blue  += c.blue;  all.black += c.black;
        all.red   += c.red;   all.green += c.green;
        all.colorless += c.colorless;   all.generic += c.generic;
        sum_float += cands[j].ritual_float;
        if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++sum_gy; }
    }
    if (pool.CanPay(all)) { return sum_float + ManaGateTriangular(sum_gy); }

    credited_flag.assign(rit.size(), 0);
    ManaCost acc;          // accumulated cost of the accelerants credited so far
    int credit = 0, gy = 0;
    bool progress = true;
    while (progress)
    {
        progress = false;
        bool skipped = false;
        ManaPool probe = pool;
        probe.wild += credit + ManaGateTriangular(gy);
        for (std::size_t i = 0; i < rit.size(); ++i)
        {
            if (credited_flag[i]) { continue; }
            const ManaCost& c = cands[rit[i]].cost;
            ManaCost trial = acc;
            trial.white += c.white; trial.blue  += c.blue;  trial.black += c.black;
            trial.red   += c.red;   trial.green += c.green;
            trial.colorless += c.colorless;     trial.generic += c.generic;
            if (!probe.CanPay(trial)) { skipped = true; continue; }   // a later credit may unlock it
            credited_flag[i] = 1;
            acc      = trial;
            credit  += cands[rit[i]].ritual_float;
            if (cands[rit[i]].def && cands[rit[i]].def->params.ritual_float_gy_self_bonus) { ++gy; }
            probe       = pool;
            probe.wild += credit + ManaGateTriangular(gy);

            progress = true;
        }
        // Nothing was passed over, so there is nothing a further pass could unlock.
        if (!skipped) { break; }
    }
    return credit + ManaGateTriangular(gy);
}

static int ManaPruneBound(const ManaPool& pool, const std::vector<Action>& cands)
{
    static const bool on = EnvOn("MTG_MANA_PRUNE", true);   // DEFAULT ON; =0 disables
    if (!on) { return std::numeric_limits<int>::max(); }
    // Affinity applies a per-subset generic discount consider() sees but this scalar bound cannot ->
    // sum(cost.ManaValue()) overstates the true cost -> disable the prune to stay byte-identical.
    for (const Action& a : cands)
    { if (a.def && a.def->params.affinity_for_subtype) { return std::numeric_limits<int>::max(); } }
    // A Medallion cast THIS turn discounts later same-colour spells in the subset -- a reduction the
    // scalar bound cannot see (like a same-turn affinity enabler), so bail when one is among the cands.
    for (const Action& a : cands)
    { if (a.def && !a.def->params.reduces_spell_color.empty()) { return std::numeric_limits<int>::max(); } }
    long long b = pool.Total();
    int gy = 0;
    for (const Action& a : cands)
    {
        b += a.ritual_float;
        b += a.rock_mana.Total();
        if (a.def && a.def->params.ritual_float_gy_self_bonus) { ++gy; }
    }
    // SOUNDNESS FIX (2026-07-30): this bound credited every candidate's base ritual_float but NOT the
    // Rite-of-Flame graveyard escalation that consider() credits, so it was too TIGHT -- i.e. UNSOUND,
    // pruning genuinely payable ritual chains. That was the real cause of the 3 Dragonstorm cases the
    // selection-exact gate appeared to "change": the gate was right and this bound was wrong. Crediting
    // the triangular term here can only LOOSEN the bound (prune less), never drop a plan.
    // MTG_LEGACY_MANA_BOUND=1 restores the old unsound bound for an A/B.
    if (!s_legacy_mana_bound) { b += ManaGateTriangular(gy); }
    return (b >= std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(b);
}

// ---- SELECTION-EXACT total-mana gate (supersedes the ManaPruneBound scalar) --------------------
//
// ManaPruneBound above is a single scalar computed from the WHOLE candidate list, which makes it
// far too loose to prune the combo cross-product it was meant for:
//   * it credits EVERY ritual_float / rock in hand to every odometer position, even positions that
//     cast none of them (a four-ritual Dragonstorm go-off hand gets a budget inflated by 12+ mana);
//   * it bails out entirely (INT_MAX) when ANY candidate carries a same-turn cost reducer or
//     affinity, because those discounts are applied per-SUBSET after the cost sum -- so Dragonstorm,
//     which runs 3x Ruby Medallion, loses the gate completely on any turn with one in hand.
// The result was that the odometer walked essentially its entire product space and paid the
// (expensive) splice/accel predicates at every position.
//
// This index makes the same test per-SELECTION instead: budget credits only the float/ramp the
// position actually casts, and the reducer/affinity bail-out applies only to positions that
// actually select such a card.
//
// It is SOUND -- a necessary condition that never prunes a subset consider()/eval_and_push() would
// have kept -- but it is deliberately NOT byte-identical against the legacy bound, because the
// LEGACY bound is the unsound one (see SelectionExactManaGateEnabled for the Rite-of-Flame case it
// wrongly drops). Soundness argument:
//   * they build the credited pool as exactly pool + SUM(selected ritual_float) + the Rite-of-Flame
//     triangular gy term + (conditionally) SUM(selected rock_mana), then require CanPay(combined);
//   * ManaPool::CanPay can only succeed when the pool's TOTAL covers the cost's total, so
//     need > budget implies CanPay false -- the subset was rejected anyway;
//   * the rock credit is applied here unconditionally where consider() gates it on
//     pool.CanPay(rock_costs), which only LOOSENS this bound (never prunes more);
//   * reducer/affinity discounts shrink the real cost below sum(ManaValue), so a selection holding
//     one is exempted from the gate rather than measured against it.
// The SubsetPayableWithFilters fallback cannot rescue a total-mana shortfall either: filters convert
// colour, they do not add total mana -- the same assumption ManaPruneBound already documents and
// relies on. (A ramp_filter producing 3+ colours would break it; none exists today -- Ferrous Lake
// and Izzet Signet are 2-colour, net +1, which is what AddSourceToPool credits.)
//
// >>> MAINTENANCE BREADCRUMB: the same rule as ManaPruneBound's -- any FUTURE cost reducer credited
// per-subset inside consider()/eval_and_push() (rather than baked into a.cost) MUST set `block` here
// too, or it silently breaks byte-identity.
struct ManaGateTerm
{
    int cost  = 0;   // this action's cost.ManaValue()  (the old gcost/icost term)
    int gain  = 0;   // ritual_float + rock_mana.Total() this action ADDS to the turn's mana
    int gy    = 0;   // 1 if it is a Rite-of-Flame-style graveyard self-scaling ritual (triangular)
    int block = 0;   // 1 if selecting it disables the gate (same-turn reducer / affinity discount)
};

struct ManaGateIndex
{
    std::vector<ManaGateTerm> term;          // per candidate index
    int  pool_total   = 0;                   // AvailableManaPool(state).Total()
    int  ind_gain_all = 0;                   // SUM gain over the `independent` actions (outer headroom)
    int  ind_gy_all   = 0;                   // SUM gy   over the `independent` actions
    bool ind_block    = false;               // any independent action would disable the gate
};

// Rite-of-Flame graveyard escalation: k copies cast this turn float +0,+1,...,+k-1 extra as each
// prior copy hits the graveyard. Mirrors the triangular term consider()/eval_and_push() credit.

// MTG_SEL_MANA_GATE=1 opts IN to the selection-exact gate. DEFAULT OFF -> every deck falls back to
// the legacy whole-list ManaPruneBound scalar and is byte-identical.
//
// It is default-off because it is NOT byte-identical, and the reason is worth recording: the legacy
// bound credits `Sum over ALL candidates of ritual_float` but NOT the Rite-of-Flame graveyard
// escalation (Tri()), while consider()/eval_and_push() DO credit it. On a hand where the whole-list
// slack is smaller than that triangular term -- e.g. four Rite of Flame, all selected, Tri(4) = +6 --
// the legacy bound therefore DROPS A PAYABLE PLAN. So the legacy bound is unsound in that corner and
// this gate cannot reproduce it without reproducing the bug. Measured on the Dragonstorm smoke cases
// the correction is a strict improvement (searched: 10 faster, 0 slower, 0 play-changed), but it is
// GT-affecting, so shipping it default-on is a separate, user-approved decision.
// MTG_MANA_PRUNE=0 still disables total-mana pruning outright (both paths).
// Namespace scope (not a function-local static) for the same reason as g_no_splice_index: this is
// read once per enumeration call, and a magic static would add a guard check to each.
// DEFAULT ON since 2026-07-30 (user-approved). Byte-identical to the legacy scalar path now that
// ManaPruneBound is sound, so enabling it needs no rebaseline -- it is purely faster. NOTE the flag
// polarity: `MTG_SEL_MANA_GATE=0` genuinely DISABLES (a bare `getenv() != nullptr` test would have
// made `=0` enable it -- the trap the MTG_MAGMA_FAITHFUL flag once fell into).
static const bool g_sel_mana_gate = []{
    const char* sel = std::getenv("MTG_SEL_MANA_GATE");
    if (sel && std::string(sel) == "0") { return false; }
    const char* prune = std::getenv("MTG_MANA_PRUNE");
    return !(prune && std::string(prune) == "0");
}();
static bool SelectionExactManaGateEnabled() { return g_sel_mana_gate; }

// Returns false when the gate would be EXACTLY the legacy bound, so the caller can skip it entirely
// and stay on the (free) legacy path: with no candidate carrying float/ramp or a per-subset discount,
// legacy's `pool.Total() + SUM_all(0)` and this gate's `pool.Total() + SUM_selected(0) + Tri(0)` are
// the same number and legacy never bails. That keeps every non-combo deck at zero cost -- filling a
// per-candidate term vector on every enumeration call otherwise showed up as +0.2..+1.0% on
// burn/knights/antilife/TH (callgrind, 2026-07-29).
// Allocation-free precheck for BuildManaGateIndex: would the selection-exact gate differ AT ALL from
// the legacy bound on this candidate list? Split out of the builder so a caller can skip even the
// ManaGateIndex allocation. Solve is the rollout leaf (once per node), so an unconditional
// make_unique cost +0.3..0.65pp on the decks that never use the gate -- burn/treasure_hunt paid a
// malloc+free per node for a gate that always declined (callgrind, 2026-07-29).
static inline bool ManaGateWouldHelp(const std::vector<Action>& cands)
{
    for (const Action& a : cands)
    {
        if (a.ritual_float > 0 || a.rock_mana.Total() > 0
            || (a.def && (a.def->params.affinity_for_subtype
                          || !a.def->params.reduces_spell_color.empty())))
        { return true; }
    }
    return false;
}

static bool BuildManaGateIndex(const ManaPool& pool, const std::vector<Action>& cands,
                               const std::vector<int>& independent, ManaGateIndex& out)
{
    if (!ManaGateWouldHelp(cands)) { return false; }

    const int m = static_cast<int>(cands.size());
    out.term.assign(m, ManaGateTerm{});
    for (int j = 0; j < m; ++j)
    {
        const Action& a = cands[j];
        ManaGateTerm& t = out.term[j];
        t.cost = a.cost.ManaValue();
        t.gain = a.ritual_float + a.rock_mana.Total();
        t.gy   = (a.def && a.def->params.ritual_float_gy_self_bonus) ? 1 : 0;
        t.block = (a.def && (a.def->params.affinity_for_subtype
                             || !a.def->params.reduces_spell_color.empty())) ? 1 : 0;
    }
    out.pool_total   = pool.Total();
    out.ind_gain_all = 0;
    out.ind_gy_all   = 0;
    out.ind_block    = false;
    for (int b : independent)
    {
        out.ind_gain_all += out.term[b].gain;
        out.ind_gy_all   += out.term[b].gy;
        if (out.term[b].block) { out.ind_block = true; }
    }
    return true;
}

// ---- TEMPORARY DIAGNOSTIC: mana-side collapse potential (MTG_ENUM_STATS=1) ----------------------
//
// Measures the CEILING of the two-stage "enumerate mana separately, then look up per plan" design
// before committing to it. Per enumeration call it reports:
//   positions  -- odometer positions the current flat enumeration walks
//   m_raw      -- distinct MANA-SIDE digit combinations inside those positions
//   m_exact    -- how many of those m_raw are actually DISTINCT in what they hand the payoff side
//                 (mana added, storm count, cards consumed, Lotus colour, Irencrag restriction)
//   m_bucket   -- same, after capping each colour at the amount the hand could actually DEMAND
//                 (the "don't make a bucket for white in Hinata" reduction)
// Two-stage cost is then roughly m_raw + m_bucket * (positions / m_raw) against today's `positions`,
// so m_raw/m_bucket is the collapse ratio available AFTER the existing symmetry prunes have run.
//
// Deliberately expensive (it enumerates the mana side separately) and env-gated to zero cost, because
// this runs inside Solve -- the rollout leaf. Measurement builds only; delete once the design lands.
namespace enumstats
{
    inline bool Enabled() { static const bool v = EnvOn("MTG_ENUM_STATS"); return v; }
    inline std::atomic<std::uint64_t> g_calls{0}, g_positions{0}, g_m_raw{0}, g_m_kept{0},
                                      g_m_exact{0}, g_m_bucket{0}, g_m_manastorm{0}, g_m_manaonly{0},
                                      g_calls_with_mana{0},
                                      g_p_lines{0}, g_p_rowskip{0}, g_pairs_live{0}, g_pairs_total{0};
    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            const double kept = (double)g_m_kept.load();
            auto ratio = [&](std::uint64_t d) { return d > 0 ? kept / (double)d : 0.0; };
            std::fprintf(stderr,
                "\n=== ENUM STATS ===\n"
                "enumeration calls          : %llu   (with a mana side: %llu)\n"
                "odometer positions         : %llu\n"
                "mana-side combos raw       : %llu\n"
                "mana-side combos KEPT      : %llu   (after the existing symmetry prunes)\n"
                "  distinct exact           : %llu   (collapse %.2fx)  [mana+storm+cards+colour]\n"
                "  distinct demand-bucketed : %llu   (collapse %.2fx)  [+ colours capped at demand]\n"
                "  distinct mana+storm      : %llu   (collapse %.2fx)  [cards-spent identity DROPPED]\n"
                "  distinct mana only       : %llu   (collapse %.2fx)  [storm DROPPED too]\n"
                "==================\n",
                (unsigned long long)g_calls.load(), (unsigned long long)g_calls_with_mana.load(),
                (unsigned long long)g_positions.load(), (unsigned long long)g_m_raw.load(),
                (unsigned long long)g_m_kept.load(),
                (unsigned long long)g_m_exact.load(),     ratio(g_m_exact.load()),
                (unsigned long long)g_m_bucket.load(),    ratio(g_m_bucket.load()),
                (unsigned long long)g_m_manastorm.load(), ratio(g_m_manastorm.load()),
                (unsigned long long)g_m_manaonly.load(),  ratio(g_m_manaonly.load()));
            const double pt = (double)g_pairs_total.load();
            std::fprintf(stderr,
                "--- two-stage gating potential ---\n"
                "payoff-side lines          : %llu   (unaffordable under EVERY mana line: %llu = %.1f%%)\n"
                "(mana x payoff) pairs      : %llu\n"
                "  pairs that are payable   : %llu   (%.1f%%)\n"
                "  => two-stage visits      : %llu   vs flat %llu   (%.2fx fewer)\n"
                "==================\n",
                (unsigned long long)g_p_lines.load(), (unsigned long long)g_p_rowskip.load(),
                g_p_lines.load() ? 100.0 * g_p_rowskip.load() / g_p_lines.load() : 0.0,
                (unsigned long long)g_pairs_total.load(),
                (unsigned long long)g_pairs_live.load(),
                pt > 0 ? 100.0 * g_pairs_live.load() / pt : 0.0,
                (unsigned long long)(g_m_kept.load() + g_p_lines.load() + g_pairs_live.load()),
                (unsigned long long)g_pairs_total.load(),
                (g_m_kept.load() + g_p_lines.load() + g_pairs_live.load())
                    ? pt / (double)(g_m_kept.load() + g_p_lines.load() + g_pairs_live.load()) : 0.0);
        }
    };
    inline Dumper g_dumper;
}

static inline void EnumStatsHash(std::uint64_t& h, std::uint64_t v)
{ h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }

// True if this action's contribution to a plan is MANA (a ritual's float, a rock's ramp, a Lotus sac).
static inline bool IsManaSideAction(const Action& a)
{ return a.ritual_float > 0 || a.rock_mana.Total() > 0; }

static void MeasureManaSideCollapse(const std::vector<Action>& cands,
                                    const std::vector<std::vector<int>>& groups,
                                    const std::vector<int>& independent,
                                    std::uint64_t positions, int pool_total,
                                    const std::function<bool(const std::vector<int>&)>& skip)
{
    enumstats::g_calls.fetch_add(1, std::memory_order_relaxed);
    enumstats::g_positions.fetch_add(positions, std::memory_order_relaxed);

    // Partition: which odometer digits are mana-side, and which independents.
    std::vector<int> m_groups, m_ind;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        for (int j : groups[g]) { if (IsManaSideAction(cands[j])) { m_groups.push_back((int)g); break; } }
    }
    for (int b : independent) { if (IsManaSideAction(cands[b])) { m_ind.push_back(b); } }
    if (m_groups.empty() && m_ind.empty()) { return; }
    enumstats::g_calls_with_mana.fetch_add(1, std::memory_order_relaxed);

    // Per-colour DEMAND cap: the most of each colour any plan out of this candidate list could need.
    // A colour with cap 0 (white in Hinata) collapses away entirely instead of getting its own bucket.
    int cap[6] = {0,0,0,0,0,0}; int cap_total = 0;
    for (const Action& a : cands)
    {
        cap[0] += a.cost.white; cap[1] += a.cost.blue;  cap[2] += a.cost.black;
        cap[3] += a.cost.red;   cap[4] += a.cost.green; cap[5] += a.cost.colorless;
        cap_total += a.cost.ManaValue();
    }

    std::unordered_set<std::uint64_t> exact, bucket, manastorm, manaonly;
    std::vector<int> m_avail;   // per surviving mana line: total mana it makes available
    const int ng = (int)m_groups.size(), ni = (int)m_ind.size();
    std::vector<int> choice(ng, 0);
    std::vector<int> full(groups.size(), 0);   // mana-side digits only; the prunes read just these
    std::uint64_t combos = 0, kept = 0;
    bool done = false;
    while (!done)
    {
        // Honour the prunes the real odometer already applies, so the denominator is what the
        // enumeration actually walks -- not a raw product it never visits.
        std::fill(full.begin(), full.end(), 0);
        for (int t = 0; t < ng; ++t) { full[m_groups[t]] = choice[t]; }
        const bool pruned = skip(full);
        for (int imask = 0; imask < (1 << ni); ++imask)
        {
            ++combos;
            if (pruned) { continue; }
            ++kept;
            // Everything the mana side hands the payoff side.
            int mv = 0, flt = 0, gy = 0, storm = 0, restrict_after = -1;
            int rock[6] = {0,0,0,0,0,0};
            std::uint64_t consumed = 1469598103934665603ull;  // order-insensitive: XOR-free sum of hashes
            std::uint64_t colour_choice = 0;
            auto fold = [&](int j) {
                const Action& a = cands[j];
                mv  += a.cost.ManaValue();
                flt += a.ritual_float;
                if (a.def && a.def->params.ritual_float_gy_self_bonus) { ++gy; }
                if (a.kind == Action::Kind::CastFromHand) { ++storm; }
                if (a.max_casts_after >= 0)
                { restrict_after = restrict_after < 0 ? a.max_casts_after
                                                      : std::min(restrict_after, a.max_casts_after); }
                rock[0] += a.rock_mana.white; rock[1] += a.rock_mana.blue;  rock[2] += a.rock_mana.black;
                rock[3] += a.rock_mana.red;   rock[4] += a.rock_mana.green; rock[5] += a.rock_mana.colorless;
                // Consumed-card identity must be in the signature: two lines with the same mana are NOT
                // interchangeable if they spent different cards (that changes later turns).
                consumed += (std::uint64_t)(std::uintptr_t)a.def * 1099511628211ull;
                if (!a.chosen_float_color.empty()) { colour_choice += (std::uint64_t)a.chosen_float_color[0]; }
            };
            for (int t = 0; t < ng; ++t)
            { if (choice[t] > 0) { fold(groups[m_groups[t]][choice[t] - 1]); } }
            for (int b = 0; b < ni; ++b) { if (imask & (1 << b)) { fold(m_ind[b]); } }

            std::uint64_t he = 0;
            for (std::uint64_t v : { (std::uint64_t)mv, (std::uint64_t)flt, (std::uint64_t)gy,
                                     (std::uint64_t)storm, (std::uint64_t)(restrict_after + 1),
                                     consumed, colour_choice })
            { EnumStatsHash(he, v); }
            std::uint64_t hb = 0;
            for (std::uint64_t v : { (std::uint64_t)std::min(mv, cap_total), (std::uint64_t)std::min(flt, cap_total),
                                     (std::uint64_t)gy, (std::uint64_t)storm,
                                     (std::uint64_t)(restrict_after + 1), consumed, colour_choice })
            { EnumStatsHash(hb, v); }
            for (int c = 0; c < 6; ++c)
            {
                EnumStatsHash(he, (std::uint64_t)rock[c]);
                EnumStatsHash(hb, (std::uint64_t)std::min(rock[c], cap[c]));   // cap 0 -> no bucket at all
            }
            std::uint64_t hs = 0, ho = 0;   // looser: drop cards-spent identity (and then storm too)
            for (std::uint64_t v : { (std::uint64_t)std::min(mv, cap_total), (std::uint64_t)std::min(flt, cap_total),
                                     (std::uint64_t)gy, (std::uint64_t)storm, (std::uint64_t)(restrict_after + 1) })
            { EnumStatsHash(hs, v); }
            for (std::uint64_t v : { (std::uint64_t)std::min(mv, cap_total), (std::uint64_t)std::min(flt, cap_total),
                                     (std::uint64_t)gy, (std::uint64_t)(restrict_after + 1) })
            { EnumStatsHash(ho, v); }
            for (int c = 0; c < 6; ++c)
            {
                EnumStatsHash(hs, (std::uint64_t)std::min(rock[c], cap[c]));
                EnumStatsHash(ho, (std::uint64_t)std::min(rock[c], cap[c]));
            }
            exact.insert(he);
            bucket.insert(hb);
            manastorm.insert(hs);
            manaonly.insert(ho);
            m_avail.push_back(pool_total + flt + ManaGateTriangular(gy)
                              + rock[0] + rock[1] + rock[2] + rock[3] + rock[4] + rock[5] - mv);
        }
        int t = 0;
        for (; t < ng; ++t)
        {
            ++choice[t];
            if (choice[t] <= (int)groups[m_groups[t]].size()) { break; }
            choice[t] = 0;
        }
        if (t == ng) { done = true; }
    }
    // ---- second mechanism: PER-SIDE gating -------------------------------------------------------
    // Even with zero signature collapse, splitting lets one test reject a whole ROW: if a payoff line
    // costs more than the BEST mana line can supply, all of its (mana x payoff) pairs die at once,
    // instead of being visited and rejected one by one as the flat odometer does today.
    int max_avail = pool_total;
    for (int a : m_avail) { max_avail = std::max(max_avail, a); }

    std::vector<int> p_groups, p_ind;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        bool is_m = false;
        for (int mg : m_groups) { if (mg == (int)g) { is_m = true; break; } }
        if (!is_m) { p_groups.push_back((int)g); }
    }
    for (int b : independent) { if (!IsManaSideAction(cands[b])) { p_ind.push_back(b); } }

    const int png = (int)p_groups.size(), pni = (int)p_ind.size();
    std::vector<int> pchoice(png, 0);
    std::uint64_t p_lines = 0, p_rowskip = 0, pairs_live = 0;
    bool pdone = false;
    while (!pdone)
    {
        for (int imask = 0; imask < (1 << pni); ++imask)
        {
            ++p_lines;
            int need = 0;
            for (int t = 0; t < png; ++t)
            { if (pchoice[t] > 0) { need += cands[groups[p_groups[t]][pchoice[t] - 1]].cost.ManaValue(); } }
            for (int b = 0; b < pni; ++b)
            { if (imask & (1 << b)) { need += cands[p_ind[b]].cost.ManaValue(); } }
            if (need > max_avail) { ++p_rowskip; continue; }         // whole row dies in ONE test
            for (int a : m_avail) { if (need <= a) { ++pairs_live; } }
        }
        int t = 0;
        for (; t < png; ++t)
        {
            ++pchoice[t];
            if (pchoice[t] <= (int)groups[p_groups[t]].size()) { break; }
            pchoice[t] = 0;
        }
        if (t == png) { pdone = true; }
    }
    enumstats::g_p_lines.fetch_add(p_lines, std::memory_order_relaxed);
    enumstats::g_p_rowskip.fetch_add(p_rowskip, std::memory_order_relaxed);
    enumstats::g_pairs_live.fetch_add(pairs_live, std::memory_order_relaxed);
    enumstats::g_pairs_total.fetch_add(p_lines * (kept ? kept : 1), std::memory_order_relaxed);

    enumstats::g_m_raw.fetch_add(combos, std::memory_order_relaxed);
    enumstats::g_m_kept.fetch_add(kept, std::memory_order_relaxed);
    enumstats::g_m_exact.fetch_add(exact.size(), std::memory_order_relaxed);
    enumstats::g_m_bucket.fetch_add(bucket.size(), std::memory_order_relaxed);
    enumstats::g_m_manastorm.fetch_add(manastorm.size(), std::memory_order_relaxed);
    enumstats::g_m_manaonly.fetch_add(manaonly.size(), std::memory_order_relaxed);
}

// ---- Two-stage plan-position enumeration: mana side x payoff side ------------------------------
//
// The flat odometer walks ONE mixed-radix counter over every candidate group, so a hand holding k
// ritual/ramp cards and n payoff cards visits the whole product -- re-testing each payoff line against
// every ritual line, one position at a time, and re-running the group predicates at each. This splits
// the walk in two:
//
//   Stage A (mana side)   -- groups whose selectable option ADDS mana: a ritual's float, a rock's ramp,
//                            a Lotus sac (IsManaSideAction). Every group predicate -- over-splice
//                            legality, the splice canonical form, the accelerant cheapest-first prefix,
//                            the Lotus independent-prefix collapse -- reads ONLY these digits (a splice
//                            base and an accelerant both carry ritual_floating_mana > 0 by
//                            construction), so the entire predicate layer runs here: once per mana
//                            line instead of once per (mana x payoff) pair.
//   Stage B (payoff side) -- everything else. Its cost is summed once per payoff line, and a line no
//                            mana line can fund dies on ONE test instead of being re-rejected against
//                            each of them. Measured: 38.5% of Dragonstorm payoff lines and 69.0% of
//                            Hinata's are unaffordable under every mana line.
//
// BYTE-IDENTICAL, which is why no ground truth moves. A (mana line, payoff line) pair fully determines
// the `choice`/`imask` the flat odometer would have held, so its flat position index is computable;
// surviving pairs are sorted by that index before being handed to the caller. Both the emitted plan SET
// and its ORDER therefore match the flat walk exactly -- load-bearing, because Solve's best-plan
// tie-break and EnumeratePlans' returned candidate order are order-sensitive.
//
// A hand with NO mana side takes the original flat loop verbatim (and skips the predicate bookkeeping
// entirely, since no mana side implies every group predicate is inert). That is the common case --
// burn, Knights, Anti-Lifegain, Auras, treasure_hunt and slivers have no rituals or rocks at all, and
// even on Dragonstorm only ~24% of enumeration calls hold one -- so those pay nothing for any of this.
//
// `emit` receives each surviving selection (the caller's consider() / eval_and_push()); `extra_ok` is
// the caller's remaining per-selection filter (Solve's Vial-capacity check). Both are template
// parameters, not std::function: this runs once per rollout node, where a per-call closure allocation
// is measurable.
template <class ExtraOk, class Emit>
static void EnumeratePlanPositions(const std::vector<Action>& cands,
                                   const std::vector<std::vector<int>>& groups,
                                   const std::vector<int>& independent,
                                   const ManaGateIndex* gate, int mana_bound,
                                   const SpliceOdometerIndex& sidx,
                                   const std::vector<int>& accel_order,
                                   bool any_splice, bool splice_collapse_on,
                                   bool accel_pred_on, bool has_ind_accel,
                                   // By const reference, NOT by value: Solve's `consider` closure
                                   // captures a dozen locals, and this is called once per rollout
                                   // node -- copying it per call is measurable.
                                   const ExtraOk& extra_ok, const Emit& emit)
{
    const int  num_groups = static_cast<int>(groups.size());
    const int  num_ind    = static_cast<int>(independent.size());
    const bool gate_on    = (gate != nullptr);

    // Aggregate of one side's selection: what it costs and what it contributes to the budget.
    struct Agg { int cost = 0, gain = 0, gy = 0, block = 0; };
    auto fold = [&](Agg& a, int j)
    {
        if (gate_on)
        {
            const ManaGateTerm& t = gate->term[j];
            a.cost += t.cost; a.gain += t.gain; a.gy += t.gy; a.block += t.block;
        }
        else { a.cost += cands[j].cost.ManaValue(); }
    };
    // The exact per-selection gate -- identical to the flat odometer's inner test.
    auto payable = [&](const Agg& m, const Agg& p) -> bool
    {
        if (!gate_on) { return m.cost + p.cost <= mana_bound; }
        if (m.block + p.block > 0) { return true; }              // per-subset discount -> not measurable
        return m.cost + p.cost <= gate->pool_total + m.gain + p.gain
                                  + ManaGateTriangular(m.gy + p.gy);
    };

    std::vector<int> sel;

    // Partition the odometer's digits. `mi`/`pi` hold SLOTS into `independent` so each side can
    // rebuild its bits at their original positions (the flat imask must be reproduced exactly).
    std::vector<int> mg, pg, mi, pi;
    for (int g = 0; g < num_groups; ++g)
    {
        bool ms = false;
        for (int j : groups[g]) { if (IsManaSideAction(cands[j])) { ms = true; break; } }
        (ms ? mg : pg).push_back(g);
    }
    for (int b = 0; b < num_ind; ++b)
    { (IsManaSideAction(cands[independent[b]]) ? mi : pi).push_back(b); }

    // Flat position weights: the mixed-radix stride of each group digit (digit 0 is fastest, matching
    // the flat odometer's carry order), so a side's position contribution is just a weighted sum.
    std::vector<std::uint64_t> stride(num_groups, 1);
    std::uint64_t acc = 1;
    for (int g = 0; g < num_groups; ++g) { stride[g] = acc; acc *= static_cast<std::uint64_t>(groups[g].size()) + 1; }

    struct Line { Agg agg; std::uint64_t pos = 0; unsigned imask = 0; int digits = 0; };
    std::vector<Line> mlines, plines;
    std::vector<int>  mdig, pdig;      // flat digit storage, |side groups| ints per line

    // ---- Stage A: the mana side (predicates run HERE, once per line) ----------------------------
    {
        const int ng = static_cast<int>(mg.size()), ni = static_cast<int>(mi.size());
        std::vector<int> choice(ng, 0);
        std::vector<int> full(num_groups, 0);   // the predicates read only mana-side digits
        bool done = false;
        while (!done)
        {
            for (int t = 0; t < ng; ++t) { full[mg[t]] = choice[t]; }
            const bool rejected =
                   (any_splice && SpliceGroupChoiceRejected(sidx, groups, full, splice_collapse_on))
                || (accel_pred_on && NonPrefixAccelViolated(accel_order, full));
            if (!rejected)
            {
                for (int imask = 0; imask < (1 << ni); ++imask)
                {
                    unsigned full_imask = 0;
                    for (int b = 0; b < ni; ++b) { if (imask & (1 << b)) { full_imask |= 1u << mi[b]; } }
                    if (has_ind_accel
                        && IndependentAccelPrefixViolated(cands, independent, static_cast<int>(full_imask)))
                    { continue; }
                    Line L;
                    L.imask  = full_imask;
                    L.digits = static_cast<int>(mdig.size());
                    for (int t = 0; t < ng; ++t)
                    {
                        mdig.push_back(choice[t]);
                        if (choice[t] > 0)
                        {
                            L.pos += static_cast<std::uint64_t>(choice[t]) * stride[mg[t]];
                            fold(L.agg, groups[mg[t]][choice[t] - 1]);
                        }
                    }
                    for (int b = 0; b < ni; ++b)
                    { if (imask & (1 << b)) { fold(L.agg, independent[mi[b]]); } }
                    mlines.push_back(L);
                }
            }
            int t = 0;
            for (; t < ng; ++t)
            {
                ++choice[t];
                if (choice[t] <= static_cast<int>(groups[mg[t]].size())) { break; }
                choice[t] = 0;
            }
            if (t == ng) { done = true; }
        }
    }
    if (mlines.empty()) { return; }   // every mana line is illegal -> no plan can be built

    // Best-case mana line, so a payoff line no mana line can fund is dropped without pairing at all.
    // `best_head` is the most spare mana any single line leaves after paying for itself; `min_mcost`
    // is the cheapest line, which is the same bound for the legacy scalar arm.
    int  best_head = std::numeric_limits<int>::min();
    int  min_mcost = std::numeric_limits<int>::max();
    bool any_block = false;
    for (const Line& L : mlines)
    {
        if (L.agg.block > 0) { any_block = true; }
        best_head = std::max(best_head, L.agg.gain + ManaGateTriangular(L.agg.gy) - L.agg.cost);
        min_mcost = std::min(min_mcost, L.agg.cost);
    }

    // ---- Stage B: the payoff side --------------------------------------------------------------
    {
        const int ng = static_cast<int>(pg.size()), ni = static_cast<int>(pi.size());
        std::vector<int> choice(ng, 0);
        bool done = false;
        while (!done)
        {
            for (int imask = 0; imask < (1 << ni); ++imask)
            {
                Line L;
                L.digits = static_cast<int>(pdig.size());
                for (int t = 0; t < ng; ++t)
                {
                    pdig.push_back(choice[t]);
                    if (choice[t] > 0)
                    {
                        L.pos += static_cast<std::uint64_t>(choice[t]) * stride[pg[t]];
                        fold(L.agg, groups[pg[t]][choice[t] - 1]);
                    }
                }
                for (int b = 0; b < ni; ++b)
                {
                    if (!(imask & (1 << b))) { continue; }
                    L.imask |= 1u << pi[b];
                    fold(L.agg, independent[pi[b]]);
                }
                // Whole-row skip: if the most generous mana line cannot fund this payoff line, no
                // pair can, so drop it here instead of re-rejecting it against every mana line.
                // The gain/gy guards are structural (a payoff-side action adds no mana by definition)
                // but cheap, and they keep the skip sound if that classification ever widens.
                if (L.agg.block == 0 && L.agg.gain == 0 && L.agg.gy == 0)
                {
                    if (gate_on)
                    {
                        if (!any_block && L.agg.cost > gate->pool_total + best_head) { continue; }
                    }
                    else if (min_mcost != std::numeric_limits<int>::max()
                             && L.agg.cost > mana_bound - min_mcost) { continue; }
                }
                plines.push_back(L);
            }
            int t = 0;
            for (; t < ng; ++t)
            {
                ++choice[t];
                if (choice[t] <= static_cast<int>(groups[pg[t]].size())) { break; }
                choice[t] = 0;
            }
            if (t == ng) { done = true; }
        }
    }

    // ---- Pair, then replay in flat-odometer order ----------------------------------------------
    struct Pair { std::uint64_t key; int m, p; };
    std::vector<Pair> pairs;
    const std::uint64_t imask_span = 1ull << num_ind;
    for (int pi_idx = 0; pi_idx < static_cast<int>(plines.size()); ++pi_idx)
    {
        const Line& P = plines[pi_idx];
        for (int mi_idx = 0; mi_idx < static_cast<int>(mlines.size()); ++mi_idx)
        {
            const Line& M = mlines[mi_idx];
            if (!payable(M.agg, P.agg)) { continue; }
            const std::uint64_t key = (M.pos + P.pos) * imask_span + (M.imask | P.imask);
            pairs.push_back(Pair{ key, mi_idx, pi_idx });
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.key < b.key; });

    const int mng = static_cast<int>(mg.size()), png = static_cast<int>(pg.size());
    std::vector<int> choice(num_groups, 0);
    for (const Pair& pr : pairs)
    {
        const Line& M = mlines[pr.m];
        const Line& P = plines[pr.p];
        for (int t = 0; t < mng; ++t) { choice[mg[t]] = mdig[M.digits + t]; }
        for (int t = 0; t < png; ++t) { choice[pg[t]] = pdig[P.digits + t]; }
        const unsigned imask = M.imask | P.imask;
        sel.clear();
        for (int g = 0; g < num_groups; ++g)
        { if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); } }
        for (int b = 0; b < num_ind; ++b)
        { if (imask & (1u << b)) { sel.push_back(independent[b]); } }
        if (!sel.empty() && extra_ok(sel)) { emit(sel); }
    }
}

// Feasible-aware EARLY ritual-drop -- the group-level, before-the-odometer analog of the late
// SubsetWastesAccelerant payoff-prune (PrunesAcceleratorWithoutPayoff; the user's spec).
// SubsetWastesAccelerant drops a ritual-bearing SUBSET with no same-turn payoff LATE, inside
// consider()/eval_and_push(), only AFTER the odometer has already enumerated the entire 2^k ritual x splice
// powerset -- the confirmed durdle on Dragonstorm's no-land combo hands (e.g. "Dragonstorm x2; Desperate
// Ritual x4", ~7.6s single-thread). When the hand cannot reach ANY payoff this turn even at its
// bootstrap-feasible mana MAX, every ritual-bearing subset is already doomed: it either casts no payoff
// (SubsetWastesAccelerant drops it) or casts a payoff the mana can never afford (consider()'s CanPay drops
// it). So we drop the ritual GROUPS from the odometer up front and skip the whole enumeration,
// BYTE-IDENTICALLY.
//
// The payoff set counts Dragons (creatures) ALWAYS -- the most permissive set -> smallest min-cost payoff ->
// the STRICTEST condition to fire -> we drop ONLY when even a fair Dragon is out of reach, never dropping a
// line either late-prune callsite would keep (Solve's uses the real storm_in_hand, which makes a fair Dragon
// a NON-payoff when a storm is in hand -> even MORE aggressive; EnumeratePlans' uses storm_in_hand=false ->
// creatures pay -> exactly our set). So our removals are a subset of both, in every storm state.
//
// SOUNDNESS -- the feasibility bound MUST match the engine's own affordability model exactly, or it drops
// lines the search keeps. consider()/eval_and_push credit a ritual's GROSS float UNCONDITIONALLY (eff.wild +=
// Σ ritual_float [+ the Rite-of-Flame gy_self triangular escalation]) while the ritual's own cost sits in the
// combined cost -> affordability is the SIMULTANEOUS total pool + Σgross >= Σcost, with NO check that each
// ritual is castable in SEQUENCE. So the exact max mana any subset can leave for a payoff is feasible_net =
// pool + Σ(gross - cost) over net-positive ramps (+ the same gy_self bonus) -- NON-sequenced. (A "bootstrap
// ideal cast-order" that only credits rituals affordable from a running total is STRICTLY TIGHTER, so at 0-1
// lands it drops ritual+payoff plans the engine still keeps -- this is NOT byte-identical and cost a
// Dragonstorm regression; do NOT reintroduce it as a byte-identical change.) If feasible_net <
// cheapest_payoff, eff.CanPay fails for every ritual+payoff subset -> dropping the rituals is byte-identical.
// feasible_net is color-blind (totals) => an UPPER bound on the color-aware CanPay (and filters only convert
// colour, never add total) => still sound. A same-turn cost reducer (Ruby Medallion) / affinity makes the
// scalar bound unsound (mirrors ManaPruneBound's bail) -> we do NOT drop. Provider-gated
// (PrunesAcceleratorWithoutPayoff = Dragonstorm only) + the same MTG_UNPRUNED(payoffprune) toggle as the late
// prune, plus an independent MTG_NO_RITUAL_EARLY_DROP opt-out for the speedup A/B. Inert for every other deck
// and whenever a payoff is reachable -> byte-identical.
static const bool s_ritual_early_drop = !EnvOn("MTG_NO_RITUAL_EARLY_DROP");

static void DropRitualGroupsIfNoPayoff(const GameState& state, const ManaPool& pool,
                                       const std::vector<Action>& cands,
                                       std::vector<std::vector<int>>& groups,
                                       std::vector<int>& group_hand_index,
                                       std::vector<int>& independent)
{
    if (!s_ritual_early_drop)                                    { return; }
    if (!ResolveProvider(state).PrunesAcceleratorWithoutPayoff()) { return; }
    if (DecisionUnpruned(UnprunedGate::PayoffPrune))             { return; }
    // Single pass over cands: detect a ritual (else nothing to drop); bail on a same-turn cost reducer /
    // affinity (they make the scalar bound unsound, mirroring ManaPruneBound's bail); find the cheapest
    // reachable PAYOFF cost; and accumulate feasible_net = pool + Σ NET (gross - cost) of every net-positive
    // ramp -- the MAX mana any subset can leave for a payoff after paying the rituals' own costs. Payoff =
    // Dragonstorm(tutor_to_battlefield) / Apex(impulse_exile) / Dragon(creature), mirroring
    // SubsetWastesAccelerant (params off def, IsCreature off def->card), creatures ALWAYS counting (the
    // permissive, conservative set). cheapest_payoff stays LLONG_MAX when NO payoff is castable at all (the
    // "no payoff in hand" case) -> the test below fires unconditionally.
    bool      any_ritual      = false;
    long long cheapest_payoff = std::numeric_limits<long long>::max();
    long long feasible_net    = pool.Total();
    int       gy_self         = 0;
    for (const Action& a : cands)
    {
        const long long gross = static_cast<long long>(a.ritual_float) + a.rock_mana.Total();
        const long long cost  = a.cost.ManaValue();
        if (a.ritual_float > 0) { any_ritual = true; }
        if (gross > cost)       { feasible_net += gross - cost; }   // net-positive ramp credit (non-seq)
        const CardDefinition* d = a.def;
        if (!d) { continue; }
        if (d->params.affinity_for_subtype)          { return; }
        if (!d->params.reduces_spell_color.empty())  { return; }
        if (d->params.ritual_float_gy_self_bonus)    { ++gy_self; }   // Rite of Flame graveyard escalation
        if (d->params.tutor_to_battlefield || d->params.impulse_exile > 0 || d->card.IsCreature())
        { cheapest_payoff = std::min<long long>(cheapest_payoff, cost); }
    }
    if (!any_ritual) { return; }   // no ritual group exists -> nothing to drop
    feasible_net += static_cast<long long>(gy_self) * (gy_self - 1) / 2;   // mirror the eval_and_push credit
    if (feasible_net >= cheapest_payoff) { return; }   // a payoff IS reachable -> keep the rituals
    // Drop every group all of whose options are rituals (ritual_float > 0) -- exactly the actions the late
    // prune governs; a mixed / non-ritual group is untouched. (A ritual card's group holds only its own
    // ritual + splice variants, so this removes the whole ritual card from the odometer.)
    std::vector<std::vector<int>> kept_groups;
    std::vector<int>              kept_hand_index;
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
    {
        bool all_ritual = !groups[g].empty();
        for (int idx : groups[g]) { if (cands[idx].ritual_float <= 0) { all_ritual = false; break; } }
        if (all_ritual) { continue; }   // drop this pure-ritual group
        kept_groups.push_back(std::move(groups[g]));
        kept_hand_index.push_back(group_hand_index[g]);
    }
    groups.swap(kept_groups);
    group_hand_index.swap(kept_hand_index);
    // Also drop independent ritual actions -- notably the in-play Lotus Bloom SacForMana (hand_index < 0,
    // ritual_float = sac_for_mana_amount > 0), which its own group can never hold. BootstrapFeasibleMana's
    // .net already credits their float (Lotus sac: cost 0, gross 3, net +3), so a fired drop means even
    // WITH all Lotus mana no payoff is affordable -> every Lotus+payoff subset fails CanPay and every
    // Lotus-without-payoff subset is late-pruned -> byte-identical. This is what collapses the Lotus-heavy
    // no-payoff hands (the 31-min class).
    std::vector<int> kept_independent;
    for (int j : independent) { if (cands[j].ritual_float <= 0) { kept_independent.push_back(j); } }
    independent.swap(kept_independent);
}

// --- Learned mid-game plan scoring (see docs/design/learned-d0-policy.md) ---------------------
// Score a plan (summarized by its board effect) with the deck's learned evaluator, clamped to int
// (Plan::value is int; the score's magnitude is irrelevant -- ranking is ordinal). Only reached when
// a model is attached AND MTG_EVAL_MODEL is set (guarded at each callsite), so default runs never
// enter here and stay byte-identical.
static int LearnedPlanScore(const GameState& state, const MidGamePlanSummary& sum, const MidGameEvaluator& ev)
{
    const long long s = ev.Score(ExtractMidGameFeatures(state, sum));
    constexpr long long kLo = -1000000000LL, kHi = 1000000000LL;   // clamp into int (ordinal use only)
    return static_cast<int>(std::min(kHi, std::max(kLo, s)));
}

// The plan summary is built by the SHARED name-based SummarizePlanByNames (KeepModel.cpp) so runtime
// inference (here) and offline label emission (AIEngine's EnumerateEarliestWins dump) compute
// byte-identical summaries from the same cast names -> no train/serve skew. These helpers just pull
// the cast-spell names (and land-drop bit) out of a selection / resolved plan.
static std::vector<std::string> PlanCastNames(const std::vector<Action>& cands, const std::vector<int>& sel)
{
    std::vector<std::string> names;
    for (int j : sel)
    {
        const Action::Kind k = cands[j].kind;
        if (k == Action::Kind::CastFromHand || k == Action::Kind::CastFromGraveyard)
        { names.push_back(cands[j].card_name); }
    }
    return names;
}
static std::vector<std::string> PlanCastNames(const std::vector<Action>& actions)
{
    std::vector<std::string> names;
    for (const Action& c : actions)
    {
        if (c.kind == Action::Kind::CastFromHand || c.kind == Action::Kind::CastFromGraveyard)
        { names.push_back(c.card_name); }
    }
    return names;
}
// Sum the hand-tuned EvalCard over a plan's cast cards -> the plan_baseline_eval feature. Called from
// BOTH the ranking seam and the label dump (via TurnSolver::PlanBaselineEval) so train == serve. It is
// intentionally the name-derived EvalCard sum (deterministic in state), NOT the seam's internal
// total_eval (which carries X/ritual overrides the name-only dump can't reconstruct) -> lockstep.
int TurnSolver::PlanBaselineEval(const GameState& state, const std::vector<std::string>& cast_names)
{
    int sum = 0;
    for (const std::string& nm : cast_names)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(nm);
        if (def) { sum += EvalCard(*def, state); }
    }
    return sum;
}
static bool PlanHasLand(const std::vector<Action>& cands, const std::vector<int>& sel)
{
    for (int j : sel) { if (cands[j].kind == Action::Kind::PlayLand) { return true; } }
    return false;
}

// ---- Sac-land burn hold ("hold Shard Volley until it buys something") -------------------------
// A sacrifice-a-land damage spell (Shard Volley: {R}, sac a land, 3 to any target) trades a
// PERMANENT mana source for a fixed lump of face damage. Goldfishing there is no race and the
// opponent never gains life, so 3 damage now is worth exactly what 3 damage later is worth -- while
// the sacrificed land costs a mana on EVERY intervening turn. So the cast is only ever right when it
// buys something THIS turn -- three exceptions, all board-readable (nothing clairvoyant):
//   (a) the plan wins this turn -- the damage is the killing blow;
//   (b) it turns Spectacle on for a spell in the same plan (Light Up the Stage {2}{R} -> {R}) -- a
//       real same-turn discount the deferred cast cannot recover;
//   (c) it pumps a PROWESS attacker that connects this turn (Monastery Swiftspear) -- likewise
//       destroyed by deferring.
// Everything else is a strictly worse ordering of the same total damage. Being FLOODED is NOT a
// fourth exception: surplus lands make an early cast harmless, never better, so there is no upside
// to weigh and the rule needs no flood branch (it also makes deferring free in exactly those hands).
//
// Motivation: at d5 the search cast its first Shard Volley by T3 in 37/100k games vs 8 at d6, and
// those games are ~60% of d5's quality deficit (seed 203265: sacs its ONLY land on T2, sits on 0
// lands through T5 unable to cast four drawn spells, and loses with the opponent on 1 life).
//
// This is a PRUNE and nothing else (search-primary): it only ever REMOVES plans, it is provider-owned
// (burn opts in -> every other deck is byte-identical), and MTG_UNPRUNED / MTG_UNPRUNE=saclandhold
// reopens the full branch set for the standing pruned-vs-unpruned audit. MTG_SV_HOLD=0 is the legacy
// hatch (pre-heuristic behaviour) for a byte-identical A/B.
//
// MEASURED (burn, shipped d6/budget-20/trust-5, 20 paired seeds x 5000 games per arm, both blocks):
//   train    -0.00143 turns  t=-12.11  20/20 seeds better
//   held-out -0.00174 turns  t=-10.72  20/20 seeds better        [negative = better]
//   cost: +0.14% instructions (callgrind, deterministic); wall clock indistinguishable.
// Exceptions (b) and (c) are NOT decoration -- they carry the result:
//   * WITHOUT (c): +0.00292  t=+16.25  0/20 better. The rule alone LOSES. A noncreature cast pumps
//     every attacking Swiftspear, which the "3 damage is worth the same later" argument misses.
//   * (b) strict-vs-permissive (must SV be the only face-damage source to count as the enabler?) is
//     byte-identical over 100k games -- the powerset never affords Light Up at full cost, so the only
//     SV+Spectacle plans come from the Plan-B builder, where SV is the sole trigger by construction.
//     Kept in its strict form because that is the rule as reasoned; the permissive branch was deleted.
static bool SacLandHoldEnabled()
{
    static const bool on = EnvOn("MTG_SV_HOLD", true);   // DEFAULT ON; =0 restores the old behaviour
    return on;
}

// True => drop this plan. Exception (a) is the caller's `!wins` gate, so this only decides (b)/(c).
// Callers also gate on `sacrifice_count > 0`, so a deck with no sac-land card never reaches the scan.
// ---- MTG_SV_LETHAL_AUDIT (diagnostic, default off) -------------------------------------------
// The prune only runs when the projection says `!wins`, so exception (c) can ONLY ever fire on plans
// already judged non-lethal. That leaves exactly two possibilities, and they call for opposite fixes:
//   * those turns really are non-lethal  -> (c) is about ACCELERATING damage, and is a real heuristic;
//   * the projection is still under-counting -> (c) is masking a LETHAL-CALCULATION bug, and the fix
//     belongs in projected_atk, not in an exception.
// This audit decides it empirically: for every plan the prune touches, APPLY it on a copy, run the
// real combat, and see whether the opponent actually dies. Counts are printed at exit.
namespace sv_audit
{
    inline std::atomic<uint64_t> g_pruned{0};          // strict rule dropped it
    inline std::atomic<uint64_t> g_pruned_lethal{0};   //   ... and it was ACTUALLY lethal  <-- a bug
    inline std::atomic<uint64_t> g_rescued{0};         // (c) kept it
    inline std::atomic<uint64_t> g_rescued_lethal{0};
    inline std::atomic<uint64_t> g_dumped{0};
    inline std::atomic<uint64_t> g_pruned_staged{0};   // strict rule dropped a STAGED (expiring) SV
    inline std::atomic<uint64_t> g_rescued_staged{0};  // (c) kept a STAGED (expiring) SV  //   ... and it was ACTUALLY lethal  <-- a bug
    struct Report
    {
        ~Report()
        {
            if (!g_pruned.load() && !g_rescued.load()) { return; }
            std::fprintf(stderr,
                "\n=== SV LETHAL AUDIT ===\n"
                "  strict-pruned      : %llu   ACTUALLY LETHAL: %llu   STAGED(expiring): %llu\n"
                "  exception-rescued  : %llu   ACTUALLY LETHAL: %llu   STAGED(expiring): %llu\n",
                (unsigned long long)g_pruned.load(),  (unsigned long long)g_pruned_lethal.load(),
                (unsigned long long)g_pruned_staged.load(),
                (unsigned long long)g_rescued.load(), (unsigned long long)g_rescued_lethal.load(),
                (unsigned long long)g_rescued_staged.load());
        }
    };
    inline Report g_report;
}
inline bool SvLethalAuditOn()
{
    static const bool on = EnvOn("MTG_SV_LETHAL_AUDIT");
    return on;
}

// rescued_by_exception (out, optional): set when exception (c) is what kept the plan -- i.e. the
// (a)+(b) rule alone would have dropped it. Used only by MTG_SV_LETHAL_AUDIT.
static bool HoldSacLandBurn(const GameState& state, const std::vector<Action>& cands,
                            const std::vector<int>& sel, bool* rescued_by_exception = nullptr)
{
    if (!SacLandHoldEnabled())                                       { return false; }
    if (!ResolveProvider(state).HoldsSacLandBurnUntilLethal())       { return false; }
    if (DecisionUnpruned(UnprunedGate::SacLandHold))                 { return false; }

    const Player& ap = state.ActivePlayer();
    bool has_sac = false, has_spectacle = false, other_face_damage = false;
    bool sac_is_staged = false, sac_expires_now = false;
    for (int j : sel)
    {
        const Action& c = cands[j];
        if (c.kind != Action::Kind::CastFromHand) { continue; }
        if (c.sacrifice_land && c.direct_damage > 0)
        {
            has_sac = true;
            if (c.hand_index >= 0 && c.hand_index < static_cast<int>(ap.hand.size())
                && ap.hand[c.hand_index].m_is_staged)
            {
                // m_staged_expiry = the LAST turn this card may be played, so it is use-it-or-lose-it
                // only on that turn. Earlier than that, holding still costs nothing.
                sac_is_staged  = true;
                sac_expires_now = (ap.hand[c.hand_index].m_staged_expiry <= state.turn_number);
            }
            continue;
        }
        if (c.has_spectacle)      { has_spectacle = true; }
        if (c.direct_damage > 0)  { other_face_damage = true; }
    }
    if (!has_sac) { return false; }   // the sac cast is a non-damage one (no such card today)
    // (b) Spectacle enabler. Already-active Spectacle needs no enabler, so the exception is dead
    // there -- and another face-damage cast in the plan is the cheaper enabler (it leaves the land).
    if (has_spectacle && !state.opponent_lost_life_this_turn && !other_face_damage) { return false; }
    // (c) EXPIRING: the card is not in our hand -- Light Up the Stage exiled it, and it is playable
    // only until the end of our next turn (CR 406; Card::m_staged_expiry is that last turn). On the
    // expiry turn "holding" it does not defer the damage, it DESTROYS the card. That is the ONLY real
    // drawback to holding, and it is a card-mechanics fact, not a strategic judgement. Before the
    // expiry turn there is still no reason to cast it early, so the exception is deliberately narrow:
    // fire on the LAST legal turn, not merely because the card is staged.
    if (sac_expires_now) { if (rescued_by_exception) { *rescued_by_exception = true; } return false; }
    (void)sac_is_staged;
    return true;
}

// ---- TurnSolver::Solve ---------------------------------------------------

TurnSolver::Plan TurnSolver::Solve(const GameState& state, bool is_pre_combat)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    const Player& ap = state.ActivePlayer();
    ManaPool pool             = AvailableManaPool(state);
    ManaPool pool_noncreature = BuildNonCreaturePool(state);
    int total_lands  = CountLands(state);
    int pending_atk  = PendingAttackDamage(state);
    int prowess_attackers   = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    // Greedy path (d0 decision + every rollout leaf): suppress the speculative safe-alt burn so
    // Invigorate is left to the tuned auto-fire (a greedy pick would cast it early and waste it).
    const bool saved_sce = g_search_candidate_enum;
    g_search_candidate_enum = false;
    std::vector<Action> cands = CollectActions(state, is_pre_combat);
    g_search_candidate_enum = saved_sce;
    int n = static_cast<int>(ap.hand.size());

    // Hinata combo: does any candidate float ritual mana (Reality Spasm / Irencrag)? Each Action's
    // gross float was stamped at enumeration (a.ritual_float), so this is a cheap scan -- no
    // per-node card lookup. `consider` sums cands[j].ritual_float over the chosen subset and credits
    // the pool's CanPay. False for every non-ritual deck -> byte-identical.
    bool any_ritual = false;
    for (const Action& ra : cands) { if (ra.ritual_float > 0) { any_ritual = true; break; } }
    // Storm-hold rule (see SubsetWastesAccelerant): is a storm payoff (Dragonstorm/Apex) already in
    // hand? Scans the FULL hand (not just castable cands -- the case that matters is a storm in hand but
    // not yet castable). Gated on the flag so it is a zero-cost no-op (storm_in_hand=false) by default.
    bool storm_in_hand = false;
    if (s_storm_hold)
    {
        for (const Card& c : ap.hand)
        {
            auto od = CardDatabase::Instance().LookupCached(c);
            if (od && (od->params.tutor_to_battlefield || od->params.impulse_exile > 0))
            { storm_in_hand = true; break; }
        }
    }
    // Same-turn mana-rock ramp: any non-creature rock (Sol Ring) stamped with its production?
    // Cheap scan -> the credit below is inert for every deck without such a rock.
    bool any_rock = false;
    for (const Action& ra : cands) { if (ra.rock_mana.Total() > 0) { any_rock = true; break; } }
    // Same-turn affinity (Thrumming Hivepool). Inert unless an affinity card is castable this turn.
    bool any_affinity = false;
    for (const Action& ra : cands) { if (ra.def && ra.def->params.affinity_for_subtype) { any_affinity = true; break; } }
    // NOTE: the same-turn cost-reducer (Ruby Medallion) generic credit is applied ONLY in
    // EnumeratePlans (the search's / viewer's plan list), NOT here in Solve's greedy consider(). The
    // credit is an OPTIMISTIC affordability hint (it assumes the reducer resolves before the spells it
    // discounts); that is sound only when a ROLLOUT validates the line -- every EnumeratePlans plan is
    // rollout-scored by the search or user-selected in the viewer, so an unrealisable Medallion+Apex
    // line rolls out to a non-win and is discarded. Solve's d0 greedy pick has NO rollout, so crediting
    // it here let the greedy commit an Apex line the executor then stranded on (smoke gi523 8->loss).
    // Leaving Solve uncredited keeps the d0 decision + every rollout leaf byte-identical.
    // Any splice-onto-Arcane base among the candidates? Gates the odometer's generate-time over-splice
    // skip (GroupChoiceOverSplices) so every non-splice deck pays nothing and stays byte-identical.
    bool any_splice = false;
    for (const Action& ra : cands) { if (ra.def && ra.def->params.splice_onto_arcane) { any_splice = true; break; } }
    // Filter/ramp land present? Enables the real-payment affordability fallback (color conversion
    // the flat pool can't model). False for every deck without such a land -> byte-identical.
    const bool any_filter = HasUntappedFilterSource(state);

    // Lands in hand -- a generic feasibility input (a plan cannot discard more lands than it
    // holds for retrace / Land's Edge additional costs; see the discard_lands_used check below).
    int lands_in_hand = 0;
    for (int i = 0; i < n; ++i)
    {
        auto def = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (def && def->card.IsLand()) { ++lands_in_hand; }
    }

    // Deck-specific reach toward THIS turn's lethal beyond combat + direct damage (the Treasure Hunt / Land's
    // Edge ammunition model) is provider-owned (HasExtraLethalModel / ExtraLethalDamage).
    // HasExtraLethalModel() gates the whole thing: a deck without such a model skips building the per-plan
    // cast list entirely, staying byte-identical to the old "all addends 0" path.
    const DecisionProvider& provider = ResolveProvider(state);
    const bool has_extra_lethal = provider.HasExtraLethalModel();
    std::vector<const CardDefinition*> casting;   // reused per subset (only when has_extra_lethal)

    // Dragonstorm acceleration-prefix collapse (HEURISTIC; provider-owned + MTG_UNPRUNED(AccelPrefix)-
    // gated -> byte-identical for every non-Dragonstorm deck and for Dragonstorm under MTG_UNPRUNED). When
    // on, GroupChoiceNonPrefixAccel below collapses the 2^K ritual-accelerant powerset to the K+1 cheapest-
    // first prefixes. any_accel gates the odometer skip on there actually being a ritual accelerant in hand,
    // mirroring the any_splice fast path -> zero overhead when the gate is off or no accelerant is present.
    const bool accel_prefix_on = provider.UseAccelPrefixCollapse()
                              && !DecisionUnpruned(UnprunedGate::AccelPrefix);
    // Desperate Ritual splice-count collapse (HEURISTIC, mirrors accel_prefix_on; see SpliceCollapseViolated
    // + CollectActions' 2-variant emission). Gated on any_splice so non-splice decks skip it entirely.
    const bool splice_collapse_on = any_splice && provider.UseSpliceCollapse()
                                 && !DecisionUnpruned(UnprunedGate::SpliceCollapse);
    bool any_accel = false;
    if (accel_prefix_on)
    {
        for (const Action& ra : cands)
        {
            if (ra.def && ra.def->params.ritual_floating_mana > 0
                && ra.kind == Action::Kind::CastFromHand) { any_accel = true; break; }
        }
    }
    // Lotus-independent accelerant prefix collapse (see IndependentAccelPrefixViolated): fungible Lotus
    // sacs -> keep only the cheapest-first (index) prefix of the 2^num_ind independent mask. Rides
    // accel_prefix_on + MTG_NO_LOTUS_PREFIX; scanned once here so the per-imask check is a cheap walk.
    bool has_ind_accel = false;
    if (accel_prefix_on && s_lotus_prefix)
    {
        for (const Action& ra : cands)
        { if (ra.ritual_float > 0 && ra.kind == Action::Kind::SacForMana) { has_ind_accel = true; break; } }
    }

    int m = static_cast<int>(cands.size());
    Plan best;
    int  best_mask = 0;     // action mask of `best` (0 = the do-nothing default); ties keep min mask

    bool have_colors[5];    // untapped-source colors -- state-only, computed once for all subsets
    ComputeAvailableColors(state, have_colors);

    // Learned mid-game evaluator (per-deck, MTG_EVAL_MODEL-gated): when attached it RANKS non-lethal
    // plans by predicted win-turn in place of the EvalCard sum. nullptr in every default run
    // (no sidecar OR flag off) -> rank_value == total_eval below -> byte-identical. learned-d0-policy.md.
    const MidGameEvaluator* ev = (UseLearnedEval() && state.m_evaluator && !state.m_evaluator->empty())
                               ? state.m_evaluator : nullptr;

    // d0/rollout "rituals-for-payoff" guard (HEURISTIC; default ON, opt-out MTG_NO_RITUAL_PAYOFF_GUARD).
    // A mana ritual (Rite of Flame, Desperate/Pyretic Ritual, Lotus sac -- anything with ritual_float > 0)
    // should only be cast to the extent its floated mana funds a same-turn payoff. On a non-winning plan
    // the scoring-site check below rejects a subset that casts a SURPLUS ritual (one removable with the
    // rest still payable) -- so a plan never spends "10 mana of rituals to hard-cast a 4-mana Scourge",
    // nor floats mana nothing uses (lost card + Sandstone depletion; the Dragonstorm setup-turn misplays
    // the user flagged). EXCEPTION (storm gate): a plan containing a tutor_to_battlefield storm wincon
    // (Dragonstorm) keeps ALL rituals -- extra spells = extra dragons; measured decisive. This is a
    // GREEDY/ROLLOUT policy fix only -- Solve is the d0 decision and every rollout leaf, so curbing waste
    // here improves both direct d0 play and the leaf win-turns the search reads. It deliberately does NOT
    // touch EnumeratePlans (the search's branch list): a ritual is still OFFERED as a branch and kept in
    // hand for a future turn. Gated on any_ritual -> byte-identical for non-ritual decks. Measured:
    // Dragonstorm d0 ~0.9 turns faster / search ~0.05-0.08; Hinata +0.05; burn byte-identical; work down.
    static const bool s_ritual_payoff_guard = !EnvOn("MTG_NO_RITUAL_PAYOFF_GUARD");

    // Evaluate one selected combination of candidate indices and, if it is the new optimum,
    // record it. The optimum is ordered by (wins, value, SMALLEST action mask): a winning plan
    // beats a non-winner; higher eval beats lower; among equals the numerically smallest mask
    // wins. That last tie-break is exactly the plan the ascending-mask powerset below settles on
    // (first-found under a strict '>' test) -- making it explicit lets the odometer enumeration
    // return the byte-identical plan despite visiting subsets in a different order.
    auto consider = [&](std::vector<int> sel)
    {
        std::sort(sel.begin(), sel.end());          // ascending -> matches the powerset's bit order
        // Reject a Swords cast not backed by a live/same-turn enabler (see the helper). Inert
        // for every deck without controller_lifegain_equals_power.
        if (SubsetHasUnbackedLifegainRemoval(state, cands, sel)) { return; }
        // Payoff-prune (PrunesAcceleratorWithoutPayoff): drop a ritual-accelerant subset that casts no payoff
        // (Dragon/Dragonstorm/Apex). Provider-owned (DragonstormProvider) + MTG_UNPRUNED(payoffprune)-
        // gated; inert for every other deck -> byte-identical. storm_in_hand feeds the storm-hold rule
        // (a fair Dragon stops justifying a ritual when a storm is in hand); off by default.
        if (ResolveProvider(state).PrunesAcceleratorWithoutPayoff()
            && !DecisionUnpruned(UnprunedGate::PayoffPrune)
            && SubsetWastesAccelerant(cands, sel, storm_in_hand)) { return; }
        // Reject two SacForMana of the same source (its colour variants are mutually exclusive). Inert
        // without a SacForMana action (Lotus Bloom) -> byte-identical.
        if (SubsetHasDuplicateSacSource(cands, sel)) { return; }
        // Reject a creature sac-for-mana whose float nothing spends (see the helper). Solve's
        // rituals-for-payoff guard already covers this on the credited/pool path; this also catches
        // the filter fallback, and keeps the rule identical on both sides. Inert without a creature
        // mana outlet -> byte-identical.
        if (SubsetWastesCreatureSacMana(state, cands, sel)) { return; }
        // Reject physically-impossible Desperate Ritual over-splice (a spliced copy must still be in
        // hand). Inert without a splice base selected -> byte-identical.
        if (SubsetHasIllegalSplice(state, cands, sel)) { return; }
        int mask = 0;
        for (int j : sel) { mask |= (1 << j); }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count    = 0;
        int noncreature_count  = 0;
        int direct_dmg         = 0;
        int total_eval         = 0;
        int self_damage        = 0;
        int vial_haste_atk     = 0;
        int haste_cast_atk     = 0;   // hard-cast haste creatures attacking this turn
        int haste_cast_prowess = 0;   // ... of which have prowess (pumped by this plan's casts)
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)

        for (int j : sel)
        {
            const Action& c = cands[j];
            discard_lands_used += c.discard_lands;

            combined.white     += c.cost.white;
            combined.blue      += c.cost.blue;
            combined.black     += c.cost.black;
            combined.red       += c.cost.red;
            combined.green     += c.cost.green;
            combined.colorless += c.cost.colorless;
            combined.generic   += c.cost.generic;

            if (c.is_noncreature)
            {
                noncreature_combined.white     += c.cost.white;
                noncreature_combined.blue      += c.cost.blue;
                noncreature_combined.black     += c.cost.black;
                noncreature_combined.red       += c.cost.red;
                noncreature_combined.green     += c.cost.green;
                noncreature_combined.colorless += c.cost.colorless;
                noncreature_combined.generic   += c.cost.generic;
                ++noncreature_count;
            }

            if (c.sacrifice_land)    { ++sacrifice_count; }
            direct_dmg        += c.direct_damage;
            total_eval        += c.eval;
            vial_haste_atk    += c.vial_attack_power;
            haste_cast_atk    += c.haste_attack_power;
            if (c.haste_prowess) { ++haste_cast_prowess; }

            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        // Hinata combo: a ritual cast in THIS subset floats mana for the rest of the subset.
        // Credit its gross float to the affordable pool (the ritual's own cost is already in
        // `combined`, so pool+gross-cost == pool+net -> exact, conservative). Zero unless a
        // ritual is selected -> byte-identical for every non-ritual deck.
        // Same-turn ramp credit. Ritual float (Reality Spasm / Irencrag) credited as wild; a mana
        // rock cast in THIS subset (Sol Ring -> {C}{C}) credited by its REAL produced colours, but
        // only once the board can already pay for the rocks themselves (a rock never funds its own
        // cost). The casts' own costs are already in `combined`, so this stays net/conservative.
        // Both inert -> byte-identical for decks without rituals or rocks.
        ManaPool eff = pool, eff_nc = pool_noncreature;
        bool credited = false;
        int  simul_ritual_credit = 0;
        if (any_ritual)
        {
            int ritual_credit = 0;
            {
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            // Rite-of-Flame graveyard self-scaling: k copies cast this turn escalate (+0,+1,...,+k-1)
            // as each prior copy hits the graveyard; the flat per-cast stamp misses that triangular
            // term. Single self-scaling name in-deck, so count-by-flag == count-by-name.
            int gy_self = 0;
            for (int j : sel) { if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++gy_self; } }
            ritual_credit += gy_self * (gy_self - 1) / 2;
            }
            simul_ritual_credit = ritual_credit;
            if (ritual_credit > 0) { eff.wild += ritual_credit; eff_nc.wild += ritual_credit; credited = true; }
        }
        if (any_rock)
        {
            ManaPool rock_prod; ManaCost rock_costs; bool sel_rock = false;
            for (int j : sel)
            {
                if (cands[j].rock_mana.Total() <= 0) { continue; }
                rock_prod.AddPool(cands[j].rock_mana);
                const ManaCost& rc = cands[j].cost;
                rock_costs.white += rc.white; rock_costs.blue += rc.blue; rock_costs.black += rc.black;
                rock_costs.red   += rc.red;   rock_costs.green += rc.green;
                rock_costs.colorless += rc.colorless; rock_costs.generic += rc.generic;
                sel_rock = true;
            }
            if (sel_rock && pool.CanPay(rock_costs)) { eff.AddPool(rock_prod); eff_nc.AddPool(rock_prod); credited = true; }
        }
        // Same-turn affinity (Hivepool): subtract the extra generic discount from same-turn slivers.
        if (any_affinity)
        {
            int acred = CostTricksEnabled() ? SameTurnAffinityGenericCredit(state, cands, sel) : 0;
            if (acred > 0)
            {
                combined.generic             = std::max(0, combined.generic - acred);
                noncreature_combined.generic = std::max(0, noncreature_combined.generic - acred);
            }
        }
        // (No same-turn reducer credit here -- see the note at the any_affinity flag above; the
        // Medallion credit is EnumeratePlans-only so Solve's greedy/leaf stays byte-identical.)
        bool mana_ok = credited ? (eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined))
                                 : (pool.CanPay(combined) && pool_noncreature.CanPay(noncreature_combined));
        // SEQUENCED ritual credit, applied LAZILY. The sequenced credit is never LARGER than the
        // simultaneous one, so a position the cheap model already rejects would be rejected by the
        // sequenced model too -- only the SURVIVORS need the walk. Most odometer positions are
        // rejected, so this keeps the honest model off the hot path (measured: Dragonstorm d5
        // +3.2% -> +1.5% instructions, Hinata2 d5 +0.2%).
        const bool seq_on = any_ritual && simul_ritual_credit > 0 && SeqRitualCreditMode() >= 1;
        int  seq_credit   = -1;   // sequenced credit actually folded into eff (-1 = not computed yet)
        // Fold the sequenced correction into eff/eff_nc. Must run for EVERY surviving position, not
        // just the mana_ok ones: `eff` is read downstream (fill_surplus, the ritual-drop re-credit),
        // so a position rescued by the filter/reframe fallback below would otherwise go on spending
        // the optimistic pool.
        auto apply_seq = [&]()
        {
            if (seq_credit >= 0) { return; }
            seq_credit = SequencedRitualCredit(pool, cands, sel);
            if (seq_credit >= simul_ritual_credit) { return; }
            const int back = simul_ritual_credit - seq_credit;
            eff.wild    -= back;
            eff_nc.wild -= back;
        };
        if (seq_on && mana_ok)
        {
            apply_seq();
            mana_ok = eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined);
        }
        // Filter/ramp-land color conversion the flat pool can't express -> real-payment fallback.
        if (!mana_ok && !(any_filter && SubsetPayableWithFilters(state, cands, sel))) { return; }
        if (seq_on) { apply_seq(); }   // filter-rescued survivor: keep its credited pool honest too
        if (sacrifice_count > total_lands)                   { return; }
        if (discard_lands_used > lands_in_hand)              { return; }
        // Accurate per-color payability (rejects wild-pool phantoms, e.g. a {U} hard-cast off a
        // W/R/B-only land). Strict tightening; inert for decks whose lands produce their colors.
        if (!SubsetPayable(have_colors, cands, sel))         { return; }

        // Irencrag Feat "you can cast only one more spell this turn": reject any subset that casts
        // more than max_casts_after spells AFTER the restricting ritual (ordered by CastOrderRank).
        // The provider ranks Irencrag as the last ritual (18, just before the Crackle payoff at 20),
        // so the realised cast order ...Reality Spasm(15) -> Irencrag(18) -> Crackle(20) is legal and
        // the executor/rollout (which cast in CastOrderRank order) match this judgement -> lockstep.
        // Loop runs lookups only when a restrictor is actually selected (rare); flag-check otherwise.
        for (int j : sel)
        {
            if (cands[j].max_casts_after < 0) { continue; }
            const CardDefinition* rd = cands[j].def;
            const int r_rank = rd ? ResolveProvider(state).CastOrderRank(state, *rd) : 20;
            int after = 0;
            for (int k : sel)
            {
                if (k == j) { continue; }
                const CardDefinition* kd = cands[k].def;
                const int k_rank = kd ? ResolveProvider(state).CastOrderRank(state, *kd) : 20;
                if (k_rank > r_rank) { ++after; }
            }
            if (after > cands[j].max_casts_after) { return; }   // illegal: too many spells after it
        }

        // Eidolon-style on-cast triggers go on top of the spell being cast (CR 603), so they
        // resolve BEFORE the spell. A plan that kills us via self-damage cannot win.
        if (self_damage >= ap.life)                          { return; }

        // FILL a scaled Magma cast UP from this plan's LEFTOVER mana (spend-all; the searched Crackle {X}
        // already took its 3-mana chunks, so the surplus is Magma's sub-chunk remainder). At most one Magma
        // per plan (mutually exclusive by hand_index). Feeds direct_dmg/total_eval BEFORE the win projection
        // and stores the filled action below; lockstep cost is recomputed from crackle_targets at execution.
        int fill_surplus = mana_ok ? std::max(0, (credited ? eff.Total() : pool.Total()) - combined.ManaValue()) : 0;
        int fill_j = -1; Action fill_action;
        if (fill_surplus > 0)
        {
            for (int j : sel)
            {
                Action ca = cands[j];
                int extra = FillScaledCastFace(state, ca, fill_surplus);
                if (extra > 0) { direct_dmg += extra; total_eval += extra * 100; fill_j = j; fill_action = ca; break; }
            }
        }

        // This turn's attack damage. A hard-cast haste creature attacks now (haste_cast_atk) and,
        // if it has prowess, is pumped by this plan's noncreature casts too -- the canonical cast
        // order resolves prowess creatures before noncreature spells. Both terms were missing, so
        // a plan whose lethal turn RELIES on a cast Goblin Guide / Swiftspear did not read as a win.
        int projected_atk = pending_atk + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        // Deck-specific extra reach toward lethal (Land's Edge ammo + clairvoyant Treasure Hunt),
        // provider-owned. Built only when the deck has such a model (byte-identical otherwise).
        int extra_lethal = 0;
        if (has_extra_lethal)
        {
            casting.clear();
            for (int j : sel) { casting.push_back(cands[j].def); }
            extra_lethal = provider.ExtraLethalDamage(state, casting);
        }
        bool wins = (projected_atk + direct_dmg + extra_lethal) >= state.Opponent().life;

        // Hold a sac-land burn (Shard Volley) unless it wins this turn or enables Spectacle -- see
        // HoldSacLandBurn. The integer test keeps every deck without such a card off the scan.
        if (sacrifice_count > 0 && !wins && HoldSacLandBurn(state, cands, sel)) { return; }

        // Rituals-for-payoff guard (see the flag above), with the STORM heuristic gate the user asked for.
        // Question: may this plan spend MORE ritual mana than the payoff's cost? Default NO -- so we reject
        // a non-winning subset with a SURPLUS ritual (one whose removal still leaves everything payable):
        // over-paying a fixed-cost spell (the user's "10 mana of rituals to hard-cast a 4-mana Scourge")
        // just burns a card + Sandstone depletion for nothing. But a STORM wincon breaks that: Dragonstorm
        // puts min(spells_cast_this_turn, ...) dragons, so EXTRA rituals ARE the payoff (more dragons) --
        // measured decisive (ungated count-minimisation robbed storm lethality, dLP -0.008 vs -0.804). So
        // when the plan contains a tutor_to_battlefield (storm) card we DON'T minimise -- keep every
        // ritual. Never blocks a winning plan; gated on any_ritual -> byte-identical for non-ritual decks.
        if (s_ritual_payoff_guard && any_ritual && !wins)
        {
            bool wants_excess = false;   // storm wincon in the plan -> extra rituals buy dragons, keep all
            for (int j : sel)
            {
                if (cands[j].def && cands[j].def->params.tutor_to_battlefield) { wants_excess = true; break; }
            }
            if (!wants_excess)
            {
                // Is ANY selected ritual surplus -- removable with everything else still payable? The
                // credited pool `eff` treats ritual float as wild, so removing a ritual reduces wild by its
                // float (triangular gy-self bonus recomputed for Rite chains) and drops its own cost from
                // the target. A cheap ritual can be the load-bearing bootstrap for an expensive one, so we
                // test EACH ritual (linear, two CanPay each -- not a powerset). SubsetPayable need not be
                // rechecked: dropping a ritual only shrinks color demand and the full subset already passed
                // it. Subsumes the zero-payoff case (all rituals removable). Runs only on the credited/pool
                // path (mana_ok), not the filter fallback.
                if (mana_ok)
                {
                    int flat_sum = 0, gy = 0;
                    for (int j : sel)
                    {
                        if (cands[j].ritual_float <= 0) { continue; }
                        flat_sum += cands[j].ritual_float;
                        if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++gy; }
                    }
                    // What was ACTUALLY credited into eff. Under the sequenced model that is not the
                    // flat sum (a self-funding ritual earns nothing), and assuming otherwise made this
                    // check over-subtract and wrongly call load-bearing rituals surplus.
                    // `seq_bit` = the sequenced model actually withheld something HERE. Where it did
                    // not, the credited pool is the flat sum and the cheap closed form is exact, so
                    // those subsets keep the pre-change code path (and its cost) untouched -- only the
                    // subsets the honest model changed pay for the exact per-ritual recompute.
                    const bool seq_bit    = (seq_credit >= 0 && seq_credit < simul_ritual_credit);
                    const int  full_credit = seq_bit ? seq_credit : flat_sum + gy * (gy - 1) / 2;
                    for (int r : sel)
                    {
                        if (cands[r].ritual_float <= 0) { continue; }
                        const bool r_gy   = cands[r].def && cands[r].def->params.ritual_float_gy_self_bonus;
                        const int  gy_r   = gy - (r_gy ? 1 : 0);
                        const int  cred_r = seq_bit
                                          ? SequencedRitualCredit(pool, cands, sel, r)
                                          : (flat_sum - cands[r].ritual_float) + gy_r * (gy_r - 1) / 2;

                        ManaPool ewr = eff, encwr = eff_nc;
                        ewr.wild   = std::max(0, ewr.wild   - (full_credit - cred_r));
                        encwr.wild = std::max(0, encwr.wild - (full_credit - cred_r));

                        const ManaCost& rc = cands[r].cost;
                        ManaCost cwr = combined, ncwr = noncreature_combined;
                        cwr.white -= rc.white; cwr.blue -= rc.blue; cwr.black -= rc.black; cwr.red -= rc.red;
                        cwr.green -= rc.green; cwr.colorless -= rc.colorless; cwr.generic = std::max(0, cwr.generic - rc.generic);
                        if (cands[r].is_noncreature)
                        {
                            ncwr.white -= rc.white; ncwr.blue -= rc.blue; ncwr.black -= rc.black; ncwr.red -= rc.red;
                            ncwr.green -= rc.green; ncwr.colorless -= rc.colorless; ncwr.generic = std::max(0, ncwr.generic - rc.generic);
                        }

                        if (ewr.CanPay(cwr) && encwr.CanPay(ncwr)) { return; }   // r is surplus -> hold it
                    }
                }
            }
        }

        // Learned ranking for NON-lethal plans (lethal stays exact and dominates, so the model never
        // ranks a win). rank_value replaces total_eval in the compare + best.value; it IS total_eval
        // whenever no model is attached / the flag is off -> byte-identical. See learned-d0-policy.md.
        int rank_value = total_eval;
        if (ev && !wins)
        {
            const std::vector<std::string> cnames = PlanCastNames(cands, sel);
            MidGamePlanSummary psum = SummarizePlanByNames(cnames, PlanHasLand(cands, sel));
            // Anchor on the EXACT heuristic ranking key (total_eval) available right here -- this carries
            // Vial/X/ritual evals the name-only PlanBaselineEval can't reconstruct, so a unit-weight model
            // reproduces the heuristic exactly and learns a correction on top. See learned-d0-policy.md.
            psum.baseline_eval = total_eval;
            rank_value = LearnedPlanScore(state, psum, *ev);
        }

        bool better;
        if (best.wins_this_turn != wins)   { better = wins; }                    // winning dominates
        else if (rank_value != best.value) { better = rank_value > best.value; } // then higher (learned) value
        else                               { better = mask < best_mask; }        // tie -> smallest mask
        if (!better) { return; }

        best.actions.clear();
        for (int j : sel) { best.actions.push_back(j == fill_j ? fill_action : cands[j]); }
        ApplyCantripFirstOrder(best.actions);   // no-op unless MTG_CANTRIP_FIRST
        best.value          = rank_value;
        best.wins_this_turn = wins;
        best_mask           = mask;
    };

    std::vector<int> sel;   // reused across subset iterations (clear keeps capacity, avoids per-call alloc)

    // ---- Combo-line short-circuit (the breadth cut; see hinata-combo-heuristic-spec) ----------
    // On a ritual-funded combo turn (Hinata: Reality Spasm / Irencrag float mana for a big Crackle)
    // the hand is bloated with the cards a deep Soulfire/cantrip dig staged, so the powerset/odometer
    // below explodes. But the lethal line is structurally fixed: cast the rituals, then the X-damage
    // finisher at the largest affordable X. So before enumerating, evaluate JUST that line -- the
    // finisher (max-X variant) plus every available ritual (more rituals == more mana == strictly
    // more affordable, and the max_casts_after order RS->Irencrag->finisher is legal) -- via the same
    // consider() the powerset uses. consider() enforces ALL the feasibility it always does: total +
    // per-color mana (the {U}{U}-for-Reality-Spasm and {R}{R}-for-Crackle pruning is exactly its
    // CanPay + SubsetPayable), the Irencrag one-more-spell restriction, self-damage, and the EXACT
    // win projection (the finisher's 5X is direct_damage). We skip the powerset ONLY when that line
    // WINS -- a turn-winning plan dominates every other plan this turn, so we lose nothing; when it
    // does not win we fall through to the full enumeration (best is merely pre-seeded, like move
    // ordering), so a line the full search would find is never missed. Gated on any_ritual, so every
    // non-ritual deck is byte-identical; MTG_UNPRUNED also disables it, leaving the full search as the
    // standing A/B that proves the cut wins the same games. MTG_NO_COMBO_LINE is a dedicated isolation
    // toggle (disables ONLY this cut, keeping every other heuristic) for a clean perf A/B.
    static const bool s_no_combo_line = EnvOn("MTG_NO_COMBO_LINE");
    if (any_ritual && !s_no_combo_line && !DecisionUnpruned(UnprunedGate::ComboLine))
    {
        int finisher = -1, finisher_dmg = -1;
        std::vector<int> rituals;
        for (int j = 0; j < m; ++j)
        {
            const Action& c = cands[j];
            if (c.kind != Action::Kind::CastFromHand) { continue; }
            const CardDefinition* d = c.def;
            if (!d) { continue; }
            // The lethal payoff: an {X} direct-damage finisher (Crackle with Power, 5X) cast at the
            // largest X CollectActions emitted (it already credited the rituals' net mana into X).
            if (d->params.x_damage_multiplier > 1 && c.chosen_x > 0 && c.direct_damage > 0)
            {
                if (c.direct_damage > finisher_dmg) { finisher_dmg = c.direct_damage; finisher = j; }
            }
            else if (c.ritual_float > 0)   // a mana ritual (Reality Spasm / Irencrag Feat)
            {
                rituals.push_back(j);
            }
        }
        if (finisher >= 0)
        {
            std::vector<int> combo = rituals;   // all rituals -> max mana to fund the finisher's X
            combo.push_back(finisher);
            consider(combo);
            if (best.wins_this_turn) { return best; }   // lethal combo found -> skip the powerset
            // Not lethal/affordable: fall through; `best` is pre-seeded (harmless move-ordering).
        }
    }

    // ---- Dragonstorm storm go-off short-circuit (sibling of the Hinata combo-line cut) --------------
    // A storm go-off turn is structurally fixed: cast every ritual + sac every Lotus to build storm count
    // and mana, then Dragonstorm -- whose min(storm, #Dragons-left) puts and Scourge/Lathliss ETB pings are
    // this turn's lethal (ExtraLethalDamage). The full search ALREADY finds this win; it just pays for the
    // entire ritual/Lotus powerset first (the Apex/Lotus straggler -- the R=40 gen atom). So evaluate JUST
    // the maximal go-off line via the same consider() and, when it WINS, skip the powerset -- a turn-winning
    // plan dominates every plan this turn, so nothing is lost; when it does NOT win (e.g. the library is
    // nearly out of Dragons -> the storm puts too few) fall through to the full enumeration (`best` merely
    // pre-seeded, like move ordering). Correct-by-construction: we assert only that a lethal this-turn line
    // is optimal, not that the maximal line is. Inert for every non-Dragonstorm deck (has_extra_lethal +
    // no tutor payoff -> BuildStormGoffLine returns -1); rides the Hinata cut's MTG_NO_COMBO_LINE +
    // MTG_UNPRUNED(ComboLine) toggles plus its own MTG_NO_GOFF_SHORTCIRCUIT.
    // Only at the TOP-LEVEL main phase (nothing cast yet this turn), never inside a draw-breakpoint
    // re-solve. Inside Apex's mid-turn re-solve the go-off model's win projection AND an isolated
    // ApplyPlanDirect re-sim both disagree with how the breakpoint handler actually replays the line (a
    // staged-Dragonstorm quirk -- the "much harder" Apex-pile case), which turned a T8 win into a loss.
    // At spells_cast_this_turn==0 no staged cards can exist (staging needs a prior cast), so the
    // constructed line is always a coherent opening-main line and the re-sim is a faithful oracle.
    if (any_ritual && has_extra_lethal && state.spells_cast_this_turn == 0
        && !s_no_goff_shortcircuit && !s_no_combo_line
        && !DecisionUnpruned(UnprunedGate::ComboLine))
    {
        std::vector<int> goff;
        if (BuildStormGoffLine(cands, goff) >= 0)
        {
            // Purely additive + SOUND. consider() builds/pre-seeds `best` from the constructed line and
            // PROJECTS its win (ExtraLethalDamage). But that projection is optimistic for the Apex-staged
            // Dragonstorm case (storm/library/mana quirks in the mid-turn re-solve): it once claimed a lethal
            // that fizzled, turning a T8 win into a loss at greedy d0 (no root re-simulation to catch it). So
            // do NOT trust the projection -- when it flags a win, VERIFY by actually simulating the line
            // (ApplyPlanDirect on a copy) and only short-circuit when the opponent is truly dead. Snapshot
            // `best`/`best_mask` and restore on any non-verified outcome so the fall-through is byte-identical
            // to the full search. Cost: one plan application, trivially cheap versus the powerset it skips.
            const Plan saved_best = best;
            const int  saved_mask = best_mask;
            consider(goff);
            if (best.wins_this_turn)
            {
                GameState copy = state;
                ApplyPlanDirect(copy, best, is_pre_combat);
                if (copy.Opponent().life <= 0) { return best; }   // verified lethal -> skip the powerset
            }
            best      = saved_best;
            best_mask = saved_mask;
        }
    }

    // ---- Board-lethal short-circuit (sibling of the combo-line / go-off cuts) -----------------------
    // If the CURRENT board's attack-all damage already kills the opponent THIS turn (pending_atk >= opp
    // life), attacking with no casts wins now -- and a turn-winning plan dominates every other plan this
    // turn, so the whole cast-subset powerset is skippable. Evaluate the do-nothing (attack-only) subset
    // via the same consider() and, when it wins, return it. GT-fingerprint-invariant by construction:
    // winning this turn is the minimum possible win-turn, so which winning plan is chosen never changes
    // the win TURN the search/executor reports (only the pre-combat casts, which are invisible to GT and
    // moot once combat is lethal). pending_atk (PendingAttackDamage) already counts only legal
    // CanAttackFull && ShouldAttackWith attackers, so vs the passive opponent it is real, cast-independent
    // damage. The guard is a cheap pre-filter; consider() owns the authoritative win projection. Isolation
    // toggle MTG_NO_LETHAL_CUT; also disabled under MTG_UNPRUNED(ComboLine) alongside the sibling cuts.
    if (!s_no_lethal_cut && ResolveProvider(state).UseLethalShortCircuit()
        && !DecisionUnpruned(UnprunedGate::ComboLine)
        && pending_atk >= state.Opponent().life)
    {
        consider(std::vector<int>{});                   // the empty (attack-only) subset
        if (best.wins_this_turn) { return best; }       // board already lethal -> skip the powerset
        // consider()'s exact projection did not confirm the win (should not happen given the guard) ->
        // fall through; `best` is merely pre-seeded (harmless move-ordering).
    }

    // The default enumeration replaces the 2^m action powerset with the PRODUCT of per-hand-card
    // choices {skip, cast, deploy-via-Vial} (same-charge Vial deploys collapse to one
    // representative, bounded by an aggregate per-charge capacity) crossed with the 2^independent
    // powerset of non-hand actions (graveyard retrace). This visits exactly the powerset's feasible
    // subsets -- the same invariant EnumeratePlans relies on -- in O(prod(1+choices)*2^independent)
    // instead of O(2^m), which is the wide-board (slivers/knights) hot path. MTG_LEGACY_SOLVE keeps
    // the reference powerset for A/B (the two must produce byte-identical game results).
    static const bool s_legacy_solve = EnvOn("MTG_LEGACY_SOLVE");
    if (s_legacy_solve)
    {
        // Reference path: full 2^m powerset with precomputed mutual-exclusion conflict masks. Two
        // actions conflict if they use the same hand card (cast vs. its Vial deploy) or tap the
        // same Vial. conflict[j] is the bitmask of actions that cannot co-occur with j.
        std::vector<int> conflict(m, 0);
        for (int j = 0; j < m; ++j)
        {
            for (int k = j + 1; k < m; ++k)
            {
                bool conf =
                    (cands[j].hand_index >= 0 && cands[j].hand_index == cands[k].hand_index)
                    || (cands[j].kind == Action::Kind::ActivateVial
                        && cands[k].kind == Action::Kind::ActivateVial
                        && cands[j].vial_bf_index == cands[k].vial_bf_index);
                if (conf) { conflict[j] |= (1 << k); conflict[k] |= (1 << j); }
            }
        }
        for (int mask = 1; mask < (1 << m); ++mask)
        {
            bool valid = true;
            for (int j = 0; j < m; ++j)
            {
                if (!(mask & (1 << j))) { continue; }
                if (mask & conflict[j]) { valid = false; break; }
            }
            if (!valid) { continue; }
            sel.clear();
            for (int j = 0; j < m; ++j) { if (mask & (1 << j)) { sel.push_back(j); } }
            consider(sel);
        }
        return best;
    }

    // --- Default: odometer over per-hand-card choices x powerset of independent actions ---

    // Per-charge Vial capacity = number of distinct untapped Vials at each charge (derived from
    // the Vial actions' vial_bf_index, matching apply_vial which taps a fresh matching Vial per
    // deploy). Mirrors EnumeratePlans.
    std::vector<std::pair<int, int>> vial_capacity;   // (charge, count)
    auto capacity_for = [&](int charge) -> int
    {
        for (const std::pair<int, int>& vc : vial_capacity)
        {
            if (vc.first == charge) { return vc.second; }
        }
        return 0;
    };
    {
        std::vector<std::pair<int, int>> seen;   // (charge, vial_bf_index) already counted
        for (const Action& a : cands)
        {
            if (a.kind != Action::Kind::ActivateVial) { continue; }
            std::pair<int, int> key{ a.card_mv, a.vial_bf_index };
            bool already = false;
            for (const std::pair<int, int>& s : seen) { if (s == key) { already = true; break; } }
            if (already) { continue; }
            seen.push_back(key);
            bool found = false;
            for (std::pair<int, int>& vc : vial_capacity)
            {
                if (vc.first == a.card_mv) { ++vc.second; found = true; break; }
            }
            if (!found) { vial_capacity.push_back({ a.card_mv, 1 }); }
        }
    }

    // Group action indices: one mutually-exclusive option list per hand card (its cast + a single
    // representative Vial deploy), plus a flat list of independent non-hand actions (retrace).
    std::vector<std::vector<int>> groups;            // per hand card: option cand indices
    std::vector<int>              group_hand_index;  // parallel: the card's hand_index
    std::vector<int>              independent;
    // Folded into this existing pass rather than a dedicated ManaGateWouldHelp scan: this loop already
    // visits every candidate, and a separate pass cost +0.03..+0.15% on the decks that never build the
    // gate (burn/TH/Knights/Anti-Lifegain hold no ritual, rock or reducer at all, so the scan always
    // answered "no"). Set BEFORE the independent-action continue so a Lotus SacForMana still counts.
    bool gate_relevant = false;
    for (int j = 0; j < m; ++j)
    {
        if (!gate_relevant
            && (cands[j].ritual_float > 0 || cands[j].rock_mana.Total() > 0
                || (cands[j].def && (cands[j].def->params.affinity_for_subtype
                                     || !cands[j].def->params.reduces_spell_color.empty()))))
        { gate_relevant = true; }
        if (cands[j].hand_index < 0) { independent.push_back(j); continue; }

        int gi = -1;
        for (int g = 0; g < static_cast<int>(groups.size()); ++g)
        {
            if (group_hand_index[g] == cands[j].hand_index) { gi = g; break; }
        }
        if (gi < 0)
        {
            groups.push_back({});
            group_hand_index.push_back(cands[j].hand_index);
            gi = static_cast<int>(groups.size()) - 1;
        }
        // Collapse all same-charge Vial deploys of one card to a single representative.
        if (cands[j].kind == Action::Kind::ActivateVial)
        {
            bool has_vial = false;
            for (int existing : groups[gi])
            {
                if (cands[existing].kind == Action::Kind::ActivateVial) { has_vial = true; break; }
            }
            if (has_vial) { continue; }
        }
        groups[gi].push_back(j);
    }

    // Breadth cap on a bloated combo-dig hand (the lethal combo line is already returned by the
    // short-circuit above, so a non-lethal turn -- the only kind that reaches here -- never drops a
    // win). Shared with EnumeratePlans; see CapGroupsBySituationalRank.
    CapGroupsBySituationalRank(state, cands, groups, group_hand_index);

    // Feasible-aware early ritual-drop: when no payoff is reachable this turn, remove the ritual groups
    // before the odometer enumerates their powerset (byte-identical; see DropRitualGroupsIfNoPayoff).
    DropRitualGroupsIfNoPayoff(state, pool, cands, groups, group_hand_index, independent);

    int num_groups = static_cast<int>(groups.size());
    int num_ind    = static_cast<int>(independent.size());

    // Reject combinations whose Vial deploys exceed the per-charge capacity.
    auto vial_ok = [&](const std::vector<int>& s) -> bool
    {
        for (int j : s)
        {
            if (cands[j].kind != Action::Kind::ActivateVial) { continue; }
            int charge = cands[j].card_mv;
            int used   = 0;
            for (int k : s)
            {
                if (cands[k].kind == Action::Kind::ActivateVial && cands[k].card_mv == charge) { ++used; }
            }
            if (used > capacity_for(charge)) { return false; }
        }
        return true;
    };

    const int mana_bound = ManaPruneBound(pool, cands);   // legacy scalar bound (MTG_LEGACY_MANA_BOUND)
    // Both indices are allocated ONLY when the deck actually uses them. Solve is the rollout leaf --
    // called once per node -- so even default-constructing their (empty) vectors on every call cost a
    // measurable ~1% on decks that use neither (Hinata/TH/burn A/B, 2026-07-29). A null pointer plus a
    // branch is the price now; the allocation happens on the rare call that needs it.
    std::unique_ptr<ManaGateIndex> gate_owned;            // selection-exact bound; see BuildManaGateIndex
    if (SelectionExactManaGateEnabled() && gate_relevant)
    {
        auto idx = std::make_unique<ManaGateIndex>();
        if (BuildManaGateIndex(pool, cands, independent, *idx)) { gate_owned = std::move(idx); }
    }
    const ManaGateIndex* gate    = gate_owned.get();      // null -> legacy scalar path
    const bool           gate_on = (gate != nullptr);
    // Precompute the accelerant-prefix order ONCE (choice-independent); the per-choice check is then a cheap
    // walk. Empty when the collapse is off / < 2 accelerant groups. See BuildAccelPrefixOrder.
    std::vector<int> accel_order;
    if (accel_prefix_on && any_accel) { BuildAccelPrefixOrder(cands, groups, group_hand_index, accel_order); }
    // Choice-independent inputs to the two splice predicates (name ids, copy positions, hand counts),
    // plus the lowest odometer digit that can change any predicate. See BuildSpliceOdometerIndex.
    std::unique_ptr<SpliceOdometerIndex> sidx_owned;
    const SpliceOdometerIndex* sidx = &g_no_splice_index;
    if (any_splice)
    {
        sidx_owned = std::make_unique<SpliceOdometerIndex>();
        BuildSpliceOdometerIndex(state, cands, groups, *sidx_owned);
        sidx = sidx_owned.get();
    }
    const int min_pred_digit = MinPredicateDigit(groups, *sidx, accel_order);
    if (enumstats::Enabled())
    {
        std::uint64_t pos = 1ull << num_ind;
        for (int g = 0; g < num_groups; ++g) { pos *= (std::uint64_t)groups[g].size() + 1; }
        MeasureManaSideCollapse(cands, groups, independent, pos, pool.Total(),
            [&](const std::vector<int>& c) {
                return (any_splice && SpliceGroupChoiceRejected(*sidx, groups, c, splice_collapse_on))
                    || ((accel_prefix_on && any_accel) && NonPrefixAccelViolated(accel_order, c));
            });
    }
    if (!(any_ritual || any_rock))
    {
        // No mana side => no splice base, no accelerant, no Lotus sac => every group predicate is
        // inert, so this walk needs none of the predicate bookkeeping. Kept INLINE here rather than
        // behind the two-stage driver's function boundary: measured, routing it through the driver
        // cost ~1.7-2.7%% on the decks that gain nothing from the split (burn/Knights, callgrind
        // 2026-07-29). Mirrored in Solve and EnumeratePlans -- change one, change both.
        std::vector<int> choice(num_groups, 0);
        std::vector<int> sel;   // reused across positions (clear keeps capacity)
        bool done = false;
        while (!done)
        {
            int mcost = 0, mgain = 0, mgy = 0, mblock = 0;
            if (gate_on)
            {
                for (int g = 0; g < num_groups; ++g)
                {
                    if (choice[g] <= 0) { continue; }
                    const ManaGateTerm& t = gate->term[groups[g][choice[g] - 1]];
                    mcost += t.cost; mgain += t.gain; mgy += t.gy; mblock += t.block;
                }
            }
            else
            {
                for (int g = 0; g < num_groups; ++g)
                { if (choice[g] > 0) { mcost += cands[groups[g][choice[g] - 1]].cost.ManaValue(); } }
            }
            // Group-level early-out FIRST: if this selection is unpayable even with every independent
            // action's float credited, no imask extension of it can be paid, so skip the whole inner
            // loop rather than building `sel` for each of 2^num_ind doomed positions. (Dropping this
            // cost ~2.4pp on burn/Knights, callgrind 2026-07-29.)
            const bool outer_ok = gate_on
                ? (mblock > 0 || gate->ind_block
                   || mcost <= gate->pool_total + mgain + gate->ind_gain_all
                               + ManaGateTriangular(mgy + gate->ind_gy_all))
                : (mcost <= mana_bound);
            for (int imask = 0; outer_ok && imask < (1 << num_ind); ++imask)
            {
                int pcost = 0, pgain = 0, pgy = 0, pblock = 0;
                sel.clear();
                for (int g = 0; g < num_groups; ++g)
                { if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); } }
                if (gate_on)
                {
                    for (int b = 0; b < num_ind; ++b)
                    {
                        if (!(imask & (1 << b))) { continue; }
                        sel.push_back(independent[b]);
                        const ManaGateTerm& t = gate->term[independent[b]];
                        pcost += t.cost; pgain += t.gain; pgy += t.gy; pblock += t.block;
                    }
                }
                else
                {
                    for (int b = 0; b < num_ind; ++b)
                    {
                        if (!(imask & (1 << b))) { continue; }
                        sel.push_back(independent[b]);
                        pcost += cands[independent[b]].cost.ManaValue();
                    }
                }
                if (sel.empty()) { continue; }
                const bool ok = gate_on
                    ? (mblock + pblock > 0
                       || mcost + pcost <= gate->pool_total + mgain + pgain
                                           + ManaGateTriangular(mgy + pgy))
                    : (mcost + pcost <= mana_bound);
                if (!ok) { continue; }
                if (vial_ok(sel)) { consider(sel); }
            }
            int g = 0;
            for (; g < num_groups; ++g)
            {
                ++choice[g];
                if (choice[g] <= static_cast<int>(groups[g].size())) { break; }
                choice[g] = 0;
            }
            if (g == num_groups) { done = true; }
        }
    }
    else
    {
        EnumeratePlanPositions(cands, groups, independent, gate, mana_bound, *sidx, accel_order,
                               any_splice, splice_collapse_on, accel_prefix_on && any_accel,
                               has_ind_accel, vial_ok, consider);
    }

    return best;
}

// ============================================================
// Multi-turn lookahead
// ============================================================

// The MTG_LEGACY_CCO_PAY diagnosis hatch (rollout-only, re-enables the pre-fix coloured-pip
// payment off colored_creature_only lands) moved into the unified TapForCostSharedOnce
// (ManaPayment.cpp, `honor_legacy_cco`). See docs/design/post-breakpoint-search.md.

// Public payment entry. Delegates to the UNIFIED TapForCostShared (ManaPayment.cpp) -- formerly a
// twin of AIEngine::TapForCost kept in lockstep by comment discipline. The rollout keeps no
// accounting pool and carries the MTG_LEGACY_CCO_PAY measurement hatch.
bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature)
{
    return TapForCostShared(state, cost_in, for_creature, nullptr, /*honor_legacy_cco=*/true);
}

// Apply a plan to the game state sequentially (bypassing the stack).
// Mana is tapped in-place as each spell is cast rather than accumulated and
// tapped at the end, so the remaining pool is always correct at each step.
// Draw spells act as breakpoints: after drawing, Solve re-runs on the updated
// state so newly revealed cards can be cast with remaining mana this turn.
// Land-play helpers (defined below, near the land enumeration). PlayLandByName plays
// a specific named land; SimulateLandPlay is the greedy fallback used when a plan did
// not search the land (depth-0 static plans).
static bool PlayLandByName(GameState& state, const std::string& name,
                           const std::string& fetch_target = "", bool allow_shock_pay = true,
                           const std::string& land_face = "");
static std::string SimulateLandPlay(GameState& state);

// MTG_FORCE_LAND="<turn>:<land name>[,<turn>:<name>...]" (inert by default): the land to force as
// that turn's drop. Diagnostic only -- there is otherwise no way to ask "what would the engine do
// from THIS land drop?", which is needed whenever a reference's human line diverges at a land
// choice (the goldfish runner has no land override; --force-mulligan only fixes the opening hand).
// Returns "" when unset or when this turn is not listed.
static std::string ForcedLandForTurn(int turn)
{
    static const std::string spec = []{ const char* e = std::getenv("MTG_FORCE_LAND");
                                        return std::string(e ? e : ""); }();
    if (spec.empty()) { return {}; }
    std::stringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) { continue; }
        if (std::atoi(tok.substr(0, colon).c_str()) == turn) { return tok.substr(colon + 1); }
    }
    return {};
}

// MTG_BP_PROBE=1 -- DIAGNOSTIC: count the post-breakpoint GREEDY re-solves (TurnSolver::Solve, the
// "d0 decision + every rollout leaf" path) per call site. Every hit is a stretch of a turn decided
// with NO search, inside an otherwise-searched game. Counts rollout and real execution alike (both
// run ApplyPlanDirect), which is the point: it measures how much of the tree is greedy.
// See docs/design/post-breakpoint-search.md.
namespace
{
    const char* const kBpSiteName[5] = {
        "stages_cards/EI  (Light Up the Stage, Expressive Iteration)",
        "DrawUntilNonland (Treasure Hunt)",
        "impulse_exile    (Apex of Power)",
        "deferred_cantrip (Ponder / Preordain)",
        "dig_through_lands(sac/cycle dig)",
    };
    struct BpProbe
    {
        std::atomic<uint64_t> hit[5]{};       // all invocations (incl. rollout evaluation)
        std::atomic<uint64_t> committed[5]{}; // subset on a COMMITTED line -> a real game decision
        std::atomic<uint64_t> searched[5]{};  // subset resolved by SEARCH (Plan::bp_choice)
        std::atomic<uint64_t> nested[5]{};    // NESTED (2nd+ breakpoint of an apply) -> still greedy
        ~BpProbe()
        {
            if (!EnvOn("MTG_BP_PROBE")) { return; }
            for (int i = 0; i < 5; ++i)
            {
                const uint64_t n = hit[i].load(std::memory_order_relaxed);
                const uint64_t c = committed[i].load(std::memory_order_relaxed);
                const uint64_t q = searched[i].load(std::memory_order_relaxed);
                const uint64_t z = nested[i].load(std::memory_order_relaxed);
                if (n) { std::fprintf(stderr,
                                      "[bp-probe] %-58s total=%-10llu greedy=%-10llu searched=%-10llu"
                                      " nested-unsearchable=%-10llu (%.1f%% searched, committed-line: %llu)\n",
                                      kBpSiteName[i], static_cast<unsigned long long>(n),
                                      static_cast<unsigned long long>(n - q),
                                      static_cast<unsigned long long>(q),
                                      static_cast<unsigned long long>(z),
                                      n ? (100.0 * static_cast<double>(q) / static_cast<double>(n)) : 0.0,
                                      static_cast<unsigned long long>(c)); }
            }
        }
    };
    BpProbe g_bp_probe;
    // `searched` distinguishes a breakpoint the search decided (Plan::bp_choice resolved it) from
    // one that still fell through to greedy TurnSolver::Solve. The purge is complete for a site when
    // greedy reaches 0 -- total alone cannot show that, and rises simply because more plans are
    // applied. See docs/design/post-breakpoint-search.md.
    inline void BpHit(int i, bool on_committed_line, bool resolved_by_search, bool nested_blocked)
    {
        static const bool on = EnvOn("MTG_BP_PROBE");
        if (!on) { return; }
        g_bp_probe.hit[i].fetch_add(1, std::memory_order_relaxed);
        if (on_committed_line)   { g_bp_probe.committed[i].fetch_add(1, std::memory_order_relaxed); }
        if (resolved_by_search)  { g_bp_probe.searched[i].fetch_add(1, std::memory_order_relaxed); }
        if (nested_blocked)      { g_bp_probe.nested[i].fetch_add(1, std::memory_order_relaxed); }
    }

    // MTG_BP_CANDS_PROBE=1: the continuation-count distribution at every SEARCHED breakpoint (see
    // g_bp_cands_last). This is the measurement that sizes the deferred-wave loop: `unreachable` is
    // the number of continuations that bp_choice can never index at the current width W, i.e. the
    // quality the width cap is costing. Compare `reach` (what W buys) against `unreachable` (what it
    // hides) per site. Buckets are the list LENGTH; `max` is the longest list seen.
    constexpr int kBpCandsBuckets = 9;   // 1, 2, 3, 4, 5-8, 9-16, 17-32, 33-64, 65+
    const char* const kBpCandsBucketName[kBpCandsBuckets] = {
        "1", "2", "3", "4", "5-8", "9-16", "17-32", "33-64", "65+"
    };
    inline int BpCandsBucket(int n)
    {
        if (n <= 4) { return n - 1; }
        if (n <= 8) { return 4; }
        if (n <= 16) { return 5; }
        if (n <= 32) { return 6; }
        if (n <= 64) { return 7; }
        return 8;
    }
    struct BpCandsProbe
    {
        std::atomic<uint64_t> hist[5][kBpCandsBuckets]{};
        std::atomic<uint64_t> n[5]{};            // searched breakpoints seen
        std::atomic<uint64_t> total[5]{};        // Σ cands.size()
        std::atomic<uint64_t> reach[5]{};        // Σ min(cands.size(), W)   -- indexable today
        std::atomic<uint64_t> unreachable[5]{};  // Σ max(cands.size()-W, 0) -- rank-gated OUT
        std::atomic<uint64_t> capped[5]{};       // breakpoints with cands.size() > W
        std::atomic<int>      maxlen[5]{};
        ~BpCandsProbe()
        {
            if (!EnvOn("MTG_BP_CANDS_PROBE")) { return; }
            for (int i = 0; i < 5; ++i)
            {
                const uint64_t cnt = n[i].load(std::memory_order_relaxed);
                if (cnt == 0) { continue; }
                const uint64_t tot = total[i].load(std::memory_order_relaxed);
                const uint64_t rch = reach[i].load(std::memory_order_relaxed);
                const uint64_t unr = unreachable[i].load(std::memory_order_relaxed);
                const uint64_t cap = capped[i].load(std::memory_order_relaxed);
                std::fprintf(stderr,
                             "[bp-cands] %-58s n=%llu mean=%.2f max=%d capped=%llu (%.1f%%)"
                             " reach=%llu unreachable=%llu (%.1f%% of all continuations)\n",
                             kBpSiteName[i], static_cast<unsigned long long>(cnt),
                             static_cast<double>(tot) / static_cast<double>(cnt),
                             maxlen[i].load(std::memory_order_relaxed),
                             static_cast<unsigned long long>(cap),
                             100.0 * static_cast<double>(cap) / static_cast<double>(cnt),
                             static_cast<unsigned long long>(rch),
                             static_cast<unsigned long long>(unr),
                             tot ? (100.0 * static_cast<double>(unr) / static_cast<double>(tot)) : 0.0);
                std::fprintf(stderr, "[bp-cands]   len:");
                for (int b = 0; b < kBpCandsBuckets; ++b)
                {
                    const uint64_t h = hist[i][b].load(std::memory_order_relaxed);
                    if (h) { std::fprintf(stderr, " %s=%llu", kBpCandsBucketName[b],
                                          static_cast<unsigned long long>(h)); }
                }
                std::fprintf(stderr, "\n");
            }
        }
    };
    BpCandsProbe g_bp_cands_probe;
    inline void BpCands(int site, int len, int width)
    {
        static const bool on = EnvOn("MTG_BP_CANDS_PROBE");
        if (!on || len <= 0) { return; }
        const int reachable = len < width ? len : width;
        g_bp_cands_probe.n[site].fetch_add(1, std::memory_order_relaxed);
        g_bp_cands_probe.total[site].fetch_add(static_cast<uint64_t>(len), std::memory_order_relaxed);
        g_bp_cands_probe.reach[site].fetch_add(static_cast<uint64_t>(reachable),
                                               std::memory_order_relaxed);
        g_bp_cands_probe.unreachable[site].fetch_add(static_cast<uint64_t>(len - reachable),
                                                     std::memory_order_relaxed);
        if (len > width) { g_bp_cands_probe.capped[site].fetch_add(1, std::memory_order_relaxed); }
        g_bp_cands_probe.hist[site][BpCandsBucket(len)].fetch_add(1, std::memory_order_relaxed);
        std::atomic<int>& m = g_bp_cands_probe.maxlen[site];
        int prev = m.load(std::memory_order_relaxed);
        while (len > prev && !m.compare_exchange_weak(prev, len, std::memory_order_relaxed)) {}
    }
}

bool TurnSolver::BatchPrepayMainCasts(GameState& state, const std::vector<Action>& acts)
{
    static const bool s_enabled = !EnvOn("MTG_NO_BATCH_PAY");
    if (!s_enabled) { return false; }
    // DRAW-SAFE: decline the whole-turn prepay on a FLOOD-ENGINE turn. The prepay assumes `acts` IS
    // the turn's cast set. That is FALSE after a dig: Treasure Hunt DRAWS the cards cast later the
    // same turn at a post-draw breakpoint (recorded in Action::breakpoint_casts). Prepaying only the
    // KNOWN casts lets the joint solve spend scarce COLOURED sources on generic pips the later cast
    // needs, so the recorded breakpoint cast is declared and then unpayable -- the committed line's
    // proven win is never realised. Repro (th_regression_d3_s3003 gi123): the search commits a T5
    // line whose turn-5 breakpoint records Land's Edge; the prepay strands its {1}{R}{R} and the game
    // is LOST outright -- "[fd-diverge] realized_win=9 predicted_win=5". MTG_NO_BATCH_PAY=1 also
    // fixes it, confirming batch-pay as the cause. Adopted default ON alongside Hooks 22/23, which
    // route MORE turns into this shape (double-dig turns); MTG_NO_BATCH_PAY_DRAWSAFE restores the
    // unconditional prepay for A/Bs.
    //
    // SCOPED to the flood-engine class (DrawUntilNonland / DigDraw), NOT the whole OrderingOpaque
    // predicate: that includes `draw > 0` and so caught plain cantrips, disabling batch-pay on most
    // Hinata turns -- measured smoke +0.019/+0.013/+0.027 at d0/d3/d5, net worse and rejected. The
    // stranding needs a cast that draws MANY cards and thereby introduces a NEW castable this turn.
    //
    // The SOURCE-level fix, if this is ever revisited: fold the recorded breakpoint_casts into the
    // combined cost below, so the joint solve pays for them instead of declining the prepay.
    static const bool s_drawsafe = !EnvOn("MTG_NO_BATCH_PAY_DRAWSAFE");
    if (s_drawsafe)
    {
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::DigDraw) { return false; }
            if (a.kind != Action::Kind::CastFromHand && a.kind != Action::Kind::CastFromGraveyard)
            { continue; }
            const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
            if (d && d->tmpl == CardTemplate::DrawUntilNonland) { return false; }
        }
    }
    // A non-empty float (e.g. a ritual's output) would be clobbered by the pre-load; producer turns
    // are declined below regardless, but guard here too so the reserve stays byte-identical there.
    if (state.floating_mana.Total() != 0) { return false; }

    const int active = state.active_player_index;
    ManaCost combined;
    int eligible = 0;
    // Whether EVERY eligible cast is a creature. A colored_creature_only land (Sliver Hive / Secluded
    // Courtyard) makes its coloured mana only for creature spells, so the combined solve may only treat
    // it as coloured when the whole batch is creatures; a mixed batch stays conservative (false), where
    // that land contributes {C} only. Fixes an all-creature batch reading as unaffordable off these
    // lands -> casting fewer creatures. See docs/design/slivers-restricted-mana-tap-order-bug.md.
    bool all_creatures = true;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand || a.sacrifice_land || a.alt_cost) { continue; }
        if (a.ritual_float > 0 || a.rock_mana.Total() > 0) { return false; } // producer breaks fungibility
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (!d) { return false; }
        // A fixed upfront combined is only valid when no cast's cost can shrink/grow dynamically:
        // {X} spells and Hinata/Soulfire per-target discounts change the cost as the line resolves.
        if (d->card.m_mana_cost.has_x && a.chosen_x > 0) { return false; }
        if (SoulfireOwnTargetDiscount(*d, state, active, a.soulfire_own_targets) > 0) { return false; }
        if (HinataGenericDiscount(*d, state, a.chosen_x) > 0) { return false; }
        ManaCost ec = EffectiveCost(*d, state);
        combined.generic += ec.generic; combined.white += ec.white; combined.blue += ec.blue;
        combined.black += ec.black; combined.red += ec.red; combined.green += ec.green;
        combined.colorless += ec.colorless;
        if (!d->card.IsCreature()) { all_creatures = false; }
        ++eligible;
    }
    // A single cast is already optimal via the per-cast complete-solver fallback; the inter-cast
    // stranding needs >=2 casts sharing the pool. <2 -> decline (single-cast turns byte-identical).
    if (eligible < 2 || combined.ManaValue() == 0) { return false; }

    // Snapshot exactly what a tap can touch so a declined solve (wild output) rolls back cleanly.
    const std::vector<Permanent> bf_snap = state.battlefield;
    const ManaPool               fm_snap = state.floating_mana;
    const int  la  = state.players[active].life;
    const int  lo  = state.players[1 - active].life;
    const bool oll = state.opponent_lost_life_this_turn;

    // Whole-turn depletion reservation ("leave out if you can"): a depletion land's counter is spent
    // the instant it taps, so try to HOLD every untapped depletion land whenever the turn's COMBINED
    // cost can still be paid without them. Sound because judged against the whole turn -- a held land
    // is simply left untapped, never stranded (a later post-draw re-solve can still tap it). Rollout
    // (ApplyPlanDirect) and executor (AIEngine::TakeTurn) reach this through the same function, so the
    // held set stays in lockstep. Cheap prescan: no untapped depletion land -> reserved=0 (every
    // non-depletion deck skips the held attempt entirely -> byte-identical + no extra solve).
    std::uint64_t reserved = 0;
    const int n = static_cast<int>(state.battlefield.size());
    if (DepletionReserveEnabled() && n <= 64)
    {
        for (int i = 0; i < n; ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.enters_tapped_with_depletion > 0) { reserved |= (1ull << i); }
        }
    }
    // "Hold your beater": also reserve the controller's greatest-power attacker WHEN it's a mana
    // source (dork/manland) -- otherwise it might tap for mana instead of swinging (and it's the
    // creature an own-creature pump lands on, so reserving it makes the pump target the one left up).
    // A non-mana beater is never in the tap set, so restrict to mana sources (else the all-or-nothing
    // hold below carries an inert bit). Same leave-out-if-you-can fallback as depletion.
    if (AttackerReserveEnabled() && n <= 64)
    {
        const int best = FindBestOwnAttacker(state, active);
        if (best >= 0 && best < 64)
        {
            const Permanent& bp = state.battlefield[best];
            const CardDefinition* bd = CardDatabase::Instance().LookupCached(bp.card);
            const bool mana_src = bd && !bp.tapped
                && ((bd->tmpl == CardTemplate::ManaDork && bp.CanTap()) || bd->params.mana_rock);
            if (mana_src) { reserved |= (1ull << best); }
        }
    }

    // Solve the combined cost. First try with the depletion lands HELD: if it pays wild-free their
    // counters are preserved for free. If holding them makes the turn unaffordable or forces a
    // wild/ambiguous tap, restore and solve WITHOUT the hold -- they are genuinely needed this turn.
    // (All-or-nothing rather than a per-source maximal subset: cheaper -- 1 solve in the common
    // slack case, <=2 otherwise -- and empirically indistinguishable on the depletion decks.)
    ManaPool produced;
    bool ok = false;
    if (reserved)
    {
        ok = TapForCostBacktrack(state, combined, /*for_creature=*/all_creatures, ManaPool{},
                                 /*rp_colors=*/nullptr, /*fail_memo=*/nullptr, /*out_leftover=*/nullptr,
                                 /*tapped_mask=*/0, /*untapped_max=*/-1, /*reserved_mask=*/reserved,
                                 /*out_full_pool=*/&produced)
             && produced.wild == 0;
        if (!ok)   // held attempt infeasible/ambiguous -> the lands are needed; restore for the plain solve
        {
            state.battlefield                  = bf_snap;
            state.floating_mana                = fm_snap;
            state.players[active].life         = la;
            state.players[1 - active].life     = lo;
            state.opponent_lost_life_this_turn = oll;
            produced = ManaPool{};
        }
    }
    if (!ok)   // no depletion land to hold, or the held attempt failed: the original unrestricted solve
    {
        ok = TapForCostBacktrack(state, combined, /*for_creature=*/all_creatures, ManaPool{},
                                 /*rp_colors=*/nullptr, /*fail_memo=*/nullptr, /*out_leftover=*/nullptr,
                                 /*tapped_mask=*/0, /*untapped_max=*/-1, /*reserved_mask=*/0,
                                 /*out_full_pool=*/&produced);
    }
    // Decline when the full batch is unaffordable (fall back to greedy, which casts what it can) or
    // when the tap set makes "any colour" (wild) mana -- pinning colours to pips is then ambiguous.
    if (!ok || produced.wild > 0)
    {
        state.battlefield                  = bf_snap;
        state.floating_mana                = fm_snap;
        state.players[active].life         = la;
        state.players[1 - active].life     = lo;
        state.opponent_lost_life_this_turn = oll;
        return false;
    }

    // Pre-load floating as the combined cost: COLOURED/{C} pips pinned to their colours (produced
    // covers them since it has no wild), the generic requirement + any over-production carried as
    // `wild`. Total == produced.Total(), so no mana is lost even if the solve over-tapped. Each main
    // cast drains this (generic pips take wild first -- see SpendFloatingTowardCost), so a colour is
    // never spent on a generic pip a later cast needed.
    ManaPool pool;
    pool.white = combined.white; pool.blue = combined.blue; pool.black = combined.black;
    pool.red   = combined.red;   pool.green = combined.green; pool.colorless = combined.colorless;
    const int pinned = combined.white + combined.blue + combined.black
                     + combined.red + combined.green + combined.colorless;
    pool.wild = produced.Total() - pinned;
    state.floating_mana = pool;
    return true;
}

static void ApplyPlanDirect(GameState& state, const TurnSolver::Plan& plan, bool is_pre_combat,
                            std::vector<Action>* out_breakpoint)   // default arg on the forward decl
{
    PROF_INC(applyplan_calls);
    Player& ap  = state.ActivePlayer();
    int opp_idx = 1 - state.active_player_index;

    // Pin this plan variant's searched ETB-dig pick (Plan::etbdig_choice) for the whole apply; the
    // first dig consumes it and any later dig falls back to the provider's ranked default. Scoped,
    // so a nested breakpoint re-solve restores the outer pin if it has not fired yet. -1 is inert.
    ScriptedEtbDig _sed(plan.etbdig_choice);
    ScriptedReorder _sr(plan.ponder_choice);   // searched Ponder disposition (own pin; see ScriptedReorder)
    ScriptedTutor _stut(plan.tutor_choice);    // searched tutor pick by index, resolved at the true
                                               // mid-plan state (MTG_TUTOR_AXIS_RESOLVE); -1 inert

    // Searched Goblin Lackey put: copied onto the STATE (not a scoped guard) because the trigger
    // fires later, in this turn's combat-damage step. Only a real variant writes it, so a plan that
    // did not branch on the axis leaves any outer value alone.
    if (plan.lackey_choice >= 0) { state.scripted_cheat_choice = plan.lackey_choice; }
    // Searched cleanup discard: same reasoning -- the shed happens in SimulateEndAndStartNextTurn,
    // after this function returns, so it rides the STATE rather than a scoped guard.
    if (plan.discard_choice >= 0) { state.scripted_discard_choice = plan.discard_choice; }

    // Commit-the-line recording (out_breakpoint != null, set only when building the
    // committed line): capture the casts each draw-breakpoint re-solve makes so the
    // AIEngine replay can reproduce them verbatim. sink_stack.back() is the container
    // for casts of the breakpoint currently being applied; an empty stack means we are
    // at the main-plan level (those casts are NOT recorded -- the committed plan.actions
    // already holds them -- but a main draw engine's OWN re-solve records into the
    // top-level out_breakpoint). See Action::breakpoint_casts.
    std::vector<std::vector<Action>*> sink_stack;

    // Deferred plain-cantrip (Ponder/Preordain) re-solve. A plain DrawSpell cast at the
    // MAIN-plan level used to re-solve INLINE — casting freshly-affordable spells right
    // after its draw, BEFORE the main plan's remaining casts. The executor instead casts
    // every main-plan spell first and replays the breakpoint only afterwards (its
    // is_draw_engine excludes plain cantrips), so the inline re-solve could spend mana a
    // later main cast still needed (Ponder+Ponder+Preordain off two blue sources) and
    // diverge from the realised game. We defer the re-solve to AFTER all main casts so it
    // uses only leftover mana — byte-for-byte the executor's post-loop replay. EI / staging
    // / Treasure Hunt / cascade (the executor's real draw engines) keep their inline
    // re-solve, and a plain cantrip cast INSIDE a re-solve (sink_stack non-empty) also stays
    // inline so its nested breakpoint records correctly. MTG_NO_DEFER_CANTRIP opts out
    // (the old inline behaviour) for the A/B. Inert for decks without plain cantrips.
    static const bool s_defer_cantrip = !EnvOn("MTG_NO_DEFER_CANTRIP");
    bool deferred_cantrip_resolve = false;

    // Karoo bounce-land play-at-end timing. A Karoo (Izzet Boilerworks: etb_bounce_land,
    // enters tapped) returns one of our lands to hand on ETB. Played land-FIRST it bounces a
    // still-UNTAPPED land we then need, losing that land's mana this turn. A Karoo enters
    // tapped, so it provides no mana this turn regardless -- we therefore DEFER its play until
    // AFTER the main casts (below, before the deferred-cantrip re-solve). By then the lands we
    // needed are tapped, and BounceKarooLand returns a spent land for zero tempo loss. The land
    // drop is RESERVED for the Karoo: while deferred, a draw/cantrip breakpoint must not play a
    // revealed land as the drop (guarded in play_breakpoint_land / play_drawn_flood_keep_land).
    // Lockstep: AIEngine::TakeTurn defers its fold_land the same way. MTG_NO_KAROO_DEFER opts
    // out (old land-first behaviour) for the A/B. Inert for decks without a Karoo.
    static const bool s_karoo_defer = !EnvOn("MTG_NO_KAROO_DEFER");
    bool        karoo_deferred = false;
    std::string karoo_land_name;
    std::string karoo_fetch;

    // Human-play mode (tools/play GUI): execute EXACTLY the committed plan -- suppress every
    // auto-heuristic that would play cards the human didn't choose (draw-breakpoint re-solve,
    // auto-dig, auto Land's Edge). After a draw the AIEngine chooser re-fires so the human
    // re-decides with the revealed cards. Set ONLY under --claude-play, so normal search /
    // goldfish runs are byte-identical (the flag is never set there). HumanPlayActive() reads the
    // env in a function-local static (AFTER main's setenv) and returns false inside the engine's
    // clairvoyant rollouts, so bottoming/keep playouts sequence plays like the autonomous game.
    const bool s_human_play = HumanPlayActive();

    // Land drop first, so the land's mana is available to the spells that follow.
    // A searched plan (land_decided) plays exactly its chosen land ("" == a deliberate
    // defer); an unsearched plan (depth-0 static Solve) falls back to greedy land play.
    // Human play (claude-play) ALSO plays the drop in the POST-combat main: EnumeratePlansWithLand
    // offers a still-open drop there (a land revealed by Light Up the Stage, played as the turn's
    // drop), so ApplyPlanDirect must execute the chosen land -- otherwise it AND any cast that needs
    // its mana are silently dropped (the plan is enumerated but never realized). Gated on
    // s_human_play, and post-combat follows ONLY the land_decided branch (never the greedy
    // SimulateLandPlay fallback, which would play a land the human didn't choose). The autonomous
    // search's post-combat plans carry no land (drop_available is pre-combat-only there), so the
    // block is a no-op for the search -> byte-identical.
    if (is_pre_combat || s_human_play)
    {
        if (plan.land_decided)
        {
            if (!plan.land_to_play.empty())
            {
                const CardDefinition* ld = CardDatabase::Instance().Lookup(plan.land_to_play);
                if (s_karoo_defer && ld && ld->params.etb_bounce_land)
                {
                    // Reserve the drop; play it after the main casts (see karoo_deferred above).
                    karoo_deferred  = true;
                    karoo_land_name = plan.land_to_play;
                    karoo_fetch     = plan.fetch_target;
                }
                else
                {
                    // Shock land (Steam Vents): the autonomous engine always pays the 2 life to
                    // enter untapped (early speed). Human play pays ONLY when this turn's plan
                    // actually spends mana (a cast/retrace/dig) -- otherwise the land would ping
                    // you 2 life for mana you never tap, so enter it tapped. Conservative (pays
                    // whenever ANY mana-costing action is present) so a needed colour is never
                    // starved. Gated on s_human_play -> autonomous byte-identical.
                    bool allow_shock_pay = true;
                    if (s_human_play)
                    {
                        allow_shock_pay = false;
                        for (const Action& a : plan.actions)
                        {
                            if (a.kind == Action::Kind::CastFromHand
                                || a.kind == Action::Kind::CastFromGraveyard
                                || a.kind == Action::Kind::DigDraw)
                            { allow_shock_pay = true; break; }
                        }
                    }
                    // Pin the land's ETB scry/surveil disposition when this plan variant carries one
                    // (searched, not narrowed -- see Plan::scry_choice). Consumed by the first look.
                    ScriptedTopChoice _stc(plan.scry_choice);
                    PlayLandByName(state, plan.land_to_play, plan.fetch_target, allow_shock_pay, plan.land_face);
                }
            }
        }
        else if (is_pre_combat)
        {
            SimulateLandPlay(state);
        }
    }

    // Deploy creatures via Aether Vial before casting spells so lord effects are live.
    auto apply_vial = [&](const std::string& name)
    {
        const CardDefinition* copt = CardDatabase::Instance().Lookup(name);
        if (!copt || !copt->card.IsCreature()) { return; }
        int mv = copt->card.m_mana_cost.ManaValue();
        auto hand_it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (hand_it == ap.hand.end()) { return; }
        int bf_sz = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_sz; ++vi)
        {
            Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            const CardDefinition* vdef = CardDatabase::Instance().LookupCached(vp.card);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }
            if (vp.charge_counters != mv) { continue; }
            Permanent perm;
            perm.card              = copt->card;
            perm.card.m_number     = hand_it->m_number;   // per-copy ID (mirror AIEngine's Vial deploy)
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            ap.hand.erase(hand_it);
            state.battlefield[vi].tapped = true;  // access by index — push_back may reallocate
            // ETB dig / legend rule also apply to Vial-deployed creatures (Vial is not a
            // cast, so no on-cast trigger, but the ETB still happens).
            if (copt->params.etb_dig_count > 0)
            {
                PerformEtbDig(state, state.active_player_index, copt->params,
                              &state.battlefield.back());
            }
            if (copt->card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }
            return;
        }
    };
    // Forward-declared so apply_one's draw breakpoints can re-apply a freshly solved
    // sub-plan (newly drawn castables) through the same canonical-order dispatch.
    std::function<void(const std::vector<Action>&, bool)> apply_plan_actions;

    // Play a revealed land as the turn's land drop inside a staged-draw breakpoint
    // (pre-combat only), mirroring the real engine's draw-engine second pass
    // (AIEngine::TryPlayLand). A Light Up the Stage land revealed by the draw frees
    // mana for the freshly revealed spells; without this the search under-developed
    // vs the real game (the gi=561 class: real cast a creature a turn earlier off the
    // revealed land). Records the played land (Kind::PlayLand) into `sink` so
    // commit-the-line replay reproduces it. Used only by the DrawSpell (stages_cards)
    // branch -- for Treasure Hunt's DrawUntilNonland the revealed lands are Land's
    // Edge ammo, not a land drop. Always mutates state; records only while building a
    // committed line (out_breakpoint && sink non-null).
    auto play_breakpoint_land = [&](std::vector<Action>* sink)
    {
        // Default engine behavior (mirrors s_fd_opp_spawns); MTG_LEGACY_SEARCH opts
        // back into the held-out baseline (byte-frozen old ground truth) for A/Bs.
        static const bool s_fd = !EnvOn("MTG_LEGACY_SEARCH");
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        // MTG_FORCE_LAND diagnostic (see EnumeratePlansWithLand): the POST-DRAW drop is picked by
        // SimulateLandPlay, a static ranker the search never branches on -- so forcing it in the
        // plan enumerator alone cannot reach this decision. Honour the override here too, else a
        // deferred-drop turn silently ignores it.
        std::string played;
        const std::string forced = ForcedLandForTurn(state.turn_number);
        if (!forced.empty() && PlayLandByName(state, forced, std::string{})) { played = forced; }
        else { played = SimulateLandPlay(state); }
        if (!played.empty() && out_breakpoint != nullptr && sink != nullptr)
        {
            Action la;
            la.kind      = Action::Kind::PlayLand;
            la.card_name = played;
            sink->push_back(la);
        }
    };

    // Part B (defer-the-land-until-you-see-the-draw): after a Treasure Hunt (DrawUntilNonland)
    // resolves, play the DEFERRED land drop now that the draw is known. If the hand is flooding
    // and no no-max-hand-size land is already in play, play a DRAWN Reliquary Tower so the whole
    // draw is KEPT as Land's Edge ammo (gi=65). Otherwise play the best normal land (chosen
    // against the post-draw hand) so the drop is developed and the land not discarded (gi=881: a
    // drawn Temple of Epiphany was discarded only because the deferred drop was never played).
    // Only when the land drop is still open (the plan deferred); records the play for
    // commit-the-line replay. Legacy keeps the frozen behavior (no land here).
    auto play_drawn_flood_keep_land = [&](std::vector<Action>* sink)
    {
        static const bool s_fd = !EnvOn("MTG_LEGACY_SEARCH");
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        Player& lp = state.ActivePlayer();
        if (lp.lands_played_this_turn >= lp.LandDropsAvailable()) { return; }   // drop already used

        // Hold the drop entirely when the lands in hand are the marginal Land's Edge ammo for a lethal this
        // turn: playing one would push the count below lethal and the fire-count heuristic (below, in this
        // same ApplyPlanDirect) would then hold the rest, slipping the win a turn (s1 gi0 T4-vs-T3).
        // Provider-owned (HoldDeferredDropForLethal); default off for every other deck.
        if (ResolveProvider(state).HoldDeferredDropForLethal(state, state.active_player_index)) { return; }

        // The keep-ammo land CHOICE is deck logic -> ask the provider (PostDrawKeepLandName); the engine
        // keeps the open-drop precondition above and the land-play mechanism below.
        std::string reliquary =
            ResolveProvider(state).PostDrawKeepLandName(state, state.active_player_index);
        if (!reliquary.empty())
        {
            if (PlayLandByName(state, reliquary, std::string{}) && out_breakpoint != nullptr && sink != nullptr)
            {
                Action la;
                la.kind      = Action::Kind::PlayLand;
                la.card_name = reliquary;
                sink->push_back(la);
            }
            return;
        }
        // No flood-keep land YET, but another dig is affordable this turn -> HOLD the drop
        // (HoldDeferredDropForFurtherDig). Developing here spends the only way to play a Reliquary Tower one
        // dig too early, so a Tower revealed by the NEXT dig is unplayable and the flood is discarded at
        // cleanup (s2 gi1). This step runs again after that dig, so a whiff still develops -- just one dig
        // later.
        if (ResolveProvider(state).HoldDeferredDropForFurtherDig(state, state.active_player_index)) { return; }
        // NO RULE FIRED -> the static ranker picks the drop (SimulateLandPlay: first multi-colour
        // land in HAND ORDER, blind to yield). This is the SEARCH RESTRICTION documented in
        // clairvoyant-reference-shortfalls.md A6: the post-dig continuation is resolved by the GREEDY
        // TurnSolver::Solve below, which does not enumerate land variants, so no depth or budget can
        // reach a different drop. MTG_BP_DROP_SEARCHED omits the static pick -- but until the
        // breakpoint is a real search node that is strictly worse (no land is played at all), so it
        // is opt-in for diagnosis only, NOT a fix.
        static const bool s_searched = EnvOn("MTG_BP_DROP_SEARCHED");
        if (!s_searched) { play_breakpoint_land(sink); }
    };

    // ---- Searched breakpoint continuation (Plan::bp_choice) ------------------------------------
    // The breakpoint at index `bp_at` becomes a real search node when the plan carries a bp_choice:
    // instead of the static land ranker + greedy Solve, the continuation is candidate k of the SAME
    // land-folded plan set the outer search ranks. The rollout that produced this plan scored this
    // exact (bp_at, k), so prediction and realisation stay in lockstep (no fd-diverge) and the
    // decision is searched end to end. bp_choice < 0 (default) -> greedy, byte-identical.
    //
    // Every breakpoint of a searchable class is REACHABLE: wave 0 emits bp_at < BpSearchDepth() and
    // the deferred wave phase opens slots for the deeper ones off g_bp_seen_last below, so nesting
    // costs spare budget rather than being a horizon. An apply still resolves at most ONE of them --
    // a line needing two simultaneous non-greedy continuations is the deliberate L*W-not-W^L trade.
    int  bp_seen = 0;
    auto bp_searched_plan = [&](int site, TurnSolver::Plan& out) -> bool
    {
        // bp_seen counts only breakpoints of an ENABLED class, and only for a plan that carries a
        // choice -- exactly the original short-circuit. Counting disabled-class breakpoints too
        // would shift every later index and silently change play, which is why BpSiteMask is one
        // global value that the executor's replay reads as well.
        const bool class_on    = (BpSiteMask() & (1 << site)) != 0;
        const int  seen_before = (plan.bp_choice >= 0 && class_on) ? bp_seen++ : -1;
        const bool eligible    = plan.bp_choice >= 0 && class_on && seen_before == plan.bp_at;
        // Report the running count so the wave walker can open a slot for each nested breakpoint it
        // discovers (see g_bp_seen_last). Written per breakpoint rather than once at the end so it
        // survives every early exit out of this apply.
        if (seen_before >= 0) { g_bp_seen_last = seen_before + 1; }
        // A 2nd+ breakpoint in the same apply that wave 0 did not cover. Still reachable (the wave
        // phase opens it); the counter measures how much of the nesting lands there.
        const bool nested_blocked = plan.bp_choice >= 0 && class_on
                                 && seen_before >= BpSearchDepth();
        bool resolved = false;
        if (eligible)
        {
            // Shared with the executor (AIEngine::resolve_draw_breakpoint): both index the SAME list.
            std::vector<TurnSolver::Plan> cands =
                TurnSolver::EnumerateBreakpointPlans(state, is_pre_combat);
            // How LONG is the ranked list? The caller can only emit bp_choice = 0..W-1 blind, so
            // report the real length back for the deferred-wave loop (and MTG_BP_CANDS_PROBE).
            // See g_bp_cands_last -- write-only for now, hence byte-identical.
            g_bp_cands_last = static_cast<int>(cands.size());
            // Sample once per distinct breakpoint, not once per variant: the W variants of one base
            // plan all re-reach the SAME breakpoint state (that is the enum memo's premise), and
            // bp_choice == 0 is always emitted, so gating on it counts each occurrence exactly once.
            if (plan.bp_choice == 0) { BpCands(site, g_bp_cands_last, BpSearchWidth()); }
            // Fewer continuations than variants -> fall back to greedy, making this variant a
            // duplicate of its base plan (a wasted node, never a wrong answer).
            if (plan.bp_choice < static_cast<int>(cands.size()))
            {
                out      = cands[plan.bp_choice];
                resolved = true;
            }
        }
        // Lockstep trace (MTG_BP_TRACE): the apply side's breakpoint SEQUENCE for the committed
        // line, printed only while g_bp_trace_arm is set (the fd-trace committed-line replay), so
        // it can be diffed line-for-line against the executor's [bp-exec] sequence. A searched
        // continuation applied at a DIFFERENT index on the two sides is the lockstep defect.
        if (g_bp_trace_arm)
        {
            std::fprintf(stderr,
                         "[bp-apply] turn=%d site=%d idx=%d bp_at=%d bp_choice=%d searched=%d%s\n",
                         state.turn_number, site, seen_before, plan.bp_at, plan.bp_choice,
                         resolved ? 1 : 0, class_on ? "" : " (class off)");
        }
        BpHit(site, out_breakpoint != nullptr, resolved, nested_blocked);
        return resolved;
    };
    // Play a continuation's SEARCHED land drop and record it for commit-the-line replay. Inert for
    // a greedy continuation (Solve never sets land_decided), so the greedy path is unchanged.
    auto bp_play_searched_land = [&](const TurnSolver::Plan& sp, std::vector<Action>* sink)
    {
        if (!sp.land_decided || sp.land_to_play.empty()) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        if (!PlayLandByName(state, sp.land_to_play, sp.fetch_target, true, sp.land_face)) { return; }
        if (out_breakpoint != nullptr && sink != nullptr)
        {
            Action la;
            la.kind      = Action::Kind::PlayLand;
            la.card_name = sp.land_to_play;
            sink->push_back(la);
        }
    };


    // One-shot flag: when set, the NEXT apply_one cast skips its mana cost (a free
    // cascade cast). Consumed at the top of apply_one so it applies to exactly one cast.
    bool cascade_free = false;
    std::function<void(const std::string&, bool, bool, int, bool, int, const std::string&, int, int, int, int, int, const std::string&, int)> apply_one;
    apply_one = [&](const std::string& name, bool is_sacrifice, bool from_graveyard, int discard_lands,
                    bool alt_cost, int alt_lifegain, const std::string& tutor_target, int chosen_x,
                    int own_targets, int ponder_keep, int crackle_targets, int splice_count,
                    const std::string& chosen_float_color, int enchant_target)
    {
        // Find the card in its zone first, then resolve its definition via the card's cached
        // pointer -- avoids a by-name Lookup (string hash) on every cast (apply_one is per-cast,
        // ~200k/game). Byte-identical: it->m_name == name so LookupCached(*it) == Lookup(name),
        // and the two early-returns (not in zone / unknown def) yield the same outcome in either
        // order.
        std::vector<Card>& zone = from_graveyard ? ap.graveyard : ap.hand;
        // Choose WHICH copy of `name` to cast: prefer the EARLIEST-EXPIRING copy, so a staged
        // (exiled, expires end-of-turn) copy is spent before a persistent hand copy. Otherwise the
        // staged copy lapses unplayed while a hand copy that would keep for a later turn is wasted --
        // the Dragonstorm s26 Scourge (1 hand + 2 staged): casting hand+staged stranded a staged copy
        // that expired, costing the T5 follow-up cast. Mirrors TryPlaySpecificLand and the executor's
        // AIEngine::cast_by_name (kept in lockstep so the committed line replays exactly). A staged
        // copy is preferred over a non-staged one, and among staged copies the earlier m_staged_expiry
        // wins. Falls to the first match when no copy is staged -> byte-identical for decks without
        // staged duplicates of `name`.
        std::vector<Card>::iterator it = zone.end();
        for (auto c = zone.begin(); c != zone.end(); ++c)
        {
            if (c->m_name != name) { continue; }
            if (it == zone.end()) { it = c; continue; }             // first match (fallback)
            if (c->m_is_staged && (!it->m_is_staged || c->m_staged_expiry < it->m_staged_expiry))
            {
                it = c;                                             // earlier-expiring staged copy wins
            }
        }
        if (it == zone.end()) { return; }
        const CardDefinition* opt = CardDatabase::Instance().LookupCached(*it);
        if (!opt) { return; }
        const CardDefinition& def = *opt;

        // Irencrag Feat "you can cast only one more spell this turn": once a restrictor has resolved, the
        // turn-scoped budget counts down; at 0 no further spell may be cast -- hand OR staged (an Apex-of-
        // Power-exiled Dragonstorm is still a spell cast this turn). This is the EXECUTION-TIME enforcement
        // the static subset checks only approximate; without it the search finds illegal Irencrag -> Apex ->
        // (dump the whole exile) lines. Checked before any mutation. Inert (budget == -1) for every deck
        // without a max_casts_after card. See GameState::casts_remaining_this_turn.
        if (state.casts_remaining_this_turn == 0) { return; }   // budget spent: this cast is illegal

        // Cast-time guard for a risky alt payload (Reverent Silence): the search may commit it
        // from a node whose enabler diverges away in the realized line (commit-the-line
        // non-convergence, gi=212). Re-check the gate on the CURRENT board: if no enabler
        // survives the wipe and it isn't lethal, SKIP it -- keep the card and our own
        // enchantments rather than self-brick. Inert for decks without a destroy-all alt.
        if (alt_cost && def.params.destroy_all_enchantments
            && !ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, def))
        {
            return;
        }

        bool is_creature = def.card.IsCreature();
        // Cascade casts its target for FREE (CR 702.84). Consume the one-shot flag here,
        // before any nested casts, so exactly THIS cast skips its mana cost.
        bool free_cast = cascade_free;
        cascade_free = false;
        // Desperate Ritual SPLICE: pay (splice_count+1) times the printed cost (single Medallion floor
        // inside EffectiveCost). Matches the enum's a.cost = EffectiveCost(def,state,k+1) and the
        // executor's CastSpellFromHand -> lockstep. copies=1 for every non-spliced cast.
        ManaCost ec = EffectiveCost(def, state, splice_count + 1);
        // Soulfire Eruption: extra Hinata discount from the searched own-creature targets (mirrors
        // the enumeration cost and the executor's CastSpellFromHand -> lockstep).
        ec.generic = std::max(0, ec.generic
                       - SoulfireOwnTargetDiscount(def, state, state.active_player_index, own_targets));
        // {X} spells: pay the chosen X, once per {X} pip (Crackle {X}{X}{X} -> 3X generic).
        if (def.card.m_mana_cost.has_x && chosen_x > 0)
        {
            int pips = def.card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
            ec.generic += chosen_x * pips;
            // Crackle: discount DERIVES from the declared count (crackle_targets); the enumeration
            // costed it the same way -> lockstep. Non-Crackle X spells pass -1 (auto formula).
            ec.generic = std::max(0, ec.generic
                           - HinataGenericDiscount(def, state, chosen_x,
                                 IsCrackleCountSpell(def.params) ? crackle_targets : -1));
        }
        // Scaled divided-damage spell (Magma Opus): the committed face (carried on crackle_targets)
        // fixes the cost via the archetype's model, recomputed on the CURRENT board so the rollout, the
        // executor (CastSpellFromHand), and CanPay price the same committed face identically -> lockstep
        // (the same recompute-from-the-searched-count pattern as Soulfire/Crackle above). Only a scaled
        // Magma variant sets crackle_targets >= 0 on a damage_divided spell; every other cast is inert.
        if (def.params.damage_divided && crackle_targets >= 0)
        {
            for (const ScaledCastVariant& v : ResolveProvider(state).ScaledCastVariants(state, def))
            { if (v.face == crackle_targets) { ec = v.cost; break; } }
        }
        if (!free_cast && !alt_cost)
        {
            if (AffordAuditOn()) { g_afford_rollout_attempts.fetch_add(1, std::memory_order_relaxed); }
            if (g_bp_trace_arm) { BpTraceCast("apply", state, name, ec, is_creature); }
            if (!TapForCostDirect(state, ec, is_creature))
            {
                if (g_bp_trace_arm) { std::fprintf(stderr, "[bp-pay]    -> FAILED\n"); }
                if (AffordAuditOn()) { g_afford_rollout_fails.fetch_add(1, std::memory_order_relaxed); }
                // SERVER-TRUTH RESOLUTION: a declared cast that can't be paid is dropped (left in hand).
                // Record its name so the play viewer learns AUTHORITATIVELY which casts failed, instead of
                // inferring it from a board diff (the false-positiving detectDropped). Only top-level
                // main-plan casts (sink_stack empty) -- nested breakpoint re-solve casts are engine-driven,
                // not part of the user's committed line. Sink is nulled off real play so GT stays identical.
                if (sink_stack.empty() && g_play_dropped_cast_sink) { g_play_dropped_cast_sink->push_back(name); }
                return;
            }
        }
        // Apex of Power cast-from-hand gate (captured BEFORE the erase invalidates `it`): a hand copy
        // has m_is_staged == false -> cast_from_hand true (adds Apex's 10-colour float); an Apex cast off
        // another Apex's staged exile has m_is_staged == true -> false (float withheld). Inert otherwise.
        const bool cast_from_hand = !it->m_is_staged;
        // Per-copy stable ID of the card being cast. The permanent must carry it (like the
        // executor's EffectHandler::EnterBattlefield does via entry.source.m_number) so that
        // aura attachment (Permanent::aura_attached_to == creature m_number) resolves to the
        // SPECIFIC creature. Without this every rollout permanent kept the definition's m_number
        // of 0, so ResolveEnchantTarget returned 0 and AuraBonusFor matched every aura (att 0)
        // against every creature (m_number 0) -- a systematic aura over-count vs the executor.
        const int cast_number = it->m_number;
        zone.erase(it);

        // STORM counter (Dragonstorm): the spell is now cast (committed to the "stack"). Count it
        // ONCE per apply_one invocation -- a spliced Desperate Ritual is ONE base cast (the k spliced
        // copies stay in hand, not cast), a cascade free-cast IS its own spell, and each nested
        // draw-breakpoint cast is its own apply_one so it self-counts. Read only by Dragonstorm's
        // resolution (below) + no BuildSimKey fold -> byte-identical for every non-storm deck.
        ++state.spells_cast_this_turn;

        // Maintain the "one more spell" budget in lockstep with the storm counter (same per-cast site).
        // A non-restrictor spends one (when a budget is active); the restrictor itself (Irencrag) INSTALLS
        // its budget -- decrement FIRST (its own cast is governed by any PRIOR budget, already checked
        // above), THEN take the min with its max_casts_after so two Irencrags compose correctly. Inert
        // (budget stays -1) for every deck without a max_casts_after card.
        if (state.casts_remaining_this_turn > 0) { --state.casts_remaining_this_turn; }
        if (def.params.max_casts_after >= 0)
        {
            state.casts_remaining_this_turn =
                (state.casts_remaining_this_turn < 0)
                    ? def.params.max_casts_after
                    : std::min(state.casts_remaining_this_turn, def.params.max_casts_after);
        }

        // Alternative cost paid as "an opponent gains alt_lifegain life" (Invigorate / Skyshroud
        // Cutter / Reverent Silence) -> reversed to damage by a Tainted Remedy / Plague Drone.
        // Paid at cast (before on-cast triggers), then the spell resolves its normal effect.
        if (alt_cost) { OpponentGainsLife(state, state.active_player_index, alt_lifegain); }

        // Commit-the-line recording: if inside a breakpoint re-solve (sink_stack
        // non-empty), record THIS cast into the current sink so AIEngine can replay it
        // verbatim; its own draw-breakpoint casts nest under my_bp_sink. A main-plan
        // cast (empty stack) is not recorded -- the committed plan.actions already holds
        // it -- but its re-solve still records into the top-level out_breakpoint.
        std::vector<Action>* my_bp_sink = out_breakpoint;
        if (out_breakpoint && !sink_stack.empty())
        {
            Action rec;
            rec.kind = from_graveyard ? Action::Kind::CastFromGraveyard
                                      : Action::Kind::CastFromHand;
            rec.card_name      = name;
            rec.sacrifice_land = is_sacrifice;
            rec.discard_lands  = discard_lands;
            rec.alt_cost       = alt_cost;
            rec.alt_lifegain   = alt_lifegain;
            rec.tutor_target   = tutor_target;
            rec.chosen_x       = chosen_x;
            rec.soulfire_own_targets = own_targets;
            rec.ponder_keep    = ponder_keep;
            rec.crackle_targets = crackle_targets;
            rec.splice_count   = splice_count;
            // LOCKSTEP: these two were MISSING, and they are exactly the last two arguments
            // AIEngine::replay_recorded feeds back into cast_by_name -- so a cast made INSIDE a
            // breakpoint continuation replayed with an EMPTY float colour and no enchant target.
            // For Apex of Power that is not a small drift: GameState documents "empty ->
            // wild / no colour choice", so the rollout floated the searched colour ("add ten mana
            // of any ONE color", which the card's own contract says must NEVER be wild because
            // wild could illegally pay a multicolour mix) while the executor floated WILD. The
            // two then paid different pips and the realised turn fell behind the committed one:
            // Dragonstorm s6006 gi362 committed a proven T6 and realised T7 ([fd-diverge]
            // realized_win=7 predicted_win=6). Latent until a continuation ranked high enough to
            // cast a second Apex became reachable -- it reproduces with MTG_BP_SEARCH=17 and no
            // waves at all, so the deferred waves EXPOSED it rather than caused it.
            // See docs/design/rollout-executor-lockstep.md.
            rec.chosen_float_color = chosen_float_color;
            rec.enchant_target     = enchant_target;
            sink_stack.back()->push_back(rec);
            my_bp_sink = &sink_stack.back()->back().breakpoint_casts;
        }

        // Retrace additional cost: discard `discard_lands` land cards from hand.
        // (The mana cost was paid above; CollectActions ensured enough lands exist.) The heuristic
        // discards the FIRST land in hand order; under --claude-play the human picks WHICH land
        // (g_play_retrace_chooser). This is the shared ApplyPlan (real claude-play) + ApplyPlanDirect
        // (rollout) path; RevealLogPause nulls the chooser for rollout/search, so autonomous play
        // keeps the first-land pick and stays byte-identical.
        if (from_graveyard && discard_lands > 0)
        {
            auto hand_land_indices = [&]() {
                std::vector<int> idx;
                for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
                {
                    const CardDefinition* hdef = CardDatabase::Instance().LookupCached(ap.hand[i]);
                    if (hdef ? hdef->card.IsLand() : ap.hand[i].IsLand()) { idx.push_back(i); }
                }
                return idx;
            };
            for (int discarded = 0; discarded < discard_lands; ++discarded)
            {
                std::vector<int> lands = hand_land_indices();
                if (lands.empty()) { break; }
                // WHICH land is provider-owned (RetraceDiscardCandidates); the base rule is the
                // historical first-land-in-hand-order, so this is byte-identical.
                const std::vector<int> rranked = ResolveProvider(state).RetraceDiscardCandidates(
                    state, state.active_player_index, lands);
                int pick = rranked.empty() ? lands.front() : rranked.front();
                if (g_play_retrace_chooser && lands.size() > 1)
                {
                    int chosen = (*g_play_retrace_chooser)(state, state.active_player_index, name, lands, pick);
                    for (int li : lands) { if (li == chosen) { pick = chosen; break; } }
                }
                ap.graveyard.push_back(ap.hand[pick]);
                ap.hand.erase(ap.hand.begin() + pick);
            }
        }

        // On-cast triggers (Eidolon of the Great Revel) and Prowess fire when the spell
        // is CAST -- before it resolves AND before this spell's own permanent (if a
        // creature/enchantment) enters the battlefield. Fire them HERE, ahead of the
        // resolution branch below, to mirror the real engine: CastSpellFromHand pushes
        // the spell onto the stack and only then fires on-cast triggers, so the spell is
        // NOT yet on the battlefield and a permanent with its own on-cast trigger does
        // not trigger on its own cast (Eidolon casting Eidolon deals 0, not 2). Firing
        // after the branch (the old position) placed the creature first, so Eidolon
        // self-triggered -- over-counting rollout self-damage by 2 per Eidolon cast.
        FireOnCastTriggers(state, def);
        FireProwess(state, def);

        if (def.tmpl == CardTemplate::DirectDamage)
        {
            // Play-viewer event: opponent life BEFORE this burn spell's damage, so the history can
            // report "N to opponent (before->after)". Measured up to (not including) the
            // opponent_lifegain rider below, which OpponentGainsLife reports as its own life event.
            // g_play_event_sink is nulled by RevealLogPause during search/rollout -> byte-identical.
            const int burn_opp_life_before = state.players[opp_idx].life;
            // Mirror EffectHandler::ResolveDirectDamage so the rollout's life total
            // matches the real game. Previously only Any/Player targeting dealt face
            // damage, so Searing Blaze (Multi) and Searing Blood (Creature) were inert
            // here while the win-check and the real engine both counted their damage —
            // a phantom-early-win source.
            Targeting t = def.params.targeting;
            // An {X} burn deals chosen X * x_damage_multiplier (Crackle = 5X; mirrors
            // EffectHandler::ResolveDirectDamage); a fixed-damage burn uses params.damage.
            int x_mult = def.params.x_damage_multiplier; if (x_mult < 1) { x_mult = 1; }
            int dmg = def.card.m_mana_cost.has_x ? (chosen_x * x_mult) : def.params.damage;
            if (!def.card.m_mana_cost.has_x
                && def.params.landfall_damage > 0 && ap.lands_played_this_turn > 0)
            {
                dmg = def.params.landfall_damage;
            }
            // Soulfire Eruption: bounded multi-target dig (exile + stage top N; face = max MV,
            // self = min MV). Mirrors EffectHandler so the rollout matches the executor (lockstep).
            if (def.params.damage_equals_top_mv)
            {
                SoulfireResult sr = SoulfireDig(state, state.active_player_index, own_targets, &def, "apply");
                dmg = sr.face_damage;
                state.players[state.active_player_index].life -= sr.self_damage;
            }

            if (t == Targeting::Any || t == Targeting::Player)
            {
                // Human-play board-click targeting (claude-play): deal `dmg` to each chosen target
                // (face or a creature) instead of always the opponent face. The chooser is nulled by
                // RevealLogPause for the search/rollout, so this is byte-identical there (face only).
                // Uniform per-target damage: a fixed burn, or Crackle's 5X to up to X targets. Soulfire
                // (damage_equals_top_mv) is handled above and never reaches here as a retargetable set.
                //
                // Crackle with Power (discount_targets_scale_x): the extra-target COUNT is the
                // searched/declared crackle_targets, so search AND human resolve identically here --
                // 5X to the opponent face plus the first `crackle_targets` extras (creatures/self),
                // which take 5X and die (SBA). This is the faithful model that makes the derived
                // discount honest (the auto-max free-discount path is gone). Lockstep with
                // EffectHandler::ResolveDirectDamage. No g_play_target_chooser detour for Crackle.
                if (IsCrackleCountSpell(def.params))
                {
                    if (dmg > 0)
                    {
                        const int ci = state.active_player_index;
                        if (g_play_target_chooser)
                        {
                            // HUMAN play: the opponent face is a NORMAL, optional target (order[0]),
                            // not a forced hit. Default = CrackleTargetOrder's first Tmin (Tmin = 1
                            // face + the plan's crackle_targets extras = the affordability FLOOR: the
                            // committed plan paid the discount for exactly this many targets). The
                            // player may retarget, deselect the face, or add extras up to Tmax = min(X,
                            // #legal) -- more targets only raise the discount (the plan overpaid, which
                            // is free). The chooser infers min = heur.size(), max = the passed max. ALL
                            // damage is applied AFTER the chooser returns, so the target decision's
                            // dumped state is not pre-reduced (no phantom negative life).
                            const int Tmin = std::max(1, crackle_targets + 1);
                            std::vector<int> order = CrackleTargetOrder(state, ci, dmg);
                            const int Xval = (def.params.x_damage_multiplier > 0)
                                             ? dmg / def.params.x_damage_multiplier : Tmin;
                            const int Tmax = std::max(Tmin, std::min(Xval, static_cast<int>(order.size())));
                            std::vector<ChosenTarget> heur;
                            for (int n = 0; n < Tmin && n < static_cast<int>(order.size()); ++n)
                            {
                                int t = order[n];
                                heur.push_back(t == CRACKLE_OPP_FACE  ? ChosenTarget{ 0, 1 - ci, 0 }
                                             : t == CRACKLE_SELF_FACE ? ChosenTarget{ 0, ci,     0 }
                                             :                          ChosenTarget{ 1, t,      0 });
                            }
                            std::vector<ChosenTarget> picked =
                                (*g_play_target_chooser)(state, def, ci, Tmax, dmg, heur);
                            if (picked.empty()) { picked = heur; }
                            std::vector<int> tlist;
                            for (const ChosenTarget& c : picked)
                            {
                                if (c.kind == 0) { tlist.push_back(c.index == ci ? CRACKLE_SELF_FACE : CRACKLE_OPP_FACE); }
                                else             { tlist.push_back(c.index); }
                            }
                            if (CrackleApplyTargets(state, ci, dmg, tlist)) { state.opponent_lost_life_this_turn = true; }
                        }
                        else
                        {
                            // AUTONOMOUS (no chooser): the win line always hits the face, so resolve the
                            // face plus the first `crackle_targets` extras -- byte-identical to the old
                            // "face -= dmg; CrackleHitExtraTargets(count)" path (same order, same SBA).
                            state.players[opp_idx].life -= dmg;              // face = 5X (the win)
                            state.opponent_lost_life_this_turn = true;
                            CrackleHitExtraTargets(state, ci, dmg, crackle_targets);
                        }
                    }
                }
                else if (g_play_target_chooser && dmg > 0 && !def.params.damage_equals_top_mv)
                {
                    // Divided damage (Fiery Justice): up to `dmg` targets, the chooser returns a
                    // per-target allocation summing to dmg. Uniform burn: 1 target (or X for Crackle),
                    // each taking the full dmg. The chooser branches on def.params.damage_divided.
                    bool divided = def.params.damage_divided;
                    int max_targets = divided ? dmg
                                    : (def.card.m_mana_cost.has_x && def.params.x_damage_multiplier > 0)
                                    ? std::max(1, chosen_x) : 1;
                    // Default: all damage to the opponent face (amount = dmg for divided, ignored else).
                    std::vector<ChosenTarget> heur = { { 0, opp_idx, divided ? dmg : 0 } };
                    // `dmg` is the ACTUAL per-target damage (fixed burn = base damage; Crackle = X*mult)
                    // or, for a divided spell, the TOTAL to allocate -- passed so the dialog shows the
                    // true number instead of recomputing (x_damage_multiplier defaults to 1, which made
                    // fixed burn mis-display as "1 damage").
                    std::vector<ChosenTarget> picked =
                        (*g_play_target_chooser)(state, def, state.active_player_index, max_targets, dmg, heur);
                    if (picked.empty()) { picked = heur; }
                    for (const ChosenTarget& c : picked)
                    {
                        int amt = divided ? c.amount : dmg;   // divided: per-target share; else flat dmg
                        if (amt <= 0) { continue; }
                        if (c.kind == 0)
                        {
                            state.players[c.index].life -= amt;
                            if (c.index == opp_idx) { state.opponent_lost_life_this_turn = true; }
                        }
                        else if (c.index >= 0 && c.index < static_cast<int>(state.battlefield.size()))
                        {
                            Permanent& tp = state.battlefield[c.index];
                            const int before = tp.damage;
                            tp.damage += amt;   // SBA sweep below removes the dead
                            // This spell has no death trigger of its own (Any/Player = Bolt/Crackle/
                            // divided), but if it KILLS a creature a prior Searing Blood damaged, that
                            // Blood's pending trigger fires now. Shared lockstep helper (0 = no own rider).
                            ApplyBurnToCreature(state, tp, before, 0, state.active_player_index);
                        }
                    }
                    // State-based: destroy creatures with lethal damage (highest index first so the
                    // erase doesn't shift a not-yet-processed index).
                    for (int bi = static_cast<int>(state.battlefield.size()) - 1; bi >= 0; --bi)
                    {
                        Permanent& p = state.battlefield[bi];
                        if (p.card.IsCreature() && p.damage > 0 && p.damage >= p.EffectiveToughness())
                        {
                            state.players[p.owner_index].graveyard.push_back(p.card);
                            state.battlefield.erase(state.battlefield.begin() + bi);
                        }
                    }
                }
                else
                {
                    // A scaled divided-damage spell (Magma Opus) deals only its committed face level to
                    // the opponent (the searched `crackle_targets`; the rest was spread onto inert
                    // targets for the Hinata discount, not simulated). crackle_targets >= 0 marks a
                    // scaled variant; every other burn (and Magma with the model off, crackle_targets
                    // == -1) deals its full dmg. Lockstep with ResolveDirectDamage + the committed cost.
                    int face = (def.params.damage_divided && crackle_targets >= 0) ? crackle_targets : dmg;
                    state.players[opp_idx].life -= face;
                    if (face > 0) { state.opponent_lost_life_this_turn = true; }
                }
            }
            else if (t == Targeting::Creature || t == Targeting::Multi)
            {
                // Both require an opponent creature; with none the spell has no legal
                // target and is not cast (mirrors CastSpellFromHand's early return).
                // Searing Blood (death rider) prefers a creature it KILLS so the 3-to-face
                // fires; Blaze (no rider) takes the first creature -- lockstep with the executor.
                int ci;
                if (def.params.death_trigger_damage > 0)
                {
                    ci = FindBurnKillTarget(state, state.active_player_index, def.params.damage);
                }
                else
                {
                    ci = -1;
                    for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
                    {
                        const Permanent& bp = state.battlefield[bi];
                        if (bp.controller_index != state.active_player_index && bp.card.IsCreature())
                        { ci = bi; break; }
                    }
                }
                // Prowess line: with no opponent creature, self-cast onto a surviving own creature
                // (the enumeration only offered the spell here when this target exists). Lockstep with
                // the executor + enumeration gate (FindOwnProwessBurnTarget).
                if (ci < 0) { ci = FindOwnProwessBurnTarget(state, def); }
                // Human-play targeting (Searing Blood / Searing Blaze): let the player pick WHICH
                // creature takes the burn -- opponent's (default) or their own (cast for prowess). The
                // chooser is nulled by RevealLogPause for search/rollout, so autonomous stays byte-
                // identical (the heuristic ci). Default = the heuristic pick; falls back to the first
                // creature (own included) when the opponent has none so the spell can still be cast.
                // Only surface the pick when there's a GENUINE choice (>= 2 creatures on the board),
                // mirroring the Invigorate pump-target gate. A single legal creature auto-targets (its
                // heuristic ci), so a lone-creature Blood/Blaze cast adds no decision to saved replays.
                int creature_count = 0;
                for (const Permanent& p : state.battlefield) { if (p.card.IsCreature()) { ++creature_count; } }
                if (g_play_target_chooser && creature_count >= 2)
                {
                    int default_ci = ci;
                    if (default_ci < 0)
                    {
                        for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
                        { if (state.battlefield[bi].card.IsCreature()) { default_ci = bi; break; } }
                    }
                    if (default_ci >= 0)
                    {
                        std::vector<ChosenTarget> heur = { { 1, default_ci, 0 } };
                        std::vector<ChosenTarget> picked =
                            (*g_play_target_chooser)(state, def, state.active_player_index, 1, dmg, heur);
                        ci = (!picked.empty() && picked[0].kind == 1) ? picked[0].index : default_ci;
                    }
                }
                if (ci >= 0)
                {
                    // Searing Blaze also hits the targeted creature's CONTROLLER (opponent in the normal
                    // line; yourself if you targeted your own creature). For autonomous play ci is always
                    // an opponent creature, so this is byte-identical.
                    const int tgt_ctrl = state.battlefield[ci].controller_index;
                    if (t == Targeting::Multi)  // Searing Blaze also hits the player
                    {
                        state.players[tgt_ctrl].life -= dmg;
                        if (dmg > 0 && tgt_ctrl == opp_idx) { state.opponent_lost_life_this_turn = true; }
                    }
                    Permanent& tgt = state.battlefield[ci];
                    const int before = tgt.damage;
                    tgt.damage += dmg;
                    // Delayed "when that creature dies" trigger (Searing Blood), accumulated so two
                    // copies on one creature both fire on death. Shared with EffectHandler (lockstep).
                    ApplyBurnToCreature(state, tgt, before, def.params.death_trigger_damage,
                                        state.active_player_index);
                    bool lethal = tgt.damage >= tgt.EffectiveToughness();
                    // State-based action: a creature with lethal damage is destroyed.
                    // Without this the rollout keeps the dead creature on the battlefield
                    // as a phantom target, so a later creature-targeting burn (Searing
                    // Blaze/Blood) "re-kills" it and invents face damage -> phantom early
                    // win. Mirrors the real engine's SBA after damage is dealt.
                    if (lethal) { state.battlefield.erase(state.battlefield.begin() + ci); }
                }
            }
            // Play-viewer history: one "burn" event for all of this spell's direct face damage
            // (Lightning Bolt, Searing Blaze's player hit, Searing Blood's death rider, Land's Edge,
            // Crackle's X-damage), coalescing the multiple internal subtractions into a single line.
            if (g_play_event_sink)
            {
                const int burn_after = state.players[opp_idx].life;
                const int dealt = burn_opp_life_before - burn_after;
                if (dealt > 0)
                {
                    EmitPlayEvent(state.turn_number, "damage",
                                  "\xF0\x9F\x94\xA5 " + def.card.m_name.str() + ": "
                                  + std::to_string(dealt) + " to opponent ("
                                  + std::to_string(burn_opp_life_before) + "\xE2\x86\x92"
                                  + std::to_string(burn_after) + ")");
                }
            }
            // Rider "target opponent gains N life" (Fiery Justice) -> reversed to damage by a
            // Tainted Remedy / Plague Drone. Mirrors EffectHandler::ResolveDirectDamage.
            if (def.params.opponent_lifegain > 0)
            {
                OpponentGainsLife(state, state.active_player_index, def.params.opponent_lifegain);
            }
            // Magma Opus rider: "draw two cards." Mirrors EffectHandler (lockstep).
            if (def.params.cast_draw > 0)
            {
                std::size_t before = ap.hand.size();
                ap.library.DrawN(def.params.cast_draw, ap.hand);
                // Human-play accurate draw reporting (nulled by RevealLogPause during search).
                if (g_play_draw_sink)
                {
                    for (std::size_t hi = before; hi < ap.hand.size(); ++hi)
                    { g_play_draw_sink->push_back({ state.turn_number, ap.hand[hi].m_name.str() }); }
                }
            }
        }
        else if (is_creature)
        {
            Permanent perm;
            perm.card              = def.card;
            perm.card.m_number     = cast_number;   // preserve per-copy ID (mirror EffectHandler)
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);

            // Dragonstorm kill-engine (rollout side): a Dragon entering fires the shared cascade
            // (Scourge ETB ping -> opponent life loss + Lathliss 5/5 token). Mirrors the executor's
            // EffectHandler::EnterBattlefield (lockstep) so the search's win projection counts the
            // ping toward lethal. No-op for every non-Dragon creature -> other decks byte-identical.
            OnDragonEnters(state, state.active_player_index,
                           static_cast<int>(state.battlefield.size()) - 1);

            // Goblins tribal ETB cascade (rollout side, lockstep with EffectHandler): self-tokens,
            // ETB burn, Matron tutor (search target = tutor_target), Muxus reveal. No-op for every
            // non-Goblin creature -> other decks byte-identical.
            OnGoblinEnters(state, state.active_player_index,
                           static_cast<int>(state.battlefield.size()) - 1, tutor_target);

            // ETB library dig (Acclaimed Contender): performed inline so the clairvoyant
            // rollout sees the dug card in hand for later turns. The real game does the
            // SAME deterministic dig at resolution (EffectHandler), reaching identical
            // hand/library state -- no breakpoint/replay needed (the dug card is cast on a
            // later turn, not re-solved this turn). Use the just-pushed permanent as self.
            if (def.params.etb_dig_count > 0)
            {
                PerformEtbDig(state, state.active_player_index, def.params,
                              &state.battlefield.back());
            }

            // Legend rule: a duplicate legendary just cast is put into the graveyard, so a
            // second copy of a legendary lord confers no benefit (the search then avoids
            // casting it). No-op for non-legendary creatures.
            if (def.card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }

            // Replicate: if Hatchery Sliver (or the card itself) grants replicate,
            // pay the mana cost additional times to create token copies.
            // Replicate cost = printed mana cost (CR 702.56a), not the effective cast cost.
            if (CanReplicate(def, state.battlefield, state.active_player_index))
            {
                ManaCost rep_cost = def.card.m_mana_cost;
                // Heuristic default: replicate GREEDILY (as many times as leftover mana allows).
                // Human play (g_play_replicate_chooser set, nulled during search/rollout) may choose
                // FEWER: count the max affordable first on a scratch copy so we can offer 0..max, then
                // cap the real loop. cap < 0 => uncapped => byte-identical to the greedy default.
                int cap = -1;
                if (g_play_replicate_chooser)
                {
                    int max_count = 0;
                    GameState scratch = state;
                    ManaPool rem = AvailableManaPool(scratch);
                    while (rem.CanPay(rep_cost) && TapForCostDirect(scratch, rep_cost, true))
                    { ++max_count; rem = AvailableManaPool(scratch); }
                    int k = (*g_play_replicate_chooser)(state, state.active_player_index,
                                                        def.card.m_name.str(), max_count);
                    cap = k < 0 ? 0 : (k > max_count ? max_count : k);
                }
                int made = 0;
                ManaPool remaining = AvailableManaPool(state);
                while ((cap < 0 || made < cap) && remaining.CanPay(rep_cost))
                {
                    if (!TapForCostDirect(state, rep_cost, true)) { break; }
                    Permanent token = perm;
                    token.card.m_number = 0;
                    state.battlefield.push_back(token);
                    remaining = AvailableManaPool(state);
                    ++made;
                }
            }
        }
        else if (def.tmpl == CardTemplate::DrawSpell)
        {
            // Expressive Iteration: look 3 -> 1 hand / 1 exiled-staged-this-turn / 1 bottom (its own
            // model). Mirrors EffectHandler::ResolveDrawSpell (lockstep). The breakpoint re-solve
            // below then plays the staged (this-turn-only) card.
            const bool is_ei = def.params.expressive_iteration;
            if (is_ei) { ResolveExpressiveIteration(state); }
            // Scry-then-draw (Preordain) / reorder-or-shuffle-then-draw (Ponder): mirror
            // ResolveDrawSpell exactly so the rollout's realised draw matches the executor.
            if (!is_ei && def.params.cast_scry > 0)    { ScryTop(state, def.params.cast_scry); }
            if (!is_ei && def.params.cast_reorder > 0) { ReorderTopOrShuffle(state, def.params.cast_reorder, def.card.m_name, ponder_keep); }
            int n = is_ei ? 0 : std::min(def.params.draw, static_cast<int>(ap.library.size()));
            if (!is_ei && def.params.stages_cards)
            {
                // Mirror ResolveDrawSpell: the cards are exiled and playable only until
                // the end of the controller's next turn (CR 406). Carry that expiry on
                // the card so the rollout expires them (see SimulateToEndImpl) instead
                // of keeping them forever — the latter let the rollout win with cards
                // the real game had already lost (a phantom-early-win source).
                int expiry = state.turn_number + 1;
                std::string staged_names;
                for (int d = 0; d < n; ++d)
                {
                    Card c = ap.library.DrawTop();
                    c.m_is_staged     = true;
                    c.m_staged_expiry = expiry;
                    // Report staged cards as an EXILE event, NOT a draw: they are exiled and only
                    // playable through the listed turn -- calling them "drew" (the draw sink) misleads
                    // the viewer. Nulled by RevealLogPause during search/rollout -> byte-identical.
                    if (!staged_names.empty()) { staged_names += ", "; }
                    staged_names += c.m_name.str();
                    ap.hand.push_back(std::move(c));
                }
                if (g_play_event_sink && !staged_names.empty())
                {
                    EmitPlayEvent(state.turn_number, "staged",
                                  "\xE2\x9F\x82 " + def.card.m_name.str() + " — exiled (playable through T"
                                  + std::to_string(expiry) + "): " + staged_names);
                }
            }
            else
            {
                std::size_t before = ap.hand.size();
                ap.library.DrawN(n, ap.hand);
                // Human-play accurate draw reporting (nulled by RevealLogPause during search).
                if (g_play_draw_sink)
                {
                    for (std::size_t hi = before; hi < ap.hand.size(); ++hi)
                    { g_play_draw_sink->push_back({ state.turn_number, ap.hand[hi].m_name.str() }); }
                }
            }

            // Draw breakpoint: play a revealed land (the real engine's second pass
            // does), then re-solve with updated hand and remaining mana so newly
            // revealed cards can be cast with mana still available this turn.
            // A plain cantrip (Ponder/Preordain — NOT EI/staging) at the MAIN-plan level
            // (sink_stack empty) defers its re-solve until after every main cast, matching
            // the executor's post-loop replay (see deferred_cantrip_resolve). Everything
            // else (EI/staging, or a cantrip already inside a re-solve) re-solves inline.
            const bool plain_cantrip = !is_ei && !def.params.stages_cards;
            if (s_human_play)
            {
                // Human play: the cantrip drew; STOP here. The chooser re-fires so the human
                // re-decides with the drawn card (no auto re-solve, no auto land play).
            }
            else if (s_defer_cantrip && plain_cantrip && sink_stack.empty())
            {
                deferred_cantrip_resolve = true;
            }
            else
            {
                if (out_breakpoint && my_bp_sink) { sink_stack.push_back(my_bp_sink); }
                TurnSolver::Plan extra;
                if (!bp_searched_plan(0, extra))
                {
                    play_breakpoint_land(my_bp_sink);
                    extra = TurnSolver::Solve(state, is_pre_combat);
                }
                bp_play_searched_land(extra, my_bp_sink);
                apply_plan_actions(extra.actions, extra.searched_order);
                if (out_breakpoint && my_bp_sink) { sink_stack.pop_back(); }
            }
        }
        else if (def.tmpl == CardTemplate::DrawUntilNonland)
        {
            // Draw cards from the top until a nonland is found (inclusive) into hand.
            while (!ap.library.empty())
            {
                Card c = ap.library.DrawTop();
                auto cdef = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                // Human-play accurate draw reporting (nulled by RevealLogPause during search).
                if (g_play_draw_sink) { g_play_draw_sink->push_back({ state.turn_number, c.m_name.str() }); }
                ap.hand.push_back(std::move(c));
                if (!is_land) { break; }
            }
            // Draw breakpoint: play a DEFERRED land now that the draw is seen, then re-solve
            // so new castables are played with remaining mana. The flood-keep land play (part
            // B) plays a drawn Reliquary Tower as the open land drop so a flooded draw is KEPT
            // for Land's Edge rather than discarded at cleanup; other revealed lands remain
            // Land's Edge ammo (no land played). See play_drawn_flood_keep_land.
            if (!s_human_play)
            {
                if (out_breakpoint && my_bp_sink) { sink_stack.push_back(my_bp_sink); }
                // Searched continuation when the plan carries one (bp_choice), else the static
                // flood-keep/ranker drop + greedy Solve. bp_play_searched_land then applies the
                // continuation's SEARCHED land: EnumeratePlansWithLand plays each candidate land
                // into a copy BEFORE enumerating casts, so `extra` was scored WITH it in play --
                // applying only extra.actions would silently discard the search's land choice.
                TurnSolver::Plan extra;
                if (!bp_searched_plan(1, extra))
                {
                    play_drawn_flood_keep_land(my_bp_sink);
                    extra = TurnSolver::Solve(state, is_pre_combat);
                }
                bp_play_searched_land(extra, my_bp_sink);
                apply_plan_actions(extra.actions, extra.searched_order);
                if (out_breakpoint && my_bp_sink) { sink_stack.pop_back(); }
            }
            // Human play: Treasure Hunt's reveal is now in hand; the chooser re-fires so the
            // human plays a land / Land's Edge / another Treasure Hunt with the revealed cards.
        }
        else if (def.params.cascade_max_mv > 0)
        {
            // Cascade: exile from top until a nonland with MV < cascade_max_mv is found.
            int limit = def.params.cascade_max_mv;
            std::vector<Card> exiled;
            int cascade_idx = -1;
            while (!ap.library.empty())
            {
                Card c = ap.library.DrawTop();
                auto cdef = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                int  mv      = cdef ? cdef->card.m_mana_cost.ManaValue()
                                    : c.m_mana_cost.ManaValue();
                exiled.push_back(std::move(c));
                if (!is_land && mv < limit)
                {
                    cascade_idx = static_cast<int>(exiled.size()) - 1;
                    break;
                }
            }
            // All non-target cards return to library bottom in exile order.
            for (int ei = 0; ei < static_cast<int>(exiled.size()); ++ei)
            {
                if (ei == cascade_idx) { continue; }
                ap.library.push_back(std::move(exiled[ei]));
            }
            // Cast the cascade target for free: place it in hand so apply_one finds it.
            if (cascade_idx >= 0)
            {
                const std::string& cname = exiled[cascade_idx].m_name;
                auto cdef2 = CardDatabase::Instance().LookupCached(exiled[cascade_idx]);
                if (cdef2)
                {
                    ap.hand.push_back(cdef2->card);
                    cascade_free = true;   // cascade cast pays no mana
                    apply_one(cname, false, false, 0, false, 0, std::string{}, 0, 0, -1, -1, 0, std::string{}, 0);
                }
            }
        }
        else if (def.tmpl == CardTemplate::Removal)
        {
            // Removal (Swords to Plowshares): exile the opponent's LARGEST creature (max life loss via
            // the controller-lifegain rider + a Tainted Remedy / Plague Drone). Enumeration only offers
            // this with an enabler in play, so FindLifegainRemovalTarget picks a target here; a non-
            // lifegain removal (none today) falls back to the first opponent creature. Mirrors
            // EffectHandler::ResolveRemoval + AIEngine::CastSpellFromHand in lockstep.
            int ci = -1;
            if (def.params.controller_lifegain_equals_power)
            {
                ci = FindLifegainRemovalTarget(state, state.active_player_index);
            }
            else
            {
                for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
                {
                    const Permanent& bp = state.battlefield[bi];
                    if (bp.controller_index != state.active_player_index && bp.card.IsCreature())
                    { ci = bi; break; }
                }
            }
            // Human-play targeting: let the player pick WHICH opponent creature Swords exiles, and do
            // NOT bail when there is no enabler. FindLifegainRemovalTarget returns -1 without a Tainted
            // Remedy (a goldfishing pruning: handing a passive opponent life is strictly bad for the
            // AI), but a human who chose to cast Swords wants it to resolve -- and the controller-
            // lifegain rider below then applies exactly as the rules say (opponent gains life with no
            // enabler; loses it with one). Default = the largest opponent creature (max life swing).
            // g_play_target_chooser is nulled by RevealLogPause during search/rollout, so the autonomous
            // engine never enters this block -> byte-identical there.
            if (g_play_target_chooser && def.params.controller_lifegain_equals_power)
            {
                int default_ci = ci;
                if (default_ci < 0)
                {
                    int best_pw = -1;
                    for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
                    {
                        const Permanent& bp = state.battlefield[bi];
                        if (bp.controller_index == state.active_player_index || !bp.card.IsCreature()) { continue; }
                        int pw = bp.EffectivePower();
                        if (pw > best_pw) { best_pw = pw; default_ci = bi; }
                    }
                }
                if (default_ci >= 0)
                {
                    std::vector<ChosenTarget> heur = { { 1, default_ci, 0 } };
                    std::vector<ChosenTarget> picked =
                        (*g_play_target_chooser)(state, def, state.active_player_index, 1, 0, heur);
                    ci = (!picked.empty() && picked[0].kind == 1) ? picked[0].index : default_ci;
                }
            }
            if (ci >= 0)
            {
                // Pump-then-Swords: redirect a free-alt Invigorate onto this creature before
                // capturing its power, so the exile life-loss is +power_bonus larger (autonomous
                // AI only; consumes the pump so the safe-alt pass below never double-fires it).
                // Shared with EffectHandler::ResolveRemoval for lockstep.
                TryPumpThenSwordsRedirect(state, state.active_player_index, ci, def);
                int tgt_controller = state.battlefield[ci].controller_index;
                int tgt_power      = state.battlefield[ci].EffectivePower();
                if (def.params.damage > 0) { state.exile.push_back(state.battlefield[ci].card); }
                else { state.players[tgt_controller].graveyard.push_back(state.battlefield[ci].card); }
                state.battlefield.erase(state.battlefield.begin() + ci);
                if (def.params.controller_lifegain_equals_power && tgt_power > 0)
                {
                    OpponentGainsLife(state, 1 - tgt_controller, tgt_power);
                }
            }
        }
        else if (IsManaRitual(def))
        {
            // Reality Spasm / Irencrag Feat -- mana RITUAL. Float its mana into the turn-scoped
            // reserve for a same-turn payoff (Crackle). Mirrors EffectHandler so the rollout and
            // the real executor realise the identical floating mana (lockstep); the planner
            // credits this same amount, so the predicted combo and the executed one never diverge.
            // Desperate Ritual SPLICE: float (splice_count+1)*{R}{R}{R} (k spliced copies each add their
            // own {R}{R}{R} and STAY IN HAND). Matches the enum's a.ritual_float and the executor's
            // EffectHandler ApplyRitualFloat off the same k -> lockstep. copies=1 for a plain ritual.
            ApplyRitualFloat(state, def, chosen_x, splice_count + 1);
        }
        else if (def.params.tutor_to_hand || def.params.tutor_to_top)
        {
            // Tutor (Idyllic / Enlightened): fetch the SEARCHED target (tutor_target); empty
            // falls back to the heuristic's top pick. Identical to the real game (EffectHandler)
            // so the clairvoyant rollout sees the same fetched card.
            PerformTutor(state, state.active_player_index, def.params, tutor_target, def.card.m_name);
        }
        else if (def.params.tutor_to_battlefield)
        {
            // Dragonstorm (Storm): mirror EffectHandler -- put min(spells_cast_this_turn, Dragons-
            // left) Dragons onto the battlefield through the shared OnDragonEnters cascade (Scourge
            // ping -> opponent life loss; Lathliss token), then shuffle. The pings/tokens are realised
            // by THIS rollout, so the win projection (opp.life <= 0 after ApplyPlanDirect) sees the
            // kill -- no fast-path hand-projection needed (an over-projection would fd-diverge; an
            // under-estimate is safe). spells_cast_this_turn was ++'d at this cast (above), so it
            // already counts Dragonstorm itself = storm copies + the original = the put count. Empty
            // preferred = provider (TutorCandidates) order, identical to EffectHandler (lockstep).
            PerformTutorToBattlefield(state, state.active_player_index, def.params,
                                      state.spells_cast_this_turn, /*preferred=*/{},
                                      def.card.m_name.str());
        }
        else if (def.params.impulse_exile > 0)
        {
            // Apex of Power: exile the top impulse_exile cards as STAGED cards playable THIS turn
            // (m_impulse_no_land -> their lands are non-playable; "cast SPELLS from among them"), then --
            // IFF cast FROM HAND (not off another Apex's exile) -- float impulse_float_amount of the
            // searched colour into the turn-scoped reserve BEFORE the breakpoint re-solve, so the re-solve
            // can spend it on the exiled spells. Mirrors EffectHandler (lockstep): the executor stages into
            // Player::staged_cards + the AIEngine draw-breakpoint merges to hand; here we push straight to
            // hand (like the stages_cards DrawSpell branch) and re-solve inline -> both realise the same
            // castable set + float. The 10 is credited as within-turn combo mana ONLY via this real float
            // (NOT projected in the wins_this_turn fast-path), so the rollout finds Apex kills without any
            // over-projection (fd-diverge). Apex-off-Apex withholds the float (cast_from_hand false).
            const int expiry = def.params.impulse_expiry_this_turn
                             ? state.turn_number : state.turn_number + 1;
            std::vector<Card> exiled = DrawTopAsImpulseStaged(
                state, state.active_player_index, def.params.impulse_exile, expiry);
            std::string staged_names;
            for (Card& ec : exiled)
            {
                if (!staged_names.empty()) { staged_names += ", "; }
                staged_names += ec.m_name.str();
                ap.hand.push_back(std::move(ec));
            }
            if (g_play_event_sink && !staged_names.empty())
            {
                EmitPlayEvent(state.turn_number, "staged",
                              "\xE2\x9F\x82 " + def.card.m_name.str()
                              + " — exiled (castable this turn): " + staged_names);
            }
            if (cast_from_hand && def.params.impulse_float_amount > 0)
            {
                AddChosenColorFloat(state, chosen_float_color, def.params.impulse_float_amount);
            }
            // Draw breakpoint: re-solve from the post-exile state (staged spells in hand + the float
            // available) so the freshly exiled spells are cast with the Apex mana. Mirrors the DrawSpell
            // stages_cards branch above (lockstep with the executor's AIEngine breakpoint re-solve).
            if (!s_human_play)
            {
                if (out_breakpoint && my_bp_sink) { sink_stack.push_back(my_bp_sink); }
                TurnSolver::Plan extra;
                if (!bp_searched_plan(2, extra))
                {
                    play_breakpoint_land(my_bp_sink);
                    extra = TurnSolver::Solve(state, is_pre_combat);
                }
                bp_play_searched_land(extra, my_bp_sink);
                // Lotus Bloom: apply (and RECORD) any SacForMana / Suspend the re-solve chose BEFORE the
                // casts, exactly as the top-level ApplyPlanDirect / TakeTurn pre-pass does. apply_plan_actions
                // handles only vial/hand/sac-land/graveyard casts, so a mid-turn Lotus sac would otherwise be
                // dropped -- the staged Dragonstorm/rituals then can't pay the floated mana (the executor's
                // breakpoint replay had the same gap). Recording into the current sink keeps the committed-line
                // replay (AIEngine::replay_recorded) in lockstep. Empty for every plan without a SacForMana.
                for (const Action& a : extra.actions)
                {
                    if (a.kind == Action::Kind::SacForMana)
                    {
                        ApplySacForMana(state, state.active_player_index, a.sac_source_id,
                                        a.chosen_float_color, a.ritual_float, a.sac_victim_id);
                        if (out_breakpoint && !sink_stack.empty()) { sink_stack.back()->push_back(a); }
                    }
                    else if (a.kind == Action::Kind::Suspend)
                    {
                        ApplySuspend(state, state.active_player_index, a.card_name);
                        if (out_breakpoint && !sink_stack.empty()) { sink_stack.back()->push_back(a); }
                    }
                }
                apply_plan_actions(extra.actions, extra.searched_order);
                if (out_breakpoint && my_bp_sink) { sink_stack.pop_back(); }
            }
        }
        else if (def.params.destroy_all_enchantments)
        {
            DestroyAllEnchantments(state);
        }
        else if (def.tmpl == CardTemplate::PumpSpell)
        {
            // "+N/+M until end of turn" (Invigorate) on the controller's best attacker, so the
            // rollout's combat reflects the pump. Mirrors EffectHandler::ResolvePumpSpell.
            int ti = def.params.target_own_creature
                     ? FindBestOwnAttacker(state, state.active_player_index)
                     : -1;
            // Human-play: let the player pick which of their creatures to pump (default = the best
            // attacker). Only surface it when there's a genuine choice (>= 2 own creatures). The
            // chooser is nulled by RevealLogPause for the search/rollout, so this stays byte-identical
            // there (the heuristic best-attacker pick is used).
            if (g_play_target_chooser && def.params.target_own_creature && ti >= 0)
            {
                int own_creatures = 0;
                for (const Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index) { continue; }
                    const CardDefinition* dd = CardDatabase::Instance().LookupCached(p.card);
                    if (dd && dd->card.IsCreature()) { ++own_creatures; }
                }
                if (own_creatures >= 2)
                {
                    std::vector<ChosenTarget> heur = { { 1, ti, 0 } };
                    std::vector<ChosenTarget> picked =
                        (*g_play_target_chooser)(state, def, state.active_player_index, 1, 0, heur);
                    if (!picked.empty() && picked[0].kind == 1) { ti = picked[0].index; }
                }
            }
            if (ti >= 0)
            {
                state.battlefield[ti].temp_power_bonus += def.params.power_bonus;
                state.battlefield[ti].temp_tough_bonus += def.params.tough_bonus;
            }
        }
        else if (!def.card.IsInstant() && !def.card.IsSorcery())
        {
            // Non-creature permanent (e.g. Aether Vial, Tainted Remedy, Aria of Flame): place
            // on battlefield, then apply any ETB "each opponent gains N life" (Aria of Flame)
            // -> reversed to damage by a Tainted Remedy / Plague Drone.
            Permanent perm;
            perm.card              = def.card;
            perm.card.m_number     = cast_number;   // preserve per-copy ID (mirror EffectHandler)
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            // Aura (Bogles): attach to the searched creature (enchant_target), then fire Light-Paws.
            // Set aura_attached_to BEFORE PerformLightPawsAttach push_backs the fetched aura. Lockstep
            // with EffectHandler's executor enchantment-enter branch.
            if (def.params.is_aura)
            {
                state.battlefield.back().aura_attached_to =
                    ResolveEnchantTarget(state, state.active_player_index, enchant_target);
                PerformLightPawsAttach(state, state.active_player_index,
                                       def.card.m_mana_cost.ManaValue(),
                                       g_bp_trace_arm ? "APPLY" : "rollout");
            }
            if (def.params.etb_opponent_lifegain > 0)
            {
                OpponentGainsLife(state, state.active_player_index, def.params.etb_opponent_lifegain);
            }
            // Legend rule (CR 704.5j, a state-based action) for a legendary NON-creature permanent
            // too, so this branch matches both the creature branch above and the executor's single
            // post-dispatch site (EffectHandler::Resolve). No legendary non-creature permanent
            // exists in any deck today, so this is inert -- it is here so the two sides cannot
            // silently disagree the day one is added.
            if (def.card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }
        }

        // (On-cast triggers + Prowess already fired above, at cast time, before the
        // resolution branch -- see the note there.)

        // A resolved instant or sorcery goes to the graveyard (mirrors the real game's
        // MoveToGraveyard). This makes a retrace card recur and keeps the inline
        // graveyard faithful; nothing reads the graveyard for decks without retrace.
        if (def.card.IsInstant() || def.card.IsSorcery())
        {
            ap.graveyard.push_back(def.card);
        }

        if (is_sacrifice)
        {
            // Heuristic default: sacrifice a tapped land if any (keeps untapped mana available),
            // else the first land. Under --claude-play the human picks WHICH land (Shard Volley's
            // additional cost); g_play_sacrifice_chooser is nulled by RevealLogPause for the
            // rollout/search, so autonomous play keeps this pick and stays byte-identical.
            // WHICH land is provider-owned (SacrificeLandCandidates); the base rule is the
            // historical "first tapped land if any, else the first land", so this is byte-identical.
            std::vector<int> lands;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
                lands.push_back(i);
            }
            int idx = -1;
            if (!lands.empty())
            {
                const std::vector<int> ranked = ResolveProvider(state).SacrificeLandCandidates(
                    state, state.active_player_index, lands);
                if (!ranked.empty()) { idx = ranked.front(); }
            }
            if (g_play_sacrifice_chooser && lands.size() > 1 && idx >= 0)
            {
                int def_opt = 0;
                for (int k = 0; k < static_cast<int>(lands.size()); ++k) { if (lands[k] == idx) { def_opt = k; break; } }
                int chosen = (*g_play_sacrifice_chooser)(state, state.active_player_index, name, lands, def_opt);
                if (chosen >= 0 && chosen < static_cast<int>(lands.size())) { idx = lands[chosen]; }
            }
            if (idx >= 0)
            {
                ap.graveyard.push_back(state.battlefield[idx].card);
                state.battlefield.erase(state.battlefield.begin() + idx);
            }
        }
    };

    // Canonical execution order, applied within each kind:
    //   ActivateVial -> hand casts (non-sacrifice, in provider CastOrderRank order)
    //   -> hand casts (sacrifice-land) -> graveyard casts (Retrace).
    // The non-sacrifice hand casts are stable-sorted by DecisionProvider::CastOrderRank
    // (enabler-first, prowess creatures before noncreature spells, on-cast self-damage
    // sources last); see the canonical branch below. Byte-identical for a deck whose ranks
    // don't reorder its casts.
    apply_plan_actions = [&](const std::vector<Action>& acts, bool explicit_order)
    {
        // Indices of sac-land casts already applied by the Spectacle hoist (opaque branch); the
        // trailing sac loop skips them so they are not double-cast. Empty for every plan without a
        // hoisted Spectacle enabler.
        std::set<size_t> spec_hoisted_sac;
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::ActivateVial) { apply_vial(a.card_name); }
        }
        if (explicit_order)
        {
            // Cast-ordering search: play the non-sacrifice hand casts in the EXACT vector
            // order the search chose (no enabler-first bucketing), so interleavings the
            // canonical order batches wrong are reachable. See Plan::searched_order.
            for (const Action& a : acts)
            {
                if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
                {
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target);
                }
            }
        }
        else
        {
            // Reorder the non-sacrifice hand casts by CastOrderRank, EXCEPT when the set has
            // a re-solve breakpoint card (draw/staging/cascade): its ordering is search-owned
            // (Light Up the Stage can't be ordered optimally without search), so keep the
            // canonical enabler-first + plan order there. The rank encodes the full-search-
            // grounded ordering rules (prowess creatures early, on-cast self-damage sources
            // last, ...) and grows as analysis surfaces more; d0 imperfection is acceptable.
            // Definitive validation is the with/without-heuristic per-game A/B.
            bool opaque = false;
            for (const Action& a : acts)
            {
                if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land
                    && OrderingOpaque(a.card_name)) { opaque = true; break; }
            }
            auto is_enabler = [&](const Action& a)
            {
                return a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !a.alt_cost
                    && ResolveProvider(state).CastEnablerFirst(state, a.card_name);
            };
            if (opaque)
            {
                for (const Action& a : acts)
                { if (is_enabler(a)) { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); } }
                // Spectacle hoist: a sac-land damage source (Shard Volley) is otherwise cast in the
                // trailing sac loop -- AFTER the non-sac Spectacle spell (Light Up), leaving
                // Spectacle un-triggered and Light Up paying full cost. When the set holds a
                // not-yet-active Spectacle spell, cast such sac-land damage enablers HERE (before
                // the non-sac casts) so Light Up unlocks its reduced cost, and mark them so the
                // trailing sac loop skips them. Only the 2-card {burn, Light Up} spectacle plans
                // pair a sac-land burn with Light Up (the general powerset never affords Light Up
                // at full cost), so this touches no other line / has no prowess interaction. Inert
                // unless a Spectacle spell is present -> non-burn byte-identical.
                bool spec_needed = !state.opponent_lost_life_this_turn;
                if (spec_needed)
                {
                    bool has_spec = false;
                    for (const Action& a : acts)
                    { if (a.kind == Action::Kind::CastFromHand && a.has_spectacle) { has_spec = true; break; } }
                    spec_needed = has_spec;
                }
                for (size_t ai = 0; spec_needed && ai < acts.size(); ++ai)
                {
                    const Action& a = acts[ai];
                    if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land && a.direct_damage > 0)
                    {
                        apply_one(a.card_name, true, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target);
                        spec_hoisted_sac.insert(ai);
                    }
                }
                for (const Action& a : acts)
                {
                    if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !is_enabler(a))
                    { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); }
                }
            }
            else
            {
                // Clean set: stable-sort the non-sacrifice hand casts by CastOrderRank
                // (enabler-first, prowess creatures before noncreature spells, on-cast
                // self-damage sources last). Stable => plan order breaks ties. Mirrored in
                // AIEngine::TakeTurn so rollout and executor stay in lockstep.
                std::vector<int> order;
                for (int i = 0; i < static_cast<int>(acts.size()); ++i)
                {
                    if (acts[i].kind == Action::Kind::CastFromHand && !acts[i].sacrifice_land)
                    { order.push_back(i); }
                }
                std::stable_sort(order.begin(), order.end(), [&](int x, int y)
                { return CastOrderLess(state, acts[x], acts[y]); });
                for (int i : order)
                {
                    const Action& a = acts[i];
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target);
                }
            }
        }
        for (size_t ai = 0; ai < acts.size(); ++ai)
        {
            if (spec_hoisted_sac.count(ai)) { continue; }   // already cast by the Spectacle hoist
            const Action& a = acts[ai];
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            {
                apply_one(a.card_name, true, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target);
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromGraveyard)
            {
                apply_one(a.card_name, false, true, a.discard_lands, false, 0, std::string{}, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target);
            }
        }

        // Discard-to-Land's-Edge activations the human/search committed as an explicit plan
        // action (Kind::DiscardToLandsEdge). In autonomous play Land's Edge is auto-fired by the
        // post-cast heuristic loop below (suppressed under s_human_play); here we apply the
        // EXACT count the committed plan carries -- discard `discard_lands` lands from hand and
        // deal `rate` per land, where rate is the best discard_land_damage among controlled
        // permanents. Fired after all casts (it is a post-stack main-phase activation), in plan
        // order; a count > lands-in-hand simply fires every land present.
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::DiscardToLandsEdge || a.discard_lands <= 0) { continue; }
            int le_rate = 0;
            for (const Permanent& p : state.battlefield)
            {
                if (p.controller_index != state.active_player_index) { continue; }
                auto le_def = CardDatabase::Instance().LookupCached(p.card);
                if (le_def && le_def->params.discard_land_damage > 0)
                { le_rate = std::max(le_rate, le_def->params.discard_land_damage); }
            }
            if (le_rate <= 0) { continue; }   // no Land's Edge in play -> nothing to fire
            Player& le_ap = state.ActivePlayer();
            const int le_life_before = state.players[opp_idx].life;   // play-viewer "before->after"
            // Same provider ranking as the executor (LandsEdgePitchOrder) -- these two must model
            // identical Land's Edge damage or the rollout's projection and the realised game
            // diverge, which is the classic executor/rollout lockstep failure.
            const std::vector<int> le_pitch =
                LandsEdgePitchOrder(state, state.m_required_pieces, a.discard_lands);
            std::vector<char> le_burn(le_ap.hand.size(), 0);
            for (int bi : le_pitch) { le_burn[static_cast<std::size_t>(bi)] = 1; }
            std::vector<Card> keep;
            int fired = 0, le_idx = -1;
            for (Card& c : le_ap.hand)
            {
                ++le_idx;
                bool is_land = le_burn[static_cast<std::size_t>(le_idx)] != 0;
                if (is_land && fired < a.discard_lands)
                {
                    le_ap.graveyard.push_back(c);
                    state.players[opp_idx].life -= le_rate;
                    state.opponent_lost_life_this_turn = true;
                    ++fired;
                }
                else { keep.push_back(std::move(c)); }
            }
            le_ap.hand = std::move(keep);
            // Play-viewer history: one "burn" event for this Land's Edge activation (fired `fired`
            // lands x le_rate). Nulled by RevealLogPause during search/rollout -> byte-identical.
            if (g_play_event_sink && fired > 0)
            {
                const int after = state.players[opp_idx].life;
                EmitPlayEvent(state.turn_number, "damage",
                              "\xF0\x9F\x94\xA5 Land's Edge: " + std::to_string(le_life_before - after)
                              + " to opponent (" + std::to_string(fired) + " land"
                              + (fired == 1 ? "" : "s") + " discarded, "
                              + std::to_string(le_life_before) + "\xE2\x86\x92" + std::to_string(after) + ")");
            }
        }

        // Human-play (claude-play) player-initiated dig: cycle a land from hand (Lonely Sandbar /
        // Forgotten Cave) or sacrifice a land in play (Fiery Islet) to draw one card. The autonomous
        // search drives digs through its own ShouldConsiderDig loop above (gated !s_human_play); here
        // the human picks each dig as a standalone plan action and the AIEngine segment loop
        // re-prompts after the draw (the library shrank), so the player uses the dug card or digs
        // again. One card per action. Mirrors the auto-loop's cost/zone mechanics exactly. Gated on
        // s_human_play; the search never puts DigDraw in plan.actions, so this is inert there.
        if (s_human_play)
        for (const Action& a : acts)
        {
            if (a.kind != Action::Kind::DigDraw) { continue; }
            const CardDefinition* sd = CardDatabase::Instance().Lookup(a.card_name);
            if (!sd) { continue; }
            if (a.dig_sacrifice)
            {
                if (!sd->params.sacrifice_draw_cost.has_value()) { continue; }
                int idx = -1;
                for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
                {
                    const Permanent& p = state.battlefield[i];
                    if (p.controller_index == state.active_player_index
                        && !p.tapped && p.card.m_name == a.card_name) { idx = i; break; }
                }
                if (idx < 0) { continue; }
                state.battlefield[idx].tapped = true;   // {T}: the source can't pay its own cost
                if (!TapForCostDirect(state, sd->params.sacrifice_draw_cost.value(), false))
                { state.battlefield[idx].tapped = false; continue; }
                ap.graveyard.push_back(state.battlefield[idx].card);
                state.battlefield.erase(state.battlefield.begin() + idx);
            }
            else
            {
                if (!sd->params.cycling_cost.has_value()) { continue; }
                if (!TapForCostDirect(state, sd->params.cycling_cost.value(), false)) { continue; }
                std::vector<Card>::iterator it = std::find_if(ap.hand.begin(), ap.hand.end(),
                    [&a](const Card& c) { return c.m_name == a.card_name; });
                if (it == ap.hand.end()) { continue; }
                ap.graveyard.push_back(*it);
                ap.hand.erase(it);
            }
            if (!ap.library.empty())
            {
                Card drawn = ap.library.DrawTop();
                // Human-play accurate draw reporting (nulled by RevealLogPause during search).
                if (g_play_draw_sink) { g_play_draw_sink->push_back({ state.turn_number, drawn.m_name.str() }); }
                ap.hand.push_back(std::move(drawn));
            }
        }

        // Auto-fire safe alt payloads (Invigorate / Skyshroud) once everything else has
        // resolved and a Remedy is live -> each is free face damage. Deterministic (not a
        // search choice), so no enumeration blow-up; re-scan after each because firing one
        // mutates the hand (and can add a verse trigger). No-op for decks without alt cards.
        // Hard termination guard: each pass must REMOVE the chosen card from hand; if a cast
        // does not (a fizzled/uncastable alt), stop -- never spin on the same card.
        // SUPPRESSED under UNPRUNED: there the safe alt is enumerated as a real cast choice
        // (CollectActions), so the search/human decides whether to fire it -- auto-firing it
        // here too would double-cast it AND override that decision.
        if (!DecisionUnpruned(UnprunedGate::AltPayload))
        for (;;)
        {
            Player& ap2 = state.ActivePlayer();
            int target = -1; int amt = 0;
            for (int i = 0; i < static_cast<int>(ap2.hand.size()); ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(ap2.hand[i]);
                if (d && ResolveProvider(state).CanAutoFireAltPayload(state, state.active_player_index, *d))
                { target = i; amt = d->params.alt_lifegain_cost; break; }
            }
            if (target < 0) { break; }
            std::string nm = ap2.hand[target].m_name;
            size_t before = ap2.hand.size();
            apply_one(nm, false, false, 0, true, amt, std::string{}, 0, 0, -1, -1, 0, std::string{}, 0);
            if (state.ActivePlayer().hand.size() >= before) { break; }   // didn't consume -> stop
        }
    };

    // Arm the greedy-stranding audit for this turn's main casts (see the audit_* declarations).
    // Sum every mana-paying hand cast's cost into audit_combined and snapshot the pre-cast board.
    // Skip entirely if any planned action PRODUCES mana (ritual/rock): those break the "all sources
    // simultaneously available" assumption the combined-sum feasibility test relies on.
    // Lotus Bloom: apply SacForMana (float the chosen colour into the reserve) and Suspend BEFORE the
    // batch pre-pay / casts, so the floated mana funds this turn's payoff. BatchPrepay declines when any
    // action produces float (a.ritual_float > 0, which SacForMana sets), so the greedy per-cast path --
    // which spends floating first -- realises the combo. Mirrors AIEngine::TakeTurn (lockstep). Both
    // loops are empty for every deck without a Lotus (no SacForMana/Suspend action) -> byte-identical.
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::SacForMana)
        { ApplySacForMana(state, state.active_player_index, a.sac_source_id, a.chosen_float_color, a.ritual_float, a.sac_victim_id); }
        else if (a.kind == Action::Kind::Suspend)
        { ApplySuspend(state, state.active_player_index, a.card_name); }
    }

    // Whole-turn batch pre-payment: tap for the combined cost of the main hand casts and pre-load
    // floating (see BatchPrepayMainCasts). The main casts below then drain the pool -- scarce colours
    // allocated jointly, filters fed, unneeded sources left up -- instead of the stranding per-cast
    // greedy. Declined turns leave state untouched and fall through to the identical greedy path.
    TurnSolver::BatchPrepayMainCasts(state, plan.actions);

    apply_plan_actions(plan.actions, plan.searched_order);

    // Krenko, Mob Boss taps AFTER the main casts, so X = Goblins you control counts this turn's
    // developed board (the tokens then count toward Skirk fuel / a later attack). Free ({T} only).
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::TapForTokens)
        { ApplyTapForTokens(state, state.active_player_index, a.sac_source_id); }
    }

    // Costed sac outlets (Siege-Gang / Pashalik) + Twinshot channel: pay the mana cost from the pool
    // left after the main casts (TapForCostDirect, the rollout pay path), then realise the effect.
    // If the cost can't be paid (mana stranded), the outlet is a no-op -- the leaf/executor share this
    // same apply, so the win projection matches realisation (no fd-diverge from a phantom outlet).
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::SacCreatureOutlet)
        {
            if (TapForCostDirect(state, a.cost, /*for_creature=*/false))
            {
                if (a.sac_count > 1)
                { ApplySacCreatureOutletBurst(state, state.active_player_index, a.sac_source_id, a.sac_count); }
                else
                { ApplySacCreatureOutlet(state, state.active_player_index, a.sac_source_id, a.sac_victim_id); }
            }
        }
        else if (a.kind == Action::Kind::Channel)
        {
            if (TapForCostDirect(state, a.cost, /*for_creature=*/false))
            { ApplyChannel(state, state.active_player_index, a.hand_index, a.card_name, a.direct_damage); }
        }
    }

    // Play the deferred Karoo bounce land now -- after the main casts have tapped the lands we
    // needed, so BounceKarooLand returns a SPENT land at no tempo cost (see karoo_deferred
    // above). Done before the deferred-cantrip re-solve so the drop is taken (lands_played==1)
    // and that re-solve never plays a freshly-revealed land as the drop. AIEngine mirrors this
    // (its Karoo play sits between the main cast loop and the breakpoint replay).
    if (karoo_deferred)
    {
        karoo_deferred = false;
        PlayLandByName(state, karoo_land_name, karoo_fetch);
    }

    // Deferred plain-cantrip re-solve: run ONCE, after every main-plan cast, using only the
    // mana those casts left — the executor's post-loop breakpoint replay does exactly this,
    // so recording it into out_breakpoint (the committed breakpoint_actions) keeps the two in
    // lockstep. A cantrip cast within this re-solve has sink_stack non-empty and so re-solves
    // inline, recording into its own nested breakpoint (replayed recursively by the executor).
    if (deferred_cantrip_resolve)
    {
        deferred_cantrip_resolve = false;
        if (out_breakpoint) { sink_stack.push_back(out_breakpoint); }
        TurnSolver::Plan extra;
        if (!bp_searched_plan(3, extra))
        {
            play_breakpoint_land(out_breakpoint);
            extra = TurnSolver::Solve(state, is_pre_combat);
        }
        bp_play_searched_land(extra, out_breakpoint);
        apply_plan_actions(extra.actions, extra.searched_order);
        if (out_breakpoint) { sink_stack.pop_back(); }
    }

    // Dig when stuck (cycling / sacrifice-to-draw lands, e.g. Lonely Sandbar, Forgotten
    // Cave, Fiery Islet): spend a surplus land to draw toward action -- chiefly Treasure
    // Hunt, whose reveal refills Land's Edge ammo. Done in the rollout so the clairvoyant
    // search MODELS the dig (a Treasure Hunt dug within the horizon pulls the win earlier);
    // each dig is recorded into out_breakpoint (Kind::DigDraw, post-draw casts nested in
    // breakpoint_casts) so the executor replays the dug line verbatim rather than
    // re-solving (which drifts on land/mana state and would miss the win). Only decks with
    // a dig source enter the loop, so burn/slivers stay byte-identical. ShouldConsiderDig
    // encodes when NOT to dig (a draw engine already in hand, a retrace engine in the
    // graveyard, fewer than two lands, or Land's Edge already lethal from the hand).
    if (!s_human_play && is_pre_combat && ResolveProvider(state).HasAnyDigSource(state))
    {
        int dig_guard = 0;
        while (dig_guard++ < 16 && ResolveProvider(state).ShouldConsiderDig(state) && !ap.library.empty())
        {
            ManaPool pool = AvailableManaPool(state);
            bool is_sac = false;
            std::string src = ResolveProvider(state).SelectDigSource(state, pool, is_sac);
            if (src.empty()) { break; }
            const CardDefinition* sd = CardDatabase::Instance().Lookup(src);
            if (!sd) { break; }

            if (is_sac)
            {
                // {cost},{T},Sacrifice: tap the source first (the {T}) so it isn't its own
                // mana source, pay the remaining mana, then sacrifice it.
                int idx = -1;
                for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
                {
                    const Permanent& p = state.battlefield[i];
                    if (p.controller_index == state.active_player_index
                        && !p.tapped && p.card.m_name == src) { idx = i; break; }
                }
                if (idx < 0) { break; }
                state.battlefield[idx].tapped = true;
                if (!TapForCostDirect(state, sd->params.sacrifice_draw_cost.value(), false))
                {
                    state.battlefield[idx].tapped = false;
                    break;
                }
                ap.graveyard.push_back(state.battlefield[idx].card);
                state.battlefield.erase(state.battlefield.begin() + idx);
            }
            else
            {
                if (!TapForCostDirect(state, sd->params.cycling_cost.value(), false)) { break; }
                std::vector<Card>::iterator it = std::find_if(ap.hand.begin(), ap.hand.end(),
                    [&src](const Card& c) { return c.m_name == src; });
                if (it == ap.hand.end()) { break; }
                ap.graveyard.push_back(*it);
                ap.hand.erase(it);
            }

            // Draw one. Record EVERY dig (even a land) so the executor replays the exact
            // cycle/sacrifice sequence and stays in library/hand sync. Deck-out safe: stop digging
            // if the library is empty (can't draw from an empty library).
            if (ap.library.empty()) { break; }
            Card drawn = ap.library.DrawTop();
            const CardDefinition* ddef = CardDatabase::Instance().LookupCached(drawn);
            bool drew_land = ddef ? ddef->card.IsLand() : drawn.IsLand();
            ap.hand.push_back(std::move(drawn));

            std::vector<Action>* my_bp_sink = out_breakpoint;
            if (out_breakpoint)
            {
                Action rec;
                rec.kind          = Action::Kind::DigDraw;
                rec.card_name     = src;
                rec.dig_sacrifice = is_sac;
                out_breakpoint->push_back(rec);
                my_bp_sink = &out_breakpoint->back().breakpoint_casts;
            }

            // Dig THROUGH lands toward the first nonland (Treasure Hunt is a nonland). On a
            // land we keep digging; on a nonland we re-solve so the found action is cast
            // THIS turn (exactly like the DrawUntilNonland breakpoint), then stop -- once
            // we have action we are no longer stuck.
            if (!drew_land)
            {
                if (out_breakpoint) { sink_stack.push_back(my_bp_sink); }
                TurnSolver::Plan extra;
                if (!bp_searched_plan(4, extra)) { extra = TurnSolver::Solve(state, is_pre_combat); }
                bp_play_searched_land(extra, my_bp_sink);
                apply_plan_actions(extra.actions, extra.searched_order);
                if (out_breakpoint) { sink_stack.pop_back(); }
                break;
            }
        }
    }

    // Activate Land's Edge after all spells resolve (mirrors GameEngine::MainPhase's
    // post-stack ActivateLandsEdge call). Human play: SUPPRESSED -- the human decides whether to
    // discard lands to Land's Edge (else the engine would auto-burn the lands they just drew).
    if (!s_human_play)
    {
        int rate = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            auto le_def = CardDatabase::Instance().LookupCached(p.card);
            if (le_def && le_def->params.discard_land_damage > 0)
            {
                rate = std::max(rate, le_def->params.discard_land_damage);
            }
        }
        if (rate > 0)
        {
            // Default uses the real engine's conditional heuristic so the search does
            // not over-count the Land's Edge burst -- the gi=947 class: the search fired
            // every drawn land for opp -24 at T5 where the real engine holds them (fire
            // only for lethal / cleanup excess), slipping the win to T10. The legacy
            // baseline (MTG_LEGACY_SEARCH) fires ALL lands -- its rollouts are frozen as
            // the held-out ground truth.
            static const bool s_fd = !EnvOn("MTG_LEGACY_SEARCH");
            int fire_count = s_fd ? ResolveProvider(state).LandsEdgeFireCount(state, rate)
                                  : std::numeric_limits<int>::max();

            std::vector<Card> keep;
            int fired = 0;
            for (Card& c : ap.hand)
            {
                auto cdef    = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
                if (is_land && fired < fire_count)
                {
                    ap.graveyard.push_back(c);
                    state.players[opp_idx].life -= rate;
                    state.opponent_lost_life_this_turn = true;
                    ++fired;
                }
                else { keep.push_back(std::move(c)); }
            }
            ap.hand = std::move(keep);
        }
    }

    // Grove of the Burnwillows drip: once a Remedy is live, tap any still-untapped Grove for its
    // free 1-damage ping even with nothing to cast (see TapDripLandsIfUseful). After all casts so
    // a spell that needed Grove's mana tapped it first; once per turn (pre-combat main only). The
    // real executor (AIEngine::TakeTurn) calls the same helper at the same point -> lockstep.
    if (is_pre_combat) { TapDripLandsIfUseful(state, state.active_player_index); }

    // Sacrifice depletion lands (e.g. Saprazzan Skerry) exhausted by this turn's taps.
    SacrificeDepletedLands(state);
}

// Deal combat damage: all eligible attackers hit the opponent.
static void SimulateCombat(GameState& state)
{
    // Mana empties when leaving the pre-combat main phase (CR 500.4): drop any reserve
    // floated this main phase so it cannot fund combat or the post-combat main. Mirrors
    // GameEngine::CombatPhase. Off (MTG_NO_FLOAT_LEFTOVER) -> no-op (pool only ever held
    // ritual float, which was already spent this main phase -> byte-identical regardless).
    if (FloatLeftoverManaEnabled()) { state.floating_mana = ManaPool{}; }
    int active  = state.active_player_index;

    // Legend rule before declaring attackers (mirror GameEngine::CombatPhase): a duplicate
    // legendary lord cannot double-count its buff. No-op without legendaries.
    EnforceLegendRule(state, active);

    // Eligible attacker indices BEFORE any token creation (push_back keeps indices stable).
    std::vector<int> atk_idx = DeclareAttackerIndices(state);

    // Attack-trigger tokens (Adeline), tapped and attacking this combat, then persist.
    if (!atk_idx.empty())
    {
        int tok_start = FireAttackCreateTokens(state, active);
        for (int i = tok_start; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            atk_idx.push_back(i);
        }
    }

    // Goblins attack-trigger self-pumps (Piledriver +2/+0 per other attacking Goblin; Muxus +1/+1
    // per other Goblin you control): applied at declare-attackers BEFORE the damage loop reads power,
    // as until-end-of-turn temp bonuses. Mirrors GameEngine::CombatPhase (executor). Gated inert.
    ApplyAttackSelfPumps(state, active, atk_idx);

    // Exalted (Ignoble Hierarch): +1/+1 per Exalted ability to a creature attacking ALONE.
    int exalted_bonus = (static_cast<int>(atk_idx.size()) == 1)
                        ? CountExalted(state.battlefield, active) : 0;

    // Firebreathing (Scourge {R}:+1/+0 self, Lathliss {1}{R}: Dragons +1/+0 team): spend LEFTOVER
    // combat mana on attacker pumps BEFORE the damage loop reads their power. Shared with the
    // executor (AIEngine::Firebreathe) on the byte-identical AvailableManaPool pool -> lockstep.
    // Inert unless a firebreathing param is present -> other decks byte-identical.
    if (!atk_idx.empty() && ControlsFirebreathingSource(state, active))
    {
        // Provider-owned count (FirebreatheActivations), the SAME hook the executor reads, so the
        // rollout never pumps to a different budget than the realised combat. Negative = greedy max
        // (the default) -> byte-identical.
        const std::vector<int> fb = ResolveProvider(state).FirebreatheActivations(state);
        const int fb_k = fb.empty() ? -1 : fb.front();
        if (fb_k < 0) { ApplyFirebreathing(state, active, atk_idx, AvailableManaPool(state)); }
        else          { ApplyFirebreathing(state, active, atk_idx, AvailableManaPool(state), fb_k); }
    }

    // Damage, attack triggers, Utvara tokens and the Goblin Lackey cheat are shared with the
    // executor (ResolveCombatDamage, Combat.cpp). The rollout wants no play-viewer descriptions.
    ResolveCombatDamage(state, atk_idx, exalted_bonus, /*collect_descs=*/false);
}


// End-of-turn cleanup + start of next turn (untap, draw).
// Returns false if the player lost on draw (empty library).
static bool SimulateEndAndStartNextTurn(GameState& state)
{
    Player& ap = state.ActivePlayer();

    // Check for "no maximum hand size" permanent (e.g. Reliquary Tower) — if present,
    // skip the discard-to-7 step so the lookahead correctly models turns after RT is played.
    bool unlimited_hand = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }

    // Discard down to hand size 7. Default mirrors AIEngine::ChooseDiscard: when a
    // Land's Edge outlet exists (the lands are ammunition, the spells are the combo)
    // shed a LAND first; otherwise shed the highest-MV card. The legacy baseline
    // (MTG_LEGACY_SEARCH) keeps the highest-MV-only rule (frozen as the held-out ground
    // truth). Without this the search kept every drawn land as Land's Edge ammo while
    // the real game discards lands here, over-counting Land's Edge damage (gi=947).
    static const bool s_fd_discard = !EnvOn("MTG_LEGACY_SEARCH");

    // STAGED cards do NOT count toward maximum hand size (CR 514.1 counts the HAND; a card exiled
    // with "you may play it until ..." -- Soulfire Eruption, Light Up the Stage, Expressive
    // Iteration, an Apex land -- is in EXILE, not hand). The engine models those as hand cards
    // flagged m_is_staged purely so the castable-set code can see them; the real executor moves
    // them back out to Player::staged_cards at the end of AIEngine::TakeTurn, so by the time
    // GameEngine::CleanupStep runs they are simply not in hand and can never be shed here.
    // The rollout keeps them in ap.hand for its whole lookahead, so counting them made it discard
    // cards the real game keeps -- a pure rollout/executor divergence (Hinata seed 4010: the T5
    // Soulfire dig staged 8 cards into an otherwise-empty hand, the rollout shed one at cleanup and
    // planned turn 6 a card short, so the realised turn missed the lethal Crackle X by one mana:
    // predicted T6, realised T7). Count and shed only NON-staged cards -> exact parity with
    // CleanupStep. Hatch: MTG_LEGACY_STAGED_HANDLIMIT restores the old counting.
    static const bool s_staged_exempt = !EnvOn("MTG_LEGACY_STAGED_HANDLIMIT");
    auto hand_count = [&]() {
        if (!s_staged_exempt) { return ap.hand.size(); }
        size_t n = 0;
        for (const Card& c : ap.hand) { if (!c.m_is_staged) { ++n; } }
        return n;
    };
    while (!unlimited_hand && hand_count() > 7)
    {
        // Default (commit-the-line): use the SHARED selector so the rollout sheds exactly the
        // card the real engine's ChooseDiscard would -- required-piece protection + land-outlet
        // ammo, reading the deck's pieces from state.m_required_pieces. Without this the rollout
        // shed high-MV spells and hoarded lands, predicting a phantom Land's Edge flood (gi=220).
        // Legacy (MTG_LEGACY_SEARCH): the frozen highest-MV-only rule (held-out ground truth).
        std::vector<Card>::iterator victim;
        if (s_fd_discard)
        {
            // Provider-owned rule (CleanupDiscardCandidates); index 0 is the ranked pick, which is
            // the single answer SelectCleanupDiscardIndex used to return directly.
            const std::vector<int> cd =
                ResolveProvider(state).CleanupDiscardCandidates(state, state.m_required_pieces);
            if (cd.empty()) { break; }
            // SEARCHED cleanup discard (Plan::discard_choice -> GameState::scripted_discard_choice):
            // the plan's pinned candidate, consumed by the FIRST shed of this cleanup and cleared,
            // so a multi-card cleanup sheds one searched card and then falls back to the ranked
            // default -- the same one-per-plan convention as the ETB-dig and Lackey axes. Clamped,
            // because the plan was enumerated before this turn's draws and casts changed the hand.
            std::size_t pick = 0;
            if (state.scripted_discard_choice > 0)
            {
                pick = std::min(static_cast<std::size_t>(state.scripted_discard_choice),
                                cd.size() - 1);
            }
            state.scripted_discard_choice = -1;
            victim = ap.hand.begin() + cd[pick];
        }
        else
        {
            victim = std::max_element(ap.hand.begin(), ap.hand.end(),
                [](const Card& a, const Card& b)
                {
                    return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
                });
        }
        // Both selectors may still land on a staged card (SelectCleanupDiscardIndex sheds one as a
        // last resort when every non-staged card is a required piece; max_element ignores the flag).
        // The count above proves a non-staged card exists, so fall back to the first one rather than
        // shedding something the real cleanup could not reach.
        if (s_staged_exempt && victim != ap.hand.end() && victim->m_is_staged)
        {
            victim = std::find_if(ap.hand.begin(), ap.hand.end(),
                                  [](const Card& c) { return !c.m_is_staged; });
            if (victim == ap.hand.end()) { break; }
        }
        ap.graveyard.push_back(*victim);
        ap.hand.erase(victim);
    }

    // Reset per-turn damage marks, "until end of turn" power/toughness boosts, and
    // animation effects (CR 514.2) — mirrors GameEngine::CleanupStep. Resetting the
    // temp_*_bonus fields is essential: without it prowess (and any until-end-of-turn
    // buff) accumulates across rollout turns, so the clairvoyant rollout over-counts a
    // prowess creature's combat damage on later turns and predicts a phantom early win
    // the real game (which clears the bonus each cleanup) never reaches. That mismatch
    // was the root cause of non-convergence on the burn deck (Monastery Swiftspear).
    for (Permanent& p : state.battlefield)
    {
        p.damage                = 0;
        p.pending_death_trigger = 0;   // delayed Searing Blood trigger expires with the damage marks
        p.temp_power_bonus      = 0;
        p.temp_tough_bonus      = 0;
        p.is_animated           = false;
    }

    // Storage-counter lands (Dwarven Hold, Mercadian Bazaar): bank +1 on any storage land left UNTAPPED
    // this turn (idle = not burst). Faithful goldfish model of both charge modes; mirrors the executor
    // (GameEngine::CleanupStep) exactly so the rollout's accumulated burst matches the real game (lockstep).
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.storage_land) { ++p.storage_counters; }
    }

    // Start of next turn
    ++state.turn_number;
    state.opponent_lost_life_this_turn = false;
    state.floating_mana            = ManaPool{};   // reserve (ritual) mana empties each turn (CR 500.4)
    state.spells_cast_this_turn   = 0;             // STORM counter resets each turn (lockstep w/ GameEngine::UntapStep)
    state.casts_remaining_this_turn = -1;          // Irencrag "one more spell" budget clears each turn (see GameState)
    state.scripted_cheat_choice   = -1;            // searched Lackey put is per-turn (lockstep w/ GameEngine::UntapStep)
    ap.lands_played_this_turn     = 0;
    ap.bonus_land_drops_this_turn = 0;

    // Untap and advance Aether Vial counters (upkeep trigger).
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index)
        {
            p.tapped            = false;
            p.entered_this_turn = false;
        }
    }
    // Materialise any passive opponent creatures scheduled for this turn, mirroring
    // GameEngine::StartTurnStep. Without this the rollout never sees the opponent's
    // board, so creature-targeted burn (Searing Blaze / Searing Blood) is wrongly
    // treated as having no legal target: the search undervalues those cards and misses
    // lines that use them. Token shape/flags match GameEngine exactly so the rollout's
    // board == the real game's.
    //
    // ON for the default (commit-the-line) engine; OFF under MTG_LEGACY_SEARCH.
    // Measured rationale, not a punt: BASELINE SolveWithLookahead re-decides every turn
    // against the REAL board, so it already handles opponent creatures where it matters
    // (the actual play) and gains NOTHING from modelling them in its rollout -- enabling
    // it for baseline left burn/slivers' fingerprints unchanged and only perturbed 3
    // games via rollout/bottoming noise (burn gi=278 5->6; th d3 s2002 gi=72 4->5, gi=97
    // 5->6), all slightly worse, 0 better. Only commit-the-line, which REPLAYS the
    // search's line and cannot re-decide, actually needs the rollout's board accurate.
    static const bool s_fd_opp_spawns = !EnvOn("MTG_LEGACY_SEARCH");
    if (s_fd_opp_spawns && state.opponent_spawns)
    {
        int opp_index = 1 - state.active_player_index;
        for (const OpponentSpawn& spawn : *state.opponent_spawns)
        {
            if (spawn.turn != state.turn_number) { continue; }
            Card token;
            token.m_name      = std::to_string(spawn.power) + "/"
                              + std::to_string(spawn.toughness) + " Creature";
            token.RehashName();
            token.AddType(CardType::Creature);
            token.m_power     = spawn.power;
            token.m_toughness = spawn.toughness;
            Permanent perm;
            perm.card             = token;
            perm.controller_index = opp_index;
            perm.owner_index      = opp_index;
            state.battlefield.push_back(perm);
        }
    }

    // Forbidden Orchard: one opponent 1/1 Spirit per Orchard the active player controls this turn
    // (assume each is tapped for mana). Mirrors GameEngine (executor) at the same turn-start point
    // -> lockstep. Gated by s_fd_opp_spawns so MTG_LEGACY_SEARCH stays byte-identical to the old model.
    if (s_fd_opp_spawns) { SpawnForbiddenOrchardTokensTurnStart(state); }

    // Suspend (Lotus Bloom): cast off suspend any card whose last time counter is removed at THIS
    // upkeep (arrive_turn <= turn). Runs after untap so the arrived artifact is untapped and available
    // this turn; placed here to mirror GameEngine::UpkeepStep's arrival-before-vial ordering (lockstep).
    // No-op for every deck without a suspended card -> byte-identical.
    ProcessSuspendArrivals(state, state.active_player_index);

    Player& ap_upkeep = state.ActivePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.upkeep_adds_charge) { continue; }
        // Hand-aware charge policy, shared with the real engine (AIEngine::DecideVialCharge)
        // so the rollout models the same charge the executor will make.
        if (ResolveProvider(state).WantVialCharge(state, p)) { ++p.charge_counters; }
    }

    // Upkeep token creation (e.g. Thrumming Hivepool). Iterate over initial size only.
    int upkeep_bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < upkeep_bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != state.active_player_index) { continue; }
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!def || def->params.upkeep_creates_tokens <= 0) { continue; }
        for (int t = 0; t < def->params.upkeep_creates_tokens; ++t)
        {
            CreateToken(state, state.active_player_index,
                        def->params.upkeep_token_power,
                        def->params.upkeep_token_toughness,
                        def->params.upkeep_token_subtypes);
        }
    }

    // Echo (Mogg War Marshal {1}{R}, Stingscourger {3}{R}): "At the beginning of your upkeep, if this
    // came under your control since your last upkeep, sacrifice it unless you pay its echo cost."
    // (CR 702.29). The FIRST upkeep an echo permanent's controller takes after it entered resolves the
    // obligation (Permanent::echo_resolved flips true here regardless of outcome, so no later upkeep
    // re-charges it). Deterministic heuristic (the search need not branch echo): a creature whose OWN
    // death makes a replacement token (Mogg War Marshal -- dies_watch_includes_self + a death token)
    // DECLINES -- sacrificing it fires OnCreatureDies for a fresh 1/1, net the same body while saving
    // the mana; any other echo creature (Stingscourger) PAYS if it can afford the cost, else is
    // sacrificed. This is the rollout/search world; it mirrors the executor block at the top of
    // AIEngine::TakeTurn using TapForCostDirect (the byte-identical mirror of AIEngine::TapForCost)
    // and OnCreatureDies -> lockstep. Gated on echo_cost, so it is a no-op for every non-echo deck.
    for (std::size_t i = 0; i < state.battlefield.size(); )
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index || p.echo_resolved)
        { ++i; continue; }
        const CardDefinition* edef = CardDatabase::Instance().LookupCached(p.card);
        if (!edef || !edef->params.echo_cost) { ++i; continue; }
        p.echo_resolved = true;   // obligation resolved this upkeep, whatever the outcome
        const CardParams& ep = edef->params;
        // Pay-vs-decline JUDGEMENT is provider-owned (PayEchoToKeep) -- the SAME decision the executor
        // (AIEngine) uses, so autonomous play and this rollout stay in lockstep. Default reproduces the
        // old fixed heuristic; GoblinsProvider adds the Mogg lethal/no-gas keep. TapForCostDirect still
        // gates on real affordability (returns false if it cannot pay), so an unaffordable keep sacrifices.
        bool kept = false;
        if (ResolveProvider(state).PayEchoToKeep(state, p))
        {
            // Pay the echo to keep the body (taps real lands -> unavailable for this turn's plays).
            kept = TapForCostDirect(state, *ep.echo_cost, /*for_creature=*/false);
        }
        if (kept) { ++i; continue; }
        // Declined or unaffordable -> sacrifice; fire OnCreatureDies (Mogg's death token, etc.).
        const Card dead = p.card;
        const int  ctrl = p.controller_index;
        state.players[ctrl].graveyard.push_back(dead);
        state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
        OnCreatureDies(state, ctrl, dead);   // may append a token at the end -> safe (no echo_cost)
        // Do not advance i: the erased slot now holds the next permanent.
    }

    // Draw
    if (ap.library.empty()) { return false; }
    ap.hand.push_back(ap.library.DrawTop());
    return true;
}

// Play a specific named land from hand onto the battlefield, resolving its
// enters-tapped / depletion / scry effects. Returns false if the land drop is
// unavailable or no such card is in hand. Shared by the greedy fallback
// (SimulateLandPlay) and the searched land fold (ApplyPlanDirect) so both produce
// byte-identical placement for the same card.
static bool PlayLandByName(GameState& state, const std::string& name,
                           const std::string& fetch_target, bool allow_shock_pay,
                           const std::string& land_face)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    // Choose WHICH copy of `name` to play. Prefer an exiled/STAGED copy (Light Up the Stage --
    // playable only until its expiry turn) over a permanent hand copy: the hand copy keeps for a
    // later turn, so spending it here would WASTE the staged copy (it expires unplayed). Applies in
    // the autonomous search too (not only human play): the executor's committed-line replay
    // (AIEngine::TryPlaySpecificLand) prefers the staged copy as well, so keeping the search's own
    // land pick in lockstep makes the committed line realise exactly. Previously this was gated on
    // s_human_play and the search took the first hand match, so a committed line built assuming the
    // staged copy stayed (playing the drawn copy here) desynced from the executor -- the burn
    // commit-the-line fd-diverge (Light Up stages a Mountain; the T4 double-Shard-Volley line needs
    // the staged land spent on T3 so the drawn Mountain is free for T4). Byte-identical for decks
    // that never stage a land (no m_is_staged copy -> falls to first match).
    auto pick = ap.hand.end();
    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        if (it->m_impulse_no_land) { continue; }                   // Apex-exiled land: castable as a SPELL only, never played
        auto d = CardDatabase::Instance().LookupCached(*it);
        if (!d || !d->card.IsLand()) { continue; }
        if (pick == ap.hand.end()) { pick = it; }                  // first match (fallback)
        if (it->m_is_staged) { pick = it; break; }                 // prefer the expiring staged copy
    }

    if (pick == ap.hand.end()) { return false; }
    const CardDefinition* def = CardDatabase::Instance().LookupCached(*pick);

    LandPlayOptions o;
    o.fetch_target         = fetch_target;
    o.land_face            = land_face;
    o.allow_shock_pay      = allow_shock_pay;
    // Human play (g_play_land_entry_chooser set, and a real choice present) picks whether to pay
    // the shock life / reveal to enter untapped; otherwise the autonomous heuristic stands
    // (byte-identical for the search, which nulls the chooser via RevealLogPause).
    o.honor_entry_chooser  = true;
    // Forbidden Orchard's on-play Spirit, gated like the turn-start spawn so MTG_LEGACY_SEARCH
    // keeps the old model.
    static const bool s_orchard_onplay = !EnvOn("MTG_LEGACY_SEARCH");
    o.spawn_orchard_spirit = s_orchard_onplay;
    return PlayLandFromHand(state, static_cast<std::size_t>(pick - ap.hand.begin()), *def, o);
}

// Greedy land play: one land drop per turn, preferring multi-color lands over
// colorless-only lands (e.g. Mutavault) so colored spells stay castable.
// Two-pass: multi-color first, then any land. Used as the fallback when a plan did
// not search its land (depth-0 static Solve plans and the rollout horizon leaf).
static std::string SimulateLandPlay(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return std::string(); }

    // Does the active player control another land (one a Karoo could bounce)?
    const int active = state.active_player_index;
    bool has_other_land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == active && p.card.IsLand()) { has_other_land = true; break; }
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (const Card& c : ap.hand)
        {
            if (c.m_impulse_no_land) { continue; }   // Apex-exiled land: never played
            auto def = CardDatabase::Instance().LookupCached(c);
            if (!def || !def->card.IsLand()) { continue; }

            // A Karoo bounce land with no other land in play must return ITSELF (the bounce is
            // mandatory) -- net no land in play and a wasted drop. Never the greedy choice; play a
            // real land first (matches the searched land drop, which rejects the self-bounce).
            if (def->params.etb_bounce_land && !has_other_land) { continue; }

            bool is_multi = def->params.produces.size() > 1;
            if (pass == 0 && !is_multi) { continue; }

            std::string name = c.m_name;
            PlayLandByName(state, name);
            return name;
        }
    }
    return std::string();
}

// Cast-ordering search gate (C): when on, EnumeratePlans expands each action SET into the
// DISTINCT orderings of its non-sacrifice hand casts (deduped by end-of-phase state),
// instead of only the canonical enabler-first order -- so interleavings the canonical
// heuristic batches wrong (enabler / destroy-all-payload rebuilds) are reachable. Off by
// default => byte-identical (canonical order). On via MTG_SEARCH_ORDER or the global
// MTG_UNPRUNED. Expensive (applies each tried ordering on a copy); run with a high budget.
static bool OrderingSearchEnabled(const GameState& state)
{
    // Global A/B knob (env / MTG_UNPRUNED), cached once.
    static const bool global = (EnvOn("MTG_SEARCH_ORDER")) || DecisionUnpruned(UnprunedGate::SearchOrder);
    // Archetype opt-in (WantsCastOrderingSearch): Dragonstorm searches its combo cast order by default.
    // Provider-scoped so every other deck stays byte-identical (base hook returns false). Cheap per-call
    // vtable check -- the real cost is the k! applies below, gated behind this.
    return global || ResolveProvider(state).WantsCastOrderingSearch();
}

// Targeted cast-ORDERING candidates for Dragonstorm (WantsCastOrderingSearch; see
// docs/design/dragonstorm-cast-order-search.md). The combo is a CHEAPEST-FIRST self-funding ritual chain with
// only a few real degrees of freedom, so we enumerate a handful of principled orderings instead of all k!
// permutations (which the caller caps at 120, SKIPPING the biggest go-off hands). The rules (user-specified):
// * mana rituals -> cheapest-first (each funds the next); never searched among themselves; * Irencrag Feat
// ("cast only one more spell") -> immediately before the finisher; * finisher (Dragonstorm / Apex / a closing
// Dragon) -> last; * multiple Desperate Ritual vs Seething Song -> two variants: splice-AFTER (preferred,
// splice the Desperates once Seething's mana is up) and interleaved-by-cost (fallback, cast individually); *
// Ruby Medallion -> the one genuinely SEARCHED position: tried at every slot so the rollout keeps the
// earliest that still goes off (earlier discounts more red rituals). The identity (given) order is always
// included so the search can never do worse than the canonical line. Returns index-orderings over `reorder`;
// the caller applies + dedups-by-end-state + scores each.
//
// NOTE (2026-07-23): the >=2-Medallion block insertion below is a theoretical hole -- it never generates a
// STAGGERED line ("one Medallion early to discount the red rituals, the next once the cheaper chain funds
// it"), and the full-permutation oracle's k!<=120 cap skips exactly those k>=6 hands. Tried behind
// MTG_MEDALLION_SPLIT (non-decreasing per-Medallion slot placement) and MEASURED uniformly ~+0.005t WORSE on
// Dragonstorm d5 (s700001/2/3): the extra orderings cost search budget with no realized upside -- the subset
// enumerator already offers single-/no-Medallion lines, so "just play one" (usually best) is handled by plan
// selection, and "M1 -> ritual -> M2" is rarely optimal. NOT adopted; block insertion kept.
static std::vector<std::vector<int>>
DragonstormCastOrderings(const std::vector<Action>& reorder)
{
    const int n = static_cast<int>(reorder.size());
    // Apex of Power is an ENABLER (adds 10 mana + lets you cast 7 exiled cards this turn), NOT a
    // closer: it belongs BEFORE Irencrag. Irencrag ("cast only one more spell") before Apex is the
    // trap (Apex's mana + exiled spells are then unusable) -> never generate it. Dragonstorm / a
    // closing Dragon are the true closers -> AFTER Irencrag (Apex -> Irencrag -> Dragonstorm is the
    // golden line). See docs/design/gi22-durdle-and-irencrag-apex.md rules 2-3.
    std::vector<int> medallion, irencrag, apex, closer, ritual_splice, ritual_plain, other;
    for (int i = 0; i < n; ++i)
    {
        const CardDefinition* d = reorder[i].def;
        if (!d)                                       { other.push_back(i); continue; }
        const CardParams& p = d->params;
        if (!p.reduces_spell_color.empty())           { medallion.push_back(i); }      // Ruby Medallion
        else if (p.max_casts_after >= 0)              { irencrag.push_back(i); }        // Irencrag Feat
        else if (p.impulse_exile > 0)                 { apex.push_back(i); }            // Apex of Power (enabler, BEFORE Irencrag)
        else if (p.tutor_to_battlefield || d->card.IsCreature())
                                                      { closer.push_back(i); }          // Dragonstorm / closing Dragon (AFTER Irencrag)
        else if (p.ritual_floating_mana > 0 && p.splice_onto_arcane) { ritual_splice.push_back(i); } // Desperate
        else if (p.ritual_floating_mana > 0)          { ritual_plain.push_back(i); }    // Rite/Pyretic/Seething
        else                                          { other.push_back(i); }
    }
    auto cheapest = [&](std::vector<int>& v) {
        std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return reorder[a].card_mv < reorder[b].card_mv; });
    };
    cheapest(ritual_plain); cheapest(ritual_splice); cheapest(apex); cheapest(closer); cheapest(other);

    // Build a base chain (Medallion NOT yet inserted). splice_after=true keeps the Desperates after the
    // plain rituals (splice line); false merges them cheapest-first (individual line).
    auto build_base = [&](bool splice_after) {
        std::vector<int> chain;
        if (splice_after)
        {
            chain.insert(chain.end(), ritual_plain.begin(),  ritual_plain.end());
            chain.insert(chain.end(), ritual_splice.begin(), ritual_splice.end());
        }
        else
        {
            std::vector<int> merged = ritual_plain;
            merged.insert(merged.end(), ritual_splice.begin(), ritual_splice.end());
            cheapest(merged);
            chain = merged;
        }
        chain.insert(chain.end(), other.begin(),    other.end());
        chain.insert(chain.end(), apex.begin(),     apex.end());       // Apex enables mana+casts: BEFORE Irencrag (never Irencrag->Apex)
        chain.insert(chain.end(), irencrag.begin(), irencrag.end());   // second-to-last: gates the one closing spell
        chain.insert(chain.end(), closer.begin(),   closer.end());     // Dragonstorm / closing Dragon: last
        return chain;
    };
    std::vector<std::vector<int>> bases;
    bases.push_back(build_base(true));                                 // preferred: splice after Seething
    if (!ritual_splice.empty() && !ritual_plain.empty()) { bases.push_back(build_base(false)); }
    // Always offer the given (canonical) order too, so search can't underperform the fixed line.
    { std::vector<int> ident(n); for (int i = 0; i < n; ++i) { ident[i] = i; } bases.push_back(std::move(ident)); }

    if (medallion.empty()) { return bases; }
    std::vector<std::vector<int>> out;
    for (const std::vector<int>& base : bases)
    {
        // `base` excludes the medallion(s) unless it is the identity order; drop them so we insert once.
        std::vector<int> stripped;
        stripped.reserve(base.size());
        for (int idx : base) { if (reorder[idx].def && reorder[idx].def->params.reduces_spell_color.empty()) { stripped.push_back(idx); } }
        const int m = static_cast<int>(stripped.size());
        // Ruby Medallion must never be the post-Irencrag "one more spell": a cost reducer cast after
        // Irencrag discounts nothing and burns the single allowed cast (the payoff should have it).
        // Cap insertion to BEFORE the restrictor (earliest-first search among the legal slots is kept).
        // No restrictor in this base -> irc == m -> all positions stay open (byte-identical there).
        int irc = m;
        for (int i = 0; i < m; ++i)
        {
            const CardDefinition* d = reorder[stripped[i]].def;
            if (d && d->params.max_casts_after >= 0) { irc = i; break; }
        }
        for (int pos = 0; pos <= irc; ++pos)
        {
            std::vector<int> cand; cand.reserve(n);
            for (int i = 0; i < pos; ++i)   { cand.push_back(stripped[i]); }
            for (int mi : medallion)        { cand.push_back(mi); }         // medallion(s) as a block
            for (int i = pos; i < m; ++i)   { cand.push_back(stripped[i]); }
            out.push_back(std::move(cand));
        }
    }
    return out;
}

// Defined after EnumeratePlans; used here only as an end-of-phase STATE signature for
// ordering dedup (two orderings with the same key drive an identical rollout, so keeping
// one is lossless -- the same omissions that make it a valid rollout memo key make it a
// valid ordering-equivalence key).
static TranspositionTable::Key BuildSimKey(const GameState& state, int depth, int max_turns,
                                           bool second_main);

// Returns candidate plans for the current turn for use by SolveWithLookahead.
//
// Base set (unchanged from original): all 2^m feasible hand subsets, so the
// lookahead can compare any combination of spells including lower-value plans
// that may win faster than the greedy-optimal one.
//
// Added: "draw-early" Plan B variants for draw spells that have a Spectacle
// alternate cost (e.g. Light Up the Stage).  In these plans a cheap damage spell
// fires first to unlock Spectacle, then the draw spell resolves while mana is
// still available.  ApplyPlanDirect's post-draw re-solve then casts newly
// revealed cards with the remaining mana — a line that static evaluation cannot
// see.  The lookahead simulation compares these against the base plans and picks
// whichever leads to the earliest win.
// --- Branching diagnostics (MTG_BRANCH_STATS, off by default = zero cost) ---------------------
// Answers "which situations cause the most branching?": per EnumeratePlans call it attributes the
// raw odometer size (product of per-group option counts x 2^independent) and the final plan count
// to the card driving the biggest option-group, aggregates by that driver, and dumps a ranked
// table at exit. Run single-threaded for clean numbers (MTG_BRANCH_STATS=1 ... --threads 1).
namespace branchstats
{
    inline bool Enabled() { static const bool v = EnvOn("MTG_BRANCH_STATS"); return v; }
    struct Bucket { uint64_t calls = 0; double odo = 0, final_plans = 0, raw_plans = 0; uint64_t max_odo = 0; };
    inline std::mutex                          g_mtx;
    inline std::map<std::string, Bucket>       g_by_driver;   // keyed by biggest-group card name
    inline std::map<std::string, Bucket>       g_by_situ;     // keyed by coarse situation label
    inline Bucket                              g_total;

    inline void Record(const std::string& driver, const std::string& situ,
                       double odo, uint64_t raw, uint64_t final_plans)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto upd = [&](Bucket& b) {
            ++b.calls; b.odo += odo; b.raw_plans += raw; b.final_plans += final_plans;
            if (static_cast<uint64_t>(odo) > b.max_odo) { b.max_odo = static_cast<uint64_t>(odo); }
        };
        upd(g_by_driver[driver]); upd(g_by_situ[situ]); upd(g_total);
    }

    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            auto dump = [](const char* title, std::map<std::string, Bucket>& m) {
                std::vector<std::pair<std::string, Bucket>> v(m.begin(), m.end());
                std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.second.odo > b.second.odo; });
                std::fprintf(stderr, "\n=== BRANCH STATS: %s (sorted by total odometer) ===\n", title);
                std::fprintf(stderr, "%-34s %10s %14s %12s %12s %10s\n",
                             "key", "calls", "sum_odo", "sum_final", "avg_odo", "max_odo");
                for (size_t i = 0; i < v.size() && i < 20; ++i) {
                    const Bucket& b = v[i].second;
                    std::fprintf(stderr, "%-34s %10llu %14.0f %12.0f %12.1f %10llu\n",
                        v[i].first.c_str(), (unsigned long long)b.calls, b.odo, b.final_plans,
                        b.calls ? b.odo / b.calls : 0.0, (unsigned long long)b.max_odo);
                }
            };
            std::fprintf(stderr, "\n=== BRANCH STATS: total EnumeratePlans calls=%llu sum_odo=%.0f sum_final=%.0f sum_raw=%.0f ===\n",
                (unsigned long long)g_total.calls, g_total.odo, g_total.final_plans, g_total.raw_plans);
            dump("by driver card (biggest option-group)", g_by_driver);
            dump("by situation", g_by_situ);
        }
    };
    inline Dumper g_dumper;
}

// ---- Human-play sequential aura enumeration (increment 1; docs/design/sequential-plan-evaluation.md) ----
// The frozen-snapshot enumerator only offers a restricted aura (Daybreak Coronet "another Aura"; Lion
// Umbra "modified") on creatures ALREADY legal at start of phase. So casting Ethereal Armor -> X then
// Daybreak Coronet -> X the SAME turn -- rules-legal, since Armor enables Coronet -- is never enumerated,
// and the viewer rejects the human's line as legal_not_enumerated. These helpers add the missing
// sequenced plans; SeqAuraOrderingEnabled() gates them.

// Increment 2(a): within-turn aura legality ordering (Daybreak Coronet after an enabling aura cast this
// turn; Lion Umbra on a creature made "modified" this turn). Increment 1 ran this under HumanPlayActive()
// ONLY (the viewer); the port makes it the autonomous default too -- a capability expansion, so it is
// GT-AFFECTING for decks with restricted auras (Auras). It stays byte-identical for every OTHER deck: no
// restricted aura in hand -> AppendSequencedAuraCandidates injects nothing -> the reject and the
// enabler-first sort are no-ops. MTG_LEGACY_NO_SEQ_AURA reverts to the increment-1 behavior (viewer-only),
// so a legacy autonomous run is byte-identical to pre-port AND the viewer keeps the feature.
inline bool SeqAuraOrderingEnabled()
{
    static const bool s_legacy = EnvOn("MTG_LEGACY_NO_SEQ_AURA");
    if (s_legacy) { return HumanPlayActive(); }
    return true;
}

// (1) For each restricted aura in hand, inject a cast candidate targeting each controlled creature that
// is NOT frozen-legal but could be ENABLED by another aura cast this turn. Mirrors CollectActions' aura
// Action exactly (same hand_index -> joins that card's mutually-exclusive group). Bounded: only fires
// when the hand holds >= 2 auras (a restricted one + a separate enabler).
static void AppendSequencedAuraCandidates(const GameState& state, std::vector<Action>& cands)
{
    if (!SeqAuraOrderingEnabled()) { return; }
    const Player& ap    = state.ActivePlayer();
    const int     active = state.active_player_index;
    int aura_count = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.is_aura) { ++aura_count; }
    }
    if (aura_count < 2) { return; }   // need a restricted aura AND a separate enabler aura in hand

    for (size_t i = 0; i < ap.hand.size(); ++i)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!def || !def->params.is_aura) { continue; }
        if (def->params.aura_enchant_requires != "another_aura"
            && def->params.aura_enchant_requires != "modified") { continue; }
        const std::vector<int> frozen = LegalEnchantTargets(state, active, def->params);
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || !p.card.IsCreature()) { continue; }
            const int num = p.card.m_number;
            if (num <= 0) { continue; }
            if (std::find(frozen.begin(), frozen.end(), num) != frozen.end()) { continue; }  // already legal
            Action a;
            a.kind           = Action::Kind::CastFromHand;
            a.card_name      = ap.hand[i].m_name;
            a.hand_index     = static_cast<int>(i);
            a.cost           = EffectiveCost(*def, state);
            a.eval           = EvalCard(*def, state);
            a.is_noncreature = true;
            a.card_mv        = def->card.m_mana_cost.ManaValue();
            a.enchant_target = num;
            cands.push_back(std::move(a));
        }
    }
}

// (2) Reject a subset that casts a restricted aura on a creature NOT frozen-legal UNLESS the same subset
// casts an ENABLER aura on that SAME creature -- a plain aura, or a restricted aura whose own target IS
// frozen-legal (it resolves first, per the enabler-first order key at plan-build). Mirrors the other
// Subset* rejects; caller gates on HumanPlayActive() so autonomous enumeration never pays for it.
static bool SubsetHasUnenabledRestrictedAura(const GameState& state,
                                             const std::vector<Action>& cands, const std::vector<int>& sel)
{
    const int active = state.active_player_index;
    auto frozen_legal = [&](const CardParams& pp, int tgt) {
        const std::vector<int> f = LegalEnchantTargets(state, active, pp);
        return std::find(f.begin(), f.end(), tgt) != f.end();
    };
    for (int idx : sel)
    {
        const Action& c = cands[idx];
        if (c.kind != Action::Kind::CastFromHand || c.enchant_target <= 0) { continue; }
        const CardDefinition* cd = CardDatabase::Instance().Lookup(c.card_name);
        if (!cd || !cd->params.is_aura) { continue; }
        if (cd->params.aura_enchant_requires != "another_aura"
            && cd->params.aura_enchant_requires != "modified") { continue; }
        if (frozen_legal(cd->params, c.enchant_target)) { continue; }   // no enabler needed
        bool enabled = false;
        for (int jdx : sel)
        {
            if (jdx == idx) { continue; }
            const Action& d = cands[jdx];
            if (d.kind != Action::Kind::CastFromHand || d.enchant_target != c.enchant_target) { continue; }
            const CardDefinition* dd = CardDatabase::Instance().Lookup(d.card_name);
            if (!dd || !dd->params.is_aura) { continue; }
            // A valid enabler is legal on that creature itself: plain aura, or restricted-but-frozen-legal.
            if (dd->params.aura_enchant_requires.empty()
                || frozen_legal(dd->params, d.enchant_target)) { enabled = true; break; }
        }
        if (!enabled) { return true; }
    }
    return false;
}

// True iff `a` is a restricted aura cast whose enchant target is NOT legal against the frozen state (so
// it must resolve AFTER its in-plan enabler). Used only to order plan.actions under human play.
static bool IsConditionalRestrictedAura(const GameState& state, const Action& a)
{
    if (a.kind != Action::Kind::CastFromHand || a.enchant_target <= 0) { return false; }
    const CardDefinition* d = CardDatabase::Instance().Lookup(a.card_name);
    if (!d || !d->params.is_aura) { return false; }
    if (d->params.aura_enchant_requires != "another_aura"
        && d->params.aura_enchant_requires != "modified") { return false; }
    const std::vector<int> f = LegalEnchantTargets(state, state.active_player_index, d->params);
    return std::find(f.begin(), f.end(), a.enchant_target) == f.end();
}

// Within-turn creature target: a plain Aura can enchant a creature you CAST earlier this same turn.
// Card numbers are stable from setup, so the Aura targets the creature's number and attaches once the
// creature (cast FIRST -- CastOrderRank 10 creatures precede rank-20 Auras in apply) is on the
// battlefield. The frozen-snapshot LegalEnchantTargets misses this (the creature isn't in play at start
// of phase), so a just-cast creature could never carry a same-turn Aura -- and with NO prior creature the
// Aura was entirely uncastable. Default on; MTG_LEGACY_NO_AURA_NEW_CREATURE reverts (byte-identical:
// injects nothing). Restricted Auras (another_aura/modified) are OUT of scope here -- a fresh creature
// satisfies those only via other same-turn casts, which the aura-aura sequencing already models.
inline bool AuraOnNewCreatureEnabled()
{
    static const bool s_off = EnvOn("MTG_LEGACY_NO_AURA_NEW_CREATURE");
    return !s_off;
}

// For each plain Aura in hand, inject a cast candidate targeting each CREATURE in hand (by its stable
// m_number). Shares the Aura's hand_index so it joins the same mutually-exclusive group (one Aura, one
// target). Bounded by auras x hand-creatures. A subset is valid only if it also casts that creature
// (SubsetHasAuraOnUncastCreature). No-op without >=1 aura AND >=1 creature in hand -> byte-identical.
static void AppendCreatureTargetAuraCandidates(const GameState& state, std::vector<Action>& cands)
{
    if (!AuraOnNewCreatureEnabled()) { return; }
    const Player& ap = state.ActivePlayer();
    std::vector<int> hand_creatures;   // hand indices of creatures castable this turn
    for (size_t i = 0; i < ap.hand.size(); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (d && d->card.IsCreature() && ap.hand[i].m_number > 0) { hand_creatures.push_back((int)i); }
    }
    if (hand_creatures.empty()) { return; }
    for (size_t i = 0; i < ap.hand.size(); ++i)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (!def || !def->params.is_aura) { continue; }
        if (!def->params.aura_enchant_requires.empty()) { continue; }   // plain auras only (see note above)
        for (int ci : hand_creatures)
        {
            Action a;
            a.kind           = Action::Kind::CastFromHand;
            a.card_name      = ap.hand[i].m_name;
            a.hand_index     = (int)i;
            a.cost           = EffectiveCost(*def, state);
            a.eval           = EvalCard(*def, state);
            a.is_noncreature = true;   // enchantments are noncreature spells
            a.card_mv        = def->card.m_mana_cost.ManaValue();
            a.enchant_target = ap.hand[ci].m_number;   // resolves once the creature (cast first) is in play
            cands.push_back(std::move(a));
        }
    }
}

// Reject a subset whose Aura targets a creature NEITHER on the battlefield (frozen) NOR cast in this
// subset -- i.e. an AppendCreatureTargetAuraCandidates candidate whose creature isn't being cast. No-op
// unless such a candidate was injected -> byte-identical otherwise.
static bool SubsetHasAuraOnUncastCreature(const GameState& state,
                                          const std::vector<Action>& cands, const std::vector<int>& sel)
{
    const Player& ap     = state.ActivePlayer();
    const int     active = state.active_player_index;
    auto on_bf = [&](int num) {
        for (const Permanent& p : state.battlefield)
            if (p.controller_index == active && p.card.IsCreature() && p.card.m_number == num) { return true; }
        return false;
    };
    for (int idx : sel)
    {
        const Action& c = cands[idx];
        if (c.kind != Action::Kind::CastFromHand || c.enchant_target <= 0) { continue; }
        const CardDefinition* cd = CardDatabase::Instance().Lookup(c.card_name);
        if (!cd || !cd->params.is_aura) { continue; }
        if (on_bf(c.enchant_target)) { continue; }   // existing creature -> the normal path handles it
        bool cast_here = false;
        for (int jdx : sel)
        {
            const Action& d = cands[jdx];
            if (d.kind != Action::Kind::CastFromHand || d.hand_index < 0
                || d.hand_index >= (int)ap.hand.size()) { continue; }
            const CardDefinition* dd = CardDatabase::Instance().Lookup(d.card_name);
            if (dd && dd->card.IsCreature() && ap.hand[d.hand_index].m_number == c.enchant_target)
            { cast_here = true; break; }
        }
        if (!cast_here) { return true; }
    }
    return false;
}

// True iff `a` is an Aura cast whose target creature is NOT on the battlefield (frozen) -- i.e. a
// creature cast THIS turn. Such an Aura must resolve AFTER its target, so it is stable-sorted to the end
// of plan.actions (the apply honours plan-action order here). Plain Auras only (the injector's scope).
static bool IsAuraOnNewCreature(const GameState& state, const Action& a)
{
    if (a.kind != Action::Kind::CastFromHand || a.enchant_target <= 0) { return false; }
    const CardDefinition* d = CardDatabase::Instance().Lookup(a.card_name);
    if (!d || !d->params.is_aura) { return false; }
    for (const Permanent& p : state.battlefield)
        if (p.controller_index == state.active_player_index && p.card.IsCreature()
            && p.card.m_number == a.enchant_target) { return false; }   // existing creature -> normal order
    return true;
}

// True iff `acts` (in order) casts an Aura on a this-turn creature BEFORE that creature is cast. Such an
// ordering is invalid: the Aura resolves with no such creature in play and mis-attaches (falls back to an
// existing creature). Used to drop those orderings from the human-play cast-ordering expansion.
static bool OrderingPlacesAuraBeforeCreature(const GameState& state, const std::vector<Action>& acts)
{
    const Player& ap = state.ActivePlayer();
    std::unordered_set<int> cast_nums;   // hand m_numbers of creatures cast so far in this ordering
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.enchant_target > 0 && IsAuraOnNewCreature(state, a) && !cast_nums.count(a.enchant_target))
        { return true; }   // Aura targets a this-turn creature not yet cast in this ordering
        const CardDefinition* cd = CardDatabase::Instance().Lookup(a.card_name);
        if (cd && cd->card.IsCreature() && a.hand_index >= 0 && a.hand_index < static_cast<int>(ap.hand.size()))
        { cast_nums.insert(ap.hand[a.hand_index].m_number); }
    }
    return false;
}

static std::vector<TurnSolver::Plan> EnumeratePlans(const GameState& state, bool is_pre_combat)
{
    // Enumeration SCORES candidate plans by applying them on copies (ApplyPlanDirect resolves their
    // scry/dig/cantrips), which is hypothetical, not real resolution. Pause the human-play choosers
    // and reveal logging for the whole scoring pass so claude-play does NOT ask the player to resolve
    // every candidate's cantrip (that produced a per-turn storm of phantom scry/reorder decisions);
    // the chooser fires only during the REAL TurnSolver::ApplyPlan, which runs outside enumeration.
    RevealLogPause _rlp_enum;
    PROF_INC(enumerate_calls);
    const Player& ap              = state.ActivePlayer();
    ManaPool      pool            = AvailableManaPool(state);
    ManaPool      pool_noncreature = BuildNonCreaturePool(state);
    int           total_lands     = CountLands(state);
    int           pending_atk     = PendingAttackDamage(state);
    int           prowess_attackers    = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    // Shared enumeration of all action sources (hand casts + Vial + retrace; LE in a
    // later phase). The subset machinery below reads the per-Action valuation scalars.
    std::vector<Action> cands = CollectActions(state, is_pre_combat);
    // Human-play only: inject sequenced restricted-aura candidates (e.g. Daybreak Coronet on a creature
    // that an Ethereal Armor cast this same turn will enable) so the viewer can play within-turn-dependent
    // aura lines. No-op in the autonomous search (HumanPlayActive() false) -> byte-identical.
    AppendSequencedAuraCandidates(state, cands);
    // Also: a plain Aura targeting a creature CAST this same turn (its stable number). No-op unless the
    // hand holds both an Aura and a creature -> byte-identical for every other deck/state.
    AppendCreatureTargetAuraCandidates(state, cands);
    int n = static_cast<int>(ap.hand.size());

    // Lands in hand: the shared budget for additional discard costs (retrace, LE).
    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        auto cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef ? cdef->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }

    // Hinata combo: cheap scan of the stamped per-Action ritual float (mirrors Solve), so the deep
    // search also enumerates the ritual->payoff combo. False for non-ritual decks -> byte-identical.
    bool any_ritual = false;
    for (const Action& ra : cands) { if (ra.ritual_float > 0) { any_ritual = true; break; } }
    // Dragonstorm go-off lethal model present? (mirrors Solve) Gates the storm go-off short-circuit below.
    const bool has_extra_lethal = ResolveProvider(state).HasExtraLethalModel();
    // Same-turn mana-rock ramp scan (mirrors Solve). Inert without a non-creature rock.
    bool any_rock = false;
    for (const Action& ra : cands) { if (ra.rock_mana.Total() > 0) { any_rock = true; break; } }
    // Same-turn affinity scan (mirrors Solve). Inert without an affinity card (Thrumming Hivepool).
    bool any_affinity = false;
    for (const Action& ra : cands) { if (ra.def && ra.def->params.affinity_for_subtype) { any_affinity = true; break; } }
    // Same-turn cost reducer (Ruby Medallion) castable this turn? Gates the reducer generic credit
    // (mirrors Solve). Inert unless a reducer is a candidate -> every non-reducer deck byte-identical.
    bool any_reducer = false;
    for (const Action& ra : cands) { if (ra.def && !ra.def->params.reduces_spell_color.empty()) { any_reducer = true; break; } }
    // Any splice-onto-Arcane base? Gates the over-splice odometer skip (mirrors Solve); non-splice decks
    // pay nothing and stay byte-identical.
    bool any_splice = false;
    for (const Action& ra : cands) { if (ra.def && ra.def->params.splice_onto_arcane) { any_splice = true; break; } }
    // Dragonstorm acceleration-prefix collapse (HEURISTIC, provider-owned + MTG_UNPRUNED(AccelPrefix)-gated;
    // mirrors Solve). Off/absent -> byte-identical (any_accel scan skipped entirely). See
    // GroupChoiceNonPrefixAccel + docs/design/dragonstorm-search-pruning.md.
    const bool accel_prefix_on = ResolveProvider(state).UseAccelPrefixCollapse()
                              && !DecisionUnpruned(UnprunedGate::AccelPrefix);
    // Desperate Ritual splice-count collapse (HEURISTIC, mirrors Solve; see SpliceCollapseViolated).
    const bool splice_collapse_on = any_splice && ResolveProvider(state).UseSpliceCollapse()
                                 && !DecisionUnpruned(UnprunedGate::SpliceCollapse);
    bool any_accel = false;
    if (accel_prefix_on)
    {
        for (const Action& ra : cands)
        {
            if (ra.def && ra.def->params.ritual_floating_mana > 0
                && ra.kind == Action::Kind::CastFromHand) { any_accel = true; break; }
        }
    }
    // Lotus-independent accelerant prefix collapse (mirrors Solve; see IndependentAccelPrefixViolated).
    bool has_ind_accel = false;
    if (accel_prefix_on && s_lotus_prefix)
    {
        for (const Action& ra : cands)
        { if (ra.ritual_float > 0 && ra.kind == Action::Kind::SacForMana) { has_ind_accel = true; break; } }
    }
    // Filter/ramp land present? (mirrors Solve) Enables the real-payment affordability fallback.
    const bool any_filter = HasUntappedFilterSource(state);

    int m = static_cast<int>(cands.size());
    std::vector<TurnSolver::Plan> plans;

    // --- Base set: enumerate feasible action combinations ---
    //
    // A naive powerset over all action sources (for mask in [1, 2^m)) blows up
    // combinatorially when a hand holds many same-mana-value creatures while several
    // untapped Aether Vials share a charge count: each creature then yields one
    // CastFromHand plus one ActivateVial per same-charge Vial, so m (=|cands|) reaches
    // the dozens and the subset loop spins for minutes building plans the name-based
    // dedup below would only discard. Two facts make almost all of those subsets
    // redundant: (1) the actions that share a hand_index (a card's cast vs. its Vial
    // deploys) are mutually exclusive, and (2) deploying a creature through Vial A vs.
    // Vial B at the same charge is byte-identical after resolution — apply_vial taps
    // the first untapped matching Vial regardless of vial_bf_index — so a second
    // same-charge Vial adds CAPACITY, not a distinct plan.
    //
    // So instead of the action powerset we enumerate the PRODUCT of per-hand-card
    // choices {skip, cast, deploy-via-Vial} (all same-charge Vial deploys of one card
    // collapse to a single representative option), crossed with the independent
    // include/exclude choices for non-hand actions (graveyard retrace). Vial usage is
    // bounded by an aggregate per-charge capacity. This generates exactly the same set
    // of plan signatures as powerset-then-dedup, but in
    // O(prod(1+choices) * 2^independent) instead of O(2^m).

    // Per-charge Vial capacity = number of distinct untapped Vials available at each
    // charge (derived from the Vial actions' vial_bf_index, matching apply_vial which
    // taps a fresh matching Vial per deploy).
    std::vector<std::pair<int, int>> vial_capacity;   // (charge, count)
    auto capacity_for = [&](int charge) -> int
    {
        for (const std::pair<int, int>& vc : vial_capacity)
        {
            if (vc.first == charge) { return vc.second; }
        }
        return 0;
    };
    {
        std::vector<std::pair<int, int>> seen;   // (charge, vial_bf_index) already counted
        for (const Action& a : cands)
        {
            if (a.kind != Action::Kind::ActivateVial) { continue; }
            std::pair<int, int> key{ a.card_mv, a.vial_bf_index };
            bool already = false;
            for (const std::pair<int, int>& s : seen)
            {
                if (s == key) { already = true; break; }
            }
            if (already) { continue; }
            seen.push_back(key);
            bool found = false;
            for (std::pair<int, int>& vc : vial_capacity)
            {
                if (vc.first == a.card_mv) { ++vc.second; found = true; break; }
            }
            if (!found) { vial_capacity.push_back({ a.card_mv, 1 }); }
        }
    }

    // Group action indices: one mutually-exclusive option list per hand card
    // (its cast + a single representative Vial deploy), plus a flat list of
    // independent non-hand actions (graveyard retrace, hand_index < 0).
    std::vector<std::vector<int>> groups;            // per hand card: option cand indices
    std::vector<int>              group_hand_index;  // parallel: the card's hand_index
    std::vector<int>              independent;
    // Folded into this existing pass rather than a dedicated ManaGateWouldHelp scan: this loop already
    // visits every candidate, and a separate pass cost +0.03..+0.15% on the decks that never build the
    // gate (burn/TH/Knights/Anti-Lifegain hold no ritual, rock or reducer at all, so the scan always
    // answered "no"). Set BEFORE the independent-action continue so a Lotus SacForMana still counts.
    bool gate_relevant = false;
    for (int j = 0; j < m; ++j)
    {
        if (!gate_relevant
            && (cands[j].ritual_float > 0 || cands[j].rock_mana.Total() > 0
                || (cands[j].def && (cands[j].def->params.affinity_for_subtype
                                     || !cands[j].def->params.reduces_spell_color.empty()))))
        { gate_relevant = true; }
        if (cands[j].hand_index < 0) { independent.push_back(j); continue; }

        int gi = -1;
        for (int g = 0; g < static_cast<int>(groups.size()); ++g)
        {
            if (group_hand_index[g] == cands[j].hand_index) { gi = g; break; }
        }
        if (gi < 0)
        {
            groups.push_back({});
            group_hand_index.push_back(cands[j].hand_index);
            gi = static_cast<int>(groups.size()) - 1;
        }
        // Collapse all same-charge Vial deploys of one card to a single representative.
        if (cands[j].kind == Action::Kind::ActivateVial)
        {
            bool has_vial = false;
            for (int existing : groups[gi])
            {
                if (cands[existing].kind == Action::Kind::ActivateVial) { has_vial = true; break; }
            }
            if (has_vial) { continue; }
        }
        groups[gi].push_back(j);
    }

    // Breadth cap on a bloated combo-dig hand (shared with Solve). This enumerator feeds both the
    // multi-turn search and the per-turn leaf rollout (SolveWithLookahead), so capping it is what
    // attacks the rollout-bound no-win games. See CapGroupsBySituationalRank.
    CapGroupsBySituationalRank(state, cands, groups, group_hand_index);

    // Feasible-aware early ritual-drop: when no payoff is reachable this turn, remove the ritual groups
    // before the odometer enumerates their powerset (byte-identical; see DropRitualGroupsIfNoPayoff).
    DropRitualGroupsIfNoPayoff(state, pool, cands, groups, group_hand_index, independent);

    int num_groups = static_cast<int>(groups.size());
    int num_ind    = static_cast<int>(independent.size());

    bool have_colors[5];   // untapped-source colors -- state-only, computed once for all subsets
    ComputeAvailableColors(state, have_colors);

    // Evaluate one selected combination (a list of candidate indices) and, if
    // feasible, append the resulting plan. Mirrors the former per-mask body.
    auto eval_and_push = [&](const std::vector<int>& sel)
    {
        // Reject a Swords cast not backed by a live/same-turn enabler (see the helper). Inert
        // for every deck without controller_lifegain_equals_power.
        if (SubsetHasUnbackedLifegainRemoval(state, cands, sel)) { return; }
        // Payoff-prune (PrunesAcceleratorWithoutPayoff): drop a ritual-accelerant subset that casts no payoff
        // (Dragon/Dragonstorm/Apex) from the SEARCH branch list -- this is where the freed budget
        // comes from. Provider-owned (DragonstormProvider) + MTG_UNPRUNED(payoffprune)-gated; inert
        // for every other deck -> byte-identical. storm_in_hand=false here on purpose: the storm-hold
        // rule biases the greedy/rollout POLICY (Solve's consider) only, leaving the search's root
        // branch list intact so it can still arbitrate the cast-a-dragon-now line.
        if (ResolveProvider(state).PrunesAcceleratorWithoutPayoff()
            && !DecisionUnpruned(UnprunedGate::PayoffPrune)
            && SubsetWastesAccelerant(cands, sel, /*storm_in_hand=*/false)) { return; }
        // Reject two SacForMana of the same source (mutually-exclusive colour variants). Inert
        // without a SacForMana action -> byte-identical.
        if (SubsetHasDuplicateSacSource(cands, sel)) { return; }
        // Reject a creature sac-for-mana whose float nothing spends -- the dominated branch this
        // enumeration otherwise hands the search (Goblins gi44). Unlike the rituals-for-payoff guard
        // above, declining an in-play outlet keeps BOTH the outlet and the body, so there is no
        // "hold it for a later turn" trade for the search to arbitrate. See the helper.
        if (SubsetWastesCreatureSacMana(state, cands, sel)) { return; }
        // Reject physically-impossible Desperate Ritual over-splice. Inert without a splice base.
        if (SubsetHasIllegalSplice(state, cands, sel)) { return; }
        // Reject a sequenced restricted aura (injected above) with no in-subset enabler on its target.
        // No-op unless AppendSequencedAuraCandidates injected such a candidate (aura decks) -> byte-identical
        // otherwise. Gated by SeqAuraOrderingEnabled() (default on; MTG_LEGACY_NO_SEQ_AURA = viewer-only).
        if (SeqAuraOrderingEnabled() && SubsetHasUnenabledRestrictedAura(state, cands, sel)) { return; }
        // Reject an Aura targeting a this-turn creature that the subset does not actually cast. No-op
        // unless AppendCreatureTargetAuraCandidates injected such a candidate -> byte-identical otherwise.
        if (AuraOnNewCreatureEnabled() && SubsetHasAuraOnUncastCreature(state, cands, sel)) { return; }
        // Reject combinations whose Vial deploys exceed the per-charge capacity.
        for (int j : sel)
        {
            if (cands[j].kind != Action::Kind::ActivateVial) { continue; }
            int charge = cands[j].card_mv;
            int used   = 0;
            for (int k : sel)
            {
                if (cands[k].kind == Action::Kind::ActivateVial && cands[k].card_mv == charge)
                {
                    ++used;
                }
            }
            if (used > capacity_for(charge)) { return; }
        }

        ManaCost combined;
        ManaCost noncreature_combined;
        int sacrifice_count    = 0;
        int noncreature_count  = 0;
        int direct_dmg         = 0;
        int total_eval         = 0;
        int self_damage        = 0;
        int vial_haste_atk     = 0;
        int haste_cast_atk     = 0;   // hard-cast haste creatures attacking this turn
        int haste_cast_prowess = 0;   // ... of which have prowess (pumped by this plan's casts)
        int discard_lands_used = 0;  // lands consumed by additional costs (retrace, LE)

        for (int j : sel)
        {
            const Action& c = cands[j];
            discard_lands_used += c.discard_lands;
            combined.white     += c.cost.white;
            combined.blue      += c.cost.blue;
            combined.black     += c.cost.black;
            combined.red       += c.cost.red;
            combined.green     += c.cost.green;
            combined.colorless += c.cost.colorless;
            combined.generic   += c.cost.generic;
            if (c.is_noncreature)
            {
                noncreature_combined.white     += c.cost.white;
                noncreature_combined.blue      += c.cost.blue;
                noncreature_combined.black     += c.cost.black;
                noncreature_combined.red       += c.cost.red;
                noncreature_combined.green     += c.cost.green;
                noncreature_combined.colorless += c.cost.colorless;
                noncreature_combined.generic   += c.cost.generic;
            }
            if (c.sacrifice_land)   { ++sacrifice_count; }
            if (c.is_noncreature)   { ++noncreature_count; }
            direct_dmg     += c.direct_damage;
            total_eval     += c.eval;
            vial_haste_atk += c.vial_attack_power;
            haste_cast_atk += c.haste_attack_power;
            if (c.haste_prowess) { ++haste_cast_prowess; }
            for (const TriggerSource& src : trigger_sources)
            {
                if (c.card_mv <= src.max_mv) { self_damage += src.damage; }
            }
        }

        // Same-turn ramp credit. Ritual float (Reality Spasm / Irencrag) credited as wild; a mana
        // rock cast in THIS subset (Sol Ring -> {C}{C}) credited by its REAL produced colours, but
        // only once the board can already pay for the rocks themselves (a rock never funds its own
        // cost). The casts' own costs are already in `combined`, so this stays net/conservative.
        // Both inert -> byte-identical for decks without rituals or rocks.
        ManaPool eff = pool, eff_nc = pool_noncreature;
        bool credited = false;
        int  simul_ritual_credit = 0;
        if (any_ritual)
        {
            int ritual_credit = 0;
            {
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            // Rite-of-Flame graveyard self-scaling: k copies cast this turn escalate (+0,+1,...,+k-1)
            // as each prior copy hits the graveyard; the flat per-cast stamp misses that triangular
            // term. Single self-scaling name in-deck, so count-by-flag == count-by-name.
            int gy_self = 0;
            for (int j : sel) { if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++gy_self; } }
            ritual_credit += gy_self * (gy_self - 1) / 2;
            }
            simul_ritual_credit = ritual_credit;
            if (ritual_credit > 0) { eff.wild += ritual_credit; eff_nc.wild += ritual_credit; credited = true; }
        }
        if (any_rock)
        {
            ManaPool rock_prod; ManaCost rock_costs; bool sel_rock = false;
            for (int j : sel)
            {
                if (cands[j].rock_mana.Total() <= 0) { continue; }
                rock_prod.AddPool(cands[j].rock_mana);
                const ManaCost& rc = cands[j].cost;
                rock_costs.white += rc.white; rock_costs.blue += rc.blue; rock_costs.black += rc.black;
                rock_costs.red   += rc.red;   rock_costs.green += rc.green;
                rock_costs.colorless += rc.colorless; rock_costs.generic += rc.generic;
                sel_rock = true;
            }
            if (sel_rock && pool.CanPay(rock_costs)) { eff.AddPool(rock_prod); eff_nc.AddPool(rock_prod); credited = true; }
        }
        // Same-turn affinity (Hivepool): subtract the extra generic discount from same-turn slivers.
        if (any_affinity)
        {
            int acred = CostTricksEnabled() ? SameTurnAffinityGenericCredit(state, cands, sel) : 0;
            if (acred > 0)
            {
                combined.generic             = std::max(0, combined.generic - acred);
                noncreature_combined.generic = std::max(0, noncreature_combined.generic - acred);
            }
        }
        // Same-turn cost reducer (Ruby Medallion): subtract the extra generic discount it gives the
        // later same-colour casts in this subset, so the Medallion-discounted go-off enumerates.
        if (any_reducer)
        {
            int rcred = CostTricksEnabled() ? SameTurnReducerGenericCredit(state, cands, sel) : 0;
            if (rcred > 0)
            {
                combined.generic             = std::max(0, combined.generic - rcred);
                noncreature_combined.generic = std::max(0, noncreature_combined.generic - rcred);
            }
        }
        bool mana_ok = credited ? (eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined))
                                 : (pool.CanPay(combined) && pool_noncreature.CanPay(noncreature_combined));
        // SEQUENCED ritual credit, applied LAZILY. The sequenced credit is never LARGER than the
        // simultaneous one, so a position the cheap model already rejects would be rejected by the
        // sequenced model too -- only the SURVIVORS need the walk. Most odometer positions are
        // rejected, so this keeps the honest model off the hot path (measured: Dragonstorm d5
        // +3.2% -> +1.5% instructions, Hinata2 d5 +0.2%).
        const bool seq_on = any_ritual && simul_ritual_credit > 0 && SeqRitualCreditMode() >= 2;
        int  seq_credit   = -1;   // sequenced credit actually folded into eff (-1 = not computed yet)
        // Fold the sequenced correction into eff/eff_nc. Must run for EVERY surviving position, not
        // just the mana_ok ones: `eff` is read downstream (fill_surplus, the ritual-drop re-credit),
        // so a position rescued by the filter/reframe fallback below would otherwise go on spending
        // the optimistic pool.
        auto apply_seq = [&]()
        {
            if (seq_credit >= 0) { return; }
            seq_credit = SequencedRitualCredit(pool, cands, sel);
            if (seq_credit >= simul_ritual_credit) { return; }
            const int back = simul_ritual_credit - seq_credit;
            eff.wild    -= back;
            eff_nc.wild -= back;
        };
        if (seq_on && mana_ok)
        {
            apply_seq();
            mana_ok = eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined);
        }
        // Filter/ramp-land color conversion the flat pool can't express -> real-payment fallback.
        bool mana_reject = !mana_ok && !(any_filter && SubsetPayableWithFilters(state, cands, sel));
        if (seq_on && !mana_reject) { apply_seq(); }   // survivor: keep its credited pool honest too
        if (mana_reject && CostReframeEnabled())
        {
            // Cost reframe (MTG_COST_REFRAME): an INTERACTING subset (reducer/ritual/affinity/rock) the
            // flat-pool aggregate rejects may still be payable once its same-turn discount/float RESOLVES in
            // the executor. Offer it under a crude OVER-optimistic bound -- assume the generic (what
            // reducers/affinity cut, or rituals float) is fully same-turn-coverable, require only the
            // COLOURED pips to be really payable -- and let the scoring apply (SolveWithLookahead 9446/9467)
            // validate for real (a truly unaffordable line strands + scores worse -> not picked). This is the
            // deck-agnostic replacement for per-deck credit patches. See
            // docs/design/enumeration-feasibility-via-executor.md.
            bool interacts = false;
            for (int j : sel)
            {
                const Action& c = cands[j];
                if (c.ritual_float > 0 || c.rock_mana.Total() > 0
                    || (c.def && c.def->params.affinity_for_subtype)
                    || (c.def && !c.def->params.reduces_spell_color.empty())) { interacts = true; break; }
            }
            if (interacts)
            {
                // Tighter bound: subtract the MAX same-turn generic discount these casts could actually give
                // (reducer + affinity, computed order-aware even with the aggregate credit gated off), not
                // "all generic covered". Ritual float is already in `eff`/`pool`. Far fewer over-optimistic
                // candidates than the crude bound -> less budget dilution on combo decks; the scoring apply
                // validates for real.
                const int disc = SameTurnAffinityGenericCredit(state, cands, sel)
                               + SameTurnReducerGenericCredit(state, cands, sel);
                ManaCost oc    = combined;              oc.generic    = std::max(0, combined.generic - disc);
                ManaCost oc_nc = noncreature_combined;  oc_nc.generic = std::max(0, noncreature_combined.generic - disc);
                const ManaPool& opt    = credited ? eff    : pool;
                const ManaPool& opt_nc = credited ? eff_nc : pool_noncreature;
                if (opt.CanPay(oc) && opt_nc.CanPay(oc_nc)) { mana_reject = false; }
            }
        }
        if (mana_reject) { return; }
        if (sacrifice_count > total_lands)                   { return; }
        if (discard_lands_used > lands_in_hand)              { return; }
        // Accurate per-color payability (rejects wild-pool phantoms; see SubsetPayable).
        if (!SubsetPayable(have_colors, cands, sel))         { return; }

        // Irencrag "one more spell this turn": reject subsets casting > max_casts_after spells after
        // the restricting ritual (mirrors Solve::consider; keeps the commit-the-line enumerator legal).
        for (int j : sel)
        {
            if (cands[j].max_casts_after < 0) { continue; }
            const CardDefinition* rd = cands[j].def;
            const int r_rank = rd ? ResolveProvider(state).CastOrderRank(state, *rd) : 20;
            int after = 0;
            for (int k : sel)
            {
                if (k == j) { continue; }
                const CardDefinition* kd = cands[k].def;
                const int k_rank = kd ? ResolveProvider(state).CastOrderRank(state, *kd) : 20;
                if (k_rank > r_rank) { ++after; }
            }
            if (after > cands[j].max_casts_after) { return; }
        }

        if (self_damage >= ap.life) { return; }

        // FILL a scaled Magma cast UP from this plan's LEFTOVER mana (spend-all; the searched Crackle {X}
        // already took its 3-mana chunks, so the surplus is Magma's sub-chunk remainder). At most one Magma
        // per plan. Feeds direct_dmg/total_eval before the win projection and stores the filled action;
        // lockstep cost recomputed from crackle_targets at execution. Mirrors Solve::consider.
        int fill_surplus = mana_ok ? std::max(0, (credited ? eff.Total() : pool.Total()) - combined.ManaValue()) : 0;
        int fill_j = -1; Action fill_action;
        if (fill_surplus > 0)
        {
            for (int j : sel)
            {
                Action ca = cands[j];
                int extra = FillScaledCastFace(state, ca, fill_surplus);
                if (extra > 0) { direct_dmg += extra; total_eval += extra * 100; fill_j = j; fill_action = ca; break; }
            }
        }

        // This turn's attack damage. A hard-cast haste creature attacks now (haste_cast_atk) and,
        // if it has prowess, is pumped by this plan's noncreature casts too -- the canonical cast
        // order resolves prowess creatures before noncreature spells. Both terms were missing, so
        // a plan whose lethal turn RELIES on a cast Goblin Guide / Swiftspear did not read as a win.
        int projected_atk = pending_atk + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
        // Sac-land burn hold (mirrors Solve::consider) -- see HoldSacLandBurn.
        if (sacrifice_count > 0 && !wins)
        {
            bool       rescued = false;
            const bool drop    = HoldSacLandBurn(state, cands, sel, &rescued);
            if (SvLethalAuditOn() && (drop || rescued))
            {
                // Was this plan ACTUALLY lethal, despite the projection saying otherwise? Apply it for
                // real and run the combat. Diagnostic path only -- one full apply per touched plan.
                TurnSolver::Plan probe;
                for (int j : sel) { probe.actions.push_back(cands[j]); }
                // CRITICAL: EnumeratePlansWithLand plays the land into a COPY before calling this
                // enumerator and stamps land_decided on every plan it returns, so the real execution
                // of this plan plays no further land. Leaving land_decided false makes ApplyPlanDirect
                // fall back to greedy SimulateLandPlay -- which fires Searing Blaze's landfall and
                // fabricates 2 extra damage, i.e. a phantom "missed lethal" that is the PROBE's bug.
                probe.land_decided = true;
                GameState copy = state;
                ApplyPlanDirect(copy, probe, is_pre_combat);
                if (is_pre_combat) { SimulateCombat(copy); }
                const bool really_lethal = (copy.Opponent().life <= 0);
                // Is the sac-land burn a STAGED (Light Up the Stage) card? Those expire at the end of
                // our next turn, so "hold it" can mean "lose it" -- the one real drawback to holding.
                bool staged = false;
                for (int j : sel)
                {
                    const Action& c2 = cands[j];
                    if (!c2.sacrifice_land || c2.kind != Action::Kind::CastFromHand) { continue; }
                    if (c2.hand_index >= 0
                        && c2.hand_index < static_cast<int>(state.ActivePlayer().hand.size())
                        && state.ActivePlayer().hand[c2.hand_index].m_is_staged) { staged = true; }
                }
                if (drop) { ++sv_audit::g_pruned;  if (really_lethal) { ++sv_audit::g_pruned_lethal; }
                            if (staged) { ++sv_audit::g_pruned_staged; } }
                else      { ++sv_audit::g_rescued; if (really_lethal) { ++sv_audit::g_rescued_lethal; }
                            if (staged) { ++sv_audit::g_rescued_staged; } }
                if (really_lethal && sv_audit::g_dumped.fetch_add(1) < 25)
                {
                    std::string names;
                    for (int j : sel) { names += cands[j].card_name; names += "|"; }
                    static std::mutex m; std::lock_guard<std::mutex> lk(m);
                    std::fprintf(stderr,
                        "[sv-audit] T%d opp_life=%d -> %d  PROJECTED atk=%d (pending=%d vial=%d hastecast=%d "
                        "nc=%d*prow=%d) direct=%d  land_played=%d  plan_land=%d  %s  casts=%s\n",
                        state.turn_number, state.Opponent().life, copy.Opponent().life,
                        projected_atk, pending_atk, vial_haste_atk, haste_cast_atk,
                        noncreature_count, prowess_attackers + haste_cast_prowess, direct_dmg,
                        state.ActivePlayer().lands_played_this_turn, PlanHasLand(cands, sel) ? 1 : 0,
                        drop ? "PRUNED" : "rescued(c)", names.c_str());
                }
            }
            if (drop) { return; }
        }
        TurnSolver::Plan plan;
        plan.value          = total_eval;
        plan.wins_this_turn = wins;
        for (int j : sel) { plan.actions.push_back(j == fill_j ? fill_action : cands[j]); }
        ApplyCantripFirstOrder(plan.actions);   // no-op unless MTG_CANTRIP_FIRST
        // A sequenced restricted aura (Coronet/Lion Umbra on a not-yet-legal creature) must resolve AFTER
        // its enabler aura, so stable-sort those conditional payoffs to the end (key 1 vs 0). Apply's own
        // clean-set sort is rank-equal for auras (all rank 20) and stable, so it preserves this order. A
        // plan with no such action (every non-aura-deck plan) is order-unchanged (stable, all keys equal).
        if (SeqAuraOrderingEnabled())
        {
            std::stable_sort(plan.actions.begin(), plan.actions.end(),
                [&](const Action& x, const Action& y)
                { return IsConditionalRestrictedAura(state, x) < IsConditionalRestrictedAura(state, y); });
        }
        // An Aura targeting a creature CAST this turn must resolve after that creature (the apply honours
        // plan-action order), so stable-sort such Auras to the end (key 1 vs 0). No-op unless the injector
        // added one -> byte-identical otherwise. Both this and the sort above key plain-vs-conditional
        // disjointly (an injected Aura is plain), so they compose.
        if (AuraOnNewCreatureEnabled())
        {
            std::stable_sort(plan.actions.begin(), plan.actions.end(),
                [&](const Action& x, const Action& y)
                { return IsAuraOnNewCreature(state, x) < IsAuraOnNewCreature(state, y); });
        }
        plans.push_back(std::move(plan));
    };

    // Odometer over per-card choices (0 = skip the card, v >= 1 selects
    // groups[g][v-1]), crossed with the 2^num_ind powerset of independent actions.
    // The empty combination (skip everything) is not a plan and is dropped.
    const int mana_bound = ManaPruneBound(pool, cands);   // legacy scalar bound (MTG_LEGACY_MANA_BOUND)
    // Both indices are allocated ONLY when the deck actually uses them. Solve is the rollout leaf --
    // called once per node -- so even default-constructing their (empty) vectors on every call cost a
    // measurable ~1% on decks that use neither (Hinata/TH/burn A/B, 2026-07-29). A null pointer plus a
    // branch is the price now; the allocation happens on the rare call that needs it.
    std::unique_ptr<ManaGateIndex> gate_owned;            // selection-exact bound; see BuildManaGateIndex
    if (SelectionExactManaGateEnabled() && gate_relevant)
    {
        auto idx = std::make_unique<ManaGateIndex>();
        if (BuildManaGateIndex(pool, cands, independent, *idx)) { gate_owned = std::move(idx); }
    }
    const ManaGateIndex* gate    = gate_owned.get();      // null -> legacy scalar path
    const bool           gate_on = (gate != nullptr);
    // Precompute the accelerant-prefix order ONCE (choice-independent); per-choice check is a cheap walk.
    std::vector<int> accel_order;
    if (accel_prefix_on && any_accel) { BuildAccelPrefixOrder(cands, groups, group_hand_index, accel_order); }
    // Choice-independent inputs to the two splice predicates + the lowest predicate-relevant odometer
    // digit (mirrors Solve). See BuildSpliceOdometerIndex / MinPredicateDigit.
    std::unique_ptr<SpliceOdometerIndex> sidx_owned;
    const SpliceOdometerIndex* sidx = &g_no_splice_index;
    if (any_splice)
    {
        sidx_owned = std::make_unique<SpliceOdometerIndex>();
        BuildSpliceOdometerIndex(state, cands, groups, *sidx_owned);
        sidx = sidx_owned.get();
    }
    const int min_pred_digit = MinPredicateDigit(groups, *sidx, accel_order);
    // Dragonstorm storm go-off short-circuit (mirrors Solve): the search also enumerates the ritual/Lotus
    // powerset at every node, so when the maximal go-off line WINS this turn, emit JUST that plan and skip
    // the odometer -- a turn-winning branch dominates, so the search commits it and needs no others. When it
    // does not win we discard the trial and enumerate normally. Same has_extra_lethal + MTG_UNPRUNED(ComboLine)
    // + MTG_NO_GOFF_SHORTCIRCUIT gating; inert for every non-Dragonstorm deck. (`plans` is empty here -- the
    // odometer + Plan-B below are the only producers -- so returning just the winner drops nothing.)
    if (any_ritual && has_extra_lethal && state.spells_cast_this_turn == 0
        && !s_no_goff_shortcircuit && !DecisionUnpruned(UnprunedGate::ComboLine))
    {
        std::vector<int> goff;
        if (BuildStormGoffLine(cands, goff) >= 0)
        {
            const size_t before = plans.size();
            eval_and_push(goff);
            if (plans.size() > before)
            {
                // VERIFY the projected win by simulation (the projection is optimistic for the Apex-staged
                // Dragonstorm case -- see the Solve sibling). Only a truly-lethal line short-circuits the
                // powerset; otherwise discard the trial and enumerate fully (byte-identical to the full search,
                // which re-simulates at the root anyway).
                if (plans.back().wins_this_turn)
                {
                    GameState copy = state;
                    ApplyPlanDirect(copy, plans.back(), is_pre_combat);
                    if (copy.Opponent().life <= 0)
                    {
                        return { std::move(plans.back()) };   // verified lethal -> skip powerset + Plan-B
                    }
                }
                plans.pop_back();   // not a verified win -> enumerate normally
            }
        }
    }

    // Board-lethal short-circuit (mirrors Solve's sibling): if the current board's attack-all damage
    // already kills the opponent this turn, emit JUST the do-nothing (attack-only) plan and skip the
    // odometer + Plan-B -- a turn-winning branch dominates, so the search commits it and needs no others.
    // pending_atk is REAL combat damage (PendingAttackDamage counts only legal CanAttackFull attackers vs
    // the passive opponent), not an optimistic projection, so no re-simulation is needed (unlike the
    // go-off cut). GT-fingerprint-invariant: which winning branch is chosen never changes the win TURN.
    if (!s_no_lethal_cut && ResolveProvider(state).UseLethalShortCircuit()
        && !DecisionUnpruned(UnprunedGate::ComboLine)
        && pending_atk >= state.Opponent().life)
    {
        const size_t before = plans.size();
        eval_and_push(std::vector<int>{});                  // the empty (attack-only) subset
        if (plans.size() > before && plans.back().wins_this_turn)
        {
            return { std::move(plans.back()) };             // board already lethal -> skip powerset + Plan-B
        }
        if (plans.size() > before) { plans.pop_back(); }    // not a confirmed win -> enumerate normally (no dup)
    }

    if (enumstats::Enabled())
    {
        std::uint64_t pos = 1ull << num_ind;
        for (int g = 0; g < num_groups; ++g) { pos *= (std::uint64_t)groups[g].size() + 1; }
        MeasureManaSideCollapse(cands, groups, independent, pos, pool.Total(),
            [&](const std::vector<int>& c) {
                return (any_splice && SpliceGroupChoiceRejected(*sidx, groups, c, splice_collapse_on))
                    || ((accel_prefix_on && any_accel) && NonPrefixAccelViolated(accel_order, c));
            });
    }
    if (!(any_ritual || any_rock))
    {
        // No mana side => no splice base, no accelerant, no Lotus sac => every group predicate is
        // inert, so this walk needs none of the predicate bookkeeping. Kept INLINE here rather than
        // behind the two-stage driver's function boundary: measured, routing it through the driver
        // cost ~1.7-2.7%% on the decks that gain nothing from the split (burn/Knights, callgrind
        // 2026-07-29). Mirrored in Solve and EnumeratePlans -- change one, change both.
        std::vector<int> choice(num_groups, 0);
        std::vector<int> sel;   // reused across positions (clear keeps capacity)
        bool done = false;
        while (!done)
        {
            int mcost = 0, mgain = 0, mgy = 0, mblock = 0;
            if (gate_on)
            {
                for (int g = 0; g < num_groups; ++g)
                {
                    if (choice[g] <= 0) { continue; }
                    const ManaGateTerm& t = gate->term[groups[g][choice[g] - 1]];
                    mcost += t.cost; mgain += t.gain; mgy += t.gy; mblock += t.block;
                }
            }
            else
            {
                for (int g = 0; g < num_groups; ++g)
                { if (choice[g] > 0) { mcost += cands[groups[g][choice[g] - 1]].cost.ManaValue(); } }
            }
            // Group-level early-out FIRST: if this selection is unpayable even with every independent
            // action's float credited, no imask extension of it can be paid, so skip the whole inner
            // loop rather than building `sel` for each of 2^num_ind doomed positions. (Dropping this
            // cost ~2.4pp on burn/Knights, callgrind 2026-07-29.)
            const bool outer_ok = gate_on
                ? (mblock > 0 || gate->ind_block
                   || mcost <= gate->pool_total + mgain + gate->ind_gain_all
                               + ManaGateTriangular(mgy + gate->ind_gy_all))
                : (mcost <= mana_bound);
            for (int imask = 0; outer_ok && imask < (1 << num_ind); ++imask)
            {
                int pcost = 0, pgain = 0, pgy = 0, pblock = 0;
                sel.clear();
                for (int g = 0; g < num_groups; ++g)
                { if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); } }
                if (gate_on)
                {
                    for (int b = 0; b < num_ind; ++b)
                    {
                        if (!(imask & (1 << b))) { continue; }
                        sel.push_back(independent[b]);
                        const ManaGateTerm& t = gate->term[independent[b]];
                        pcost += t.cost; pgain += t.gain; pgy += t.gy; pblock += t.block;
                    }
                }
                else
                {
                    for (int b = 0; b < num_ind; ++b)
                    {
                        if (!(imask & (1 << b))) { continue; }
                        sel.push_back(independent[b]);
                        pcost += cands[independent[b]].cost.ManaValue();
                    }
                }
                if (sel.empty()) { continue; }
                const bool ok = gate_on
                    ? (mblock + pblock > 0
                       || mcost + pcost <= gate->pool_total + mgain + pgain
                                           + ManaGateTriangular(mgy + pgy))
                    : (mcost + pcost <= mana_bound);
                if (!ok) { continue; }
                eval_and_push(sel);
            }
            int g = 0;
            for (; g < num_groups; ++g)
            {
                ++choice[g];
                if (choice[g] <= static_cast<int>(groups[g].size())) { break; }
                choice[g] = 0;
            }
            if (g == num_groups) { done = true; }
        }
    }
    else
    {
        EnumeratePlanPositions(cands, groups, independent, gate, mana_bound, *sidx, accel_order,
                               any_splice, splice_collapse_on, accel_prefix_on && any_accel,
                               has_ind_accel, [](const std::vector<int>&) { return true; },
                               eval_and_push);
    }

    // --- Plan B: draw-early variants for Spectacle draw spells ---
    // Helper: field-by-field ManaCost addition.
    auto add_cost = [](ManaCost a, const ManaCost& b) -> ManaCost
    {
        a.white     += b.white;  a.blue  += b.blue;  a.black += b.black;
        a.red       += b.red;    a.green += b.green;
        a.colorless += b.colorless; a.generic += b.generic;
        return a;
    };

    struct TriggerCand { int idx; ManaCost cost; int damage; int eval; };
    std::vector<TriggerCand> triggers;
    for (int i = 0; i < m; ++i)
    {
        const Action& c = cands[i];
        // A spectacle-enabling trigger is any burn that reaches the opponent's face this turn --
        // including a sac-land burn (Shard Volley). The sacrificed land can be one already tapped
        // for mana, so pool.CanPay(trigger + spectacle) below stays a sufficient affordability
        // test for the 2-card {trigger, draw} plan. The apply hoists a sac-land enabler ahead of
        // the (opaque) spectacle spell so the reduced cost is realised (see ApplyPlanDirect).
        if (c.direct_damage > 0 && !c.has_spectacle)
        {
            triggers.push_back({i, c.cost, c.direct_damage, c.eval});
        }
    }
    std::stable_sort(triggers.begin(), triggers.end(),
        [](const TriggerCand& a, const TriggerCand& b)
        {
            return a.cost.ManaValue() < b.cost.ManaValue();
        });

    for (int i = 0; i < m; ++i)
    {
        const Action& draw = cands[i];
        if (!draw.is_draw || !draw.has_spectacle) { continue; }

        ManaCost spectacle_cost = draw.cost; // already set to Spectacle cost if active
        // If Spectacle not yet active, we need a trigger first; use the Spectacle cost directly.
        const CardDefinition* draw_def =
            CardDatabase::Instance().LookupCached(ap.hand[draw.hand_index]);
        if (!draw_def || !ResolveProvider(state).ShouldStageSpectacleDraw(state, state.active_player_index, *draw_def)) { continue; }
        ManaCost spectacle_only = draw_def->params.spectacle_cost.value();

        bool spectacle_active = state.opponent_lost_life_this_turn;

        // Emit a {trigger, draw} plan for EACH distinct-named affordable trigger, not just the
        // cheapest -- so the search (and the human viewer) can commit any legal spectacle line,
        // e.g. "Searing Blood -> Light Up {R}" as well as "Lightning Bolt -> Light Up {R}". Each
        // such plan is inherently damage-source-FIRST (the trigger is pushed before the draw), so
        // the opaque apply casts the burn before Light Up -> spectacle unlocks -> reduced cost
        // realised. When spectacle is already active no trigger is needed (nullptr). Zero triggers
        // -> no plan, as before. Only fires for a spectacle draw (Light Up) -> non-burn byte-id.
        std::vector<const TriggerCand*> chosen_triggers;
        if (!spectacle_active)
        {
            std::unordered_set<std::string> seen_trig;
            for (const TriggerCand& tc : triggers)
            {
                if (!pool.CanPay(add_cost(tc.cost, spectacle_only))) { continue; }
                // Dedup by the trigger's real card name so two copies of the same burn don't
                // emit identical plans.
                if (!seen_trig.insert(cands[tc.idx].card_name).second) { continue; }
                chosen_triggers.push_back(&tc);
            }
            if (chosen_triggers.empty()) { continue; }
        }
        else
        {
            if (!pool.CanPay(spectacle_only)) { continue; }
            chosen_triggers.push_back(nullptr);
        }

        for (const TriggerCand* trigger : chosen_triggers)
        {
            TurnSolver::Plan plan;
            int direct_dmg = 0;
            if (trigger)
            {
                // The trigger Action already carries its real card name (set from the true hand
                // index when candidates were built). A prior version overwrote it with
                // ap.hand[trigger->idx], treating the *candidate* index as a hand index -- wrong
                // whenever the hand holds lands (it would name/cast e.g. a Mountain instead of the
                // burn), which is exactly why "Searing Blood -> Light Up" was not enumerated when a
                // Mountain sat earlier in hand. Use the Action's own name -> correct spell cast.
                Action ta = cands[trigger->idx];
                plan.actions.push_back(ta);  // cheap damage spell unlocks Spectacle
                plan.value += trigger->eval;
                direct_dmg += trigger->damage;
            }
            plan.actions.push_back(draw);  // draw spell at its Spectacle cost
            plan.value += draw.eval;
            plan.wins_this_turn = (pending_atk + direct_dmg) >= state.Opponent().life;
            plans.push_back(std::move(plan));
        }
    }

    // Sort so the highest-value plans are simulated first.  When a timeout fires
    // the search returns the best plan found so far, so we want the most promising
    // candidates evaluated before cheaper ones.
    std::stable_sort(plans.begin(), plans.end(),
        [](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn > b.wins_this_turn; }
            return a.value > b.value;
        });

    // Dedup by EFFECT signature. The powerset enumeration treats each copy of a
    // 4-of as a distinct candidate, so subsets that differ only in WHICH copy they
    // pick (e.g. Bolt #2 vs Bolt #5) produce byte-identical plans — a 2-16x phantom
    // blowup. ApplyPlanDirect resolves spells/sacrifices/Vial activations purely by
    // NAME (it finds the first matching card), so two plans with the same multiset
    // of names are indistinguishable downstream. Collapsing them is exactly lossless
    // and removes the duplicate inline-first-turn + transposition lookups they'd
    // otherwise each incur. Done after the sort so the surviving copy is the
    // highest-ranked (identical plans share rank, so order is unaffected either way).
    // Human play keeps plans that differ ONLY in a sub-decision (tutor target / X / Ponder keep /
    // Soulfire count / land / fetch) as DISTINCT plans, so the player can choose among them.
    //
    // The autonomous dedup collapses them to one cast-name representative (the FIRST enumerated,
    // i.e. the tutor heuristic's best-first pick). NB this is NOT a correctness property -- it is an
    // efficiency shortcut that DELEGATES the sub-decision to the heuristic and never search-branches
    // over the alternatives. It is a real (heuristic-masked) limitation in the search too; it only
    // "works" there insofar as TutorCandidates' ordering is trusted. For human play that shortcut is
    // simply wrong -- the human IS the decision-maker -- so we keep every variant. Gated on
    // MTG_HUMAN_PLAY: the autonomous search and the MTG_UNPRUNED A/B stay byte-identical (the
    // shortcut, warts and all, is unchanged there -- widening the search is a separate question).
    const bool s_human_play_sig = HumanPlayActive();
    auto plan_signature = [s_human_play_sig](const TurnSolver::Plan& p) -> std::string
    {
        std::vector<std::string> v, s, a, g, l, u, msf;
        for (const Action& act : p.actions)
        {
            switch (act.kind)
            {
                case Action::Kind::ActivateVial:      v.push_back(act.card_name); break;
                case Action::Kind::CastFromHand:
                    // Apex of Power: distinct float COLOURS are DISTINCT plans (each floats a different
                    // colour that pays different exiled spells), so keep the colour in the signature so the
                    // autonomous dedup never collapses the colour decision (core invariant -- same as
                    // SacForMana below). Empty colour (every non-impulse cast) -> just the card name ->
                    // byte-identical for every other deck.
                    (act.sacrifice_land ? a : s).push_back(
                        act.chosen_float_color.empty() ? act.card_name
                                                       : act.card_name + "#" + act.chosen_float_color); break;
                case Action::Kind::CastFromGraveyard: g.push_back(act.card_name); break;
                case Action::Kind::DiscardToLandsEdge:
                    l.push_back(act.card_name + "#" + std::to_string(act.discard_lands)); break;
                case Action::Kind::Suspend:  u.push_back(act.card_name); break;
                // SacForMana: distinct colours are DISTINCT plans (different floated colour); keep the
                // colour in the signature so the dedup never collapses the colour decision (core invariant).
                case Action::Kind::SacForMana:
                    msf.push_back(act.card_name + "#" + act.chosen_float_color
                                  + "#" + std::to_string(act.sac_source_id)
                                  + "#" + std::to_string(act.ritual_float)); break;   // sac-1 vs sac-k burst distinct
                case Action::Kind::DigDraw:  break;  // human-play only; not a plan.actions signature key
                case Action::Kind::PlayLand: break;  // never appears in plan.actions
                // Krenko tap: key on which source so a tap plan is distinct from a no-tap plan.
                case Action::Kind::TapForTokens:
                    u.push_back("KRENKO#" + std::to_string(act.sac_source_id)); break;
                // Costed outlets (not yet emitted -- see the enumeration note); future signature keys.
                case Action::Kind::SacCreatureOutlet:
                    msf.push_back("SAC#" + std::to_string(act.sac_source_id)
                                  + ">" + std::to_string(act.sac_victim_id)
                                  + "x" + std::to_string(act.sac_count)); break;
                case Action::Kind::Channel:
                    s.push_back("CHANNEL#" + act.card_name); break;
            }
        }
        std::sort(v.begin(), v.end());
        std::sort(s.begin(), s.end());
        std::sort(a.begin(), a.end());
        std::sort(g.begin(), g.end());
        std::sort(l.begin(), l.end());
        std::sort(u.begin(), u.end());
        std::sort(msf.begin(), msf.end());
        std::string sig;
        for (const std::string& n : v) { sig += 'V'; sig += n; }
        for (const std::string& n : s) { sig += 'S'; sig += n; }
        for (const std::string& n : a) { sig += 'A'; sig += n; }
        for (const std::string& n : g) { sig += 'G'; sig += n; }
        for (const std::string& n : l) { sig += 'L'; sig += n; }
        for (const std::string& n : u)   { sig += 'U'; sig += n; }
        for (const std::string& n : msf) { sig += 'M'; sig += n; }
        if (s_human_play_sig)
        {
            // Per-action sub-decisions, order-independent; plus the land/fetch the plan commits.
            std::vector<std::string> sub;
            for (const Action& act : p.actions)
            {
                if (!act.tutor_target.empty())   { sub.push_back("t" + act.card_name + ">" + act.tutor_target); }
                if (act.chosen_x > 0)            { sub.push_back("x" + act.card_name + "=" + std::to_string(act.chosen_x)); }
                if (act.ponder_keep >= 0)        { sub.push_back("p" + act.card_name + "=" + std::to_string(act.ponder_keep)); }
                if (act.soulfire_own_targets > 0){ sub.push_back("f" + act.card_name + "=" + std::to_string(act.soulfire_own_targets)); }
                // Crackle declared extra-target COUNT: distinct counts are distinct human choices
                // (each kills one more creature for one more {1} discount), so keep them as separate
                // variants instead of collapsing to the first-enumerated. >= 0 only under
                // HumanPlayActive() (the count range); the autonomous single count leaves it at -1
                // and so never reaches this human-play branch.
                if (act.crackle_targets >= 0)    { sub.push_back("c" + act.card_name + "=" + std::to_string(act.crackle_targets)); }
                // Desperate Ritual splice COUNT: distinct k are distinct human choices (each floats
                // another {R}{R}{R} for another {1}{R}), so keep them as separate plan variants under
                // human-play. Autonomous dedup keys only on cast NAMES (the 's' bucket above), so distinct
                // k collapse to the first-enumerated representative -- exactly the chosen_x precedent.
                if (act.splice_count > 0)        { sub.push_back("k" + act.card_name + "=" + std::to_string(act.splice_count)); }
                // Aura enchant TARGET: which creature the Aura attaches to is a human choice (Bogles piles
                // auras on one creature, but Daybreak Coronet / Lion Umbra restrictions and simple threat
                // choice make the target meaningful). Keyed on the stable target m_number so plans differing
                // only in placement survive as distinct variants instead of collapsing to the first-enumerated
                // target. Autonomous dedup keys only on cast NAMES (the 's' bucket), so distinct targets
                // collapse to the heuristic's best-first pick there -- exactly the tutor_target precedent.
                if (act.enchant_target > 0)      { sub.push_back("e" + act.card_name + ">" + std::to_string(act.enchant_target)); }
            }
            std::sort(sub.begin(), sub.end());
            for (const std::string& x : sub) { sig += '#'; sig += x; }
            if (p.land_decided)            { sig += "|land="  + p.land_to_play; }
            if (!p.fetch_target.empty())   { sig += "|fetch=" + p.fetch_target; }
            if (!p.land_face.empty())      { sig += "|face="  + p.land_face; }
        }
        return sig;
    };
    std::unordered_set<std::string> seen;
    seen.reserve(plans.size() * 2);
    std::vector<TurnSolver::Plan> deduped;
    deduped.reserve(plans.size());
    for (TurnSolver::Plan& p : plans)
    {
        if (seen.insert(plan_signature(p)).second) { deduped.push_back(std::move(p)); }
    }

    // Branching diagnostics (off by default): attribute this call's odometer size + plan count to
    // the card driving the biggest option-group and to a coarse situation label.
    if (branchstats::Enabled())
    {
        double odo = 1.0;
        for (const std::vector<int>& gp : groups) { odo *= (1.0 + static_cast<double>(gp.size())); }
        odo *= static_cast<double>(1u << std::min(num_ind, 24));
        int max_opts = 0; std::string driver = "(casts<=1)";
        for (const std::vector<int>& gp : groups)
        {
            if (static_cast<int>(gp.size()) > max_opts)
            { max_opts = static_cast<int>(gp.size()); driver = cands[gp[0]].card_name; }
        }
        const int bf = static_cast<int>(state.battlefield.size());
        char situ[96];
        std::snprintf(situ, sizeof situ, "groups=%s board=%s hinata=%d",
            (num_groups <= 4 ? "0-4" : num_groups <= 8 ? "5-8" : num_groups <= 12 ? "9-12" : "13+"),
            (bf <= 6 ? "0-6" : bf <= 10 ? "7-10" : bf <= 15 ? "11-15" : "16+"),
            HinataInPlay(state) ? 1 : 0);
        branchstats::Record(driver, situ, odo,
                            static_cast<uint64_t>(plans.size()), static_cast<uint64_t>(deduped.size()));
    }

    // Cast-ordering search (C): expand each action set into the DISTINCT orderings of its
    // non-sacrifice hand casts, deduped by end-of-phase state. Off by default => return
    // the canonical-order sets unchanged (byte-identical). See OrderingSearchEnabled.
    if (!OrderingSearchEnabled(state)) { return deduped; }

    std::vector<TurnSolver::Plan> ordered;
    ordered.reserve(deduped.size());
    for (TurnSolver::Plan& p : deduped)
    {
        // Reorderable = non-sacrifice hand casts (where enabler/payload interactions live);
        // everything else (Vial / sacrifice-land / retrace) keeps its canonical bucket.
        std::vector<Action> reorder, fixed;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { reorder.push_back(a); }
            else { fixed.push_back(a); }
        }
        if (reorder.size() < 2) { ordered.push_back(std::move(p)); continue; }

        // Candidate orderings to try. Dragonstorm (WantsCastOrderingSearch) uses the TARGETED generator
        // (cheapest-first chain + Irencrag-before-finisher + Medallion-position + Desperate/Seething splice
        // variants) -- O(k) principled orderings, and it covers the big go-off hands the k! cap below would
        // skip. The global A/B knob (MTG_SEARCH_ORDER on a non-Dragonstorm deck) keeps the full-permutation
        // search.
        std::vector<std::vector<int>> orderings;
        if (ResolveProvider(state).WantsCastOrderingSearch())
        {
            orderings = DragonstormCastOrderings(reorder);
        }
        else
        {
            // Bound the work: permutations grow as k! (distinct end-states are far fewer, but we still
            // APPLY each tried ordering). Beyond the cap keep the canonical order only.
            long long perms = 1; bool too_many = false;
            for (size_t i = 2; i <= reorder.size(); ++i)
            { perms *= static_cast<long long>(i); if (perms > 120) { too_many = true; break; } }
            if (too_many) { ordered.push_back(std::move(p)); continue; }
            // Permute reorderable casts by NAME (next_permutation over a name-sorted index list yields
            // each distinct multiset ordering once -- identical copies don't multiply).
            std::vector<int> idx(reorder.size());
            for (size_t i = 0; i < idx.size(); ++i) { idx[i] = static_cast<int>(i); }
            auto by_name = [&](int x, int y) { return reorder[x].card_name < reorder[y].card_name; };
            std::sort(idx.begin(), idx.end(), by_name);
            do { orderings.push_back(idx); } while (std::next_permutation(idx.begin(), idx.end(), by_name));
        }

        std::unordered_set<TranspositionTable::Key, TranspositionTable::KeyHash> seen_states;
        for (const std::vector<int>& idx : orderings)
        {
            TurnSolver::Plan cand = p;
            cand.actions.clear();
            for (int j : idx)            { cand.actions.push_back(reorder[j]); }
            for (const Action& a : fixed){ cand.actions.push_back(a); }
            cand.searched_order = true;
            // Drop an ordering that casts an Aura on a this-turn creature BEFORE that creature (it would
            // mis-attach to an existing creature). No-op unless such an Aura+creature line is present.
            if (AuraOnNewCreatureEnabled() && OrderingPlacesAuraBeforeCreature(state, cand.actions)) { continue; }

            // Apply this ordering on a copy; dedup by the resulting end-of-phase state.
            GameState copy = state;
            ApplyPlanDirect(copy, cand, is_pre_combat);
            if (seen_states.insert(BuildSimKey(copy, 0, 0, false)).second)
            {
                // Combat is order-independent, so inherit the base plan's combat-based win; a reordering
                // can only ADD direct damage (e.g. the rebuild), so also mark a win if this ordering
                // kills outright. Keeps winning orderings sorted first (not cut under budget).
                cand.wins_this_turn = p.wins_this_turn || (copy.Opponent().life <= 0);
                ordered.push_back(std::move(cand));
            }
        }
    }

    return ordered;
}

// Land-folded candidate enumeration: the land drop is searched alongside the spells.
//
// When a pre-combat land drop is available, for each DISTINCT playable land in hand
// (deduped by static effect signature so 4-ofs and mechanically-identical lands
// collapse to one representative) plus a DEFER option (play no land), we play that
// land on a copy, enumerate the spell subsets on the resulting board, and tag every
// plan with its land_to_play. A "play this land, cast nothing" baseline is always
// included so a turn may legally develop only its land. SolveWithLookahead runs this
// at every searched turn — in the real game AND in the rollout — so the land choice is
// modelled identically end to end (no greedy-rollout / searched-reality divergence,
// which otherwise makes searched land choices play out worse than the greedy heuristic).
// Human-play only: make Land's Edge a PICKABLE line action. When the active player controls a
// Land's Edge and holds lands, fan out a DiscardToLandsEdge(N) variant of every base plan -- and a
// STANDALONE "pass + Land's Edge" line if there are no base plans (the post-Treasure-Hunt
// breakpoint: land drop used, no mana left, hand full of drawn lands). Autonomous search auto-fires
// Land's Edge in ApplyPlanDirect (suppressed under s_human_play), so without this the human could
// cast a Land's-Edge deck but never fire it. N is bounded at lethal (over-fire only pings a dead
// opponent). Deterministic -> plan indices stay stable across CheckLine validation and the
// --choices stateless replay. Gated on MTG_HUMAN_PLAY: a no-op (byte-identical) for every
// autonomous goldfish/search run. Applied on BOTH EnumeratePlansWithLand return paths.
static void AppendHumanPlayLandsEdgePlans(const GameState& state, std::vector<TurnSolver::Plan>& all)
{
    const bool s_human_play_enum = HumanPlayActive();
    if (!s_human_play_enum) { return; }

    const Player& ap = state.ActivePlayer();
    int le_rate = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.discard_land_damage > 0)
        { le_rate = std::max(le_rate, d->params.discard_land_damage); }
    }
    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d ? d->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }
    if (le_rate <= 0 || lands_in_hand <= 0) { return; }

    const int opp_life = state.players[1 - state.active_player_index].life;
    const int lethal_lands = (opp_life > 0) ? (opp_life + le_rate - 1) / le_rate : 0;
    // No base plan to fan from (out of other plays) -> seed a "pass" base so Land's Edge is still
    // offered as a standalone line; otherwise the chooser sees zero plans and the turn advances.
    if (all.empty()) { all.push_back(TurnSolver::Plan{}); }
    const size_t base_count = all.size();
    for (size_t i = 0; i < base_count; ++i)
    {
        const bool plays_land = !all[i].land_to_play.empty();   // a played/fetched land leaves hand
        const int  avail = lands_in_hand - (plays_land ? 1 : 0);
        const int  maxN  = std::min(avail, std::max(0, lethal_lands));
        for (int n = 1; n <= maxN; ++n)
        {
            TurnSolver::Plan v = all[i];
            Action le;
            le.kind          = Action::Kind::DiscardToLandsEdge;
            le.card_name     = "Land's Edge";
            le.discard_lands = n;
            v.actions.push_back(std::move(le));
            v.value          = all[i].value + n * le_rate;
            v.wins_this_turn = all[i].wins_this_turn || (opp_life - n * le_rate <= 0);
            all.push_back(std::move(v));
        }
    }
}

// Human-play only: make cycling (a land in hand, e.g. Lonely Sandbar / Forgotten Cave) and
// sac-to-draw (a land in play, e.g. Fiery Islet) PICKABLE standalone lines, so the player can dig
// "when things go south". The autonomous search drives these digs through its own ShouldConsiderDig
// loop (suppressed under s_human_play in ApplyPlanDirect); without this the human controlling such a
// deck could never fire them. Each distinct affordable source becomes one plan with a single
// Kind::DigDraw action; ApplyPlanDirect draws one card and the AIEngine segment loop re-prompts (the
// library shrank) so the player uses the dug card or digs again. Affordability is checked with the
// executor's real payment path (TapForCostDirect on a copy, tapping a sac source first so it can't
// pay its own {T}) so an offered dig never silently no-ops. Deterministic (zone/battlefield order,
// one plan per distinct NAME) -> plan indices stay stable for CheckLine + the --choices replay.
// Gated on MTG_HUMAN_PLAY: a no-op (byte-identical) for every autonomous goldfish/search run.
static void AppendHumanPlayDigPlans(const GameState& state, std::vector<TurnSolver::Plan>& all)
{
    const bool s_human_play_enum = HumanPlayActive();
    if (!s_human_play_enum) { return; }
    const Player& ap = state.ActivePlayer();

    auto can_afford = [&](const std::string& name, bool is_sac, const ManaCost& cost) -> bool
    {
        GameState copy = state;
        if (is_sac)
        {
            int idx = -1;
            for (int i = 0; i < static_cast<int>(copy.battlefield.size()); ++i)
            {
                const Permanent& p = copy.battlefield[i];
                if (p.controller_index == copy.active_player_index && !p.tapped
                    && p.card.m_name == name) { idx = i; break; }
            }
            if (idx < 0) { return false; }
            copy.battlefield[idx].tapped = true;   // {T}: source can't pay its own cost
        }
        return TapForCostDirect(copy, cost, false);
    };
    auto add_dig = [&](const std::string& name, bool is_sac)
    {
        TurnSolver::Plan v;
        v.land_decided = true;   // a dig spends no land drop; never greedy-play a land
        Action dg;
        dg.kind          = Action::Kind::DigDraw;
        dg.card_name     = name;
        dg.dig_sacrifice = is_sac;
        v.actions.push_back(std::move(dg));
        all.push_back(std::move(v));
    };

    std::unordered_set<std::string> seen;
    for (const Card& c : ap.hand)   // cycling lands in hand
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !d->params.cycling_cost.has_value()) { continue; }
        std::string nm = c.m_name.str();
        if (!seen.insert("c:" + nm).second) { continue; }
        if (can_afford(nm, false, d->params.cycling_cost.value())) { add_dig(nm, false); }
    }
    for (const Permanent& p : state.battlefield)   // sac-to-draw lands in play (Fiery Islet)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->params.sacrifice_draw_cost.has_value()) { continue; }
        std::string nm = p.card.m_name.str();
        if (!seen.insert("s:" + nm).second) { continue; }
        if (can_afford(nm, true, d->params.sacrifice_draw_cost.value())) { add_dig(nm, true); }
    }
}

// Does this plan cast something that opens a mid-turn breakpoint (a spell whose resolution reveals
// NEW castables, so ApplyPlanDirect re-decides the rest of the turn)? Mirrors the ApplyPlanDirect
// branches that call TurnSolver::Solve: DrawUntilNonland (Treasure Hunt), staged exile / EI / plain
// cantrip (DrawSpell), and impulse_exile (Apex of Power). Only those plans get bp_choice variants.
static int PlanOpensBreakpoint(const TurnSolver::Plan& p)
{
    int mask = 0;
    for (const Action& a : p.actions)
    {
        if (a.kind != Action::Kind::CastFromHand && a.kind != Action::Kind::CastFromGraveyard)
        { continue; }
        const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
        if (d == nullptr) { continue; }
        if (d->tmpl == CardTemplate::DrawUntilNonland) { mask |= 1 << 1; }
        else if (d->tmpl == CardTemplate::DrawSpell)
        {
            // Staging / EI re-solve inline (site 0); a plain cantrip defers to site 3.
            mask |= (d->params.stages_cards || d->params.expressive_iteration) ? (1 << 0) : (1 << 3);
        }
        if (d->params.impulse_exile > 0) { mask |= 1 << 2; }
    }
    return mask;
}

// PERFORMANCE PRUNE (MTG_BP_MAXBASE, default 16): how many breakpoint-opening base plans get a
// wave-0 fan-out at all. On a very wide node the fan-out multiplies an already-huge candidate set:
// Dragonstorm at the overnight budget (its ritual/storm powersets) ran 5.3x slower for -0.033 avg.
// The plan list is sorted best-first, so fanning out only the top N keeps the widening where the
// search actually is while bounding the blowup. 0 lifts the cap (free rein).
//
// On its own this is the SAME class of cap as the width W -- a rank gate, hence a QUALITY prune --
// so the deferred wave phase (BpWaveWalker) covers the plans it drops, starting them at rank 0.
// That is what demotes it to a cost prune: it decides who waits, not who is reachable.
static int BpMaxBase()
{
    static const int n = []() -> int
    {
        const char* v = std::getenv("MTG_BP_MAXBASE");
        if (v == nullptr || *v == '\0') { return 16; }
        const int k = std::atoi(v);
        return k < 0 ? 0 : k;
    }();
    return n;
}

// A cycle/sacrifice dig opens a breakpoint without any cast naming it (the dig loop runs off the
// board), so on a deck that is about to dig EVERY plan qualifies. Pre-checking ShouldConsiderDig
// (the same predicate the dig loop uses, here on the pre-apply state) keeps that fan-out off the
// turns that will not dig at all; a plan that only becomes dig-worthy after its own casts simply
// keeps the greedy dig continuation. Shared by wave 0 and the deferred waves so both walk the
// SAME base-plan set.
static bool BpDigFanoutPending(const GameState& state, int sites)
{
    if ((sites & (1 << 4)) == 0) { return false; }
    const DecisionProvider& prov = ResolveProvider(state);
    return prov.HasAnyDigSource(state) && prov.ShouldConsiderDig(state);
}

// Append the SEARCHED-BREAKPOINT variants (MTG_BP_SEARCH=W; see Plan::bp_choice). For every plan
// that opens a breakpoint, add W copies tagged bp_choice = 0..W-1 so the outer rollout scores W
// distinct post-breakpoint continuations (land drop AND casts) instead of trusting the greedy
// re-solve. Appended AFTER the caller's sort so the existing candidate order -- and with it the
// win-this-turn shortcut and every non-breakpoint decision -- is untouched; a variant can only win
// on a strictly better rolled-out win turn, because the equal-turn tiebreak is `value`, which it
// shares with its base plan. W=0 (default) or a nested breakpoint enumeration emits nothing, so
// the whole mechanism is byte-identical off. See docs/design/post-breakpoint-search.md.
//
// This is WAVE 0. It reaches ranks 0..W-1 of the continuation list only; the deferred wave phase
// (BpWaveWalker) walks ranks W.. afterwards, so no rank is unreachable at an unbounded budget.
static void AppendBreakpointVariants(const GameState& state, std::vector<TurnSolver::Plan>& plans)
{
    const int w = BpSearchWidth();
    if (w <= 0 || g_bp_enum_depth != 0 || plans.empty()) { return; }
    if (!g_bp_root_enum && !BpSearchInRollouts()) { return; }   // committed decision only
    const int  s_max_base = BpMaxBase();
    const int  sites  = BpWave0SiteMask();   // wave-0 SELECTION only; the wave phase uses the full mask
    const bool dig_bp = BpDigFanoutPending(state, sites);
    std::vector<TurnSolver::Plan> variants;
    int fanned = 0;
    for (TurnSolver::Plan& p : plans)
    {
        if (!dig_bp && (PlanOpensBreakpoint(p) & sites) == 0) { continue; }
        if (s_max_base > 0 && fanned++ >= s_max_base) { break; }
        p.bp_wave0 = true;   // covered by wave 0; the wave phase starts this plan at rank W, not 0
        for (int at = 0; at < BpSearchDepth(); ++at)
        {
            for (int k = 0; k < w; ++k)
            {
                TurnSolver::Plan v = p;
                v.bp_choice = k;
                v.bp_at     = at;
                v.bp_wave0  = false;   // the marker belongs to the base plan only
                variants.push_back(std::move(v));
            }
        }
    }
    plans.insert(plans.end(), std::make_move_iterator(variants.begin()),
                              std::make_move_iterator(variants.end()));
}

// ---- DEFERRED CONTINUATION WAVES --------------------------------------------------------------
// docs/design/post-breakpoint-search.md, "The width cap is itself a quality prune".
//
// THE DEFECT THIS CLOSES. `Plan::bp_choice = k` indexes cands[k] of a HEURISTICALLY RANKED list, so
// wave 0 (AppendBreakpointVariants, ranks 0..W-1) leaves every continuation ranked >= W unreachable
// at ANY depth and ANY budget -- an infinite budget does not reach the line. Measured: Hinata seed
// 4259 gi255 at --budget-ms 0 is T6 at W=2 and T5 at W=16. The lists are long, so this hides a lot:
// MTG_BP_CANDS_PROBE at W=2 reports TH mean 6.1 / max 19, Hinata mean 6.0 / max 44, Dragonstorm mean
// 29 / max 90 -- 69-93% of all continuations rank-gated out. A CONSTANT wider W is not the answer
// either: it pays the fan-out at every breakpoint including the ones where it buys nothing, and the
// smoke W-sweep degrades past W=8 while the digests never converge even at W=64.
//
// THE FIX: defer, don't cap -- turn the rank gate from a QUALITY prune into a COST prune.
//   * Wave 0 is unchanged, so a node whose budget the search already spent is BYTE-IDENTICAL.
//   * A wave phase then walks ranks W, W+1, ... breadth-first across that node's breakpoint-opening
//     base plans, for as long as the budget allows. Unbounded budget => every rank is reached =>
//     full coverage is structural, which is the user's bar ("under unbounded I absolutely do not
//     want anything like this").
//   * No magic number: the real list length is not knowable before the apply (the breakpoint state
//     depends on the base plan's own casts), so the walker LEARNS it from the apply (g_bp_cands_last)
//     and retires the slot. Nothing here hard-codes a width.
//
// WHERE IT RUNS, AND WHY NOT AT THE ROOT ONLY. The first cut ran the waves once per committed
// decision, as a phase after the hybrid. MEASURED DEAD END: on Hinata 4259 that phase entered twice
// in the whole game (commit-the-line searches once per committed line, not once per turn) and found
// no breakpoint-opening plan at either root, so it changed nothing -- and the control confirms the
// placement, not the loop, was the problem: `MTG_BP_SEARCH=16 MTG_NO_BP_SEARCH_ROLLOUT=1` (root-only
// widening, unbounded) still gives T6 where every-ply W=16 gives T5. That matches the earlier
// finding recorded in the doc ("the win comes from rollouts, not the root"): the greedy continuation
// hurts by making the LEAF EVALUATOR mis-score lines. So the waves attach to the two loops that
// actually fan out -- FSLineWin (the commit-the-line search's nodes) and SolveWithLookahead's
// per-pass candidate loop (every rollout turn).
//
// POSITION-KEYING HAZARD, AVOIDED BY APPENDING. The escalation reorders a node's `pre` by the
// probe's per-plan ranks, INDEXED BY POSITION and keyed by BuildSimKey (g_probe_plan_vals). Changing
// the plan set while the node key stays the same would land the recorded ranks on the wrong plans --
// a silent mis-ordering, no crash. Waves run AFTER the node's candidate loop and after its
// `node_vals` are stored, and never extend `node_vals`, so positions 0..N-1 keep their identity and
// the probe/escalation pair stays on a stable prefix.
//
// ANYTIME-SAFE, so stopping mid-wave cannot hurt: a variant is adopted only on a STRICTLY better
// rolled-out win turn, exactly as wave 0's tiebreak works. Aborting a wave can only fail to improve.
//
// DETERMINISM: every stop decision reads the deterministic VIRTUAL work-unit counter (SearchBudget),
// never the clock, so runs stay thread-invariant and ground-truth comparisons stay reproducible.
//
// THE WAVES ARE A BUDGET LEVER, NOT AN UNBOUNDED-ONLY MODE. A node runs waves while it still has
// budget, so raising the budget buys deeper rank coverage continuously -- there is no cliff where a
// large-but-finite budget behaves like a tiny one. An UNLIMITED budget is just the end of that
// scale: every rank is reached, and the ceiling is gone entirely.
//
// Gating waves on Unlimited() alone was tried and REJECTED (user, 2026-07-29): it made a very high
// budget buy nothing, which is precisely the "cap that no budget can lift" this whole mechanism
// exists to remove. The measurement backs the lever -- on HELD-OUT seeds, spending the budget on
// deferred ranks is -0.00228 avg with 21 cases better, 0 worse, AND a faster suite.
//
//   MTG_BP_WAVES=0   off -- byte-identical to the pre-wave engine (the A/B hatch).
//   MTG_BP_WAVES=1   DEFAULT: waves run while the node's budget allows; unlimited => exhaustive.
static int BpWaveMode()
{
    static const int m = []() -> int
    {
        const char* v = std::getenv("MTG_BP_WAVES");
        if (v == nullptr || *v == '\0') { return 1; }
        const int n = std::atoi(v);
        return n < 0 ? 0 : n;
    }();
    return m;
}

// May this node run a wave phase at all? Only the budget decides: an unlimited budget always may, a
// budgeted one may while it has anything left (re-checked per candidate inside the loop, so the
// phase stops the moment the budget does -- that is the anytime contract).
static bool BpWavesHere(const SearchBudget* budget)
{
    if (BpWaveMode() <= 0 || BpSearchWidth() <= 0) { return false; }
    if (budget == nullptr || budget->Unlimited()) { return true; }
    // Skipping the phase for lack of budget leaves ranks unexplored -- a truncation, so any
    // enclosing no-win stops being a refutation (see g_fs_trunc_events).
    if (budget->Exhausted()) { ++g_fs_trunc_events; return false; }
    return true;
}

// COMPLETE NODES (MTG_BP_WAVE_COMPLETE, default ON).
//
// `FSLineWin` returns the FIRST in-horizon win it finds, and that shortcut is only sound because
// `FullSearchLine` breaks its ladder at the first verified win: pass L runs only after passes
// 1..L-1 found none, so nothing can win before this pass's horizon edge and the first in-horizon
// win must BE that edge. The premise is "pass L-1 was a COMPLETE refutation".
//
// The width cap already broke that premise -- pass L-1 could not see any continuation ranked >= W,
// so a turn-4 line hiding at rank 5 was invisible and the turn-5 win was returned as optimal. Waves
// restore it at an unlimited budget (every pass exhausts every rank). But with the shortcut in
// place a node that finds an in-horizon win RETURNS BEFORE ITS OWN WAVE PHASE RUNS, so under a
// budget it can still answer turn+L-1 while a deferred rank held turn+L-2.
//
// With this on, an in-horizon win BREAKS the candidate loop instead of returning: the wave phase
// runs and the node answers with the minimum over every rank it could afford, not the first win it
// stumbled on. A THIS-TURN win still returns immediately -- `turn_number` is the absolute floor, so
// no rank can beat it and the premise is irrelevant there.
//
// 0 restores the first-in-horizon-win shortcut (and is byte-identical whenever waves are off, since
// the deferral is gated on a wave phase actually being able to run).
static bool BpWaveCompleteNodes()
{
    static const bool on = []() -> bool
    {
        const char* v = std::getenv("MTG_BP_WAVE_COMPLETE");
        if (v == nullptr || *v == '\0') { return true; }
        return std::atoi(v) != 0;
    }();
    return on;
}

// MTG_BP_WAVE_PROBE=1: did a wave phase get to run, how far up the ranks did it reach, and did the
// search ever PREFER a deferred continuation? `no-slots` counts nodes with no breakpoint-opening
// plan (the phase is inert there), `budget-stopped` the ones cut short with ranks still unseen.
namespace
{
    struct BpWaveProbe
    {
        std::atomic<uint64_t> nodes{0};        // nodes that ran a wave phase
        std::atomic<uint64_t> no_slots{0};     // nodes with no breakpoint-opening base plan
        std::atomic<uint64_t> slots{0};        // (base plan x bp_at) slots offered waves
        std::atomic<uint64_t> scored{0};       // wave candidates applied
        std::atomic<uint64_t> rolled{0};       // ... that survived dedup and got a rollout
        std::atomic<uint64_t> improved{0};     // ... that beat the node's incumbent
        std::atomic<uint64_t> stopped{0};      // wave phases cut short by the budget
        std::atomic<int>      maxrank{0};      // deepest rank reached
        std::atomic<uint64_t> nested{0};       // slots OPENED for a discovered nested breakpoint
        std::atomic<uint64_t> nested_scored{0};// wave candidates handed out with bp_at > 0
        std::atomic<int>      maxat{0};        // deepest bp_at reached
        ~BpWaveProbe()
        {
            if (!EnvOn("MTG_BP_WAVE_PROBE")) { return; }
            std::fprintf(stderr,
                         "[bp-waves] nodes=%llu no-slots=%llu slots=%llu scored=%llu"
                         " rolled=%llu improved=%llu budget-stopped=%llu max-rank=%d"
                         " nested-slots=%llu nested-scored=%llu max-at=%d\n",
                         static_cast<unsigned long long>(nodes.load()),
                         static_cast<unsigned long long>(no_slots.load()),
                         static_cast<unsigned long long>(slots.load()),
                         static_cast<unsigned long long>(scored.load()),
                         static_cast<unsigned long long>(rolled.load()),
                         static_cast<unsigned long long>(improved.load()),
                         static_cast<unsigned long long>(stopped.load()),
                         maxrank.load(),
                         static_cast<unsigned long long>(nested.load()),
                         static_cast<unsigned long long>(nested_scored.load()),
                         maxat.load());
        }
    };
    BpWaveProbe g_bp_wave_probe;
    inline bool BpWaveProbeOn()
    {
        static const bool on = EnvOn("MTG_BP_WAVE_PROBE");
        return on;
    }
    inline void BpWaveMax(std::atomic<int>& m, int k)
    {
        int prev = m.load(std::memory_order_relaxed);
        while (k > prev && !m.compare_exchange_weak(prev, k, std::memory_order_relaxed)) {}
    }
    inline void BpWaveRank(int k) { BpWaveMax(g_bp_wave_probe.maxrank, k); }
    // A wave candidate at bp_at > 0 is a NESTED continuation -- the axis wave 0 never reaches.
    inline void BpWaveAt(int at)
    {
        if (at <= 0) { return; }
        g_bp_wave_probe.nested_scored.fetch_add(1, std::memory_order_relaxed);
        BpWaveMax(g_bp_wave_probe.maxat, at);
    }
}

// Hands one node's deferred continuations to its caller, one variant at a time. The caller owns the
// scoring (the two candidate loops score differently), the walker owns WHICH variant comes next and
// when a slot is exhausted.
//
// Breadth-first over ranks (round-robin across slots), so a budget that runs out part-way has spread
// across base plans instead of exhausting one of them.
class BpWaveWalker
{
public:
    // `plans` is the node's candidate list AFTER wave 0 -- base plans and their variants mixed. Only
    // base plans (bp_choice < 0) that open a breakpoint become slots.
    //
    // `limit` is how many leading entries the node's own loop actually SCANNED. It matters when the
    // escalation beam cut the loop short: the beam dropped those plans (and with them their wave-0
    // variants) on purpose, and that carve-out is reasoned -- it never applies at the root, so it
    // cannot drop the committed play. Widening a plan the beam declined would undo it, so the waves
    // stay inside whatever the loop looked at.
    // Uses the FULL BpSiteMask, not wave 0's selection mask: a class kept out of wave 0
    // (BpWave0SiteMask) is a cost prune precisely because its plans are picked up here at rank 0.
    BpWaveWalker(const GameState& state, const std::vector<TurnSolver::Plan>& plans,
                 std::size_t limit)
    {
        const int  sites  = BpSiteMask();
        const bool dig_bp = BpDigFanoutPending(state, sites);
        if (limit > plans.size()) { limit = plans.size(); }
        for (std::size_t i = 0; i < limit; ++i)
        {
            const TurnSolver::Plan& p = plans[i];
            if (p.bp_choice >= 0) { continue; }                                  // a wave-0 variant
            if (!dig_bp && (PlanOpensBreakpoint(p) & sites) == 0) { continue; }
            m_bases.push_back(i);
            AddSlots(plans, m_bases.size() - 1, BpSearchDepth());
        }
    }

    bool Empty() const { return m_slots.empty(); }
    std::size_t SlotCount() const { return m_slots.size(); }
    int LastRank() const { return m_last_k; }

    // Fills `out` with the next variant to score. False once every slot is retired.
    bool Next(const std::vector<TurnSolver::Plan>& plans, TurnSolver::Plan& out)
    {
        const std::size_t n = m_slots.size();
        for (std::size_t tried = 0; tried < n; ++tried)
        {
            const std::size_t here = m_cursor;
            m_cursor = (m_cursor + 1) % n;
            Slot& sl = m_slots[here];
            if (sl.done) { continue; }
            if (sl.n >= 0 && sl.k_next >= sl.n) { sl.done = true; continue; }
            m_last     = here;
            m_last_k   = sl.k_next++;
            out            = plans[sl.base];
            out.bp_choice  = m_last_k;
            out.bp_at      = sl.at;
            out.bp_wave0   = false;
            if (BpWaveProbeOn()) { BpWaveAt(sl.at); }
            return true;
        }
        return false;
    }

    // Report what the apply of the last handed-out variant observed: `n` = the continuation list's
    // REAL length (g_bp_cands_last; 0 when the apply reached no eligible breakpoint at all), and
    // `seen` = how many breakpoints of a searchable class that apply reached (g_bp_seen_last).
    //
    // `seen` is the NESTING discovery: an apply that walked past 1 breakpoint proves bp_at = 1
    // exists, so a slot opens for it at rank 0. That is the only way to know -- the count depends on
    // the continuation this very apply chose, so it cannot be enumerated in advance. Slots only ever
    // get ADDED, and only for indices not already covered, so the walk stays finite and each
    // (base, at) is offered exactly one rank sequence.
    //
    // Returns true when the rank was past the end -- the continuation fell back to greedy, making
    // the variant a copy of its own base plan, which is also what terminates the walk for this slot.
    bool Report(const std::vector<TurnSolver::Plan>& plans, int n, int seen)
    {
        Slot& sl = m_slots[m_last];
        sl.n = n;
        const std::size_t bi = sl.base_slot;
        const bool past_end = (n <= m_last_k);
        if (past_end) { sl.done = true; }
        // Open slots for every nested breakpoint this apply proved exists.
        if (seen > 0 && BpNestDiscover()) { AddSlots(plans, bi, seen); }
        return past_end;
    }

private:
    struct Slot
    {
        std::size_t base;       // index into the node's plan list
        std::size_t base_slot;  // index into m_bases / m_at_count (which base plan this belongs to)
        int         at;         // which breakpoint of the apply (Plan::bp_at)
        int         k_next;     // next rank to hand out
        int         n;          // the list's real length, -1 until an apply reports it
        bool        done;
    };

    // Ensure base `bi` has slots for bp_at = 0 .. want-1. Wave 0 covered ranks 0..W-1 of
    // bp_at < BpSearchDepth() for a plan it fanned out (bp_wave0), so those resume at rank W; every
    // other slot -- a plan MTG_BP_MAXBASE or the wave-0 class mask dropped, and every NESTED index --
    // starts at rank 0, because nothing has scored it yet.
    void AddSlots(const std::vector<TurnSolver::Plan>& plans, std::size_t bi, int want)
    {
        if (bi >= m_bases.size()) { return; }
        if (m_at_count.size() < m_bases.size()) { m_at_count.resize(m_bases.size(), 0); }
        const std::size_t idx  = m_bases[bi];
        const bool        w0   = plans[idx].bp_wave0;
        const int         wid  = BpSearchWidth();
        for (int at = m_at_count[bi]; at < want; ++at)
        {
            const int k0 = (w0 && at < BpSearchDepth()) ? wid : 0;
            m_slots.push_back(Slot{ idx, bi, at, k0, -1, false });
            if (at > 0 && BpWaveProbeOn())
            { g_bp_wave_probe.nested.fetch_add(1, std::memory_order_relaxed); }
        }
        if (want > m_at_count[bi]) { m_at_count[bi] = want; }
    }

    std::vector<Slot>        m_slots;
    std::vector<std::size_t> m_bases;      // node-plan index of each base plan, in slot-creation order
    std::vector<int>         m_at_count;   // how many bp_at slots each base already has
    std::size_t              m_cursor = 0;
    std::size_t              m_last   = 0;
    int                      m_last_k = 0;
};

// MTG_LAND_SIG_COMPLETE=1 OPTS IN to the completed land signature (every behaviour-affecting land
// param discriminated, plus dominance-promotion for strictly-optional abilities). DEFAULT OFF per the
// user's call (2026-07-29): lands with different names are mostly mechanically different, so widening
// this dedupe key buys little while any grouping of distinct cards stays risky. Kept in-tree because
// the underlying finding is real -- see docs/design/land-signature-completeness.md.
static const bool s_complete_land_sig = EnvOn("MTG_LAND_SIG_COMPLETE");
static const bool s_legacy_land_sig    = !s_complete_land_sig;
// Land-priority knobs: shared readers in EngineFlags.h -- greedy_land_name below reimplements
// TryPlayLand's passes as the search's last-resort tiebreak, and the two must stay in lockstep.

static std::vector<TurnSolver::Plan> EnumeratePlansWithLand(const GameState& state,
                                                            bool is_pre_combat)
{
    RevealLogPause _rlp_enum;   // candidate scoring is hypothetical -- see EnumeratePlans
    const Player& ap = state.ActivePlayer();
    // Human play also offers the land drop in the POST-combat main when it is still open: a real
    // game can pass the drop pre-combat, cast a Spectacle dig (Light Up the Stage) post-combat, then
    // play a land it revealed as the turn's drop. The autonomous search only drops pre-combat (its
    // second main is cast-only), so this is gated on MTG_HUMAN_PLAY -> byte-identical for the search.
    const bool s_human_play_drop = HumanPlayActive();
    bool drop_available = (is_pre_combat || s_human_play_drop)
                       && ap.lands_played_this_turn < ap.LandDropsAvailable();

    if (!drop_available)
    {
        // Nothing to decide; mark land as resolved so ApplyPlanDirect does not fall
        // back to greedy land play for these searched plans. Human play: still offer Land's Edge
        // as a standalone line here (this is the post-Treasure-Hunt breakpoint, drop already used).
        std::vector<TurnSolver::Plan> plans = EnumeratePlans(state, is_pre_combat);
        AppendHumanPlayLandsEdgePlans(state, plans);
        AppendHumanPlayDigPlans(state, plans);
        for (TurnSolver::Plan& p : plans) { p.land_decided = true; }
        AppendBreakpointVariants(state, plans);
        return plans;
    }

    // Static effect signature: two lands with the same signature are interchangeable
    // for the search (identical mana, ETB, and abilities), so only one need be tried.
    auto land_sig = [](const CardParams& pp) -> std::string
    {
        std::string s;
        std::vector<int> prod;
        for (Color c : pp.produces) { prod.push_back(static_cast<int>(c)); }
        std::sort(prod.begin(), prod.end());
        for (int c : prod) { s += std::to_string(c); s += ','; }
        s += "n" + std::to_string(pp.produces_amount);
        s += pp.enters_tapped ? "T" : "U";
        s += "l" + std::to_string(pp.etb_pay_life_to_untap);
        for (const std::string& sub : pp.etb_untap_reveal_subtypes) { s += "r" + sub; }
        s += "s" + std::to_string(pp.etb_scry);
        s += "d" + std::to_string(pp.enters_tapped_with_depletion);
        s += pp.no_max_hand_size      ? "H" : "-";
        s += pp.is_filter             ? "F" : "-";
        s += pp.cycling_cost          ? "C" : "-";
        s += pp.sacrifice_draw_cost   ? "D" : "-";
        // creature-only mana (Ancient Ziggurat) is NOT interchangeable with an unrestricted
        // any-colour land: deduping the two loses the unrestricted land's non-creature lines
        // (and could force the strictly-worse Ziggurat as the sole representative). Distinguish.
        s += pp.creature_mana_only    ? "M" : "-";
        // Fetchlands with different target colours are NOT interchangeable; distinguish
        // them. Empty for ordinary lands -> sig unchanged (other decks byte-identical).
        for (const std::string& ft : pp.fetch_land_types) { s += "f" + ft; }
        // MDFC (Pathway) lands are NOT interchangeable with a plain single-colour land of the same
        // FRONT colour: they can also play their back face for a different colour. Distinguish them
        // by the back face so the front is never deduped against, say, a basic Forest. Empty for
        // ordinary lands -> sig unchanged (every non-MDFC deck stays byte-identical).
        if (!pp.mdfc_back_name.empty())
        {
            s += "m" + pp.mdfc_back_name;
            std::vector<int> bprod;
            for (Color c : pp.mdfc_back_produces) { bprod.push_back(static_cast<int>(c)); }
            std::sort(bprod.begin(), bprod.end());
            for (int c : bprod) { s += "b" + std::to_string(c); }
        }
        if (s_legacy_land_sig) { return s; }

        // ---- ability / drawback discriminators ------------------------------------------------
        // Everything below appends ONLY when the param is non-default, so a land carrying none of
        // them yields the byte-identical signature it did before -- a deck whose lands differ on
        // none of these is unaffected. Each is a real behavioural difference that makes two
        // same-mana lands NON-interchangeable; omitting one let the dedupe drop the strictly
        // richer land, and since only the surviving representative is enumerated as a land play,
        // the dropped land's ability became UNREACHABLE for the search. Found by auditing every
        // land param in cards.json against this signature -- three suite decks had a LIVE
        // collision: Auras (Brushland vs Razorverge Thicket), Dragonstorm (Dwarven Hold vs
        // Mercadian Bazaar) and slivers_vial (Sliver Hive vs Cavern of Souls). MTG_LEGACY_LAND_SIG=1
        // restores the old signature for a byte-identical A/B.
        // Restricted-colour mana (Cavern of Souls / Unclaimed Territory / Secluded Courtyard):
        // coloured mana for CREATURE spells only -- same reasoning as creature_mana_only above.
        if (pp.colored_creature_only)         { s += "cco"; }
        // Reflecting Pool: colours are the runtime UNION of the other lands, not this static list.
        if (pp.reflecting)                    { s += "rp"; }
        // Forbidden Orchard: every tap hands the opponent a 1/1 Spirit -- a real cost.
        if (pp.taps_spawn_opp_token)          { s += "spw"; }
        // Pain land (Brushland / Horizon Canopy): tapping costs life.
        if (pp.tap_self_damage > 0)           { s += "pd" + std::to_string(pp.tap_self_damage); }
        // Grove of the Burnwillows: tapping gives the OPPONENT life.
        if (pp.tap_opponent_lifegain > 0)     { s += "og" + std::to_string(pp.tap_opponent_lifegain); }
        // Fastland: enters untapped only while few other lands are out -- a turn-dependent ETB the
        // static enters_tapped flag cannot express, so not interchangeable with a plain dual.
        if (pp.fastland_max_other_lands >= 0) { s += "fl" + std::to_string(pp.fastland_max_other_lands); }
        // Karoo (Izzet Boilerworks): ETB bounces a land back to hand.
        if (pp.etb_bounce_land)               { s += "kb"; }
        // Thundering Falls: ETB surveil.
        if (pp.etb_surveil > 0)               { s += "sv" + std::to_string(pp.etb_surveil); }
        // Ferrous Lake: ramp filter (feed 1 -> one of each colour) -- different net mana to is_filter.
        if (pp.ramp_filter)                   { s += "rf"; }
        // Storage land: Dwarven Hold and Mercadian Bazaar differ only in their CHARGE MODE.
        if (pp.storage_land)                  { s += "st" + pp.storage_charge_mode; }
        // NOTE: strictly-OPTIONAL extra activated abilities (Mutavault's animate, Sliver Hive's
        // token) are deliberately NOT discriminated here -- see land_bonus below. Splitting on them
        // doubles the land branch for no new line and measured +61% instructions on slivers_vial.
        return s;
    };

    // Strictly-OPTIONAL extra activated abilities on an otherwise signature-identical land. A land
    // carrying one DOMINATES a land without it: same mana, same ETB, and every line the plainer land
    // allows the richer one also allows (the ability may simply be ignored). So rather than splitting
    // the signature -- which doubles the enumerated land branch without adding a single reachable
    // line -- the richer land is PROMOTED to be the group's representative, keeping the old
    // enumeration width while making the ability reachable. Returns "" for a plain land, so a deck
    // with none of these is unaffected. Anything NOT strictly optional (an ETB, a tap drawback, a
    // restricted colour) is a real behavioural difference and belongs in land_sig above.
    auto land_bonus = [](const CardParams& pp) -> std::string
    {
        std::string b;
        if (pp.can_animate)                     // Mutavault: {1}: becomes a 2/2 creature
        {
            b += "an" + std::to_string(pp.animate_power) + "/" + std::to_string(pp.animate_toughness);
            if (pp.animate_cost) { b += pp.animate_cost->ToString(); }
        }
        if (pp.tap_token_cost || pp.tap_token_power > 0 || pp.tap_token_toughness > 0)
        {                                       // Sliver Hive: {5}, {T}: create a 1/1 Sliver
            b += "tk";
            if (pp.tap_token_cost) { b += pp.tap_token_cost->ToString(); }
            b += std::to_string(pp.tap_token_power) + "/" + std::to_string(pp.tap_token_toughness);
            for (const std::string& sub : pp.tap_token_subtypes)          { b += "u" + sub; }
            for (const std::string& sub : pp.tap_token_requires_subtypes) { b += "q" + sub; }
        }
        return b;
    };

    // Human play (the play GUI) enumerates one plan per distinct land NAME rather than per static
    // signature: the player chose a SPECIFIC land and expects that exact card played (not a
    // signature-equivalent representative), and a different-but-equivalent land must never read as
    // a reject. Gated on MTG_HUMAN_PLAY -> byte-identical for every autonomous goldfish/search run,
    // which keeps deduping by signature for enumeration economy.
    const bool s_human_play_lands = HumanPlayActive();
    std::vector<std::string>        land_names;   // representatives, in hand order
    std::unordered_set<std::string> seen_key;
    if (s_human_play_lands || s_legacy_land_sig)
    {
        for (const Card& c : ap.hand)
        {
            if (c.m_impulse_no_land) { continue; }   // Apex-exiled land: castable as a SPELL only, not enumerable as a land play
            const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
            if (!def || !def->card.IsLand()) { continue; }
            const std::string key = s_human_play_lands ? c.m_name : land_sig(def->params);
            if (seen_key.insert(key).second) { land_names.push_back(c.m_name); }
        }
    }
    else
    {
        // Dominance-aware representative choice. Group by static signature as before, then within a
        // group prefer a copy carrying a strictly-optional extra activated ability (land_bonus): it
        // dominates the plain copy, so promoting it makes the ability reachable at the OLD
        // enumeration width. Splitting instead would double the land branch without adding a
        // reachable line (+61% instructions on slivers_vial, measured).
        struct SigGroup { std::vector<std::pair<std::string, std::string>> members; };   // (name, bonus)
        std::vector<SigGroup>                        order;
        std::unordered_map<std::string, std::size_t> slot;
        for (const Card& c : ap.hand)
        {
            if (c.m_impulse_no_land) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
            if (!def || !def->card.IsLand()) { continue; }
            const std::string sg = land_sig(def->params);
            auto it = slot.find(sg);
            if (it == slot.end())
            {
                it = slot.emplace(sg, order.size()).first;
                order.emplace_back();
            }
            order[it->second].members.emplace_back(c.m_name, land_bonus(def->params));
        }
        for (const SigGroup& g : order)
        {
            std::vector<std::string> bonuses;   // distinct NON-EMPTY ability sets, in hand order
            for (const auto& m : g.members)
            {
                if (m.second.empty()) { continue; }
                if (std::find(bonuses.begin(), bonuses.end(), m.second) == bonuses.end())
                { bonuses.push_back(m.second); }
            }
            if (bonuses.empty()) { land_names.push_back(g.members.front().first); continue; }
            // One representative per DISTINCT optional-ability set. A plain copy is dominated by
            // every one of them, so it is never enumerated separately. Two DIFFERENT optional
            // abilities are mutually incomparable, so each keeps its own representative -- no deck
            // has that shape today, and it must not silently collapse if one is added.
            for (const std::string& b : bonuses)
            {
                for (const auto& m : g.members)
                {
                    if (m.second == b) { land_names.push_back(m.first); break; }
                }
            }
        }
    }

    // Greedy land the heuristic (AIEngine::TryPlayLand) would play from this hand.
    // Used ONLY as the last-resort ordering tiebreak below: when the search is
    // genuinely indifferent between land lines (equal win-turn AND equal first-turn
    // value), we default to the proven heuristic rather than to hand order. The
    // clairvoyant rollout often rates two land choices identically at the horizon,
    // and letting an arbitrary order decide picks a land that plays out marginally
    // worse than greedy in the realized game (the small fold-vs-greedy regressions).
    // Shares TryPlayLand's ranker outright (GreedyLandChoiceIndex, LandPlay.cpp) rather than
    // re-implementing it: the hand-rolled mirror this replaces had drifted three ways -- no
    // hand-flooding clause in the Reliquary pre-pass, no Apex-exiled skip in that pre-pass, and no
    // Karoo self-bounce skip in the four-pass -- so the tiebreak named a land the executor would
    // not have played, which is the one thing this lambda exists to avoid.
    const std::string greedy_land_name = [&]() -> std::string
    {
        const int i = GreedyLandChoiceIndex(state);
        return i < 0 ? std::string() : std::string(ap.hand[i].m_name);
    }();

    // Early land-choice PRUNE (provider-owned; see DecisionProvider::ForcedEarlyLandName). When the
    // provider names an early land that is actually in hand, collapse the fan-out to that one name --
    // the land-choice breadth on turns 1..2 is paid for out of the same fixed budget as the spell
    // decisions. Empty (every deck by default) leaves land_names untouched -> byte-identical.
    // Gated MTG_FORCED_EARLY_LAND (default on) so the with/without A/B is one env flag.
    static const bool s_forced_early_land = EnvOn("MTG_FORCED_EARLY_LAND");
    if (s_forced_early_land)
    {
        const std::string forced =
            ResolveProvider(state).ForcedEarlyLandName(state, state.active_player_index);
        if (!forced.empty()
            && std::find(land_names.begin(), land_names.end(), forced) != land_names.end())
        {
            land_names.assign(1, forced);
        }
    }
    // Land fan-out probe (MTG_TRACE=landfan): how many DISTINCT land options this turn's enumeration
    // branches over. One line per committed decision. The question it answers is whether an early
    // land PRUNE actually reduces total branching or merely defers it -- forcing the opening Mountain
    // removes the turn 1-2 fan-out but leaves the singleton utility lands in hand, where they branch
    // on every later turn instead.
    // g_bp_root_enum (not g_real_resolution) is the committed-node marker here: this enumeration runs
    // INSIDE the search, where reveal logging is paused and g_real_resolution is false.
    if (TRACE_ON("landfan") && g_bp_root_enum)
    { TRACE("landfan", "turn=%d options=%zu", state.turn_number, land_names.size()); }

    std::vector<TurnSolver::Plan> all;

    auto add_for_land = [&](const std::string& land_name, const std::string& fetch_target,
                            const std::string& land_face = "")
    {
        PROF_INC(gamestate_copies);
        GameState copy = state;
        if (!land_name.empty() && !PlayLandByName(copy, land_name, fetch_target, true, land_face)) { return; }

        // "Play this land, cast nothing" baseline (neutral value 0).
        TurnSolver::Plan idle;
        idle.value        = 0;
        idle.land_decided = true;
        idle.land_to_play = land_name;
        idle.fetch_target = fetch_target;
        idle.land_face    = land_face;
        all.push_back(std::move(idle));

        std::vector<TurnSolver::Plan> plans = EnumeratePlans(copy, is_pre_combat);
        for (TurnSolver::Plan& p : plans)
        {
            p.land_decided = true;
            p.land_to_play = land_name;
            p.fetch_target = fetch_target;
            p.land_face    = land_face;
            all.push_back(std::move(p));
        }
    };

    // Pass 2 of the real-fetch model: a fetchland whose FetchCandidates returns MORE THAN
    // ONE legal target is a genuine search choice (which colours to commit to). Emit one
    // land-variant per candidate so the rollout picks the best, capped at the heuristic's
    // top few (it orders best-first; lower-ranked targets are strictly worse on colour and
    // a basic always ranks last, so the cap drops only clearly-inferior fetches). A single
    // candidate (or none) plays the heuristic top pick with no extra branching (Pass 1).
    // The cap is provider-owned policy (DecisionProvider::FetchSearchCap, audit A2).
    const int kMaxFetchSearchTargets = ResolveProvider(state).FetchSearchCap();
    for (const std::string& ln : land_names)
    {
        const CardDefinition* ld = CardDatabase::Instance().Lookup(ln);
        if (ld && !ld->params.mdfc_back_name.empty())
        {
            // Modal double-faced land (Pathway): emit BOTH faces as distinct land-play options
            // (front colour vs back colour). The search picks the face that pays the turn; human
            // play surfaces a "which face?" choose grid (CheckLine 'face' sub). Never a fetchland.
            add_for_land(ln, "", "front");
            add_for_land(ln, "", "back");
            continue;
        }
        if (ld && !ld->params.fetch_land_types.empty())
        {
            std::vector<std::string> cands =
                ResolveProvider(state).FetchCandidates(state, state.active_player_index, ld->params);
            if (cands.size() > 1)
            {
                // Unpruned audit: search EVERY fetch candidate (no cap), so a costly
                // fetch-target heuristic can be detected. See DecisionUnpruned.
                int cap = DecisionUnpruned(UnprunedGate::Fetch) ? static_cast<int>(cands.size())
                                             : kMaxFetchSearchTargets;
                int n = std::min(static_cast<int>(cands.size()), cap);
                for (int i = 0; i < n; ++i) { add_for_land(ln, cands[i]); }
                continue;
            }
        }
        add_for_land(ln, "");   // ordinary land, or fetchland with <=1 candidate (heuristic)
    }
    // Defer: play no land pre-main. On a flood-engine turn the strict gate (ShouldCastDrawEngine,
    // MTG_TH_STRICT_FLOOD) suppresses "play land THEN cast Treasure Hunt/Throes" in the per-land
    // plans above, so the ONLY way to cast the flood engine with no outlet is via this defer plan
    // (drop still open) -- the search then decides, at the post-draw breakpoint, whether to play the
    // drawn Reliquary as the drop, play a hand land, cast Land's Edge for the win, or nothing. That
    // is the whole of "don't play a land before TH/Throes"; nothing here dictates the after-play.
    add_for_land("", "");

    // Land's Edge activation as a PICKABLE plan action (human play only) -- see the helper. Applied
    // here for the land-drop-available path; the !drop_available early-return applies it too.
    AppendHumanPlayLandsEdgePlans(state, all);
    AppendHumanPlayDigPlans(state, all);

    // A tapped land that is a fine early play (a dual/tri/scry/surveil/depletion/Karoo tap
    // land played on a turn you don't need its mana) vs one you'd rather NOT play as a normal
    // tapped land. EXCLUDED: cycling lands -- kept in hand for their from-hand cycling utility
    // (draw a card when flooding), not played as plain tap lands. KEPT (strong early when you
    // don't need the mana): scry/surveil duals (Thundering Falls, Temple of Epiphany), plain
    // tapped duals, depletion bursts (Saprazzan Skerry), and Karoo bounce lands (Izzet
    // Boilerworks) -- a Karoo SHOULD take a tapland turn; its only issue is play TIMING (it must
    // be played last so it bounces an already-tapped land), fixed separately.
    //
    // COLOUR-COVERAGE gate (field-aware): a tapped land is a fine early play UNLESS playing it
    // wastes the drop on a colour we don't need while a colour we DO need goes uncovered. The
    // discriminator is UNCOVERED need = (colours a non-land hand card requires) minus (colours the
    // lands we already control on the battlefield can make). Two refinements over a naive
    // colour-need check, both proven on logs/tiebreak_changed:
    //   * Field-aware: if the field already produces every colour our hand demands, colour is MOOT
    //     -- fall back to plain tapped-first tempo (== colour-blind). (gi=68: T1 Saprazzan Skerry
    //     already gives the U that Treasure Hunt needs, so at T2 we must NOT prefer the on-colour
    //     surveil land over a vanilla tapland just for redundant blue.)
    //   * Demand from the HAND, coverage from the FIELD only (not other hand lands): an empty field
    //     leaves the need uncovered, so an off-colour tapland is still correctly demoted. (gi=271:
    //     T1 field empty, Treasure Hunt needs U, Sandstone Needle makes only R -> demote; play a
    //     U source instead.) This is the "somewhere in between colour-blind and strict-colour-need"
    //     rule. MTG_COLOR_BLIND_TIEBREAK restores the old colour-blind rule for A/B.
    static const bool s_color_blind_tiebreak = EnvOn("MTG_COLOR_BLIND_TIEBREAK");
    bool needed[6] = { false, false, false, false, false, false };
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        const ManaCost& mc = d->card.m_mana_cost;   // authoritative cost (hand Card's may be unset)
        if (mc.white > 0) { needed[static_cast<int>(Color::White)] = true; }
        if (mc.blue  > 0) { needed[static_cast<int>(Color::Blue)]  = true; }
        if (mc.black > 0) { needed[static_cast<int>(Color::Black)] = true; }
        if (mc.red   > 0) { needed[static_cast<int>(Color::Red)]   = true; }
        if (mc.green > 0) { needed[static_cast<int>(Color::Green)] = true; }
    }
    // Colours the lands we already control can make (tapped or not -- a tapped land still covers
    // its colour on later turns, which is what "do we already have this colour" asks).
    bool have[6] = { false, false, false, false, false, false };
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->card.IsLand()) { continue; }
        for (Color col : EffectiveProduces(state, state.active_player_index, *d))
        {
            have[static_cast<int>(col)] = true;
        }
    }
    bool any_uncovered = false;
    bool uncovered[6];
    for (int i = 0; i < 6; ++i)
    {
        uncovered[i] = needed[i] && !have[i];
        if (i != static_cast<int>(Color::Colorless) && uncovered[i]) { any_uncovered = true; }
    }
    auto land_good_early_tapped = [&](const std::string& name) -> bool
    {
        if (name.empty()) { return false; }
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (!d || !d->params.enters_tapped) { return false; }
        if (d->params.cycling_cost.has_value()) { return false; }   // hold to cycle for a card
        if (s_color_blind_tiebreak) { return true; }                // legacy colour-blind A/B
        if (!any_uncovered) { return true; }                        // colour moot -> tempo governs
        for (Color col : d->params.produces)
        {
            if (col != Color::Colorless && uncovered[static_cast<int>(col)]) { return true; }
        }
        return false;   // off-colour: wastes the drop while a needed colour stays uncovered
    };
    static const bool s_develop_tiebreak = !EnvOn("MTG_NO_DEVELOP_TIEBREAK");

    // Winning plans first, then by value — matches EnumeratePlans' ordering so the
    // win-this-turn shortcut in SolveWithLookahead still returns the best winning plan.
    // Final tiebreak (only fires on equal wins AND equal value, i.e. true search
    // indifference): order the greedy land's plans first. SolveWithLookahead replaces
    // its incumbent only on a STRICTLY better rollout win-turn or STRICTLY higher
    // value, so among equal-value/equal-win-turn candidates it keeps the first one —
    // this ordering makes that first one the greedy land, defaulting indifferent ties
    // to the proven heuristic without overriding any strictly-better searched line.
    // Burn banks spare lands once flooded (BurnProvider::PreferHoldLandDrop) so a future Searing
    // Blaze gets its landfall: among EQUAL-VALUE plans, order the DEFER (no-land) plans first
    // instead of developing. Only flips the tiebreak below -- the value/win comparisons above are
    // untouched, so a strictly-better (e.g. landfall-enabling) develop plan still wins.
    // Learned mid-game evaluator (Seam B): re-rank the search's ROOT plans by predicted win-turn for
    // NON-lethal plans, so the full-depth search orders (the stable_sort below) and its equal-win-turn
    // tie-break (pass selection) follow the learned preference. Covers idle / spectacle / dig plans
    // uniformly. nullptr in every default run (no sidecar OR flag off) -> value unchanged ->
    // byte-identical. See docs/design/learned-d0-policy.md.
    const MidGameEvaluator* ev_root = (UseLearnedEval() && state.m_evaluator && !state.m_evaluator->empty())
                                    ? state.m_evaluator : nullptr;
    if (ev_root)
    {
        for (TurnSolver::Plan& p : all)
        {
            if (p.wins_this_turn) { continue; }
            const std::vector<std::string> cnames = PlanCastNames(p.actions);
            MidGamePlanSummary sum = SummarizePlanByNames(cnames, !p.land_to_play.empty());
            sum.baseline_eval = TurnSolver::PlanBaselineEval(state, cnames);
            p.value = LearnedPlanScore(state, sum, *ev_root);
        }
    }

    const bool hold_land = ResolveProvider(state).PreferHoldLandDrop(state, state.active_player_index);
    std::stable_sort(all.begin(), all.end(),
        [&](const TurnSolver::Plan& a, const TurnSolver::Plan& b)
        {
            if (a.wins_this_turn != b.wins_this_turn) { return a.wins_this_turn > b.wins_this_turn; }
            if (a.value != b.value) { return a.value > b.value; }
            if (s_develop_tiebreak)
            {
                const bool a_has = !a.land_to_play.empty();
                const bool b_has = !b.land_to_play.empty();
                // (1) develop first -- UNLESS banking for landfall, then hold (no-land) first.
                if (a_has != b_has) { return hold_land ? (a_has < b_has) : (a_has > b_has); }
                if (a_has && b_has)
                {
                    const bool a_tap = land_good_early_tapped(a.land_to_play);
                    const bool b_tap = land_good_early_tapped(b.land_to_play);
                    if (a_tap != b_tap) { return a_tap > b_tap; }                // (2) tapped-first
                }
            }
            bool a_greedy = (a.land_to_play == greedy_land_name);
            bool b_greedy = (b.land_to_play == greedy_land_name);
            return a_greedy > b_greedy;
        });

    // DIAGNOSTIC (MTG_FORCE_LAND="<turn>:<land name>[,<turn>:<name>...]", inert by default):
    // restrict this turn's plans to those playing the named land. There is otherwise no way to ask
    // "what would the engine do from THIS land drop?" -- the goldfish runner has no land override and
    // --force-mulligan only fixes the opening hand -- so a reference whose human line diverges at a
    // land choice cannot be investigated past turn 1. Filter-only: it removes plans, never adds or
    // reorders, and a turn with no matching plan is left untouched (so a turn where the land is not
    // in hand degrades to normal behaviour rather than emptying the candidate set).
    const std::string forced_land = ForcedLandForTurn(state.turn_number);
    if (!forced_land.empty())
    {
        std::vector<TurnSolver::Plan> keep;
        for (const TurnSolver::Plan& p : all)
        {
            if (p.land_to_play == forced_land) { keep.push_back(p); }
        }
        if (!keep.empty()) { all.swap(keep); }
    }

    AppendBreakpointVariants(state, all);
    // SEARCHED land-ETB scry/surveil (MTG_SCRY_SEARCH, opt-in). The disposition resolves inline
    // inside the land's ETB, so it cannot be an Action -- instead emit one plan variant per
    // candidate disposition and let the outer rollout score each, exactly as fetch_target and
    // land_face do for the other land sub-decisions. Candidate 0 is the provider heuristic, so
    // the k=1 case (and every deck with no etb_scry/etb_surveil land) is byte-identical.
    // See docs/design/searched-scry-disposition.md.
    if (ScrySearchEnabled())
    {
        std::vector<TurnSolver::Plan> extra;
        for (const TurnSolver::Plan& p : all)
        {
            // Base plans only. Running AFTER AppendBreakpointVariants and skipping the breakpoint
            // variants keeps this a second AXIS rather than a cross product: cost is L+S, not L*S.
            // Same trade the bp_at axis makes -- a line needing a non-heuristic scry AND a
            // non-greedy breakpoint continuation at once is deliberately out of reach.
            if (p.land_to_play.empty() || p.scry_choice >= 0 || p.bp_choice >= 0) { continue; }
            const CardDefinition* d = CardDatabase::Instance().Lookup(p.land_to_play);
            if (d == nullptr) { continue; }
            const bool  surveil = d->params.etb_surveil > 0;
            const int   n       = surveil ? d->params.etb_surveil : d->params.etb_scry;
            if (n <= 0) { continue; }
            const int look = std::min<int>(n, static_cast<int>(ap.library.size()));
            if (look <= 0) { continue; }
            // The land drop is applied before this plan's casts, so the true top `look` cards are
            // what the ETB will see. A plan that draws first still resolves safely: the scripted
            // index is clamped to the candidate count at resolution.
            const std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);
            const std::size_t k = TopDispositionCandidates(
                state, looked, surveil ? LookKind::Surveil : LookKind::Scry).size();
            for (std::size_t c = 1; c < std::min(k, ScrySearchWidth()); ++c)
            {
                TurnSolver::Plan v = p;
                v.scry_choice = static_cast<int>(c);
                extra.push_back(std::move(v));
            }
        }
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }

    // SEARCHED TUTOR TARGET (a cost-neutral action sub-decision -- see TutorAxisEnabled). The target
    // does not change what the plan can afford, so its variants all share a cast-name signature and
    // the dedup inside EnumeratePlans discarded every one but the provider's best. Fan them out HERE,
    // after the dedup, so they survive to be scored.
    //
    // Why the axis loses nothing this turn: a tutor is NOT a breakpoint site (the five sites are
    // stages/EI, DrawUntilNonland, impulse_exile, plain cantrip, dig-through-lands) and Gamble is
    // not a draw spell, so no re-solve follows the fetch -- the plan's action list is frozen before
    // the card arrives and the fetched card CANNOT be cast this turn. The target therefore cannot
    // interact with the rest of this turn's subset, which is exactly the condition that makes a
    // second axis equivalent to the cross product rather than an approximation of it.
    // RESOLVE MODE (MTG_TUTOR_AXIS_RESOLVE=1): the same additive axis, bound by INDEX instead of by
    // name. No ranking happens here at all -- the provider runs inside PerformTutor, on each plan's
    // own resolution state (land played, prefix casts applied and paid for, the source on the
    // battlefield), which is the state the name axis below only ever approximated (POSTLAND added
    // the land but not the spent mana; PLAN_AWARE adjusted counts but not the state). The only
    // state-read here is a SIZING call: how many distinct names the axis could take, so the loop
    // knows how many variants to emit. A pinned index past the resolution-state list clamps to the
    // last candidate (see PerformTutor), the same duplicate-not-whiff rule as the ETB dig.
    // Base plans stay tutor_choice = -1 == the provider's front AT RESOLUTION, so base and variants
    // are one ranking at one state by construction.
    if (TutorAxisResolveMode() && TutorAxisEnabled() && TutorAxisWidth(state) > 1
        && !HumanPlayActive())
    {
        std::vector<TurnSolver::Plan> extra;
        std::map<std::string, std::size_t> size_cache;   // tutor card name -> distinct-name count
        for (const TurnSolver::Plan& p : all)
        {
            // Base plans only -- one axis at a time, so cost stays additive (same trade as scry).
            if (p.scry_choice >= 0 || p.bp_choice >= 0 || p.tutor_choice >= 0) { continue; }
            for (const Action& act : p.actions)
            {
                if (act.kind != Action::Kind::CastFromHand) { continue; }
                const CardDefinition* d = CardDatabase::Instance().Lookup(act.card_name);
                if (d == nullptr || !(d->params.tutor_to_hand || d->params.tutor_to_top)) { continue; }
                auto it = size_cache.find(act.card_name);
                if (it == size_cache.end())
                {
                    // Turn-start sizing only; resolution clamps any drift (a state-dependent cut
                    // can shorten the list by the time the tutor resolves).
                    std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(
                        state, state.active_player_index, d->params);
                    std::vector<std::string> uniq;
                    for (const std::string& c : cands)
                    { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
                    it = size_cache.emplace(act.card_name, uniq.size()).first;
                }
                const std::size_t k = std::min(it->second, TutorAxisWidth(state));
                for (std::size_t c = 1; c < k; ++c)
                {
                    TurnSolver::Plan v = p;
                    v.tutor_choice = static_cast<int>(c);
                    extra.push_back(std::move(v));
                }
                break;   // vary ONE tutor per variant; the first to resolve consumes the pin
            }
        }
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }
    else if (TutorAxisEnabled() && TutorAxisWidth(state) > 1 && !HumanPlayActive())
    {
        std::vector<TurnSolver::Plan> extra;
        // MTG_TUTOR_PREFIX_STATS=1: how many DISTINCT prefixes (land played + the casts that precede
        // the tutor) reach one tutor decision. This is the feasibility number for ranking at the
        // true per-plan state: the cost of doing it properly is one provider call per distinct
        // prefix, not per plan, so if tutors sit early in the action order -- as they usually should,
        // since fetching before you commit mana is normally right -- the land drop is nearly the only
        // thing that varies and the cache collapses to a handful of entries.
        static const bool prefix_stats = EnvOn("MTG_TUTOR_PREFIX_STATS");
        std::set<std::string> distinct_prefix;
        std::size_t prefix_plans = 0, prefix_pos_sum = 0;
        // `state`. Ranking it pre-land was a real defect: providers feed mana_now / mana_next into a
        // deploy discount, so a turn whose land is still in hand prices every expensive card one turn
        // further away than the plan actually leaves it -- and a card pushed below the axis width is
        // then EXCLUDED, not merely ranked low. The base plan never had this problem (EnumeratePlans
        // runs on the post-land `copy`); only this post-dedup fan-out, which supplies the alternatives
        // the search actually chooses among, used the wrong state. Goblins gi101 is the case: at the
        // T4 Matron the pre-land ranking puts Siege-Gang 8th (mana_next=4, so {3}{R}{R} reads t=2)
        // and the post-land ranking puts it 4th (mana_next=5, t=1) -- inside a 6-wide window.
        // Keyed by the land the plan plays, so it is still one provider call per distinct land.
        //
        // DEFAULT OFF -- the defect is real and correctly located, and fixing it is measurably WORSE.
        // Held-out overnight, goblins (8,000 searched) and hinata (2,800):
        //
        //   goblins  postland=1                   +18.0    0 better / 18 worse
        //   hinata   postland=1                    ~-3      (raw -275 is a GT artifact: three games
        //                                                    GT recorded as UNWON under batch load
        //                                                    actually win -- gi90 is a genuine 9->8,
        //                                                    gi158 is churn that converges 6/6 by
        //                                                    budget 320. The 99-point loss penalty
        //                                                    turns a -2 into a -275.)
        //
        // The obvious rescue -- the provider's deploy-discount curve was fitted AGAINST the buggy
        // pre-land projection, so refit it -- does not work. Sweeping the t=1 constant with the fix
        // ON (MTG_GOBLIN_DISC_T1, train s4004+s5005 / validate s6006+s7007) saturates at +10 and
        // never approaches baseline, and NOT ONE arm produces a single better game:
        //
        //   DISC_T1   85    75    65    55    45    38        (train / validate turn-units)
        //   train    +10    +6    +6    +6    +6    +8
        //   valid     +8    +6    +4    +4    +4    +4
        //   better     0     0     0     0     0     0   <-- across all six arms, every seed
        //
        // So this is not a mis-tuned constant absorbing a bias. The pessimistic pre-land view is
        // acting as a TEMPO PRIOR that suits goldfishing: "the card I can deploy now" beats "the card
        // I could deploy next turn", and pricing next turn accurately promotes expensive cards a race
        // deck does not want. Making the projection honest would mean re-deriving the discount from
        // tempo rather than from turns-to-cast -- a real project, not a constant refit.
        //
        // Kept here, default-off, because the defect it fixes is genuine and worth finding again:
        // the base plan is ranked on the POST-land state (EnumeratePlans runs on `copy`) while these
        // variants are ranked pre-land, so the two halves of the same plan set disagree, and the
        // pre-land list's own rank-0 card is silently dropped (the loop below starts at c=1).
        // MTG_TUTOR_AXIS_POSTLAND=1 enables. See docs/design/goblins-enabler-worse-games.md round 13.
        static const bool axis_postland = EnvOn("MTG_TUTOR_AXIS_POSTLAND", false);
        // MTG_TUTOR_AXIS_REBASE=1: re-resolve the BASE plan's target from the same list the variants
        // come from, instead of leaving whatever CollectActions picked. Two defects in one:
        //
        //   1. The base target is chosen during action COLLECTION, before a plan exists, so it can
        //      never be plan-aware. With MTG_GOBLIN_PLAN_AWARE on, the variants below become
        //      plan-aware while the base pick stays blind -- exactly the "one input honest, the rest
        //      calibrated to the old value" incoherence that cost +20/+9/+18 in rounds 12-14, only
        //      one level up, in the plan set rather than inside the model.
        //   2. The loop starts at c=1 on the assumption index 0 IS the base target. When the two
        //      lists disagree, this list's own top pick is never emitted at all.
        //
        // Rebasing makes base and variants come from one ranking at one state, which is the whole
        // point. Mutates `all` in place; `extra` is a separate vector, so this is safe.
        static const bool axis_rebase = EnvOn("MTG_TUTOR_AXIS_REBASE", false);
        std::map<std::string, std::vector<std::string>> cand_cache;
        for (TurnSolver::Plan& p : all)
        {
            // Base plans only -- one axis at a time, so cost stays additive (same trade as scry).
            if (p.scry_choice >= 0 || p.bp_choice >= 0) { continue; }
            for (std::size_t ai = 0; ai < p.actions.size(); ++ai)
            {
                const Action& act = p.actions[ai];
                if (act.kind != Action::Kind::CastFromHand || act.tutor_target.empty()) { continue; }
                const CardDefinition* d = CardDatabase::Instance().Lookup(act.card_name);
                if (d == nullptr || !(d->params.tutor_to_hand || d->params.tutor_to_top)) { continue; }
                // The plan this candidate list is FOR -- see PlanContext.h. Providers that ignore it
                // (all of them today) are byte-identical; the point is that the state mismatch above
                // is only half the missing information, and the other half is "what else does this
                // plan do this turn", which the provider currently guesses at.
                const PlanContext pc{ &p.actions, ai, &p.land_to_play,
                                      /*land_done=*/axis_postland && !p.land_to_play.empty() };
                PlanContextScope _pcs(&pc);
                if (prefix_stats)
                {
                    std::string sig = p.land_to_play + "|" + p.fetch_target + "|" + p.land_face + "|";
                    for (std::size_t j = 0; j < ai; ++j) { sig += p.actions[j].card_name + ";"; }
                    distinct_prefix.insert(sig);
                    ++prefix_plans;
                    prefix_pos_sum += ai;
                }
                std::string key = act.card_name;
                if (axis_postland)
                {
                    key += '\x1f'; key += p.land_to_play;
                    key += '\x1f'; key += p.fetch_target;
                    key += '\x1f'; key += p.land_face;
                }
                std::vector<std::string>& cands = cand_cache[key];
                if (cands.empty())
                {
                    if (axis_postland && !p.land_to_play.empty())
                    {
                        GameState ls = state;
                        PlayLandByName(ls, p.land_to_play, p.fetch_target, true, p.land_face);
                        cands = ResolveProvider(ls).TutorCandidates(ls, ls.active_player_index,
                                                                    d->params);
                    }
                    else
                    {
                        cands = ResolveProvider(state).TutorCandidates(state, state.active_player_index,
                                                                       d->params);
                    }
                }
                const std::size_t k = std::min(cands.size(), TutorAxisWidth(state));
                if (axis_rebase && !cands.empty() && !cands[0].empty())
                { p.actions[ai].tutor_target = cands[0]; }
                const std::string& base_tgt = p.actions[ai].tutor_target;
                for (std::size_t c = (axis_rebase ? 0 : 1); c < k; ++c)
                {
                    if (cands[c] == base_tgt) { continue; }
                    TurnSolver::Plan v = p;
                    v.actions[ai].tutor_target = cands[c];
                    extra.push_back(std::move(v));
                }
                break;   // vary ONE tutor per variant; a second tutor keeps its heuristic target
            }
        }
        if (prefix_stats && prefix_plans > 0)
        {
            std::fprintf(stderr, "[tutor-prefix] T%d plans=%zu distinct_prefixes=%zu avg_pos=%.2f\n",
                         state.turn_number, prefix_plans, distinct_prefix.size(),
                         static_cast<double>(prefix_pos_sum) / static_cast<double>(prefix_plans));
        }
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }

    // SEARCHED ETB-DIG PICK -- the same post-dedup fan-out as the tutor target above, for the same
    // reason: the pick is cost-neutral, so every variant shares a cast-name signature and the dedup
    // inside EnumeratePlans kept only the provider's first candidate. The base rule it replaces is
    // "first legal match in LOOK order", i.e. library order -- an arbitrary pick among the matches,
    // live in 94% of digs (MTG_ETBDIG_TRACE, 200 Knights games).
    //
    // Why the axis loses nothing this turn: the dug card enters HAND, and an ETB dig is not one of
    // the five breakpoint sites, so no re-solve follows and the card cannot be cast this turn. The
    // pick therefore cannot interact with the rest of this turn's subset -- the condition that makes
    // a second axis equivalent to the cross product rather than an approximation of it.
    if (EtbDigAxisEnabled() && EtbDigAxisWidth() > 1 && !HumanPlayActive())
    {
        std::vector<TurnSolver::Plan> extra;
        for (const TurnSolver::Plan& p : all)
        {
            // Base plans only -- one axis at a time, so cost stays additive (same trade as scry).
            if (p.scry_choice >= 0 || p.bp_choice >= 0 || p.etbdig_choice >= 0) { continue; }
            for (const Action& act : p.actions)
            {
                if (act.kind != Action::Kind::CastFromHand
                    && act.kind != Action::Kind::ActivateVial) { continue; }
                const CardDefinition* d = CardDatabase::Instance().Lookup(act.card_name);
                if (d == nullptr || d->params.etb_dig_count <= 0) { continue; }
                const std::size_t cands_now = EtbDigCandidateCountNow(state, d->params);
                const std::size_t k = std::min(cands_now, EtbDigAxisWidth());
                TRACE("etbdig", "T%d %s cands_now=%zu -> %zu variants",
                      state.turn_number, act.card_name.c_str(), cands_now, k > 0 ? k - 1 : 0);
                for (std::size_t c = 1; c < k; ++c)
                {
                    TurnSolver::Plan v = p;
                    v.etbdig_choice = static_cast<int>(c);
                    extra.push_back(std::move(v));
                }
                break;   // vary ONE dig per variant; a second dig keeps its ranked default
            }
        }
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }

    // SEARCHED CLEANUP DISCARD -- the post-dedup fan-out for the END-OF-TURN shed. Unlike every
    // other axis here the decision does not happen during the plan at all: it fires in
    // SimulateEndAndStartNextTurn, after the apply, which is why the pick rides the STATE.
    //
    // It is also the one axis that cannot size its candidate list at enumeration time -- the hand
    // that will be over the limit is the hand AFTER this turn's draws and casts, and for Treasure
    // Hunt the whole point is that a DrawUntilNonland resolving mid-turn is what floods it. So the
    // width is taken on faith and the index is clamped at resolution (the scry axis does the same),
    // and the axis is gated on the PROVIDER opting in rather than on a hand-size guess: five of
    // nine suite decks never reach a cleanup discard at all, and a variant pinning an index nothing
    // consumes is a duplicate plan that costs a rollout to discover it changed nothing.
    const int discard_width = ResolveProvider(state).CleanupDiscardSearchWidth();
    if (discard_width > 1 && !HumanPlayActive())
    {
        std::vector<TurnSolver::Plan> extra;
        for (const TurnSolver::Plan& p : all)
        {
            // Base plans only -- one axis at a time, so cost stays additive.
            if (p.scry_choice >= 0 || p.bp_choice >= 0 || p.etbdig_choice >= 0
                || p.lackey_choice >= 0 || p.ponder_choice >= 0) { continue; }
            for (int c = 1; c < discard_width; ++c)
            {
                TurnSolver::Plan v = p;
                v.discard_choice = c;
                extra.push_back(std::move(v));
            }
        }
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }

    // SEARCHED PONDER KEEP-vs-SHUFFLE -- the post-dedup fan-out that makes the decision real. Both
    // ponder_keep values are always legal, so unlike the tutor/dig axes there is no candidate list to
    // size: emit the two pinned alternatives and let the base plan carry the heuristic. One of the
    // two duplicates whatever the heuristic resolves to; a duplicate scores identically and the
    // search tie-breaks to the base plan, so it costs a variant but cannot change the answer.
    if (PonderAxisEnabled() && !HumanPlayActive())
    {
        std::vector<TurnSolver::Plan> extra;
        for (const TurnSolver::Plan& p : all)
        {
            // Base plans only -- one axis at a time, so cost stays additive.
            if (p.scry_choice >= 0 || p.bp_choice >= 0
                || p.etbdig_choice >= 0 || p.lackey_choice >= 0) { continue; }
            for (std::size_t ai = 0; ai < p.actions.size(); ++ai)
            {
                const Action& act = p.actions[ai];
                if (act.kind != Action::Kind::CastFromHand || act.ponder_keep >= 0) { continue; }
                const CardDefinition* d = CardDatabase::Instance().Lookup(act.card_name);
                if (d == nullptr || d->params.cast_reorder <= 0) { continue; }
                if (PonderAxisPartial() && !PonderSetIsMixed(state, d->params.cast_reorder)) { break; }
                if (PonderOrderAxis())
                {
                    // ORDER axis: branch on the DISPOSITION (which card ends up on top, plus the
                    // shuffle), not just keep-vs-shuffle. Ponder draws immediately, so the top card
                    // is what you actually receive; ReorderCandidatesNarrow keeps exactly the
                    // options that differ in that card (m + 1) instead of every permutation (m! + 1).
                    // Sized off the library as it stands now -- an earlier cantrip in the same plan
                    // can shift it, and the pin clamps, so a stale size costs a wasted or missed
                    // variant, never correctness.
                    const int look = std::min(d->params.cast_reorder,
                                              static_cast<int>(ap.library.size()));
                    const std::size_t k_max =
                        std::min<std::size_t>(static_cast<std::size_t>(look) + 1, PonderOrderWidth());
                    for (std::size_t k = 1; k < k_max; ++k)
                    {
                        TurnSolver::Plan v = p;
                        v.ponder_choice = static_cast<int>(k);
                        extra.push_back(std::move(v));
                    }
                }
                else
                {
                    for (int k = 0; k <= 1; ++k)
                    {
                        TurnSolver::Plan v = p;
                        v.actions[ai].ponder_keep = k;
                        extra.push_back(std::move(v));
                    }
                }
                break;   // vary ONE Ponder per variant; a second keeps its resolution heuristic
            }
        }
        TRACE("ponder", "T%d ponder axis -> %zu variants", state.turn_number, extra.size());
        all.insert(all.end(), std::make_move_iterator(extra.begin()),
                              std::make_move_iterator(extra.end()));
    }

    // SEARCHED GOBLIN LACKEY PUT -- the same post-dedup fan-out, keyed on the BOARD rather than on a
    // cast: the trigger belongs to a Lackey already in play, not to anything in `actions`. Only the
    // pre-combat plan can carry it, since the put resolves in that combat's damage step.
    if (LackeyAxisEnabled() && LackeyAxisWidth() > 1 && !HumanPlayActive() && is_pre_combat)
    {
        const std::size_t cands_now = LackeyCandidateCountNow(state);
        const std::size_t k = std::min(cands_now, LackeyAxisWidth());
        if (k > 1)
        {
            std::vector<TurnSolver::Plan> extra;
            for (const TurnSolver::Plan& p : all)
            {
                // Base plans only -- one axis at a time, so cost stays additive.
                if (p.scry_choice >= 0 || p.bp_choice >= 0
                    || p.etbdig_choice >= 0 || p.lackey_choice >= 0) { continue; }
                for (std::size_t c = 1; c < k; ++c)
                {
                    TurnSolver::Plan v = p;
                    v.lackey_choice = static_cast<int>(c);
                    extra.push_back(std::move(v));
                }
            }
            TRACE("lackey", "T%d cands_now=%zu -> %zu variants/plan", state.turn_number, cands_now, k - 1);
            all.insert(all.end(), std::make_move_iterator(extra.begin()),
                                  std::make_move_iterator(extra.end()));
        }
    }

    TRACE("plans", "T%d EnumeratePlansWithLand -> %zu plans (lands=%zu, hand=%zu)",
          state.turn_number, all.size(), land_names.size(), ap.hand.size());
    PROF_ADD(plans_generated, all.size());
    return all;
}

// ---- Transposition key over the future-determining state ------------------
//
// Folds every game-state field the rollout reads into a 128-bit key, plus the
// rollout depth. Fields the rollout never reads (graveyard, exile, poison, the
// always-empty rollout stack contents) are omitted so genuinely-equivalent
// states share a key. Order-sensitive sequences (hand, battlefield) are folded
// in order because plan tie-breaks and mana/sacrifice selection read that order.
// The library is keyed by remaining size + top card (see TranspositionTable.h
// for why size alone is exact within one decision).
namespace
{
    inline void Fold(TranspositionTable::Key& k, uint64_t v)
    {
        k.h1 ^= v + 0x9e3779b97f4a7c15ULL + (k.h1 << 6) + (k.h1 >> 2);
        k.h2 ^= (v * 0xff51afd7ed558ccdULL) + 0xc4ceb9fe1a85ec53ULL
              + (k.h2 << 5) + (k.h2 >> 3);
    }

    inline void FoldName(TranspositionTable::Key& k, const std::string& s)
    {
        Fold(k, static_cast<uint64_t>(std::hash<std::string>{}(s)));
    }
}

// MTG_CANON_SIMKEY=1 (EXPERIMENT, default OFF = byte-identical): fold the hand and battlefield as
// ORDER-INSENSITIVE multisets instead of ordered sequences. The ordered fold means "play Sandbar
// T3, Cave T4" and "Cave T3, Sandbar T4" -- the SAME position -- never share a memo entry, so with
// searched breakpoint continuations (which generate every play order) the FSLineCache/TT DAG
// degenerates into a tree: distinct-state growth counts SEQUENCES, x12/ply on treasure_hunt
// (docs/design/th-d5-five-hour-game.md). Canonicalizing collapses permutations. NOT provably
// byte-identical ON: greedy tie-breaks read vector order, so a memo hit may return a line computed
// under a permuted internal order -- the A/B (same answers?) is the test this flag exists for.
inline bool CanonSimKeyOn()
{
    static const bool v = EnvOn("MTG_CANON_SIMKEY");
    return v;
}

static TranspositionTable::Key BuildSimKey(const GameState& state, int depth, int max_turns,
                                           bool second_main)
{
    TranspositionTable::Key k;
    const bool canon = CanonSimKeyOn();
    // Order-insensitive fold of a section: sub-hash each item into its own Key, sort the
    // (h1,h2) pairs, fold them in sorted order. Sorting (not a commutative sum) keeps the
    // multiset exact -- no weaker-than-128-bit collision class is introduced.
    std::vector<std::pair<uint64_t, uint64_t>> canon_items;
    auto canon_flush = [&]()
    {
        std::sort(canon_items.begin(), canon_items.end());
        for (const auto& [a, b] : canon_items) { Fold(k, a); Fold(k, b); }
        canon_items.clear();
    };

    Fold(k, 0x5117); // section tag: scalars
    Fold(k, static_cast<uint64_t>(depth));
    Fold(k, static_cast<uint64_t>(max_turns));
    Fold(k, second_main ? 1u : 0u);
    Fold(k, static_cast<uint64_t>(state.turn_number));
    Fold(k, static_cast<uint64_t>(state.active_player_index));
    Fold(k, state.on_the_play ? 1u : 0u);
    Fold(k, state.opponent_lost_life_this_turn ? 1u : 0u);
    Fold(k, static_cast<uint64_t>(state.vial_target_mv));
    Fold(k, static_cast<uint64_t>(state.stack.size()));

    // With search-shuffle ON the library order is NO LONGER a deterministic function of
    // its size (a fetch/tutor reshuffles it), so the cheap (size + front) library digest
    // below would let two differently-ordered libraries share a memo entry -- a stale TT/
    // FSLineCache hit yielding a wrong rollout. Fold search_count (it seeds the NEXT
    // shuffle, so it distinguishes states with identical current order but different
    // futures) and the FULL ordered library (below) to make the key exact. OFF (default):
    // not folded => byte-identical keys, and the clairvoyant "size => content" assumption
    // still holds because nothing shuffles mid-search.
    const bool shuffle_keys = SearchShuffleEnabled();
    if (shuffle_keys) { Fold(k, 0x5ADF); Fold(k, state.search_count); }

    for (int pi = 0; pi < 2; ++pi)
    {
        const Player& p = state.players[pi];
        Fold(k, 0x9100 + static_cast<uint64_t>(pi)); // section tag: player pi
        Fold(k, static_cast<uint64_t>(p.life));
        Fold(k, static_cast<uint64_t>(p.lands_played_this_turn));
        Fold(k, static_cast<uint64_t>(p.bonus_land_drops_this_turn));
        Fold(k, static_cast<uint64_t>(p.library.size()));
        if (!p.library.empty()) { Fold(k, p.library.front().m_name_hash); }
        // Full ordered library when shuffle can reorder it (see above). Skipped when OFF.
        if (shuffle_keys) { for (const Card& c : p.library) { Fold(k, c.m_name_hash); } }

        Fold(k, 0x4A00 + static_cast<uint64_t>(pi)); // sub-section: hand (ordered; multiset when canon)
        Fold(k, static_cast<uint64_t>(p.hand.size()));
        for (const Card& c : p.hand)
        {
            if (canon)
            {
                TranspositionTable::Key ck;
                Fold(ck, c.m_name_hash);
                if (c.m_is_staged) { Fold(ck, static_cast<uint64_t>(c.m_staged_expiry)); }
                canon_items.emplace_back(ck.h1, ck.h2);
                continue;
            }
            Fold(k, c.m_name_hash);
            // A staged card's expiry changes when it can still be played, so two hands
            // with identical names but different staged expiries are different rollout
            // states. Folded ONLY when staged, so non-staging decks keep their exact
            // prior key (byte-identical results).
            if (c.m_is_staged) { Fold(k, static_cast<uint64_t>(c.m_staged_expiry)); }
        }
        if (canon) { canon_flush(); }

        Fold(k, 0x57A6 + static_cast<uint64_t>(pi)); // sub-section: staged cards
        Fold(k, static_cast<uint64_t>(p.staged_cards.size()));
        for (const StagedCard& sc : p.staged_cards)
        {
            Fold(k, sc.card.m_name_hash);
            Fold(k, static_cast<uint64_t>(sc.expiry_turn));
        }

        // Suspended cards (Lotus Bloom): a future-determining zone (they arrive on later turns), so it
        // MUST be folded or two states differing only in their suspend timers would collide in the TT.
        // Folded ONLY when non-empty (like the graveyard fold below), so a deck that never suspends keeps
        // the EXACT key -- and identical TT/budget behaviour -- it had before this change (byte-identical).
        if (!p.suspended_cards.empty())
        {
            Fold(k, 0x5C5D + static_cast<uint64_t>(pi)); // sub-section: suspended cards
            Fold(k, static_cast<uint64_t>(p.suspended_cards.size()));
            for (const SuspendedCard& sc : p.suspended_cards)
            {
                Fold(k, sc.card.m_name_hash);
                Fold(k, static_cast<uint64_t>(sc.arrive_turn));
            }
        }

        // Graveyard: the rollout reads it only via Retrace, so fold it ONLY when it
        // holds a retrace-castable card. Decks without retrace never fold it and keep
        // the exact key — and identical TT/budget behaviour — they had before. Folded
        // order-insensitively (commutative sum of name hashes): two states differing
        // only in cast/discard order share a key, so this never adds spurious misses.
        {
            uint64_t gy_acc       = 0;
            bool     gy_retraceable = false;
            for (const Card& c : p.graveyard)
            {
                gy_acc += c.m_name_hash;  // cached std::hash(m_name)
                const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
                if (cdef && cdef->params.retrace) { gy_retraceable = true; }
            }
            if (gy_retraceable)
            {
                Fold(k, 0x6748 + static_cast<uint64_t>(pi)); // sub-section: graveyard (retrace live)
                Fold(k, static_cast<uint64_t>(p.graveyard.size()));
                Fold(k, gy_acc);
            }
        }
    }

    Fold(k, 0xB1F1); // section tag: battlefield (ordered; multiset when canon)
    Fold(k, static_cast<uint64_t>(state.battlefield.size()));
    for (const Permanent& perm : state.battlefield)
    {
        TranspositionTable::Key ck;             // canon: per-perm sub-key, folded sorted below
        TranspositionTable::Key& tk = canon ? ck : k;
        Fold(tk, perm.card.m_name_hash);
        Fold(tk, static_cast<uint64_t>(perm.controller_index));
        Fold(tk, perm.tapped ? 1u : 0u);
        Fold(tk, perm.entered_this_turn ? 1u : 0u);
        Fold(tk, perm.is_animated ? 1u : 0u);
        Fold(tk, static_cast<uint64_t>(perm.charge_counters));
        Fold(tk, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_power_bonus)));
        Fold(tk, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_tough_bonus)));
        Fold(tk, static_cast<uint64_t>(static_cast<int64_t>(perm.damage)));
        for (const Counter& ctr : perm.counters)
        {
            Fold(tk, static_cast<uint64_t>(ctr.type));
            Fold(tk, static_cast<uint64_t>(static_cast<int64_t>(ctr.count)));
        }
        if (canon) { canon_items.emplace_back(ck.h1, ck.h2); }
    }
    if (canon) { canon_flush(); }

    return k;
}

// Simulate from the current state (at the START of a pre-combat main phase,
// land already played) to game end. Uses SolveWithLookahead(depth) for
// pre-combat decisions. A post-combat (second) main is played only when
// second_main is set (greedy, via Solve) — see AIEngine::TakeTurn for why it is
// otherwise skipped. Returns the win turn, or max_turns+1 if not won in time.
// MTG_TT_NOWIN_CACHE: DEFAULT OFF pending measurement. The LEAF table (TranspositionTable, written by
// SimulateToEnd) has the same asymmetry FSLineCache had before 2026-08-05 -- it stores only WINS, so a
// position whose rollouts never find a win caches nothing and every leaf re-rolls from scratch.
// Measured on treasure_hunt seed 9010 gi 1: 138,346 lookups, 0 stores, 100% no-win.
// See docs/design/th-d5-five-hour-game.md.
inline bool TTNoWinCacheOn()
{
    static const bool v = EnvOn("MTG_TT_NOWIN_CACHE");
    return v;
}

static int SimulateToEndImpl(GameState& state, int depth, int max_turns,
                             SearchBudget* budget, int cutoff_turn,
                             bool second_main, TranspositionTable* tt)
{
    // TRUNCATED ROLLOUT (MTG_ROLLOUT_HORIZON=K, default -1 = unlimited/off): after K simulated turns with
    // no win yet, cap the tail with the O(1) value-leaf estimate instead of playing greedily to game end.
    // Bridges the pure value leaf (K=0) and the full rollout (K=inf), cutting the dominant per-leaf cost --
    // especially the escalation's many rollouts. A fidelity trade (NOT byte-identical when on) -> sweep K
    // for quality vs speed. Only caps when a value model is attached; value-less decks run the full rollout.
    static const int s_roll_horizon = []{ const char* e = std::getenv("MTG_ROLLOUT_HORIZON");
                                          return (e && *e) ? std::atoi(e) : -1; }();
    const int roll_start = state.turn_number;
    if (s_rollout_stats) { g_rollout_calls.fetch_add(1, std::memory_order_relaxed); }   // deterministic telemetry
    while (state.turn_number <= max_turns)
    {
        // Branch-and-bound: a line that hasn't won by cutoff_turn cannot beat the
        // incumbent best win turn, so abandon it (win turn only grows from here).
        // Abort only AFTER cutoff_turn so a win exactly on cutoff_turn still
        // registers for the value tiebreak.
        if (state.turn_number > cutoff_turn) { return max_turns + 1; }

        // Truncated-rollout cap: K turns simulated with no win yet -> hand the tail to the cheap value leaf
        // (see MTG_ROLLOUT_HORIZON above). Fires only for lines that DON'T win fast (the expensive ones);
        // fast wins are fully simulated and never reach here.
        if (s_roll_horizon >= 0 && (state.turn_number - roll_start) >= s_roll_horizon
            && state.m_value_model && !state.m_value_model->empty())
        {
            const std::vector<int> feats = ExtractMidGameFeatures(state, MidGamePlanSummary{});
            int w = static_cast<int>((state.m_value_model->Score(feats) + 500) / 1000);
            if (w < state.turn_number) { w = state.turn_number; }
            if (w > max_turns)         { w = max_turns + 1; }
            return w;
        }

        // Count one work unit per simulated turn-step. The rollout normally never self
        // truncates on the budget — it only consumes; the top-level decision decides when
        // to stop adding more rollouts. EXCEPTION: the mid-pass OVERRUN ceiling (armed by
        // FullSearchLine). A pathological no-early-win decision spawns a huge number of these
        // deep leaf rollouts; once the pass has blown kOverrunBeta x the whole budget, bail
        // out here too (return no-win) so the leaf can't run unbounded. Overrun() is false
        // unless armed (m_overrun_limit==0 on the baseline path / normal decisions), so this
        // is byte-identical for every non-pathological rollout.
        if (budget) { budget->Consume(1); }
        if (s_rollout_stats) { g_rollout_steps.fetch_add(1, std::memory_order_relaxed); }   // one simulated turn-step
        if (budget && budget->Overrun()) { ++g_fs_trunc_events; return max_turns + 1; }

        // Expire staged (Light Up the Stage) cards whose play window has passed,
        // mirroring AIEngine::TakeTurn's expiry check (CR 406). Without this the
        // rollout would keep casting cards the real game has already lost. The hand
        // only ever holds staged cards for staging decks, so this is a no-op (and
        // byte-identical) for every other deck.
        {
            Player& rp = state.ActivePlayer();
            rp.hand.erase(std::remove_if(rp.hand.begin(), rp.hand.end(),
                [&](const Card& c)
                {
                    return c.m_is_staged && c.m_staged_expiry < state.turn_number;
                }), rp.hand.end());
        }

        // Pre-combat main: pick and apply plan (includes Vial activations), then animate + tokens.
        TurnSolver::Plan pre_plan;
        if (g_honest_teacher && depth > 0)
        {
            // HONEST-TEACHER label: choose this turn's plan against a RESHUFFLED unseen library (a
            // random future), then RESOLVE it against the true order below. The plan references only
            // the known hand/battlefield, so it transfers to the real state; any within-turn dig
            // resolves against the true library in ApplyPlanDirect. This strips the base-draw-order
            // clairvoyance a depth>0 lookahead would otherwise have (g_shuffle_eval only decouples
            // mid-game shuffle EVENTS, not the opening order). Applied at EVERY continuation turn ->
            // the whole continuation policy is non-clairvoyant ("E inside the max" at every ply).
            // The reshuffle salt folds shuffle_salt_search (varied per-k by the dump loop) so the K
            // outer samples get independent futures. tt is NOT shared: the clairvoyant leaf memo keys
            // on library SIZE (BuildSimKey folds order only when SearchShuffleEnabled), so reusing it
            // would return a real-order value for a reshuffled state -- pass nullptr (offline tool).
            GameState ss = state;
            const uint64_t hsalt = SaltSeed(
                state.game_seed + 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(state.turn_number),
                state.shuffle_salt_search);
            ss.ActivePlayer().library.Shuffle(hsalt);
            pre_plan = TurnSolver::SolveWithLookahead(
                ss, true, depth, max_turns, budget, false, second_main, nullptr);
        }
        else
        {
            pre_plan = TurnSolver::SolveWithLookahead(
                state, true, depth, max_turns, budget, false, second_main, tt);
        }
        int life_before_pl = state.Opponent().life;
        ApplyPlanDirect(state, pre_plan, true);
        AnimateLandsShared(state, nullptr);
        ActivateTapTokensShared(state, nullptr);

        // Combat
        SimulateCombat(state);
        if (state.Opponent().life <= 0)
        {
            return state.turn_number;
        }

        // Post-combat (second) main, only for second-main-relevant decks (e.g.
        // spectacle finishers unlocked by combat damage). Played greedily here in
        // the rollout; the real game searches it. Skipped entirely otherwise — in a
        // goldfish combat creates no new resources, so everything was castable in
        // the first main, and modelling a second main the real game skips would let
        // the search optimise against plays that never happen. See AIEngine::TakeTurn.
        if (second_main)
        {
            TurnSolver::Plan post_plan;
            if (g_honest_teacher && depth > 0)
            {
                // Honest ceiling policy: SEARCH the continuation's second main against a
                // RESHUFFLED library (mirroring the pre-combat above), then resolve against
                // the true order. Greedy here would understate a second-main deck's finisher
                // sequencing at every future ply -> understate the ceiling -> OVERSTATE EVPI.
                // This is the last greedy site in the non-clairvoyant continuation policy.
                GameState ss2 = state;
                const uint64_t hsalt2 = SaltSeed(
                    state.game_seed
                        + 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(state.turn_number)
                        + 7919ULL,   // distinct stream from the pre-combat reshuffle
                    state.shuffle_salt_search);
                ss2.ActivePlayer().library.Shuffle(hsalt2);
                post_plan = TurnSolver::SolveWithLookahead(
                    ss2, false, depth, max_turns, budget, false, second_main, nullptr);
            }
            else
            {
                // Default rollout: greedy second main (speed). The real game searches it.
                post_plan = TurnSolver::Solve(state, false);
            }
            ApplyPlanDirect(state, post_plan, false);
            if (state.Opponent().life <= 0) { return state.turn_number; }
        }

        // End of turn + start of next. The next turn's land drop is searched as part
        // of that turn's plan (folded into SolveWithLookahead / played by
        // ApplyPlanDirect), so no greedy land play happens here.
        if (!SimulateEndAndStartNextTurn(state)) { return max_turns + 1; }
    }
    return max_turns + 1;
}

// Memoizing wrapper around SimulateToEndImpl. On a cache hit the rollout is
// skipped entirely (and no budget is consumed — avoided recompute is the point).
// Only REAL win turns (<= max_turns) are cached: they are exact and cutoff-
// independent, whereas a max_turns+1 result may be a branch-and-bound abort
// rather than a genuine no-win, so it is never stored. See TranspositionTable.h.
// Takes the rollout's starting state by rvalue reference: the sole caller passes a
// throwaway scratch copy it no longer needs, so binding (and, on a miss, mutating)
// it in place avoids a full GameState deep clone of the whole state on every call —
// previously the by-value parameter cloned ~60 cards even when the very next line
// returned a transposition-table hit. The TT key is read before any mutation, so
// the result is byte-identical to the by-value version.
static int SimulateToEnd(GameState&& state, int depth, int max_turns,
                         SearchBudget* budget, int cutoff_turn,
                         bool second_main, TranspositionTable* tt)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    ShuffleEvalGuard _seg(true);  // decoupling instrument: rollout shuffles use shuffle_salt_search
    TranspositionTable::Key key;
    if (tt != nullptr)
    {
        key = BuildSimKey(state, depth, max_turns, second_main);
        PROF_INC(tt_lookups);
        const int* cached = tt->Lookup(key);
        if (cached != nullptr)
        {
            PROF_INC(tt_hits);
            // SOUNDNESS HARNESS (MTG_LEAF_VERIFY): recompute this hit fresh (loose cutoff, no tt/budget so it
            // fully resolves) and compare. A mismatch means two states shared a BuildSimKey but roll out
            // differently => the key omits some rollout-determining state. Counts mismatches + dumps the first.
            static const bool s_leaf_verify = EnvOn("MTG_LEAF_VERIFY");
            if (s_leaf_verify)
            {
                GameState copy = state;
                const int fresh = SimulateToEndImpl(copy, depth, max_turns, nullptr, max_turns + 1,
                                                    second_main, nullptr);
                // Only REAL wins are cached, so compare the cached real win to a fresh real win. A fresh no-win
                // (max_turns+1) at a cached real win is also a mismatch (the cache claims a win that isn't).
                if (fresh != *cached)
                {
                    static std::atomic<long long> s_mm{0};
                    const long long n = s_mm.fetch_add(1) + 1;
                    if (n <= 8)
                    {
                        std::cerr << "[leaf-verify] STALE HIT #" << n << " turn=" << state.turn_number
                                  << " depth=" << depth << " cached=" << *cached << " fresh=" << fresh
                                  << " (key collides two different-rollout states)\n";
                    }
                }
            }
            return *cached;
        }
        // Bound-qualified NO-WIN. The entry answers only "no win at turn <= bound", so a query asking
        // about a LATER turn than the refutation covered must re-roll. Cheap: one extra map probe on
        // the miss path, and only when the flag is on.
        if (TTNoWinCacheOn())
        {
            const int* bound = tt->LookupNoWinBound(key);
            if (bound != nullptr && cutoff_turn <= *bound) { PROF_INC(tt_nowin_hit); return max_turns + 1; }
        }
    }

    // Watermark: SimulateToEndImpl returns a FALSE no-win when the budget overruns (it bumps
    // g_fs_trunc_events at that site). Such a result is "I ran out", not "there is no win", and
    // storing it would poison the table with fabricated losses.
    const unsigned long long trunc_at_entry = g_fs_trunc_events;

    int result = SimulateToEndImpl(state, depth, max_turns, budget, cutoff_turn, second_main, tt);

    if (tt != nullptr && result <= max_turns) { PROF_INC(tt_stores); tt->Store(key, result); }
    else if (tt != nullptr)
    {
        PROF_INC(tt_nowin);
        // The rollout aborts at `turn > cutoff_turn`, so the refutation covers exactly cutoff_turn --
        // clamped to max_turns+1, the widest question the search can ask, so an unbounded query hits.
        if (TTNoWinCacheOn() && g_fs_trunc_events == trunc_at_entry)
        {
            PROF_INC(tt_nowin_stored);
            tt->StoreNoWin(key, std::min(cutoff_turn, max_turns + 1));
        }
    }
    return result;
}

// ---- Full-depth search (experimental, MTG_FULL_DEPTH) -------------------------
//
// "depth N" here means: fully search N COMPLETE turns (pre-combat main + combat +
// optional second main), branching over EVERY plan at each phase, then estimate
// the tail with a greedy rollout. Objective = earliest win turn, with
// branch-and-bound: a this-turn win is the hard floor; any branch that cannot
// beat the running best is pruned (`cutoff`). Contrast SolveWithLookahead, which
// reduces future-turn fidelity (shallower rollout + greedy second main).

// Interior-node memo for the full-depth search: maps a pre-combat-main state
// (+ remaining search depth, folded into the key) to the optimal SearchLine from
// it. Different opening sequences that transpose to the same later board reuse one
// another's result instead of re-searching the whole subtree -- the big cost the
// leaf SimulateToEnd table does NOT touch. Like that table it is per-FullSearchLine
// scope (one fixed root library, so library size uniquely identifies remaining
// content -- the cached line's draw-dependent breakpoint_actions stay valid).
//
// A genuine WIN is cutoff-independent (pruning never removes a strictly-earlier win, and selection
// replaces only on strict improvement), so it is stored unconditionally and reused by any query.
// A NO-WIN used to be discarded entirely, on the grounds that it "may be a branch-and-bound abort".
// That is true but throws away the commonest result in the search: every dead-end subtree is
// re-explored from scratch at every transposition. The classic fix is to qualify it -- store the
// cutoff the refutation was proved under and reuse it only for a query that asks no more. See
// FSLineEntry::nowin_bound.
struct FSLineEntry
{
    TurnSolver::SearchLine line;
    // For a NO-WIN: "no win at turn <= nowin_bound exists from here". Sound because a node whose
    // result is a no-win never tightened its own incumbent (best stays max_turns+1, so every child
    // was searched at the full cutoff) -- the bound is therefore exactly the cutoff it ran under.
    // A WIN carries INT_MAX: valid for every query, i.e. byte-identical to the old behaviour.
    int nowin_bound = std::numeric_limits<int>::max();
};
using FSLineCache = std::unordered_map<TranspositionTable::Key, FSLineEntry,
                                       TranspositionTable::KeyHash>;

// MTG_FS_NOWIN_CACHE: DEFAULT ON; =0 restores the win-only memo.
//
// This is NOT a byte-identical speedup, and the reason is worth stating precisely: a cache hit
// consumes no budget, so under a bounded search the memo does not make a decision finish sooner --
// it makes the same budget go FURTHER. Per decision, either the search converges under budget (the
// memo saves real work and the answer is identical) or the budget binds (the freed units buy depth
// and the line moves). Both happen, and the first dominates heavily.
//
// Measured before adopting (2026-08-05):
//   SOUND      unbounded d3, 1800 games, 9 decks -> byte-identical digests. With no budget to
//              reallocate a sound memo must return the same line, and it does. Corroborated by the
//              `stale` counter (a query rejected for too narrow a bound) being 0 on every deck.
//   PLAY       smoke 25/27, regression 38/45. Every changed case is same-avg/different-digest
//              except hinata_d3_s2002 (5.7150 -> 5.7100 = one game won a turn earlier). Of 15
//              games that played differently, 1 better and 0 worse. Reference gate: 0 play-drift.
//   COST       d5/20ms, 200 games: Hinata 58.6s -> 16.6s (3.53x, -58.3% search nodes),
//              treasure_hunt 22.3s -> 16.9s (1.32x), the rest ~neutral. Do NOT reduce that to one
//              multiplier -- the aggregate is just Hinata, which dominates every suite's cost.
//
// The OFFLINE labeller forces it on for itself regardless (see the guard below): there the budget is
// effectively unbounded, so there is nothing for freed budget to change and the labels come out
// identical -- while the saving is what makes the horizon ladder affordable at all.
inline thread_local bool g_force_nowin_cache = false;
struct ForceNoWinCacheGuard
{
    bool prev;
    explicit ForceNoWinCacheGuard(bool v) : prev(g_force_nowin_cache) { g_force_nowin_cache = v; }
    ~ForceNoWinCacheGuard() { g_force_nowin_cache = prev; }
};
inline bool FSNoWinCacheOn()
{
    static const bool v = EnvOn("MTG_FS_NOWIN_CACHE", true);
    return v || g_force_nowin_cache;
}

// Store a WIN: final and cutoff-independent, so it supersedes any bounded no-win a looser earlier
// query left behind. With the no-win cache off no such entry can exist, so only the emplace branch
// is ever reached == the old `lc->emplace(key, line)` exactly.
inline void FSLineStoreWin(FSLineCache* lc, const TranspositionTable::Key& key,
                           const TurnSolver::SearchLine& line)
{
    if (lc == nullptr) { return; }
    FSLineCache::iterator it = lc->find(key);
    if (it == lc->end())
    { lc->emplace(key, FSLineEntry{ line, std::numeric_limits<int>::max() }); }
    else if (it->second.nowin_bound != std::numeric_limits<int>::max())
    { it->second = FSLineEntry{ line, std::numeric_limits<int>::max() }; }
}

// Store a NO-WIN proved under `bound`. A later, WIDER refutation of the same node supersedes a
// narrower one; a win entry is never downgraded.
inline void FSLineStoreNoWin(FSLineCache* lc, const TranspositionTable::Key& key,
                             const TurnSolver::SearchLine& line, int bound)
{
    if (lc == nullptr) { return; }
    FSLineCache::iterator it = lc->find(key);
    if (it == lc->end())                                        { lc->emplace(key, FSLineEntry{ line, bound }); }
    else if (it->second.nowin_bound != std::numeric_limits<int>::max()
             && it->second.nowin_bound < bound)                 { it->second.nowin_bound = bound; }
}

// Hybrid value-leaf policy (MTG_VALUE_MIN_DEPTH, read at the caller): the learned value-leaf is
// exact-parity with the heuristic rollout only at PASS depth >= ~5 (measured; shallower it costs quality:
// antilife d4 +0.06, d3 +0.25). Rather than gate per-pass (which starves the deep pass of budget and
// collapses to heuristic), the caller (AIEngine) runs the whole search with the cheap value-leaf, then --
// only if the committed pass landed SHALLOWER than the safe depth -- re-runs it with the exact heuristic
// leaf (forced via g_force_heuristic_leaf). This keeps full value-leaf speed whenever the search reaches
// the safe depth, and falls back to heuristic quality (no regression) when it can't. See TurnSolver::
// ForceHeuristicLeafGuard / AIEngine's full-depth path / learned-d0-policy.md.
inline thread_local bool g_force_heuristic_leaf = false;
struct ForceHeuristicLeafGuard
{
    bool prev;
    explicit ForceHeuristicLeafGuard(bool v) : prev(g_force_heuristic_leaf) { g_force_heuristic_leaf = v; }
    ~ForceHeuristicLeafGuard() { g_force_heuristic_leaf = prev; }
};
// The INVERSE (MTG_LADDER_VALUE_LEAF): use the cheap value leaf even where the caller asked for the
// heuristic one. Set only on FullSearchLine's NON-committed ladder passes -- the deepest pass, which
// is the one whose line is committed, always gets the leaf the caller asked for. See LadderValueLeaf().
inline thread_local bool g_force_value_leaf = false;
struct ForceValueLeafGuard
{
    bool prev;
    explicit ForceValueLeafGuard(bool v) : prev(g_force_value_leaf) { g_force_value_leaf = v; }
    ~ForceValueLeafGuard() { g_force_value_leaf = prev; }
};

// K-predictor state (MTG_ESC_PREDICT). The PROBE ladders d1..committed for free (cheap value leaf) and, per
// depth, reveals the leaf count -- the quantity the escalation's dominant rollout cost scales with. The
// escalation reuses that MEASURED structure to predict its own deepest affordable depth K, runs ONE pass at
// K (which IS the committed pass when the estimate is right), and only falls back to K-1 if K's actual cost
// aborts. Thread_local: each worker pairs its own probe->escalation, and the values depend only on the
// current decision's search (deterministic). g_fs_leaf_evals is a running counter; per-pass deltas are the
// per-depth leaf counts, snapshotted by FullSearchLine into g_probe_leaves while g_probe_recording is set.
inline thread_local long long g_fs_leaf_evals   = 0;
// ESCALATION BEAM (value-guided frontier pruning): when > 0, FSLineWin / FSLineTail expand only the top
// `g_esc_beam_width` MoveOrderPlans-ranked plans per node. The escalation re-search then visits only the
// probe's top value-ranked lines (a W^depth frontier) and pays for exactly that many heuristic rollouts --
// the escalation's dominant (94%) cost -- instead of the full B&B frontier. The value ordering IS the probe's
// "first pass" ranking, so this reuses that work; only the incremental rollouts are new. 0 = unlimited =
// byte-identical (the probe always runs with 0). Set only around the heuristic escalation pass. See the
// escalation-fallback / verify-the-line work.
inline thread_local int g_esc_beam_width = 0;
// The beam applies ONLY to nodes at remaining depth <= g_esc_beam_leafdepth (near the leaf, where the frontier
// is widest = most of the rollout cost). Nodes ABOVE it (the top plies -- crucially the ROOT, which is the play
// we actually commit) keep the full static MoveOrder exploration, so a narrow beam can never drop the
// heuristic-best PLAY -- it only prunes the deep win-turn-ESTIMATE frontier. INT_MAX (default when
// MTG_ESC_BEAM_LEAFDEPTH is unset) => beam every node (uniform beam, the original behavior). See the
// depth-aware-beam work.
inline thread_local int g_esc_beam_leafdepth = 2147483647;
// STATIC beam (MTG_ESC_BEAM_STATIC): keep the beam's WIDTH cap but DON'T reorder `pre` by the probe's value
// ranking -- prune by the static MoveOrderPlans order instead. Diagnostic for shallow searches, where the
// value leaf is a weak ranking proxy so the value reorder can perturb which line commits (measured d3 quality
// drift even at no-prune width). 0 = value-ranked (default, the adopted behavior).
inline thread_local bool g_esc_beam_static = false;
struct EscBeamGuard
{
    int prev_w, prev_ld; bool prev_st;
    EscBeamGuard(int w, int ld, bool st)
        : prev_w(g_esc_beam_width), prev_ld(g_esc_beam_leafdepth), prev_st(g_esc_beam_static)
    { g_esc_beam_width = w; g_esc_beam_leafdepth = ld; g_esc_beam_static = st; }
    ~EscBeamGuard() { g_esc_beam_width = prev_w; g_esc_beam_leafdepth = prev_ld; g_esc_beam_static = prev_st; }
};
// VALUE-RANKED BEAM reuse: when the beam is enabled, the probe (value-leaf) pass RECORDS, per interior node,
// the value-win-turn each MoveOrderPlans-ordered plan produced (indexed by its position in the ordered `pre`).
// The escalation then reorders that same `pre` by these recorded value ranks before beam-capping, so the top-W
// beam holds the lines the VALUE pass actually rated best -- the faithful "reuse the first pass's ranking"
// (vs. the static MoveOrderPlans order, which misses the value-best line at ~knife-edge nodes). Recorded only
// on the probe's FULL-loop completion (non-winning nodes, where every plan was evaluated and ordering matters);
// win-nodes early-return and don't need reordering. Keyed by the SAME BuildSimKey the FSLineCache uses, so the
// escalation's identical `pre` maps position-for-position. thread_local (per worker's probe->escalation pair);
// null map / recording off => zero overhead => byte-identical. Only allocated when MTG_ESC_BEAM > 0.
using ProbePlanVals = std::unordered_map<TranspositionTable::Key, std::vector<int>,
                                         TranspositionTable::KeyHash>;
inline thread_local ProbePlanVals* g_probe_plan_vals   = nullptr;   // node key -> per-plan value-win-turns
inline thread_local bool           g_probe_val_recording = false;   // set around the probe pass when beaming
struct ProbeValsGuard   // point g_probe_plan_vals at a per-decision map for the probe->escalation pair
{
    ProbePlanVals* prev;
    explicit ProbeValsGuard(ProbePlanVals* m) : prev(g_probe_plan_vals) { g_probe_plan_vals = m; }
    ~ProbeValsGuard() { g_probe_plan_vals = prev; }
};
inline thread_local long long g_probe_leaves[16] = {0};   // probe leaf count at each pass depth (0 = unmeasured)
inline thread_local long long g_probe_cost[16]   = {0};   // probe budget-work at each pass depth
inline thread_local bool       g_probe_recording = false; // set while the probe's FullSearchLine ladders
// AUDIT-only: the HEURISTIC ladder's per-pass INCREMENTAL cost (what the start-gate actually consumes -- the
// memo makes this much less than a cold pass). Recorded around the audit shadow ladder to reveal the true
// commit recurrence. Not on the hot path (only when g_hlad_recording is set inside the audit block).
inline thread_local long long g_hlad_cost[16]   = {0};
inline thread_local long long g_hlad_leaves[16] = {0};
inline thread_local bool       g_hlad_recording = false;
// AMORTIZED per-leaf ROLLOUT cost (MTG_ESC_PREDICT): a running estimate learned from each escalation's actual
// cold pass at its committed depth. Persisting it across escalations (a) removes the per-escalation d1
// calibration pass (whose cost ~= the shallow pass we skip, erasing the saving for shallow commits), and (b)
// FIXES the depth-decay bias -- R learned from DEEP committed passes reflects deep-leaf rollout cost (~125),
// not the inflated d1 cost (~194). Seeded lazily on the first escalation. 0 = uninitialized.
inline thread_local double g_esc_R = 0.0;
// AUDIT-only: the climb's per-depth MEASURED pass costs + start depth, for the lossy-case dump.
inline thread_local double    g_climb_cmeas[16] = {0};
inline thread_local int       g_climb_start = 0;

static TurnSolver::SearchLine FSLineWin(const GameState& state, int depth, int max_turns,
                                        int cutoff, bool second_main, TranspositionTable* tt,
                                        FSLineCache* lc, SearchBudget* budget);

// Expire staged (Light Up the Stage) cards whose play window has passed, mirroring
// SimulateToEndImpl's top-of-turn expiry and AIEngine::TakeTurn's merge skip (CR
// 406). Without this the full-depth search keeps casting cards the real game has
// already lost, over-valuing deferral and picking a slower line. No-op for decks
// that never stage cards (hand never holds m_is_staged cards).
static void ExpireStagedCards(GameState& state)
{
    Player& rp = state.ActivePlayer();
    rp.hand.erase(std::remove_if(rp.hand.begin(), rp.hand.end(),
        [&](const Card& c) { return c.m_is_staged && c.m_staged_expiry < state.turn_number; }),
        rp.hand.end());
}

// `state` is positioned just AFTER this turn's combat, opponent still alive.
// Returns the best line (min win turn) for the optional second main this turn plus
// `depth` further complete turns. The returned line is prefixed with the chosen
// second-main phase (when second_main) and continues with the recursed turns.
// `cutoff` is the incumbent best (a line that cannot win by it is abandoned).
static TurnSolver::SearchLine FSLineTail(const GameState& state, int depth, int max_turns,
                                         int cutoff, bool second_main, TranspositionTable* tt,
                                         FSLineCache* lc, SearchBudget* budget)
{
    // Mid-pass overrun guard (see FSLineWin): abort the runaway pass.
    if (budget && budget->Overrun()) { ++g_fs_trunc_events; return { max_turns + 1, {} }; }
    if (second_main)
    {
        std::vector<TurnSolver::Plan> post = EnumeratePlans(state, false);
        // Always allow casting nothing in the second main and just advancing the
        // turn. EnumeratePlans returns an empty vector when no post-combat play is
        // castable (e.g. the pre-combat main tapped out), so without this the loop
        // below would never run and the whole line would be (wrongly) scored as a
        // no-win — making any tap-out play look strictly worse than idling. The
        // baseline rollout always advances past an empty second main; mirror that.
        post.push_back(TurnSolver::Plan{});
        MoveOrderPlans(post);   // lethal-looking / higher-value second mains first -> earlier cutoff
        // NOTE: we do NOT shortcut on the projected `wins_this_turn` flag here. That
        // projection (pending_atk + direct_dmg >= opp life) can over-count what the
        // actual ApplyPlanDirect + SimulateCombat deals, and trusting it would commit
        // a phantom win turn the replayed line never realises. The loop below decides
        // lethality by actually simulating, so the committed line's win turn always
        // matches replaying it. (Baseline SolveWithLookahead can trust the projection
        // because it re-decides every turn; commit-the-line locks the line in.)
        TurnSolver::SearchLine best;
        best.win_turn = max_turns + 1;
        int _beam_i = 0;
        const bool beam_here = (g_esc_beam_width > 0 && depth <= g_esc_beam_leafdepth);
        for (const TurnSolver::Plan& q : post)
        {
            // The beam leaves plans unexplored, so a no-win from this node is not a refutation.
            if (beam_here && _beam_i++ >= g_esc_beam_width) { ++g_fs_trunc_events; break; }   // value-guided beam (near-leaf only)
            if (budget) { budget->Consume(1); }   // one interior node (plan applied)
            GameState s2 = state;
            std::vector<Action> bp;
            ApplyPlanDirect(s2, q, false, &bp);
            // Self-lethal second main (Eidolon on-cast self-damage) -> we die to the
            // triggers before the spell resolves; not a viable line. See FSLineWin.
            if (s2.ActivePlayer().life <= 0) { continue; }
            if (s2.Opponent().life <= 0)
            {
                TurnSolver::Plan q_rec = q;
                q_rec.breakpoint_actions = std::move(bp);
                return { state.turn_number, { { false, std::move(q_rec) } } };
            }
            if (!SimulateEndAndStartNextTurn(s2)) { continue; }
            ExpireStagedCards(s2);
            TurnSolver::SearchLine sub =
                FSLineWin(s2, depth, max_turns, std::min(cutoff, best.win_turn), second_main, tt, lc, budget);
            if (sub.win_turn < best.win_turn)
            {
                best.win_turn = sub.win_turn;
                best.phases.clear();
                TurnSolver::Plan q_rec = q;
                q_rec.breakpoint_actions = std::move(bp);
                best.phases.push_back({ false, std::move(q_rec) });
                best.phases.insert(best.phases.end(), sub.phases.begin(), sub.phases.end());

                // Stop at the first VERIFIED win (within horizon) -- the pass minimum.
                // Same reasoning as FSLineWin; the second-main FSLineWin runs at turn+1
                // with `depth` more turns, so its horizon edge is state.turn_number+depth.
                if (sub.win_turn <= state.turn_number + depth)
                {
                    return best;
                }
            }
        }
        return best;
    }

    GameState s = state;
    if (!SimulateEndAndStartNextTurn(s)) { return { max_turns + 1, {} }; }
    ExpireStagedCards(s);
    return FSLineWin(s, depth, max_turns, cutoff, second_main, tt, lc, budget);
}

// `state` is positioned at the START of the active player's pre-combat main (land
// not yet played; EnumeratePlansWithLand folds the land choice). Returns the best
// line (min win turn) fully searching `depth` complete turns from here, prefixed
// with the chosen pre-combat phase.
static TurnSolver::SearchLine FSLineWin(const GameState& state, int depth, int max_turns,
                                        int cutoff, bool second_main, TranspositionTable* tt,
                                        FSLineCache* lc, SearchBudget* budget)
{
    if (state.turn_number > max_turns) { return { max_turns + 1, {} }; }
    if (state.turn_number > cutoff)    { return { max_turns + 1, {} }; }  // can't beat incumbent
#ifdef MTG_PROFILE
    if (state.turn_number >= 0 && state.turn_number < 12) { PROF_INC(fsw_by_turn[state.turn_number]); }
    if (depth >= 0 && depth < 12)                         { PROF_INC(fsw_by_depth[depth]); }
#endif
    // Mid-pass overrun: this pass has blown far past its budget estimate; bail out with a
    // no-win so FullSearchLine rolls back to the last completed pass (see SetOverrunLimit).
    if (budget && budget->Overrun())   { ++g_fs_trunc_events; return { max_turns + 1, {} }; }
    if (depth <= 0)
    {
        ++g_fs_leaf_evals;   // count leaf evaluations (value-leaf or rollout) for the K-predictor's leaf-count model
        // Learned leaf VALUE (MTG_VALUE_MODEL): distil the deep search into an O(1) win-turn estimate
        // that REPLACES the horizon rollout -- the rollout is the weak, slow link (greedy play-out, not
        // searched). The model's Score is a fixed-point WIN TURN (x1000); round + clamp to the legal
        // window [this turn, loss]. nullptr / empty / flag-off -> the exact rollout below (byte-identical).
        // Hybrid: the caller forces the exact heuristic leaf (g_force_heuristic_leaf) on the re-run pass
        // when value-leaf committed too shallow; otherwise use the learned leaf as before.
        // g_force_value_leaf (the ladder's cheap warm-up passes) turns the value leaf on even when
        // UseValueModel() is off; g_force_heuristic_leaf still wins over it, so the committed pass is
        // never affected. Both require a model to actually be attached.
        const MidGameEvaluator* vm = (!g_force_heuristic_leaf && (UseValueModel() || g_force_value_leaf)
                                      && state.m_value_model && !state.m_value_model->empty())
                                   ? state.m_value_model : nullptr;
        if (vm)
        {
            const std::vector<int> feats = ExtractMidGameFeatures(state, MidGamePlanSummary{});
            const long long score = vm->Score(feats);                 // milliturns (x1000)
            int w = static_cast<int>((score + 500) / 1000);           // round to a turn (score > 0)
            if (w < state.turn_number) { w = state.turn_number; }
            if (w > max_turns)         { w = max_turns + 1; }
            return { w, {} };
        }
        // Tail estimate beyond the horizon: roll out to game end at s_fd_leaf_depth
        // fidelity (default 1 = a 1-ply lookahead; see s_fd_leaf_depth), no committed
        // plays (the caller re-searches once it exhausts the committed line). The
        // rollout only CONSUMES the budget (enforce_budget is false inside), so it
        // never truncates -- the start gate alone reads the budget, between passes.
        GameState leaf = state;
        int w = SimulateToEnd(std::move(leaf), s_fd_leaf_depth, max_turns, budget, cutoff, second_main, tt);
        return { w, {} };
    }

    // Interior-node memo: a transposed re-entry at this (state, depth) returns the
    // already-computed optimal line. depth is folded into the key, so different
    // remaining depths never collide. See FSLineCache.
    TranspositionTable::Key key;
    if (lc != nullptr)
    {
        key = BuildSimKey(state, depth, max_turns, second_main);
        FSLineCache::const_iterator it = lc->find(key);
        PROF_INC(fsline_lookups);
        if (it != lc->end())
        {
            if (it->second.nowin_bound == std::numeric_limits<int>::max()) { PROF_INC(fsline_win_hit); }
            else if (cutoff <= it->second.nowin_bound)                     { PROF_INC(fsline_nowin_hit); }
            else                                                          { PROF_INC(fsline_nowin_stale); }
        }
        // A win entry carries nowin_bound = INT_MAX and is always reusable. A no-win entry answers
        // only "no win at turn <= nowin_bound", so a query that asks about a LATER turn than the
        // refutation covered must re-search.
        if (it != lc->end() && cutoff <= it->second.nowin_bound) { return it->second.line; }
    }
    // Truncation watermark for this node's own exploration (see g_fs_trunc_events).
    const unsigned long long trunc_at_entry = g_fs_trunc_events;

    // No projected-`wins_this_turn` shortcut: lethality is decided by actually
    // simulating each plan below, so the committed line's win turn always matches
    // replaying it (the projection can over-count vs ApplyPlanDirect+SimulateCombat).
    // Searched-breakpoint fan-out: `bp_root` marks the COMMITTED node (nest 0), but since
    // BpSearchInRollouts() defaulted ON the fan-out is emitted at EVERY ply and every rollout turn
    // (AppendBreakpointVariants gates on `!g_bp_root_enum && !BpSearchInRollouts()`, which never
    // fires by default) -- root-only was measured to recover none of the gain (see the flag's
    // comment). bp_root still gates root-only concerns (tie scan, landfan trace). A stale version
    // of this comment claimed deeper nodes keep the greedy continuation; that mis-aimed the whole
    // 2026-08-06 depth-curve investigation (th-d5-five-hour-game.md).
    const bool bp_root = (g_fsline_nest == 0);
    FsLineNestGuard _fs_nest;
    g_bp_root_enum = bp_root;
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);
    g_bp_root_enum = false;
    MoveOrderPlans(pre);   // lethal-looking / higher-value plans first -> earlier B&B cutoff

    // VALUE-RANKED BEAM: reorder `pre` by the PROBE's recorded value-win-turns for this node, so the beam-cap
    // below keeps the top-W lines the VALUE pass rated best (not the static MoveOrderPlans order). The probe
    // enumerated + MoveOrder'd the identical `pre` at this same key, so its recorded ranks map position-for-
    // position. Missing/un-evaluated positions sort last (keeping their MoveOrder). Only when beaming AND the
    // probe recorded this node; otherwise (incl. beam off) `pre` keeps the static order == byte-identical.
    // Gated to depth <= g_esc_beam_leafdepth so the top plies (the committed play) keep the exact static order.
    const bool beam_here = (g_esc_beam_width > 0 && depth <= g_esc_beam_leafdepth);
    if (beam_here && !g_esc_beam_static && g_probe_plan_vals != nullptr && lc != nullptr)
    {
        ProbePlanVals::const_iterator pit = g_probe_plan_vals->find(key);
        if (pit != g_probe_plan_vals->end() && !pit->second.empty())
        {
            const std::vector<int>& vals = pit->second;
            const int nv = static_cast<int>(vals.size());
            std::vector<int> order(pre.size());
            for (int i = 0; i < static_cast<int>(pre.size()); ++i) { order[i] = i; }
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const int va = (a < nv) ? vals[a] : (max_turns + 1);
                const int vb = (b < nv) ? vals[b] : (max_turns + 1);
                return va < vb;
            });
            std::vector<TurnSolver::Plan> ranked;
            ranked.reserve(pre.size());
            for (int j : order) { ranked.push_back(std::move(pre[j])); }
            pre.swap(ranked);
        }
    }
    // Record the probe's per-plan value-win-turns for this node (only during the probe pass, when beaming).
    const bool rec_vals = (g_probe_val_recording && g_probe_plan_vals != nullptr && lc != nullptr);
    std::vector<int> node_vals;
    if (rec_vals) { node_vals.reserve(pre.size()); }

    // Searched-breakpoint variant DEDUP. A bp_choice variant is a duplicate whenever its
    // continuation resolves to a state some earlier candidate already reached -- k past the end of
    // the breakpoint's candidate list (it falls back to greedy == its own base plan), or two k's
    // that pick the same continuation. Those cost a full rollout each and dilute a fixed node
    // budget for nothing. Key every candidate's POST-APPLY state, but only ever SKIP a plan that
    // carries a bp_choice: ordinary plans are never deduped, so a deck with no variants (and any
    // run with MTG_BP_SEARCH=0) is byte-identical. Reuses the apply already done below.
    bool bp_variants_here = false;
    for (const TurnSolver::Plan& p : pre) { if (p.bp_choice >= 0) { bp_variants_here = true; break; } }
    std::unordered_set<TranspositionTable::Key, TranspositionTable::KeyHash> bp_seen_states;

    TurnSolver::SearchLine best;
    best.win_turn = max_turns + 1;
    int _beam_i = 0;
    std::size_t scanned = 0;   // how far the loop actually got (the beam can cut it short) -- the
                               // deferred wave phase below stays inside it
    // Set when an in-horizon win BROKE the loop so the wave phase can try to beat it (see
    // BpWaveCompleteNodes). Suppresses the node_vals store exactly as the old early RETURN did.
    bool deferred_win = false;
    // Indifference probe (MTG_TRACE=tie) -- the OFFER/PRUNE audit, blind spot 1. This loop keeps a
    // plan only when it is STRICTLY better, and `pre` is MoveOrderPlans-sorted by static value, so
    // among plans the search cannot tell apart the highest STATIC VALUE wins. Record every scored
    // plan's (win_turn, action keys) at the committed node; the report below asks whether the
    // chosen plan strictly CONTAINS an equally-scoring one -- i.e. whether it took actions that
    // bought nothing measurable. Committed node only (bp_root), so rollout interior nodes stay
    // silent and the volume is one line per real decision.
    static const bool s_trace_tie = TRACE_ON("tie");
    const bool tie_scan_here = s_trace_tie && bp_root;
    std::vector<std::pair<int, std::vector<std::string>>> tie_scan;
    PROF_INC(fsw_nodes);
    PROF_ADD(fsw_cands, pre.size());
    for (const TurnSolver::Plan& p : pre)
    {
        // Value-guided escalation beam: expand only the top-W value-ranked plans (g_esc_beam_width), but only at
        // near-leaf nodes (beam_here); the top plies keep full exploration so the committed play is never pruned.
        // 0 = unlimited = byte-identical. pre is value-ordered above, so this keeps the best W lines.
        if (beam_here && _beam_i++ >= g_esc_beam_width) { ++g_fs_trunc_events; break; }
        ++scanned;
        if (budget) { budget->Consume(1); }   // one interior node (plan applied)
        if (s_rollout_stats)
        {
            g_interior_nodes.fetch_add(1, std::memory_order_relaxed);
            if (g_force_heuristic_leaf) { g_interior_nodes_esc.fetch_add(1, std::memory_order_relaxed); }
        }
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);
        // Breakpoint-variant dedup (see bp_seen_states): a variant whose continuation lands on an
        // already-scored state is redundant -- skip its rollout. Ordinary plans only RECORD.
        if (bp_variants_here)
        {
            const bool fresh = bp_seen_states.insert(BuildSimKey(s, 0, 0, false)).second;
            if (!fresh && p.bp_choice >= 0)
            { if (rec_vals) { node_vals.push_back(max_turns + 1); } continue; }
        }
        // A plan that kills the active player via its own on-cast triggers (Eidolon of
        // the Great Revel) or self-damage cannot win: those triggers go on top of the
        // spell and resolve BEFORE it (CR 603.3), so we die to them before our spell or
        // combat deals any damage. Skip the line entirely -- mirrors the baseline plan
        // guard (`self_damage >= ap.life`) so commit-the-line never commits a suicide
        // (burn gi=492: two Eidolons + an extra Goblin Guide = 8 self-damage at 6 life).
        if (s.ActivePlayer().life <= 0) { if (rec_vals) { node_vals.push_back(max_turns + 1); } continue; }
        AnimateLandsShared(s, nullptr);
        ActivateTapTokensShared(s, nullptr);
        SimulateCombat(s);
        if (s.Opponent().life <= 0)  // win this turn -> floor
        {
            TurnSolver::Plan p_rec = p;
            p_rec.breakpoint_actions = std::move(bp);
            TurnSolver::SearchLine win = { state.turn_number, { { true, std::move(p_rec) } } };
            // A this-turn win is the earliest possible from here, so it is the final
            // optimal line for this node -- cache it (cutoff-independent).
            FSLineStoreWin(lc, key, win);
            return win;
        }

        TurnSolver::SearchLine tail =
            FSLineTail(s, depth - 1, max_turns, std::min(cutoff, best.win_turn), second_main, tt, lc, budget);
        if (rec_vals) { node_vals.push_back(tail.win_turn); }   // value-rank for the beam reorder
        if (tie_scan_here) { tie_scan.emplace_back(tail.win_turn, PlanActionKeys(p)); }
        if (tail.win_turn < best.win_turn)
        {
            best.win_turn = tail.win_turn;
            best.phases.clear();
            TurnSolver::Plan p_rec = p;
            p_rec.breakpoint_actions = std::move(bp);
            best.phases.push_back({ true, std::move(p_rec) });
            best.phases.insert(best.phases.end(), tail.phases.begin(), tail.phases.end());

            // Stop at the first VERIFIED win (within this node's horizon, found by real
            // simulation -- not the greedy leaf). Under the iterative-deepening caller
            // (FullSearchLine) a pass runs only after every shallower pass found no win,
            // and every node in a pass shares the same horizon edge, so any in-horizon
            // win is at that edge = the global minimum. Hence the FIRST one found is
            // optimal; no later plan can beat it (only tie), so commit and return. This
            // is the sound, general form of the old `best<=turn+1` sibling break, which
            // assumed lethal plans sort first and so skipped a later this-turn kill (the
            // slivers regression). Greedy-tail estimates (beyond the horizon) are NOT
            // mutually tied, so they fall through to keep min-tracking. NOTE: this
            // couples FSLineWin's correctness to that calling convention -- it is not a
            // standalone earliest-win finder.
            if (tail.win_turn <= state.turn_number + depth - 1)
            {
                // COMPLETE NODES: this win is only known-optimal if every shallower pass was a
                // complete refutation, which a budget-truncated wave phase cannot promise. Break
                // instead of returning so this node's own deferred ranks get a chance to beat it.
                if (BpWaveCompleteNodes() && BpWavesHere(budget)) { deferred_win = true; break; }
                FSLineStoreWin(lc, key, best);
                return best;
            }
        }
    }

    // Store the probe's per-plan value ranks for this fully-evaluated node (loop completed = every plan
    // scored), so the escalation's beam can reorder by them. Win-node early-returns above skip this (they
    // don't need reordering). Overwrites are harmless (depth is folded into the key, so no cross-pass clash).
    if (rec_vals && !node_vals.empty() && !deferred_win) { (*g_probe_plan_vals)[key] = std::move(node_vals); }

    // ---- DEFERRED CONTINUATION WAVES (see BpWaveWalker) ------------------------------------------
    // Wave 0 above reached only ranks 0..W-1 of each breakpoint's RANKED continuation list; walk the
    // rest here. Deliberately AFTER the loop and AFTER node_vals is stored, and node_vals is never
    // extended, so the probe's position-keyed ranks keep mapping to the same plans (the documented
    // silent-mis-ordering hazard). Off under a budget by default => byte-identical there.
    if (BpWavesHere(budget))
    {
        BpWaveWalker walker(state, pre, scanned);
        if (walker.Empty())
        {
            if (BpWaveProbeOn()) { g_bp_wave_probe.no_slots.fetch_add(1); }
        }
        else
        {
            if (BpWaveProbeOn())
            {
                g_bp_wave_probe.nodes.fetch_add(1);
                g_bp_wave_probe.slots.fetch_add(walker.SlotCount());
            }
            TurnSolver::Plan v;
            while (walker.Next(pre, v))
            {
                // Anytime: a variant only ever wins on a STRICTLY better win turn, so stopping here
                // keeps `best` exactly as the search left it.
                if (budget != nullptr && !budget->Unlimited() && budget->Exhausted())
                { ++g_fs_trunc_events;
                  if (BpWaveProbeOn()) { g_bp_wave_probe.stopped.fetch_add(1); } break; }
                if (BpWaveProbeOn()) { g_bp_wave_probe.scored.fetch_add(1); BpWaveRank(walker.LastRank()); }

                if (budget) { budget->Consume(1); }   // one interior node (plan applied)
                if (s_rollout_stats)
                {
                    g_interior_nodes.fetch_add(1, std::memory_order_relaxed);
                    if (g_force_heuristic_leaf) { g_interior_nodes_esc.fetch_add(1, std::memory_order_relaxed); }
                }
                GameState s = state;
                std::vector<Action> bp;
                g_bp_cands_last = 0;              // 0 == this apply reached no eligible breakpoint
                g_bp_seen_last  = 0;              // ... and reached no nested one either
                ApplyPlanDirect(s, v, true, &bp);
                // Past the end of the list: the continuation fell back to greedy, so this variant is
                // a copy of its own base plan (already scored) and the slot retires. The apply's
                // breakpoint COUNT is reported alongside so nested indices open their own slots.
                if (walker.Report(pre, g_bp_cands_last, g_bp_seen_last)) { continue; }
                // Same post-apply state as an already-scored candidate -- the loop's own dedup set,
                // so a wave candidate is also checked against every wave-0 one.
                if (!bp_seen_states.insert(BuildSimKey(s, 0, 0, false)).second) { continue; }
                if (BpWaveProbeOn()) { g_bp_wave_probe.rolled.fetch_add(1); }
                if (s.ActivePlayer().life <= 0) { continue; }   // self-kill guard, as above
                AnimateLandsShared(s, nullptr);
                ActivateTapTokensShared(s, nullptr);
                SimulateCombat(s);
                if (s.Opponent().life <= 0)       // wins THIS turn -> the earliest possible from here
                {
                    if (BpWaveProbeOn()) { g_bp_wave_probe.improved.fetch_add(1); }
                    TurnSolver::Plan p_rec = v;
                    p_rec.breakpoint_actions = std::move(bp);
                    TurnSolver::SearchLine win = { state.turn_number, { { true, std::move(p_rec) } } };
                    FSLineStoreWin(lc, key, win);
                    return win;
                }
                TurnSolver::SearchLine tail =
                    FSLineTail(s, depth - 1, max_turns, std::min(cutoff, best.win_turn), second_main,
                               tt, lc, budget);
                if (tail.win_turn < best.win_turn)
                {
                    if (BpWaveProbeOn()) { g_bp_wave_probe.improved.fetch_add(1); }
                    best.win_turn = tail.win_turn;
                    best.phases.clear();
                    TurnSolver::Plan p_rec = v;
                    p_rec.breakpoint_actions = std::move(bp);
                    best.phases.push_back({ true, std::move(p_rec) });
                    best.phases.insert(best.phases.end(), tail.phases.begin(), tail.phases.end());
                    // Same reasoning as the main loop: with COMPLETE NODES on we keep walking the
                    // remaining ranks rather than stopping at the first in-horizon win, so the node
                    // answers with the minimum over every rank it could afford. The walk still ends
                    // on its own when the slots retire or the budget runs out.
                    if (!BpWaveCompleteNodes() && tail.win_turn <= state.turn_number + depth - 1)
                    {
                        FSLineStoreWin(lc, key, best);
                        return best;
                    }
                }
            }
        }
    }

    // ---- Indifference report (MTG_TRACE=tie) -----------------------------------------------
    // The committed plan reached best.win_turn. If a LATER-scored plan reached the same win turn
    // with a strict SUBSET of its actions, the extra actions changed nothing the search could
    // measure -- they were taken on static value (the MoveOrderPlans order), not on merit. Report
    // the LARGEST such difference, since that is the widest set of actions shown to be free.
    // A card that recurs in these lines is a prune candidate: exactly how the duplicate-legend
    // cast was found. See docs/design/searched-design-audit-blind-spots.md.
    if (tie_scan_here && !best.phases.empty() && best.win_turn <= max_turns)
    {
        const std::vector<std::string> best_keys = PlanActionKeys(best.phases.front().plan);
        std::vector<std::string>       widest;
        for (const auto& [wt, keys] : tie_scan)
        {
            if (wt != best.win_turn || keys.size() >= best_keys.size()) { continue; }
            if (!std::includes(best_keys.begin(), best_keys.end(), keys.begin(), keys.end()))
            { continue; }
            std::vector<std::string> diff;
            std::set_difference(best_keys.begin(), best_keys.end(), keys.begin(), keys.end(),
                                std::back_inserter(diff));
            if (diff.size() > widest.size()) { widest = std::move(diff); }
        }
        // Coverage line (MTG_TRACE=tiescan): EVERY committed decision, whether or not it found a
        // free action -- so a silent `tie` stream reads as "no indifference here" rather than "the
        // probe never ran" (the inert-probe trap this audit has hit repeatedly). It also carries
        // the chosen plan's full action list, which is the DENOMINATOR the analysis needs: a single
        // free action means little (at a bounded horizon almost any play is free for the projected
        // win turn), but a card that is free EVERY time it is cast is a prune candidate. Free-rate
        // per card = `tie` count / `tiescan` count -- see scripts/tie_probe_report.py.
        {
            std::string chosen;
            for (const std::string& k : best_keys)
            { if (!chosen.empty()) { chosen += "|"; } chosen += k; }
            TRACE("tiescan", "turn=%d scored=%zu win=%d free=%zu chosen: %s",
                  state.turn_number, tie_scan.size(), best.win_turn, widest.size(),
                  chosen.empty() ? "<pass>" : chosen.c_str());
        }
        if (!widest.empty())
        {
            std::string joined;
            for (const std::string& k : widest)
            { if (!joined.empty()) { joined += "|"; } joined += k; }
            TRACE("tie", "turn=%d win=%d free=%zu/%zu extra: %s",
                  state.turn_number, best.win_turn, widest.size(), best_keys.size(),
                  joined.c_str());
        }
    }

    // ---- Board-nullity report (MTG_TRACE=nil) ----------------------------------------------
    // For each action of the committed plan, apply the plan WITH and WITHOUT it and compare the
    // resulting board signature. An action that leaves it identical had no board effect at all --
    // the duplicate-legend shape. Horizon-independent, unlike the `tie` probe above.
    // Costs one extra ApplyPlanDirect per action per committed decision, so it is strictly a
    // diagnostic; the stream is off in every normal run. See BoardSignature.
    static const bool s_trace_nil = TRACE_ON("nil");
    if (s_trace_nil && bp_root && !best.phases.empty())
    {
        const TurnSolver::Plan& chosen = best.phases.front().plan;
        if (chosen.actions.size() >= 1)
        {
            GameState full = state;
            ApplyPlanDirect(full, chosen, true);
            const std::string full_sig = BoardSignature(full);
            for (std::size_t i = 0; i < chosen.actions.size(); ++i)
            {
                TurnSolver::Plan without = chosen;
                without.actions.erase(without.actions.begin() + static_cast<long>(i));
                GameState w = state;
                ApplyPlanDirect(w, without, true);
                if (BoardSignature(w) == full_sig)
                {
                    // `strand` = the card is STILL IN HAND after applying the plan, i.e. the cast was
                    // enumerated but unpayable and silently no-opped. That distinguishes an
                    // ENUMERATION-feasibility gap from a genuinely effect-less play (the
                    // duplicate-legend shape), which are different bugs with different fixes.
                    auto in_hand = [&](const GameState& g)
                    {
                        for (const Card& c : g.ActivePlayer().hand)
                        { if (c.m_name == chosen.actions[i].card_name) { return true; } }
                        return false;
                    };
                    std::string others;
                    for (std::size_t j = 0; j < chosen.actions.size(); ++j)
                    {
                        if (j == i) { continue; }
                        if (!others.empty()) { others += "|"; }
                        others += chosen.actions[j].card_name;
                    }
                    TRACE("nil", "turn=%d win=%d oppLife=%d hand=%zu/%zu gy=%zu/%zu bf=%zu/%zu "
                                 "land=%s strand=%d with=[%s] board-null: %s#%d",
                          state.turn_number, best.win_turn, full.Opponent().life,
                          full.ActivePlayer().hand.size(), w.ActivePlayer().hand.size(),
                          full.ActivePlayer().graveyard.size(), w.ActivePlayer().graveyard.size(),
                          full.battlefield.size(), w.battlefield.size(),
                          chosen.land_to_play.empty() ? "-" : chosen.land_to_play.c_str(),
                          in_hand(full) ? 1 : 0, others.empty() ? "-" : others.c_str(),
                          chosen.actions[i].card_name.c_str(),
                          static_cast<int>(chosen.actions[i].kind));
                }
            }
        }
    }

    if (best.win_turn <= max_turns)
    {
        FSLineStoreWin(lc, key, best);
    }
    else if (lc != nullptr)
    {
        PROF_INC(fsline_nowin_result);
        if (FSNoWinCacheOn() && g_fs_trunc_events == trunc_at_entry)
        {
            PROF_INC(fsline_nowin_stored);
            // BOUND-QUALIFIED NO-WIN. The loop ran to completion with nothing truncated anywhere
            // beneath it, so this node genuinely has no win at turn <= cutoff. Note the bound is the
            // node's OWN cutoff, not a child's: a no-win result means `best` never improved on
            // max_turns+1, so `min(cutoff, best.win_turn)` was `cutoff` for every child -- no
            // incumbent tightening ever happened. Clamped to max_turns+1 (the widest question the
            // search can be asked) so an unbounded query still hits.
            FSLineStoreNoWin(lc, key, best, std::min(cutoff, max_turns + 1));
        }
    }
    return best;
}

// Estimate-and-skip tuning (deterministic budget), shared by FullSearchLine's
// iterative-deepening start gate and SolveWithLookahead. See
// project-deterministic-budget.
namespace
{
    // Start gate: begin pass k only if its estimated cost <= alpha * remaining
    // budget; a little over (>1.0) is allowed since the overrun guard backs it up.
    constexpr double kStartGateAlpha = 1.10;
    // Bootstrap growth ratio used for pass 1's estimate, before two completed
    // passes exist to measure a real C_{k-1}/C_{k-2} branching ratio.
    constexpr double kDefaultGrowth = 6.0;
    // Overrun guard: once a pass is running past budget, abort + roll back only
    // when it has spent more than the ceiling below. "Almost done" passes always finish.
    // The ceiling is max(beta*budget, FLOOR): for small per-decision budgets beta*budget
    // is tiny (th d3: 2*9000=18k units) and collides with a LEGITIMATE deep pass that
    // genuinely needs ~2x budget, aborting it and changing the result (this broke th d3
    // s3003 game 278's win turn). The absolute FLOOR keeps the guard above any normal
    // completing pass (well under ~1e5 units for the suite decks) while staying far below
    // a true no-win runaway (millions of units), so normal games of every deck remain
    // byte-identical and only a genuine runaway aborts. Calibrated with the pathological
    // antilife deck OUT of the suite; revisit if NODES_PER_VIRTUAL_MS is rebased.
    // See search-perf-investigation memory.
    constexpr double    kOverrunBeta  = 2.0;
    constexpr long long kOverrunFloor = 1000000;
}

TurnSolver::SearchLine TurnSolver::FullSearchLine(const GameState& state, int depth,
                                                  int max_turns, bool second_main,
                                                  TranspositionTable* tt, SearchBudget* budget,
                                                  int* out_committed_depth)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    // Memoize the greedy tail rollouts across the whole branch-and-bound tree. The
    // deep search revisits identical leaf states many times; without a table each
    // is a fresh full rollout. When the caller hands no table (the common non-
    // bottoming path), own a per-call one. Mirrors SolveWithLookahead's local_table.
    // Byte-identical to nullptr (SimulateToEnd is a pure function of its key); the
    // table only skips recompute.
    TranspositionTable local_table;
    if (tt == nullptr) { tt = &local_table; }

    // Interior-node line memo. Always per-call (never shared like the bottoming int
    // table): it caches draw-dependent lines, valid only under THIS call's single
    // fixed root library (library size => remaining content). See FSLineCache.
    FSLineCache line_cache;

    // Iterative deepening with a deterministic START GATE (mirrors SolveWithLookahead).
    // Search 1, 2, ... `depth` complete turns and commit the deepest pass that fits the
    // budget. Both memo tables are SHARED across passes and keyed by (state, remaining-
    // depth), so pass L+1 reuses every node pass L already solved at the same remaining
    // depth and only expands its new top layer -- the re-search is almost entirely cache
    // hits. The start gate skips a pass whose estimated cost won't fit the remaining
    // budget (committing the prior, deepest-fitted pass); a pass that DOES start always
    // runs to completion (no mid-pass abort/overrun-guard yet). With no budget (nullptr)
    // or a generous one every pass runs, so the committed line is pass `depth`'s --
    // byte-identical to the former single FSLineWin(depth) call.
    // depth <= 0 keeps the former single call (its FSLineWin greedy-leaf fallback);
    // depth >= 1 deepens 1..depth. Either way the LAST committed pass is FSLineWin(depth).
    SearchLine line;
    line.win_turn = max_turns + 1;
    int committed_depth = depth;             // depth actually searched for `line`
    SearchLine prev_line = line;             // last pass that COMPLETED (overrun rollback target)
    int        prev_committed = committed_depth;

    long long c_prev = 0, c_prev2 = 0;       // work units of passes k-1, k-2
    bool      have_prev = false, have_prev2 = false;

    // Value-leaf start-gate relaxation (MTG_VALUE_STARTGATE_ALPHA, ADOPTED default 8.0; 1.0 == off): when the
    // CHEAP value-leaf is driving this search (free leaves), a pass whose estimate slightly overshoots the
    // budget is still worth FINISHING within this search -- reaching the value-leaf trust depth here avoids
    // the hybrid's separate (expensive) heuristic redo. A LARGER alpha lets a nearly-affordable transitional
    // pass start (and run over budget, capped by the overrun guard) while a genuinely-explosive pass
    // (estimate many x remaining, e.g. slivers g4) is still rejected -> no blowup. Only affects runs with the
    // value leaf active (UseValueModel + attached model); pure-heuristic search is byte-identical.
    // See learned-d0-policy.md.
    const bool vl_active = (UseValueModel() && !g_force_heuristic_leaf
                            && state.m_value_model && !state.m_value_model->empty());
    static const double s_vl_alpha_mult = []{ const char* e = std::getenv("MTG_VALUE_STARTGATE_ALPHA");
                                              return (e && *e) ? std::atof(e) : 8.0; }();
    const double gate_alpha = (vl_active && s_vl_alpha_mult > 1.0)
                            ? kStartGateAlpha * s_vl_alpha_mult : kStartGateAlpha;

    // JUMP-LADDER (MTG_ESC_JUMP): heuristic-escalation only. The escalation's shallow ladder passes are
    // ~pure overhead -- they almost never find a verified win (measured 1/966) and, at matched committed
    // depth, a single cold pass is ~18% cheaper than d1..K (the memo does NOT pay for the shallow passes).
    // So after two completed passes give a growth ratio, extrapolate the start-gate to the deepest AFFORDABLE
    // depth K and jump straight there, skipping d(pass+1)..d(K-1). The jumped pass still runs under the
    // overrun guard. Byte-identical when off; gated to the escalation so the probe's ladder is untouched.
    static const bool s_esc_jump_env = EnvOn("MTG_ESC_JUMP");
    const bool s_esc_jump = s_esc_jump_env && g_force_heuristic_leaf;

    // LADDER ON THE VALUE LEAF (MTG_LADDER_VALUE_LEAF, default OFF).
    //
    // The ladder's warm-up passes exist to refute shallow horizons, and their leaf estimates never
    // reach the committed line -- only pass `depth` does. So paying the exact heuristic rollout for
    // passes 1..depth-1 buys nothing but time, and on the deep H cells of the value-leaf depth matrix
    // that time is most of the cell.
    //
    // This is byte-identical for the committed line, by construction rather than by hope:
    //   * FSLineCache cannot carry a warm-up pass's entry into the committed one. Every node of pass
    //     k satisfies turn + depth == turn0 + k (FSLineWin recurses turn+1 / depth-1), and the key
    //     folds both -- so the sum is a pass fingerprint and two passes never share a key.
    //   * The leaf table `tt` is likewise untouched by a value-leaf pass: the value leaf RETURNS
    //     before SimulateToEnd, which is the only thing in this recursion that writes to it.
    //   * The remaining coupling is the BUDGET (cheaper warm-ups leave more for the deep pass), so
    //     identity holds exactly when the budget does not bind -- which is how the matrix runs every
    //     cell (unbounded, so each config reaches its nominal depth).
    // Under a bounded budget it is a real (and favourable) change, not a free one: that arm needs its
    // own A/B, exactly like MTG_FS_NOWIN_CACHE on the play path.
    static const bool s_ladder_value_leaf = EnvOn("MTG_LADDER_VALUE_LEAF");

    for (int pass_depth = (depth >= 1 ? 1 : depth); pass_depth <= depth; ++pass_depth)
    {
        // Cheap leaf for every pass but the one that commits.
        ForceValueLeafGuard _lvl(s_ladder_value_leaf && pass_depth < depth);
        // Start gate: skip (and commit the prior pass) when the next pass clearly
        // won't fit. Keyed on the running work-unit count, never the clock, so a
        // deeper search makes the same skip decision and can never come out worse.
        if (budget != nullptr && have_prev)
        {
            double ratio    = (have_prev2 && c_prev2 > 0)
                            ? static_cast<double>(c_prev) / static_cast<double>(c_prev2)
                            : kDefaultGrowth;
            double estimate = static_cast<double>(c_prev) * ratio;
            if (estimate > gate_alpha * static_cast<double>(budget->Remaining()))
            {
                break;
            }
        }

        long long used_before = budget ? budget->Used() : 0;
        long long leaves_before = g_fs_leaf_evals;   // K-predictor: per-pass leaf-count delta (probe recording)
        // Arm the OVERRUN guard: this pass may exceed its estimate, but if its real cost
        // blows past kOverrunBeta x the whole decision budget it is pathological -- abort
        // and keep the last completed pass. Normal passes finish far under this ceiling, so
        // the guard never fires for them (parity preserved). Only for a limited budget.
        if (budget != nullptr && !budget->Unlimited())
        {
            long long beta_ceiling = static_cast<long long>(
                kOverrunBeta * static_cast<double>(budget->Limit()));
            budget->SetOverrunLimit(used_before + std::max(beta_ceiling, kOverrunFloor));
        }
        SearchLine attempt = FSLineWin(state, pass_depth, max_turns, max_turns + 1, second_main, tt,
                                       &line_cache, budget);
        bool aborted = (budget != nullptr && budget->Overrun());
        if (budget != nullptr) { budget->SetOverrunLimit(0); }   // disarm

        if (aborted)
        {
            // Runaway pass: discard its partial result, commit the last completed pass.
            TRACE("search", "T%d pass=%d OVERRUN abort (used=%lld limit=%lld) -> commit depth=%d",
                  state.turn_number, pass_depth,
                  budget ? budget->Used() : 0, budget ? budget->Limit() : 0, prev_committed);
            line = prev_line; committed_depth = prev_committed;
            break;
        }

        line = attempt;
        committed_depth = pass_depth;
        prev_line = line; prev_committed = committed_depth;   // this pass completed
        // K-predictor: record the PROBE's leaf count at this depth (leaf-independent structure the escalation
        // reuses to predict its own affordable depth). Only while the probe records; index guarded.
        long long cost = (budget ? budget->Used() : 0) - used_before;
        if (g_probe_recording && pass_depth >= 0 && pass_depth < 16)
        {
            g_probe_leaves[pass_depth] = g_fs_leaf_evals - leaves_before;
            g_probe_cost[pass_depth]   = cost;
        }
        if (g_hlad_recording && pass_depth >= 0 && pass_depth < 16)
        {
            g_hlad_leaves[pass_depth] = g_fs_leaf_evals - leaves_before;
            g_hlad_cost[pass_depth]   = cost;
        }
        TRACE("search", "T%d pass=%d done win=%d cost=%lld used=%lld",
              state.turn_number, pass_depth, line.win_turn, cost, budget ? budget->Used() : 0);

        c_prev2 = c_prev;   have_prev2 = have_prev;
        c_prev  = cost;     have_prev  = true;

        // Jump-ladder: with a measured growth ratio, extrapolate to the deepest affordable K and skip the
        // intermediate passes d(pass+1)..d(K-1) -- go straight to K. K is chosen by the SAME start-gate math
        // (est <= gate_alpha * remaining), so it lands where the full ladder would have committed, at ~18%
        // less work. If no intermediate passes exist (K <= pass+1) this is a no-op and the normal ladder runs.
        if (s_esc_jump && have_prev2 && c_prev2 > 0 && budget != nullptr && pass_depth + 1 < depth)
        {
            // CONSERVATIVE growth: the escalation's rollout cost ACCELERATES with depth, so the shallow d1->d2
            // ratio under-estimates deep-pass cost and a naive extrapolation predicts too-deep K (measured: it
            // converts cheap h2/h3-committing escalations into full d5 searches, +130% work). Floor the ratio
            // at kDefaultGrowth so the estimate grows at least as fast as the ladder's bootstrap assumption --
            // this predicts SHALLOWER (safe: under-prediction just resumes laddering; over-prediction overshoots).
            double r = static_cast<double>(c_prev) / static_cast<double>(c_prev2);
            if (r < kDefaultGrowth) { r = kDefaultGrowth; }
            double est = static_cast<double>(c_prev);
            int K = pass_depth;
            for (int d = pass_depth + 1; d <= depth; ++d)
            {
                est *= r;
                if (est <= gate_alpha * static_cast<double>(budget->Remaining())) { K = d; }
                else { break; }
            }
            if (K > pass_depth + 1) { pass_depth = K - 1; }   // loop ++ -> K, skipping the intermediates
        }

        // Stop at the first VERIFIED win (within this pass's searched horizon). A
        // deeper pass only extends the horizon to LATER turns, so it can never find
        // an earlier win -- this line is already optimal. Lossless: the shallowest
        // verified win equals the depth-`depth` search's min win (same first-value-
        // order line). A win turn BEYOND the horizon is a greedy-tail estimate, so we
        // keep deepening to verify or beat it (until the start gate / `depth` stops).
        if (line.win_turn <= state.turn_number + pass_depth - 1) { break; }
    }
    if (out_committed_depth != nullptr) { *out_committed_depth = committed_depth; }

    static const bool fd_trace   = EnvOn("MTG_FD_TRACE");
    if (fd_trace)
    {
        std::cerr << "[fd] T" << state.turn_number << " LINE win=" << line.win_turn;
        for (const PhasePlan& pp : line.phases)
        {
            std::cerr << " | " << (pp.is_pre_combat ? "pre:" : "2nd:") << PlanDesc(pp.plan);
            if (pp.plan.land_decided)
            {
                std::cerr << "{land=" << (pp.plan.land_to_play.empty() ? "<none>" : pp.plan.land_to_play);
                if (!pp.plan.fetch_target.empty()) { std::cerr << " fetch=" << pp.plan.fetch_target; }
                std::cerr << "}";
            }
            else { std::cerr << "{land=undecided}"; }
        }
        std::cerr << "\n";

        // Replay the committed line on a copy and print the search's PREDICTED opp
        // life after each phase, mirroring FSLineWin/FSLineTail's own simulation. Diff
        // this against the realised [traj] opp_life to pinpoint where ApplyPlanDirect/
        // SimulateCombat over-counts vs real execution.
        GameState copy  = state;
        bool      first = true;
        for (const PhasePlan& pp : line.phases)
        {
            if (pp.is_pre_combat && !first)
            {
                if (!SimulateEndAndStartNextTurn(copy)) { break; }
                ExpireStagedCards(copy);
            }
            if (pp.is_pre_combat)
            {
                // Diagnostic: dump the library top BEFORE the plan resolves so the
                // rollout's draw source can be diffed against the executor's [traj] libtop.
                {
                    const auto& lib = copy.ActivePlayer().library;
                    int pre_hand_lands = 0, pre_nomax = 0;
                    for (const Card& hc : copy.ActivePlayer().hand)
                    { auto hd = CardDatabase::Instance().LookupCached(hc);
                      if (hd ? hd->card.IsLand() : hc.IsLand()) ++pre_hand_lands; }
                    for (const Permanent& perm : copy.battlefield)
                    { if (perm.controller_index != copy.active_player_index) continue;
                      auto pd = CardDatabase::Instance().LookupCached(perm.card);
                      if (pd && pd->params.no_max_hand_size && pd->card.IsLand()) ++pre_nomax; }
                    std::cerr << "[fd-pred]   turn=" << copy.turn_number
                              << " POST-CLEANUP hand_lands=" << pre_hand_lands
                              << " handsize=" << copy.ActivePlayer().hand.size()
                              << " nomax=" << pre_nomax << " libtop=";
                    for (std::size_t li = 0; li < lib.size() && li < 6; ++li)
                    { std::cerr << lib[li].m_name << "; "; }
                    std::cerr << " (libsize=" << lib.size() << ")\n";
                }
                g_bp_trace_arm = BpTraceEnabled();
                ApplyPlanDirect(copy, pp.plan, true);
                g_bp_trace_arm = false;
                AnimateLandsShared(copy, nullptr);
                ActivateTapTokensShared(copy, nullptr);
                SimulateCombat(copy);
            }
            else
            {
                g_bp_trace_arm = BpTraceEnabled();
                ApplyPlanDirect(copy, pp.plan, false);
                g_bp_trace_arm = false;
            }
            int my_creatures = 0;
            for (const Permanent& perm : copy.battlefield)
            {
                if (perm.controller_index == copy.active_player_index && perm.card.IsCreature())
                { ++my_creatures; }
            }
            int hand_lands = 0, bf_le = 0, bf_nomax = 0;
            for (const Card& hc : copy.ActivePlayer().hand)
            { auto hd = CardDatabase::Instance().LookupCached(hc); if (hd ? hd->card.IsLand() : hc.IsLand()) ++hand_lands; }
            for (const Permanent& perm : copy.battlefield)
            { if (perm.controller_index != copy.active_player_index) continue;
              auto pd = CardDatabase::Instance().LookupCached(perm.card);
              if (pd && pd->params.discard_land_damage>0) ++bf_le;
              if (pd && pd->params.no_max_hand_size && pd->card.IsLand()) ++bf_nomax; }
            std::cerr << "[fd-pred]   turn=" << copy.turn_number
                      << (pp.is_pre_combat ? " pre " : " 2nd ")
                      << "opp_life=" << copy.Opponent().life
                      << " hand_lands=" << hand_lands << " LE=" << bf_le << " nomax=" << bf_nomax
                      << " my_creatures=" << my_creatures
                      << "  " << PlanDesc(pp.plan) << "\n";
            first = false;
        }
    }
    return line;
}

// Hybrid value-leaf search (see header). Run once with the cheap learned leaf; if it committed a pass
// shallower than the value-leaf trust depth, re-run once with the exact heuristic rollout leaf on a fresh
// budget (the value-leaf pass was a cheap probe of reachable depth). value_min_depth <= 0 or no value
// model => plain FullSearchLine (byte-identical).
// Opt-in hybrid diagnostics (MTG_HYBRID_STATS): per value-leaf probe, record the committed depth and
// whether the heuristic redo fired, so we can tell WHY a redo happens -- probe landed at depth K-1 (a
// start-gate nudge could finish it) vs way down at depth 1-2 (interior-node cost alone blows the budget,
// no nudge helps). Printed once at process exit. Off => zero overhead. See learned-d0-policy.md.
namespace
{
    struct HybridStats
    {
        bool enabled = EnvOn("MTG_HYBRID_STATS");
        std::atomic<long long> decisions{0};
        std::atomic<long long> redos{0};
        std::atomic<long long> verified{0};
        std::array<std::atomic<long long>, 16> probe_depth{};   // histogram of value-leaf committed depth
        std::array<std::atomic<long long>, 16> redo_depth{};    // committed depth AT the decisions that redid
        std::array<std::atomic<long long>, 16> redo_hdepth{};   // heuristic-escalation ACHIEVED depth (hcommitted)
        std::atomic<long long> redo_short{0};                   // escalations whose heuristic pass fell short of user depth
        std::atomic<long long> esc_verified{0};                 // escalations that COMMITTED a verified win (win <= turn+hcommitted-1)
        std::atomic<long long> esc_verified_short{0};           // ... AND at hcommitted < probe committed (verified win the probe pruned)
        std::array<std::atomic<long long>, 16> esc_ver_hdepth{}; // achieved-depth histogram of the VERIFIED-win escalations
        ~HybridStats()
        {
            if (!enabled || decisions.load() == 0) { return; }
            std::cerr << "[hybrid-stats] decisions=" << decisions.load()
                      << " redos=" << redos.load()
                      << " verified=" << verified.load()
                      << " redo_short=" << redo_short.load()
                      << " (escalations that did NOT reach user depth)\n";
            std::cerr << "[hybrid-stats] esc_verified=" << esc_verified.load()
                      << " (escalations committing a VERIFIED win) of which at depth < probe: "
                      << esc_verified_short.load()
                      << " (verified wins the value-leaf probe PRUNED)\n";
            std::cerr << "[hybrid-stats] verified-escalation ACHIEVED-depth histogram:\n";
            for (int d = 0; d < 16; ++d)
            {
                long long c = esc_ver_hdepth[d].load();
                if (c == 0) { continue; }
                std::cerr << "    hv" << d << ": " << c << "\n";
            }
            std::cerr << "[hybrid-stats] probe committed-depth histogram (d:count / of-which-redid):\n";
            for (int d = 0; d < 16; ++d)
            {
                long long c = probe_depth[d].load();
                if (c == 0) { continue; }
                std::cerr << "    d" << d << ": " << c << " / redid " << redo_depth[d].load() << "\n";
            }
            std::cerr << "[hybrid-stats] escalation ACHIEVED-depth histogram (heuristic pass hcommitted):\n";
            for (int d = 0; d < 16; ++d)
            {
                long long c = redo_hdepth[d].load();
                if (c == 0) { continue; }
                std::cerr << "    h" << d << ": " << c << "\n";
            }
        }
    };
    HybridStats g_hybrid_stats;

    // Escalation-outcome dataset (MTG_ESCALATION_DUMP=<path>, opt-in). One row per escalation:
    //   <taken> <wt_changed> <turn> <committed> <gap> <est_wt> <midgame-feature vector...>
    // taken      = the heuristic re-search REPLACED the value-leaf line (cleared the crossover)
    // wt_changed = ... and it moved win_turn (the objective actually changed)
    // The "was this escalation worth its cost" label is (taken && wt_changed); the features are all
    // observable BEFORE the escalation runs, so a classifier can decide to SKIP predicted no-ops. The
    // point is to test whether rich features separate no-ops BETTER than committed-depth alone (which is
    // just value_trust_depth) -- if not, the gate collapses to the trust-depth rule we already ship.
    // Inert (no rows, byte-identical) when unset. See docs/design/escalation-and-rollout-cost.md.
    struct EscalationDump
    {
        std::ofstream out;
        std::mutex    mu;
        bool          enabled = false;
        EscalationDump()
        {
            const char* p = std::getenv("MTG_ESCALATION_DUMP");
            if (p && *p) { out.open(p, std::ios::app); enabled = out.is_open(); }
        }
        void row(const std::vector<int>& feats, int taken, int wt_changed,
                 int turn, int committed, int gap, int est_wt)
        {
            std::lock_guard<std::mutex> lk(mu);
            out << taken << ' ' << wt_changed << ' ' << turn << ' ' << committed
                << ' ' << gap << ' ' << est_wt;
            for (int f : feats) { out << ' ' << f; }
            out << '\n';
        }
    };
    EscalationDump g_escalation_dump;

    // --- Escalation confidence-GATE (MTG_ESCALATION_GATE=<gate.json>, opt-in) -------------------
    // Serves the logistic trained by scripts/esc_train_gate.py: predicts P(NO-OP) for an escalation
    // from features observed BEFORE it runs, so the engine can SKIP predicted no-op escalations
    // (the ~82% of Hinata escalations that re-search to the same win_turn). Skip iff P(no-op) >
    // threshold (MTG_ESCALATION_GATE_T, default 0.5); a skipped escalation keeps the value-leaf line.
    // Off (unset / unreadable / malformed) => never skips => byte-identical. This is a SPEED/quality
    // lever, NOT lossless -- validate LP on held-out play before adoption. See the levers doc.
    std::vector<double> ParseJsonNumArray(const std::string& s, const std::string& key)
    {
        std::vector<double> out;
        auto kp = s.find("\"" + key + "\"");
        if (kp == std::string::npos) { return out; }
        auto lb = s.find('[', kp);
        auto rb = (lb == std::string::npos) ? std::string::npos : s.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) { return out; }
        std::stringstream ss(s.substr(lb + 1, rb - lb - 1));
        std::string tok;
        while (std::getline(ss, tok, ',')) { try { out.push_back(std::stod(tok)); } catch (...) {} }
        return out;
    }
    struct EscalationGate
    {
        std::vector<double> mean, sd, w;      // w[0]=bias, w[1..]=standardized-feature coefs
        double threshold = 0.5;
        bool   enabled   = false;
        std::atomic<long long> seen{0}, skipped{0};
        EscalationGate()
        {
            const char* p = std::getenv("MTG_ESCALATION_GATE");
            if (!p || !*p) { return; }
            std::ifstream in(p);
            if (!in) { return; }
            std::stringstream buf; buf << in.rdbuf();
            const std::string s = buf.str();
            mean = ParseJsonNumArray(s, "mean");
            sd   = ParseJsonNumArray(s, "std");
            w    = ParseJsonNumArray(s, "w");
            if (mean.empty() || sd.size() != mean.size() || w.size() != mean.size() + 1) { return; }
            const char* t = std::getenv("MTG_ESCALATION_GATE_T");
            if (t && *t) { try { threshold = std::stod(t); } catch (...) {} }
            enabled = true;
        }
        // raw feature order MUST match the trainer: [committed, gap, turn, est_wt] + 46 midgame feats.
        double PNoOp(const std::vector<double>& raw) const
        {
            double z = w[0];
            for (std::size_t k = 0; k < raw.size() && k < mean.size(); ++k)
            { z += w[k + 1] * (raw[k] - mean[k]) / sd[k]; }
            if (z > 30) { z = 30; } else if (z < -30) { z = -30; }
            return 1.0 / (1.0 + std::exp(-z));
        }
        ~EscalationGate()
        {
            if (enabled && seen.load() > 0)
            {
                std::cerr << "[escalation-gate] seen=" << seen.load()
                          << " skipped=" << skipped.load()
                          << " (" << (100.0 * skipped.load() / seen.load()) << "%)"
                          << " threshold=" << threshold << "\n";
            }
        }
    };
    EscalationGate g_escalation_gate;
}


TurnSolver::SearchLine TurnSolver::FullSearchLineHybrid(const GameState& state, int depth,
                                                        int max_turns, bool second_main,
                                                        TranspositionTable* tt, SearchBudget* budget,
                                                        int* out_committed_depth,
                                                        int value_min_depth, int budget_ms,
                                                        bool value_no_fallback,
                                                        const std::vector<int>& value_fallback_take_at,
                                                        double escalation_fresh_frac,
                                                        int beam_width, int beam_leafdepth,
                                                        int escalation_cap,
                                                        double escalation_r)
{
    // --- Escalation budget shaping (all opt-in; ALL unset => byte-identical to the shared-budget hybrid) ---
    // MTG_ESC_SPLIT=c   : CAP the value-leaf probe to c*budget_ms so the probe cannot eat the whole decision
    //                     budget; the remaining (1-c) is RESERVED for the heuristic escalation. No total
    //                     budget increase -- pure reservation (the user's "don't let the value-leaf eat it all").
    // MTG_ESC_SINGLE    : the escalation runs ONE heuristic pass at a single target depth (committed-offset),
    //                     not FullSearchLine's 1..depth ladder -- "only do one depth", concentrating the
    //                     reserved budget and skipping the shallow-pass rework. MTG_ESC_SINGLE_OFFSET=k picks
    //                     the depth = clamp(committed-k, 1, depth); k=2 targets the crossover-min depth
    //                     (heuristic-d(committed-2) is the shallowest that beats value-leaf-d(committed)), and
    //                     the single pass's overrun-abort then acts as the crossover-skip (an unaffordable
    //                     escalation is dropped, keeping the value-leaf line, instead of committing a partial).
    // See docs/design/escalation-and-rollout-cost.md + hinata-escalation-budget-restore memory.
    static const double s_esc_split   = []{ const char* e = std::getenv("MTG_ESC_SPLIT");
                                            return (e && *e) ? std::atof(e) : -1.0; }();
    SearchBudget  probe_cap_budget;
    SearchBudget* probe_budget = budget;
    if (s_esc_split >= 0.0 && budget_ms > 0)
    {
        probe_cap_budget = SearchBudget::FromVirtualMs(
            std::max(1, static_cast<int>(std::lround(s_esc_split * budget_ms))));
        probe_budget = &probe_cap_budget;
    }
    // K-predictor (MTG_ESC_PREDICT): record the probe's per-depth leaf counts so the escalation can predict
    // its own affordable depth from this MEASURED structure. Cleared before the probe; recorded during it.
    // The single-depth predict path (MTG_ESC_SINGLE_PREDICT) uses the SAME free probe structure, so arm the
    // recording for it too (else g_probe_leaves stays 0 and the affordability walk sees an empty tree).
    static const bool s_esc_predict = EnvOn("MTG_ESC_PREDICT");
    static const bool s_esc_single_predict = EnvOn("MTG_ESC_SINGLE_PREDICT");
    static const bool s_esc_single_env     = EnvOn("MTG_ESC_SINGLE");
    // Effective single-depth escalation. Precedence mirrors the beam/fresh_frac: an explicitly-set MTG_ESC_SINGLE
    // env is the RESEARCH override (its own _PREDICT/_ABS/_FALLBACK knobs apply, byte-identical A/B path). Else a
    // per-deck escalation_cap>0 (the ADOPTED policy) drives the single-pass predicted-affordable path. The deck
    // path ALWAYS predicts (that is what makes it safe fleet-wide); the env path predicts only if _PREDICT is set.
    const bool eff_single_deck    = !s_esc_single_env && escalation_cap > 0;
    const bool eff_single         = s_esc_single_env || eff_single_deck;
    const bool eff_single_predict = s_esc_single_env ? s_esc_single_predict : eff_single_deck;
    if (s_esc_predict || eff_single_predict)
    {
        for (int d = 0; d < 16; ++d) { g_probe_leaves[d] = 0; g_probe_cost[d] = 0; }
        g_probe_recording = true;
    }
    // VALUE-RANKED BEAM (MTG_ESC_BEAM=W): capture the probe's per-node value ranking so the escalation reorders
    // its beam by the lines the value pass rated best (see g_probe_plan_vals). Off (W=0) => map stays null =>
    // recording is a no-op => byte-identical. The map lives for this whole decision (probe + escalation).
    static const bool s_esc_beam_env_set = EnvSet("MTG_ESC_BEAM");
    static const int s_esc_beam = []{ const char* e = std::getenv("MTG_ESC_BEAM");
                                      return (e && *e) ? std::atoi(e) : 0; }();
    // MTG_ESC_BEAM_LEAFDEPTH=D: apply the beam only to nodes within D plies of the leaf (the top plies keep full
    // exploration so the committed PLAY is never beamed out). Unset => INT_MAX => uniform beam (original).
    static const int s_esc_beam_leafdepth = []{ const char* e = std::getenv("MTG_ESC_BEAM_LEAFDEPTH");
                                                return (e && *e) ? std::atoi(e) : 2147483647; }();
    static const bool s_esc_beam_static = EnvOn("MTG_ESC_BEAM_STATIC");  // prune by static order
    // DEPTH-ADAPTIVE BEAM (per-deck path). Precedence (mirrors escalation_fresh_frac): an EXPLICITLY-SET
    // MTG_ESC_BEAM env wins (keeps env A/B working + its literal uniform-beam semantics -- a research tool, no
    // depth adaptation). Else the per-deck value_play beam (beam_width >= 0 whenever an enabled block drives,
    // passed at ANY depth; beam_width < 0 => no block => off => byte-identical).
    //
    // The value_play block configures the DEEP (production) beam; at a shallower search depth the value leaf is a
    // weaker ranking proxy, so a NARROW value-ranked beam mis-prunes the winning line (measured d3 quality loss).
    // Measured answer (escalation-beam-verify.md, width ladder): a WIDE STATIC leaf beam is quality-neutral at d3
    // on all 6 decks and faster. So auto-select the regime by SEARCH DEPTH:
    //   deep    (depth >= 5): value-ranked, the deck's own width/leafdepth == the ADOPTED d5 config -> BYTE-
    //                         IDENTICAL (antilife/hinata W3 ld2; light decks beam_width 0 = off; burn at d5 off).
    //   shallow (depth == 3): STATIC, width 20, leafdepth 1 (MEASURED neutral+faster; overrides the deck config).
    //   d1/d2/d4 + all else : beam OFF -> BYTE-IDENTICAL to pre-adoption. d3 is the ONLY shallow depth turned on
    //                         (the sole validated + suite-covered off-policy depth). d4 is a deliberate hole:
    //                         plausibly neutral (same W20/ld1 mechanism) but UNMEASURED, so it stays off until a
    //                         d4 sweep confirms it -- see escalation-beam-verify.md "d4 widening (deferred)".
    // (The escalation-budget renewal / fresh_frac stays on-policy-only in AIEngine, so a shallow beam runs on the
    // legacy budget as measured.)
    int  eff_beam;
    int  eff_beam_leafdepth;
    bool eff_beam_static;
    if (s_esc_beam_env_set)
    {
        eff_beam           = s_esc_beam;
        eff_beam_leafdepth = s_esc_beam_leafdepth;
        eff_beam_static    = s_esc_beam_static;
    }
    else if (beam_width >= 0)   // an enabled value_play block drives; enable ONLY at VALIDATED depths (d3, d5)
    {
        // The beam fires only where it has been measured + validated: d5 (deep, the heavy decks' production
        // target -- value W3 ld2, byte-identical adoption) and d3 (shallow, static W20 ld1). EVERY other depth
        // (d1/d2/d4 and d6+) is UNTESTED -> beam OFF -> byte-identical. d6+ used to fire via `depth >= 5`; it's
        // gated off now so off-policy deep runs don't hit an unvalidated beam path. Widen only after measuring.
        if (depth == 5)      { eff_beam = beam_width; eff_beam_leafdepth = beam_leafdepth; eff_beam_static = false; }
        else if (depth == 3) { eff_beam = 20;         eff_beam_leafdepth = 1;              eff_beam_static = true;  }
        else                 { eff_beam = 0;          eff_beam_leafdepth = beam_leafdepth; eff_beam_static = false; }
    }
    else { eff_beam = 0; eff_beam_leafdepth = beam_leafdepth; eff_beam_static = false; }
    ProbePlanVals beam_vals_map;
    // Value recording is consumed only by the VALUE-ranked reorder; the static beam ignores it, so arm it only
    // for the value regime (deep). Off/static => null map => no recording overhead => byte-identical.
    ProbeValsGuard _pvg((eff_beam > 0 && !eff_beam_static) ? &beam_vals_map : nullptr);
    if (eff_beam > 0 && !eff_beam_static) { g_probe_val_recording = true; }
    int committed = depth;
    SearchLine line = FullSearchLine(state, depth, max_turns, second_main, tt, probe_budget, &committed);
    g_probe_recording = false;
    g_probe_val_recording = false;

    const bool value_active = UseValueModel() && state.m_value_model && !state.m_value_model->empty();
    // A VERIFIED win (win_turn within the committed horizon) is decided by real simulation, NOT the leaf
    // estimate, so the value-leaf can't have mis-ranked it -> keep it (never escalate). Only an UNVERIFIED
    // committed line (win_turn is a beyond-horizon leaf ESTIMATE) depends on the WEAK value leaf. The raw
    // value leaf reaches converged-heuristic quality only at ~d5 and is materially worse below it (measured:
    // value-leaf-d(k) ~= heuristic-d(k-3)); so an unverified line committed below `value_min_depth` (the
    // per-model trust depth: knights/slivers stop at d5 where their leaf matches, others escalate up to the
    // user depth) is escalated to the exact heuristic leaf. See learned-d0-policy.md.
    const bool verified = (line.win_turn <= state.turn_number + committed - 1);
    const bool escalate = (value_min_depth > 0 && value_active && committed < value_min_depth && !verified);
    if (g_hybrid_stats.enabled && value_active)
    {
        g_hybrid_stats.decisions.fetch_add(1);
        int di = (committed >= 0 && committed < 16) ? committed : 15;
        g_hybrid_stats.probe_depth[di].fetch_add(1);
        if (verified)  { g_hybrid_stats.verified.fetch_add(1); }
        if (escalate)  { g_hybrid_stats.redos.fetch_add(1); g_hybrid_stats.redo_depth[di].fetch_add(1); }
    }
    if (escalate)
    {
        // ONE heuristic search on the REMAINING shared budget (not a fresh one): its start gate commits the
        // deepest AFFORDABLE depth Hd -- "the best result we can afford", per the fallback design. TAKE it only
        // if it clears the crossover: value-leaf-d(committed) ~= heuristic-d(committed - kValueTrustOffset), so
        // heuristic-Hd beats the committed value-leaf line iff Hd > committed - kValueTrustOffset. Otherwise the
        // affordable heuristic is SHALLOWER than the value leaf is worth (e.g. Hd=1 vs value-leaf-d5 ~= H2) ->
        // keep the (deeper, cheaper) value-leaf line. The value leaf spent little of the budget (free leaves),
        // so the remaining budget is nearly the whole decision budget. kValueTrustOffset=3 is uniform across all
        // measured decks (value-leaf-d5 ~= heuristic-d2). g_force_heuristic_leaf makes FSLineWin use the exact
        // rollout leaf; the shared tt holds only leaf-independent tail rollouts, so it is uncontaminated.
        // MTG_VALUE_TRUST_OFFSET overrides the crossover offset (experiments): a LARGE value => always TAKE the
        // escalation (never fall back to the value-leaf line) = "never trust the leaf once we've escalated";
        // useful to test whether the crossover is what leaks value-leaf quality on a deck (e.g. hinata).
        static const int s_vto_override = []{ const char* e = std::getenv("MTG_VALUE_TRUST_OFFSET");
                                              return (e && *e) ? std::atoi(e) : -1; }();
        const int kValueTrustOffset = (s_vto_override >= 0) ? s_vto_override : 3;
        const int old_wt = line.win_turn;
        // Confidence-gate: skip escalations predicted to be no-ops (byte-identical when unset). The gate
        // scores the SAME features the dump records -- all observed BEFORE the re-search -- so it can
        // bypass the expensive heuristic search entirely. A skip keeps the value-leaf line + committed.
        if (g_escalation_gate.enabled)
        {
            g_escalation_gate.seen.fetch_add(1);
            std::vector<double> raw;
            raw.reserve(4 + 46);
            raw.push_back(committed);
            raw.push_back(value_min_depth - committed);   // gap
            raw.push_back(state.turn_number);
            raw.push_back(old_wt);                          // est_wt
            for (int f : ExtractMidGameFeatures(state, MidGamePlanSummary{})) { raw.push_back(f); }
            if (g_escalation_gate.PNoOp(raw) > g_escalation_gate.threshold)
            {
                g_escalation_gate.skipped.fetch_add(1);
                if (out_committed_depth) { *out_committed_depth = committed; }
                return line;   // skip: trust the value-leaf line, save the full re-search
            }
        }
        // Escalation budget source. DEFAULT IS OFF (-1 == legacy shared REMAINING budget) so this refactor is
        // BYTE-IDENTICAL to the committed regression GT with no env set. The FRESH-FULL budget below is an
        // UNADOPTED candidate: it fixes the value-leaf budget-exhaustion regression (the probe spends most of
        // the shared decision budget reaching committed depth, starving the heuristic re-search to ~d1 so it
        // mis-commits a hair shallower on knife-edge games -> antilife regression, overnight-audit-2026-07-11)
        // and a fresh budget recovers those wins. But it is a genuine QUALITY *and* PERFORMANCE tradeoff, not a
        // free win: single-seed A/B shows antilife/hinata better (+~0.01-0.02 LP) BUT TH slightly worse
        // (4.11037->4.11371) and ~+16% wall on hinata. Adopting it requires a full-regression A/B (train+held-
        // out) + perf measurement + user approval + a deliberate GT rebaseline -- do NOT flip the default back
        // to 1.0 without that. Turn it on for that A/B with MTG_ESCALATION_FRESH_FRAC=1.0. See hinata-
        // escalation-budget-restore + docs/design/escalation-refactor-drift.md. Overrides:
        //   MTG_ESC_SPLIT set              -> reserve: cap probe, dedicated ((1-split)+restore)*budget_ms
        //   MTG_ESCALATION_FRESH_FRAC=f>=0 -> fresh f*budget_ms (f=1.0 == full fresh budget, the candidate)
        //   MTG_ESCALATION_FRESH_FRAC=-1   -> LEGACY shared REMAINING budget (DEFAULT; == committed GT)
        static const bool   s_fresh_frac_env_set = EnvSet("MTG_ESCALATION_FRESH_FRAC");
        static const double s_fresh_frac = []{ const char* e = std::getenv("MTG_ESCALATION_FRESH_FRAC");
                                               return (e && *e) ? std::atof(e) : -1.0; }();  // default OFF (legacy)
        // Precedence: an EXPLICITLY-SET env (MTG_ESCALATION_FRESH_FRAC, the experiment/A-B override) wins over
        // everything -- needed so the env-based A/B still works once a deck ships an enabled value_play block
        // carrying its own escalation_fresh_frac. Else the per-deck value_play value (a real number, i.e. not
        // the -2.0 sentinel) wins; else legacy -1. Env UNSET + block sentinel/-1 => -1 == byte-identical.
        const double eff_fresh_frac = s_fresh_frac_env_set
                                    ? s_fresh_frac
                                    : (escalation_fresh_frac <= -1.5 ? s_fresh_frac : escalation_fresh_frac);
        static const int    s_esc_single_off = []{ const char* e = std::getenv("MTG_ESC_SINGLE_OFFSET");
                                                   return (e && *e) ? std::atoi(e) : 0; }();
        ForceHeuristicLeafGuard _fh(true);
        // VALUE-GUIDED BEAM (MTG_ESC_BEAM=W): restrict the heuristic escalation to the probe's top-W
        // value-ranked lines per node, so it rolls out only ~W^depth frontier states instead of the full
        // B&B frontier. `pre` is reordered by the probe's recorded value ranks (g_probe_plan_vals) before the
        // cap, so the kept W lines are the value pass's best -- only their rollouts are new work. s_esc_beam was
        // read + recording armed at the top of the hybrid (before the probe). 0 = unlimited = byte-identical.
        // eff_beam_leafdepth restricts the beam to near-leaf nodes (protects the top plies / committed play).
        EscBeamGuard _beam(eff_beam, eff_beam_leafdepth, eff_beam_static);
        int hcommitted = depth;
        SearchBudget  esc_alloc_budget;
        SearchBudget* esc_budget = budget;   // legacy shared REMAINING budget (only when fresh_frac < 0)
        if (s_esc_split >= 0.0 && budget_ms > 0)
        {
            const double frac = std::max(0.0, 1.0 - s_esc_split);
            esc_alloc_budget = SearchBudget::FromVirtualMs(
                std::max(1, static_cast<int>(std::lround(frac * budget_ms))));
            esc_budget = &esc_alloc_budget;
        }
        else if (eff_fresh_frac >= 0.0 && budget_ms > 0)
        {
            esc_alloc_budget = SearchBudget::FromVirtualMs(
                std::max(1, static_cast<int>(std::lround(eff_fresh_frac * budget_ms))));
            esc_budget = &esc_alloc_budget;
        }
        SearchLine hline;
        static const bool s_esc_predict_warm = EnvOn("MTG_ESC_PREDICT_WARM");
        if (s_esc_predict && (tt == nullptr || s_esc_predict_warm))
        {
            // K-PREDICTOR (MTG_ESC_PREDICT): reproduce the ladder's committed depth D by running the REAL ladder
            // start-gate on MEASURED pass costs (not a cheap prediction, which was too irregular on hinata),
            // starting two below a rough guess so the boundary gate has both adjacent costs measured (exact), and
            // skipping the cheap passes below. FSLineWin is a pure function of state+depth, so matching D exactly
            // is byte-identical to the ladder. The climb MEASURES real costs, so it is robust to a WARM shared tt
            // too (bottoming lookahead) -- MTG_ESC_PREDICT_WARM lifts the cold-only restriction; the climb then
            // uses the SAME shared tt the ladder would, so its measured costs match. See escalation-interior-reuse.
            TranspositionTable  pred_tt_local;
            TranspositionTable* pred_tt = (tt != nullptr) ? tt : &pred_tt_local;   // warm shared tt when present
            FSLineCache pred_cache;
            const long long esc_units = (esc_budget && !esc_budget->Unlimited())
                                      ? esc_budget->Limit()
                                      : SearchBudget::FromVirtualMs(std::max(1, budget_ms)).Limit();
            const int pred_max = std::min(depth, std::max(1, committed));
            // Optional alpha multiplier (MTG_ESC_PREDICT_MULT, default 1.0 == exact ladder). A small >1 lets
            // the replay commit one deeper (bias toward "never below the ladder") at a tuning cost.
            static const double s_pred_mult = []{ const char* e = std::getenv("MTG_ESC_PREDICT_MULT");
                                                  return (e && *e) ? std::atof(e) : 1.0; }();
            const double gate_alpha = kStartGateAlpha * s_pred_mult;
            // EMA weight for the amortized R update.
            constexpr double s_r_alpha = 0.4;
            // 1) Rough GUESS D0 of the committed depth from the free probe structure + amortized R. This only
            //    picks WHERE to start the real ladder (efficiency); correctness comes from the MEASURED costs
            //    below, not this estimate. No separate d1 seed pass (it polluted the climb's memo -> cmeas[1]=0);
            //    a default R bootstraps the first guess and the climb learns R from its own passes.
            if (g_esc_R <= 0.0) { g_esc_R = 120.0; }   // deck-agnostic prior; refined by the first measured pass
            const double R = g_esc_R;
            double chat[16] = {0};
            for (int d = 1; d <= pred_max; ++d)
            {
                chat[d] = static_cast<double>(std::max<long long>(0, g_probe_cost[d]))
                        + R * static_cast<double>(std::max<long long>(0, g_probe_leaves[d]));
            }
            const long long esc_c1 = static_cast<long long>(chat[1]);   // audit-dump naming
            int D0 = 1;
            {
                double rem = static_cast<double>(esc_units) - chat[1];
                double cprev = chat[1], cprev2 = 0.0; bool haveprev2 = false;
                for (int d = 2; d <= pred_max; ++d)
                {
                    const double ratio = haveprev2 ? cprev / std::max(1.0, cprev2) : kDefaultGrowth;
                    if (cprev * ratio > gate_alpha * std::max(0.0, rem)) { break; }
                    D0 = d; rem -= chat[d]; cprev2 = cprev; cprev = chat[d]; haveprev2 = true;
                }
            }
            const int K = D0;   // stats/audit naming (the pre-measurement guess)
            // 2) Run the REAL ladder starting at lo = D0-2 (two below the guess, so the guess boundary's gate has
            //    both adjacent costs MEASURED = exact) and CLIMB. The gate fires ONLY where it is exact: a
            //    MEASURED cprev/cprev2 ratio, or the pass-2 kDefaultGrowth bootstrap the real ladder itself uses.
            //    Where neither holds (the very bottom of the measured range), the pass is BELOW the boundary for
            //    a decent guess, so we run it WITHOUT gating (never stop early on an estimate = never lossy). A
            //    too-shallow guess just climbs more measured passes (still exact); a too-deep guess runs one
            //    extra (deeper, never shallower). Skips only the cheap passes below lo (small cumulative, est).
            hline = { max_turns + 1, {} };
            hcommitted = 1;
            int aborts = 0;
            double cmeas[16] = {0};          // measured pass costs (budget units); 0 = not measured
            double Rl = std::max(1.0, R);    // local per-leaf rollout cost, refined by each measured pass
            auto run_meas = [&](int t) -> bool
            {
                t = std::max(1, t);
                SearchBudget vb(esc_units);
                vb.SetOverrunLimit(std::max<long long>(
                    static_cast<long long>(kOverrunBeta * static_cast<double>(esc_units)), kOverrunFloor));
                const long long lv0 = g_fs_leaf_evals;
                SearchLine r = FSLineWin(state, t, max_turns, max_turns + 1, second_main,
                                         pred_tt, &pred_cache, &vb);
                if (vb.Overrun()) { return false; }
                hline = r; hcommitted = t; cmeas[std::clamp(t, 0, 15)] = static_cast<double>(vb.Used());
                g_climb_cmeas[std::clamp(t, 0, 15)] = static_cast<double>(vb.Used());
                const double lv = static_cast<double>(std::max<long long>(1, g_fs_leaf_evals - lv0));
                if (t >= 2)   // learn R only from >=d2 passes (d1's per-leaf cost is the inflated one)
                {
                    const double roll = std::max(0.0, static_cast<double>(vb.Used())
                        - static_cast<double>(std::max<long long>(0, g_probe_cost[std::clamp(t, 0, 15)])));
                    if (roll > 0.0) { Rl = std::max(1.0, roll / lv);
                                      g_esc_R = (1.0 - s_r_alpha) * g_esc_R + s_r_alpha * Rl; }
                }
                return true;
            };
            // Estimate the (small) cumulative budget consumed by the SKIPPED passes below `s`, from probe struct.
            auto cum_below = [&](int s) -> double
            {
                double c = 0.0;
                for (int k = 1; k < s; ++k)
                {
                    c += static_cast<double>(std::max<long long>(0, g_probe_cost[std::clamp(k, 0, 15)]))
                       + Rl * static_cast<double>(std::max<long long>(0, g_probe_leaves[std::clamp(k, 0, 15)]));
                }
                return c;
            };
            static const int s_lookback = []{ const char* e = std::getenv("MTG_ESC_PREDICT_LOOKBACK");
                                               return (e && *e) ? std::max(1, std::atoi(e)) : 2; }();
            const int lo = std::clamp(D0 - s_lookback, 1, pred_max);
            for (int d = 0; d < 16; ++d) { g_climb_cmeas[d] = 0; }
            g_climb_start = lo;
            if (!run_meas(lo))
            {
                ++aborts; (void)run_meas(std::max(1, lo - 1));   // lo pass overran -> one shallower
            }
            else
            {
                double cum = cum_below(lo) + cmeas[std::clamp(lo, 0, 15)];
                for (int t = lo + 1; t <= pred_max; ++t)
                {
                    // Gate only where EXACT: measured cprev AND cprev2, or the pass-2 kDefaultGrowth bootstrap
                    // (which the real ladder also uses). Otherwise (bottom of range, cprev2 unmeasured) do NOT
                    // gate -> run the pass (it is below the boundary for a decent guess; never stop on an estimate).
                    const bool have_ratio = (t >= 3) && (t - 2 >= lo) && (cmeas[std::clamp(t - 2, 0, 15)] > 0.0);
                    const bool bootstrap  = (t == 2) && (lo == 1);
                    double est = -1.0;
                    if (have_ratio)
                    {
                        const double cprev = cmeas[std::clamp(t - 1, 0, 15)];
                        const double cprev2 = cmeas[std::clamp(t - 2, 0, 15)];
                        est = cprev * (cprev / std::max(1.0, cprev2));
                    }
                    else if (bootstrap)
                    {
                        est = cmeas[1] * kDefaultGrowth;
                    }
                    if (est >= 0.0)
                    {
                        const double rem = static_cast<double>(esc_units) - cum;
                        if (est > gate_alpha * std::max(0.0, rem)) { break; }   // exact gate stops -> commit t-1
                    }
                    if (!run_meas(t)) { break; }                                // overran -> keep last completed
                    cum += cmeas[std::clamp(t, 0, 15)];
                }
            }
            if (s_esc_predict_audit)
            {
                // Shadow-run the true ladder (from d1) and compare its committed depth to the predictor's.
                // predict < ladder = LOSSY (the estimate under-shot -> quality risk). Audit only; result unused.
                TranspositionTable  audit_tt_local;
                TranspositionTable* audit_tt = (tt != nullptr) ? tt : &audit_tt_local;
                SearchBudget audit_budget(esc_units);
                int ladder_committed = depth;
                for (int d = 0; d < 16; ++d) { g_hlad_cost[d] = 0; g_hlad_leaves[d] = 0; }
                g_hlad_recording = true;
                (void)FullSearchLine(state, depth, max_turns, second_main, audit_tt, &audit_budget,
                                     &ladder_committed);
                g_hlad_recording = false;
                g_pred_audit_n.fetch_add(1, std::memory_order_relaxed);
                const int delta = hcommitted - ladder_committed;
                if (delta < 0)
                {
                    g_pred_lossy.fetch_add(1, std::memory_order_relaxed);
                    if (g_pred_lossy_dumped.fetch_add(1) < 12)
                    {
                        std::cerr << "[esc-predict-audit] LOSSY turn=" << state.turn_number
                                  << " predicted_K=" << K << " predict_committed=" << hcommitted
                                  << " ladder_committed=" << ladder_committed
                                  << " (probe committed=" << committed << ", esc_c1=" << esc_c1
                                  << ", esc_units=" << esc_units
                                  << ", probe_leaves d1.." << depth << "=";
                        for (int d = 1; d <= depth; ++d) { std::cerr << g_probe_leaves[d] << (d<depth?"/":""); }
                        std::cerr << ")\n";
                        std::cerr << "    [hladder] incremental cost d1.." << depth << "=";
                        for (int d = 1; d <= depth; ++d) { std::cerr << g_hlad_cost[d] << (d<depth?"/":""); }
                        std::cerr << " (start-gate consumes THESE; ladder_committed=" << ladder_committed << ")\n";
                        std::cerr << "    [climb] start=" << g_climb_start << " measured cmeas d1.." << depth << "=";
                        for (int d = 1; d <= depth; ++d) { std::cerr << g_climb_cmeas[d] << (d<depth?"/":""); }
                        std::cerr << " (mine; compare to hladder incremental)\n";
                    }
                }
                else if (delta > 0) { g_pred_deeper.fetch_add(1, std::memory_order_relaxed); }
            }
        }
        else if (eff_single)
        {
            // SINGLE-DEPTH ESCALATION: run ONE heuristic pass instead of the full 1..depth ladder. The ADOPTED
            // path (per-deck value_play.escalation_cap>0 => eff_single_deck) predicts the budget-AFFORDABLE depth
            // and runs one pass there, capped at the deck's convergence depth -- see the PREDICTED-AFFORDABILITY
            // block below (this tracks the ladder's own depth, so it is quality-neutral and cheaper fleet-wide;
            // measured antilife 4-seed dLP=0.0000 at ~45% work, light decks dLP=0.0000 + faster). The env
            // research path (MTG_ESC_SINGLE) targets committed-offset. Overrun-guarded with depth fallback
            // (below): a pass that cannot complete falls back a depth; if none fit, the value-leaf line is
            // kept (hcommitted=0).
            // `cap` = the CONVERGENCE cap (heuristic gains ~0 past it). Per-deck path (adopted): the profile's
            // escalation_cap. Env research path: committed-offset. Never search deeper.
            const int cap = eff_single_deck
                          ? std::clamp(escalation_cap, 1, depth)
                          : std::clamp(committed - s_esc_single_off, 1, depth);
            // DETERMINISM: the adopted per-deck path (eff_single_deck) must use a FIXED cost-per-leaf R and NEVER
            // mutate the thread_local g_esc_R. g_esc_R's EMA trajectory depends on the game->thread schedule (each
            // worker converges R over the games it happens to run), so an adaptive R makes PLAY non-reproducible
            // across runs / thread counts -- fatal for GT digest baselining and cross-machine determinism. Frozen
            // R (calibrated offline into value_play.escalation_r, else a deck-agnostic prior) keeps the predicted
            // target a pure function of this decision's probe structure. The env research path keeps adaptive R.
            // Env override MTG_ESC_SINGLE_R=<v> forces a fixed R in the env RESEARCH path too (deterministic),
            // so R can be swept per deck without editing profiles. >0 => frozen at v; unset => adaptive (research).
            static const double s_esc_single_r = []{ const char* e = std::getenv("MTG_ESC_SINGLE_R");
                                                     return (e && *e) ? std::atof(e) : 0.0; }();
            const bool   frozen_R = eff_single_deck || (s_esc_single_r > 0.0);
            const double R_fixed  = eff_single_deck ? ((escalation_r > 0.0) ? escalation_r : 120.0)
                                                    : ((s_esc_single_r > 0.0) ? s_esc_single_r : 120.0);
            // PREDICTED-AFFORDABILITY target (MTG_ESC_SINGLE_PREDICT): run ONE pass at the depth the LADDER
            // would commit to (its budget-affordable depth), capped at `cap`. Estimated from the value-leaf
            // probe's FREE per-depth leaf structure x an amortized heuristic cost-per-leaf (g_esc_R), walked
            // through the SAME start-gate the ladder uses. This makes the single pass track the ladder depth on
            // EVERY deck: heavy decks that afford the cap hit the cap (the skip-shallow win); light decks whose
            // ladder stops at d1/d2 target d1/d2 too -- killing the forced-d3 confound (their whole regression)
            // where it is cheapest to fix, with no ladder rework. Off (predict unset) => target == cap (the
            // fixed-depth research path). No probe structure (all leaves 0) => walk stops at d1 => target 1.
            int target = cap;
            if (eff_single_predict)
            {
                double R;
                if (frozen_R) { R = R_fixed; }             // deterministic per-deck constant (no g_esc_R mutation)
                else { if (g_esc_R <= 0.0) { g_esc_R = 120.0; } R = g_esc_R; }  // env research: adaptive
                const int pmax = std::min(depth, std::max(1, committed));
                double chat[16] = {0};
                for (int d = 1; d <= pmax && d < 16; ++d)
                {
                    chat[d] = static_cast<double>(std::max<long long>(0, g_probe_cost[d]))
                            + R * static_cast<double>(std::max<long long>(0, g_probe_leaves[d]));
                }
                // Budget the ladder actually sees at escalation: the escalation budget's REMAINING units (fresh
                // budget => its full Limit; legacy shared => what the probe left). Walk the start-gate on the
                // per-pass estimate chat[d] directly (we HAVE the whole cost curve, unlike the ladder which must
                // extrapolate from measured passes) and take the deepest pass whose incremental cost still fits.
                const long long esc_units = (esc_budget && !esc_budget->Unlimited())
                                          ? esc_budget->Remaining()
                                          : SearchBudget::FromVirtualMs(std::max(1, budget_ms)).Limit();
                int daff = 1;
                double rem = static_cast<double>(esc_units) - chat[1];
                for (int d = 2; d <= pmax; ++d)
                {
                    if (chat[d] > kStartGateAlpha * std::max(0.0, rem)) { break; }
                    daff = d; rem -= chat[d];
                }
                target = std::min(cap, std::max(1, daff));
            }
            FSLineCache single_cache;
            // BUGFIX: FSLineWin memoizes leaf rollouts only through a non-null tt. In normal play the
            // hybrid's `tt` is nullptr (m_shared_tt is set only during bottoming), so passing it straight
            // through gave the single pass NO leaf cache -- while the ladder path (FullSearchLine) always
            // owns a local_table. That made "single-depth" re-roll every transposed leaf and falsely look
            // more expensive than the ladder. Give the single pass the same per-call leaf cache the ladder
            // has, so the skip-earlier-depths comparison is apples-to-apples.
            TranspositionTable single_tt_local;
            TranspositionTable* single_tt = (tt != nullptr) ? tt : &single_tt_local;
            const long long ub = esc_budget ? esc_budget->Used() : 0;
            if (esc_budget && !esc_budget->Unlimited())
            {
                // Bound the single pass by the RESERVED budget itself (not the 1M runaway floor): the point is
                // to spend the reserve on one depth and, if the pass does not fit, ABORT = crossover-skip. A
                // generous 2x headroom lets an "almost done" pass finish while still capping the cost, so this
                // is the performance guard the user asked for (no unbounded d5 heuristic per escalation).
                const long long ceil = 2 * esc_budget->Limit();
                esc_budget->SetOverrunLimit(ub + std::max<long long>(ceil, 1));
            }
            // B&B INCUMBENT for the single pass. Without a tight ACHIEVABLE win-turn bound, FSLineWin cannot
            // prune -> a lone deep pass rolls out the whole tree (measured 2-4x the ladder's work: the ladder
            // gets its bound free from its shallow passes). Supply one. MTG_ESC_SINGLE_BOUND:
            //   0 = loose max_turns+1 (COLD default -- no pruning; the un-handled baseline).
            //   1 = value-leaf committed win turn (== legacy MTG_ESC_SINGLE_WARM). OPTIMISTIC and often
            //       UNACHIEVABLE -> the search can't find a line beating it, so it never establishes an
            //       incumbent and prunes nothing (measured WORSE, not better). A diagnostic, not a fix.
            //   2 = SPARSE-LADDER seed: a cheap shallow FSLineWin at depth 2, reusing
            //       the SAME tt+cache (both keyed by (state, remaining-depth), so cross-depth reuse is sound).
            //       Its win turn is a REAL achievable bound (a genuine line at that depth). Tighter bound, but
            //       costs a shallow search. (Conceptually the same family as MTG_ESC_JUMP.)
            //   3 = ROLLOUT bound: a greedy playout win turn from the CURRENT state (real + achievable, cheapest
            //       to obtain; looser than (2) since the greedy policy is suboptimal).
            // Back-compat: MTG_ESC_SINGLE_WARM (with BOUND unset) selects mode 1.
            static const bool s_esc_single_warm  = EnvOn("MTG_ESC_SINGLE_WARM");
            static const int  s_esc_single_bound = []{ const char* e = std::getenv("MTG_ESC_SINGLE_BOUND");
                                                       return (e && *e) ? std::atoi(e) : -1; }();
            const int bound_mode = (s_esc_single_bound >= 0) ? s_esc_single_bound : (s_esc_single_warm ? 1 : 0);
            int single_cut = max_turns + 1;                                     // 0: loose (cold)
            if (bound_mode == 1) { single_cut = old_wt; }                       // 1: value-leaf (optimistic)
            else if (bound_mode == 2)                                           // 2: sparse-ladder seed
            {
                const int sd = std::clamp(2, 1, std::max(1, target - 1));   // sparse-ladder seed depth
                if (sd < target)
                {
                    SearchLine seed = FSLineWin(state, sd, max_turns, max_turns + 1, second_main,
                                                single_tt, &single_cache, esc_budget);
                    if (seed.win_turn <= max_turns) { single_cut = seed.win_turn; }   // real achievable bound
                }
            }
            else if (bound_mode == 3)                                           // 3: ONE deep rollout bound
            {
                // ONE heuristic rollout from the current state with STRUCTURED lookahead to the search
                // depth (a deep, value-leaf-depth-ish playout that follows good play rather than greedy).
                // Its win turn is a REAL achievable bound at depth -- the cheap incumbent a lone deep pass
                // otherwise lacks, WITHOUT the ladder's shallow rework. This is the user's "one rollout at
                // the value-leaf's max depth" made concrete.
                const int rd = depth;
                GameState rs = state;
                const int rwt = SimulateToEnd(std::move(rs), rd, max_turns, esc_budget,
                                              max_turns + 1, second_main, single_tt);
                if (rwt <= max_turns) { single_cut = rwt; }
            }
            // BUDGET-LIMITED single pass with DEPTH FALLBACK: try `target`; if it OVERRUNS the budget,
            // retry one depth SHALLOWER (reusing the warmed tt/cache, so the d3 attempt's exploration
            // primes the d2 retry) -- down to d1. A depth that never fits keeps the value-leaf line
            // (hcommitted=0). ON for the adopted per-deck path (it IS the budget-limited fallback);
            // the env research path runs one attempt at target (abort => keep value-leaf).
            const bool eff_fallback = eff_single_deck;
            // UP-CLIMB (adopted path always ON; env research path: MTG_ESC_SINGLE_CLIMB).
            // The frozen-R hint picks the START depth cheaply (no d1/d2 tax). After that pass, use its LIVE measured
            // cost x the probe's leaf-expansion ratio to test whether ONE deeper still fits the remaining budget; if
            // so, climb (capped at `cap`). This corrects a too-shallow (pessimistic-hint) target from THIS decision's
            // own measurement -- deterministic + adaptive, no cross-decision state. It only fires with real budget
            // headroom, so the common case (hint ~right => little budget left => estimate exceeds remaining) stays a
            // single pass. Combined with the existing overrun fallback (corrects a too-DEEP hint), the hint need only
            // be roughly right: R is a hint, the live estimate makes the final depth reliable.
            static const bool s_esc_single_climb = EnvOn("MTG_ESC_SINGLE_CLIMB");
            const bool eff_climb = eff_single_deck || s_esc_single_climb;
            int  td = target;
            bool aborted = true;
            bool first = true;
            long long c_last = 0;   // measured budget-work of the last COMPLETED pass (for the climb estimate)
            for (; td >= 1; --td)
            {
                if (esc_budget && !esc_budget->Unlimited())
                {
                    const long long ub2 = esc_budget->Used();
                    esc_budget->SetOverrunLimit(ub2 + std::max<long long>(2 * esc_budget->Limit(), 1));
                }
                const long long r_ub0 = esc_budget ? esc_budget->Used() : 0;
                const long long r_lv0 = g_fs_leaf_evals;
                hline = FSLineWin(state, td, max_turns, single_cut, second_main, single_tt, &single_cache, esc_budget);
                aborted = (esc_budget && esc_budget->Overrun());
                if (!aborted && esc_budget) { c_last = esc_budget->Used() - r_ub0; }
                // Self-calibrate R = heuristic cost per (un-beamed) PROBE leaf, anchored to this pass's actual
                // cost. CRITICAL: the affordability walk uses the PROBE's un-beamed per-depth leaf counts, but the
                // escalation runs BEAMED -- so R must be measured against the probe's CUMULATIVE leaves through td
                // (not the pass's own beamed leaf count), else R is inflated by the beam-pruning factor and the
                // walk stops too shallow (measured hinata under-search). roll = pass cost minus the probe's
                // interior cost; R = roll / cumulative-probe-leaves. Then chat[d]=probe_cost[d]+R*probe_leaves[d]
                // is self-consistent (its cumulative sum through td reproduces this measured cost). EMA-smoothed;
                // d1 skipped (short-tree per-leaf overhead is unrepresentative), mirroring the ladder.
                (void)r_lv0;
                if (eff_single_predict && first && !aborted && td >= 2 && esc_budget)
                {
                    long long cumL = 0, cumC = 0;
                    for (int k = 1; k <= td && k < 16; ++k)
                    {
                        cumL += std::max<long long>(0, g_probe_leaves[k]);
                        cumC += std::max<long long>(0, g_probe_cost[k]);
                    }
                    const double lv = std::max(1.0, static_cast<double>(cumL));
                    const double roll = std::max(0.0, static_cast<double>(esc_budget->Used() - r_ub0)
                        - static_cast<double>(cumC));
                    if (roll > 0.0)
                    {
                        const double Rsample = std::max(1.0, roll / lv);
                        // Adaptive EMA ONLY on the env research path -- the adopted per-deck path (frozen_R) holds
                        // R fixed for determinism (see the frozen_R note above).
                        if (!frozen_R) { g_esc_R = 0.6 * g_esc_R + 0.4 * Rsample; }
                    }
                }
                first = false;
                if (!aborted || !eff_fallback) { break; }
            }
            // CLIMB: only when the hint pass FIT ON THE FIRST try (td == target). A descent means the hint OVER-shot,
            // so climbing would just re-overrun -- the affordable depth is at/below where we landed. Estimate the
            // next depth's cost from the last pass's LIVE cost x a growth factor; climb while it fits the remaining
            // budget, capped at `cap`. A deeper pass that overruns is reverted (keep the current committed line).
            // No probe structure for the next depth => cannot estimate => stop (never gamble blind).
            // GROWTH estimate mode (MTG_ESC_CLIMB_GROWTH; research toggle, default 0 == shipped, byte-identical):
            //   0 = probe UN-BEAMED leaf-count ratio at every step. Conservative -- it over-estimates the BEAMED
            //       deep-pass cost on explosive-tree combo decks (hinata) so the climb stops one depth short there
            //       (measured: climb reaches d5 on 841 decisions vs the ladder's 1098). Light decks are safe.
            //   1 = BEAM-AWARE: use the MEASURED beamed growth (c_last/c_prev) for climb steps >=2, keeping the
            //       leaf-ratio bootstrap for step 1. Step 1 -- and thus light decks, which climb <=1 step -- is
            //       byte-identical to mode 0; only hinata-style MULTI-step climbs change, using this decision's
            //       real beamed cost curve instead of the un-beamed proxy. (Likely deeper => more work; A/B it.)
            //   2 = like 1 but bootstraps step 1 from the probe COST ratio (interior-aware) rather than leaves.
            static const int s_climb_growth = []{ const char* e = std::getenv("MTG_ESC_CLIMB_GROWTH");
                                                  return (e && *e) ? std::atoi(e) : 0; }();
            if (eff_climb && !aborted && td == target && esc_budget && !esc_budget->Unlimited())
            {
                double c_prev_climb = 0.0;   // cost of the pass before c_last (measured beamed growth for steps >=2)
                while (td < cap && td + 1 < 16)
                {
                    const long long pl_cur  = std::max<long long>(1, g_probe_leaves[td]);
                    const long long pl_next = std::max<long long>(0, g_probe_leaves[td + 1]);
                    if (pl_next <= 0) { break; }
                    double est_next;
                    if (s_climb_growth == 0)
                    {
                        // ORIGINAL expression (kept verbatim so mode 0 stays byte-identical to the shipped path).
                        est_next = static_cast<double>(c_last)
                                 * static_cast<double>(pl_next) / static_cast<double>(pl_cur);
                    }
                    else
                    {
                        double growth;
                        if (c_prev_climb > 0.0)  // step >=2: this decision's measured beamed growth
                        {
                            growth = static_cast<double>(c_last) / c_prev_climb;
                        }
                        else if (s_climb_growth == 2)  // step 1 bootstrap: probe interior-cost ratio
                        {
                            const long long pc_cur  = std::max<long long>(1, g_probe_cost[td]);
                            const long long pc_next = std::max<long long>(0, g_probe_cost[td + 1]);
                            growth = static_cast<double>(pc_next) / static_cast<double>(pc_cur);
                        }
                        else                           // step 1 bootstrap: leaf ratio (== mode 0)
                        {
                            growth = static_cast<double>(pl_next) / static_cast<double>(pl_cur);
                        }
                        if (growth < 1.0) { growth = 1.0; }   // deeper never costs less
                        est_next = static_cast<double>(c_last) * growth;
                    }
                    const double rem = static_cast<double>(std::max<long long>(0, esc_budget->Remaining()));
                    if (est_next > kStartGateAlpha * rem) { break; }
                    const long long ub2 = esc_budget->Used();
                    esc_budget->SetOverrunLimit(ub2 + std::max<long long>(2 * esc_budget->Limit(), 1));
                    const long long r_ub0 = esc_budget->Used();
                    SearchLine up = FSLineWin(state, td + 1, max_turns, single_cut, second_main,
                                              single_tt, &single_cache, esc_budget);
                    if (esc_budget->Overrun()) { break; }   // deeper pass did not fit => keep current line
                    c_prev_climb = static_cast<double>(c_last);
                    hline  = up;
                    c_last = esc_budget->Used() - r_ub0;
                    ++td;
                }
            }
            if (esc_budget) { esc_budget->SetOverrunLimit(0); }
            hcommitted = aborted ? 0 : td;
        }
        else
        {
            // ESCALATION DEPTH CAP (MTG_ESC_DEPTH_CAP=D; per-deck this would be the convergence depth from the
            // value_leaf_table). The heuristic escalation CONVERGES ~d3 on every deck (heuristic_lp gains ~0 past
            // d3), so ladder passes beyond D are pure WASTE. Cap the ladder at D: it still deepens BUDGET-
            // ADAPTIVELY up to D (deepest affordable via the start-gate, so a budget too small for d3 lands on d2
            // and we live with that), it just never spends budget past convergence -- which also frees budget so
            // MORE decisions reach the useful depth. Keeps the ladder's incumbent + move-ordering (unlike a cold
            // single pass). 0 = off = byte-identical.
            static const int s_esc_depth_cap = []{ const char* e = std::getenv("MTG_ESC_DEPTH_CAP");
                                                   return (e && *e) ? std::atoi(e) : 0; }();
            const int esc_depth = (s_esc_depth_cap > 0) ? std::min(depth, s_esc_depth_cap) : depth;
            const long long used_before = (s_esc_measure && esc_budget) ? esc_budget->Used() : 0;
            hline = FullSearchLine(state, esc_depth, max_turns, second_main, tt, esc_budget, &hcommitted);
            if (s_esc_measure && esc_budget && hcommitted >= 1)
            {
                // Ladder work for this escalation (interior + rollout units).
                const long long ladder_units = esc_budget->Used() - used_before;
                // COLD single pass at the ladder's ACTUAL committed depth (fresh leaf cache + interior memo +
                // an unlimited budget so it completes -- we only want its work count). Same target depth as the
                // ladder committed to, so this is the fair "skip earlier depths" comparison. Result discarded.
                TranspositionTable meas_tt;
                FSLineCache        meas_cache;
                SearchBudget       meas_budget;   // default: unlimited (no overrun), just counts Used()
                (void)FSLineWin(state, hcommitted, max_turns, max_turns + 1, second_main,
                                &meas_tt, &meas_cache, &meas_budget);
                g_esc_ladder_units.fetch_add(ladder_units, std::memory_order_relaxed);
                g_esc_single_units.fetch_add(meas_budget.Used(), std::memory_order_relaxed);
                g_esc_measure_n.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (g_hybrid_stats.enabled)
        {
            int hi = (hcommitted >= 0 && hcommitted < 16) ? hcommitted : 15;
            g_hybrid_stats.redo_hdepth[hi].fetch_add(1);
            // Did the escalation COMMIT a verified win (lethal within its searched horizon)? If so and at a
            // depth < the probe's committed depth, it's a verified win the value-leaf probe PRUNED (leaf-driven
            // B&B cutoff differs) -- proving the shallow escalation passes are NOT skippable.
            const bool esc_ver = (hcommitted > 0)
                              && (hline.win_turn <= state.turn_number + hcommitted - 1);
            if (esc_ver)
            {
                g_hybrid_stats.esc_verified.fetch_add(1);
                g_hybrid_stats.esc_ver_hdepth[hi].fetch_add(1);
                if (hcommitted < committed) { g_hybrid_stats.esc_verified_short.fetch_add(1); }
            }
            if (hcommitted < depth) { g_hybrid_stats.redo_short.fetch_add(1); }
        }
        // Decide whether to TAKE the heuristic escalation or fall back to the value-leaf line.
        //  (1) Table-driven crossover (preferred): the measured per-committed-depth fall-back level
        //      hc*[committed] from value_fallback_take_at -- take iff the escalation reached it. This replaces
        //      the uniform "committed-3" assumption (different committed depths fall back at DIFFERENT levels;
        //      weak-leaf decks sooner, strong-leaf decks never). Clamp committed to the measured range.
        //  (2) Legacy uniform offset (no table): value-leaf-d(committed) ~= heuristic-d(committed-offset), plus
        //      the value_no_fallback override (always take).
        // The env override MTG_VALUE_TRUST_OFFSET (s_vto_override>=0) forces the uniform crossover for A/B.
        bool taken;
        if (!value_fallback_take_at.empty() && s_vto_override < 0)
        {
            const int hi = static_cast<int>(value_fallback_take_at.size()) - 1;   // max measured committed depth
            const int c  = committed < 1 ? 1 : (committed > hi ? hi : committed);
            taken = (hcommitted >= value_fallback_take_at[static_cast<std::size_t>(c)]);
        }
        else
        {
            const bool no_fallback = value_no_fallback && (s_vto_override < 0);
            taken = no_fallback ? (hcommitted >= 1)
                                : (hcommitted > committed - kValueTrustOffset);
        }
        const bool wt_changed = taken && (hline.win_turn != old_wt);
        // Opt-in escalation-outcome dump (byte-identical when MTG_ESCALATION_DUMP unset). All features are
        // observed BEFORE the escalation ran, so they can gate a future confidence-skip. See the doc.
        if (g_escalation_dump.enabled)
        {
            const std::vector<int> feats = ExtractMidGameFeatures(state, MidGamePlanSummary{});
            g_escalation_dump.row(feats, taken ? 1 : 0, wt_changed ? 1 : 0,
                                  state.turn_number, committed, value_min_depth - committed, old_wt);
        }
        if (taken)
        {
            line      = hline;
            committed = hcommitted;
        }
    }
    if (out_committed_depth) { *out_committed_depth = committed; }
    return line;
}

// ---- Rule-miner: enumerate-all-earliest-wins (offline diagnostic, see header) ----------
TurnSolver::EarliestWinReport TurnSolver::EnumerateEarliestWins(const GameState& state,
                                                                int max_turns, bool second_main,
                                                                bool rollout_label, int rollout_depth,
                                                                bool honest, bool earliest_only)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    ShuffleEvalGuard _seg(true);  // decoupling instrument: planning shuffles use shuffle_salt_search
    // MTG_LABEL_NOWIN_CACHE: DEFAULT ON; =0 disables. The bound-qualified no-win memo is not an
    // optional speedup here, it is what makes the horizon ladder affordable: the ladder's cost IS
    // proving absence at each horizon, and this is the only thing that stops every candidate
    // re-proving the refutations its siblings already proved. Measured over 40 games/deck it is
    // 6.8x on Goblins, 3.4x on burn, 2.6x on antilife, 1.6x on Dragonstorm -- and 0.7x on
    // treasure_hunt, the one deck where the bookkeeping outweighs the reuse. Labels are identical
    // in every arm (unbounded budget => freed budget changes nothing). See
    // docs/design/label-horizon-ladder.md.
    ForceNoWinCacheGuard _nwc(EnvOn("MTG_LABEL_NOWIN_CACHE", true));
    // Full-strength honest teacher: decouple the rollout continuation's per-turn lookahead from the
    // real draw order (see g_honest_teacher). Only meaningful with a depth>0 rollout label.
    HonestTeacherGuard _htg(honest && rollout_label && rollout_depth > 0);
    EarliestWinReport report;
    report.turn     = state.turn_number;
    report.earliest = max_turns + 1;
    report.bounded_candidates = earliest_only;

    // Same candidate set the search ranks (cast ORDERINGS included iff MTG_SEARCH_ORDER /
    // MTG_UNPRUNED is set -- EnumeratePlansWithLand expands them there).
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);

    // Cross-candidate B&B is OFF by default (cutoff = max_turns+1) so each candidate gets its TRUE
    // earliest win rather than a pruned bound -- the eval-row dump needs every candidate's own
    // number. With earliest_only the caller wants only the MIN, so we carry the running incumbent
    // as the cutoff instead; see the header. Depth is deep enough to reach any win up to max_turns.
    int depth = max_turns - state.turn_number + 1;
    if (depth < 1) { depth = 1; }

    // Shared tail memo across candidates (downstream states transpose). The overrun guard is never
    // armed, so no pass is aborted mid-flight -- but m_limit still gates whether the iterative
    // deepening STARTS a further pass, and on an expensive deck it does bind. When it binds the
    // label silently degrades to whatever depth was reached, i.e. a LATER win turn than the truth.
    // MTG_VALUE_LABEL_BUDGET_MS exposes it (0 = unlimited) so a label run can be checked for, or
    // freed from, budget degradation. Offline-only: this function is a dump/diagnostic path.
    TranspositionTable tt;
    FSLineCache        lc;
    static const int s_label_budget_ms = EnvInt("MTG_VALUE_LABEL_BUDGET_MS", 1000000);
    SearchBudget     budget = s_label_budget_ms > 0 ? SearchBudget::FromVirtualMs(s_label_budget_ms)
                                                    : SearchBudget();
    // ARM THE OVERRUN GUARD. Without this the budget only gated whether a FURTHER ladder pass
    // STARTS; a pass already running had no ceiling at all, so one position could search forever.
    // Measured: Hinata seed 555005 turn 2 held a worker for 65 minutes and emitted nothing -- and
    // because rows are produced per position in turn order, it took that game's remaining SEVEN
    // rows down with it. That is the likely mechanism behind "slivers/treasure_hunt/Knights
    // produced zero rows in 34 hours": not slow decks, one hung position per game.
    //
    // The ceiling is the budget itself, so a run's cost per position is now bounded by the knob
    // that always claimed to bound it. Unlimited (MTG_VALUE_LABEL_BUDGET_MS=0) stays unarmed and
    // byte-identical.
    if (!budget.Unlimited()) { budget.SetOverrunLimit(budget.Limit()); }
    // Watermark for the truncation test below (see g_fs_trunc_events): every budget abort in the
    // search bumps it, so a change across this call means some number in this report came back as
    // a fabricated no-win rather than a searched one.
    const unsigned long long trunc_at_entry = g_fs_trunc_events;
    // MTG_LABEL_COLD_CACHE=1 (DIAGNOSTIC, default off): give each candidate a FRESH tt/lc instead of
    // sharing them. Tests whether a label depends on the ORDER candidates are evaluated in -- it must
    // not. FSLineWin returns the FIRST in-horizon win it finds, and that shortcut is only sound under
    // FullSearchLine's ladder ("pass L-1 was a COMPLETE refutation"); this path calls FSLineTail at a
    // single depth instead, so a cached entry may hold *a* win rather than *the earliest*, and sharing
    // would then propagate it to later candidates. If cold caches move labels EARLIER, that is the
    // mechanism behind the MTG_VALUE_LABEL_BNB anomaly and the shipped labels are pessimistic.
    // Pair with MTG_VALUE_LABEL_BUDGET_MS=0 so budget exhaustion is not a confound.
    static const bool s_cold_cache = EnvOn("MTG_LABEL_COLD_CACHE");

    // ---- ITERATIVE-DEEPENING HORIZON (MTG_LABEL_LADDER) -----------------------------------------
    // DEFAULT ON; =0 restores the old single-depth call.
    //
    // FSLineWin stops at the FIRST in-horizon win it finds and returns it as the node optimum. That
    // shortcut is sound only under FullSearchLine's LADDER, whose premise it names explicitly: pass
    // L-1 was a complete refutation, so every in-horizon win found at pass L sits exactly at the
    // horizon edge and the first one is therefore the minimum. Called at a SINGLE deep depth -- which
    // is what this labeller did -- that premise is simply false: the horizon is `max_turns` away, wins
    // at many different turns are all "in horizon", and the first one found is *a* win, not *the
    // earliest*. The label is then silently PESSIMISTIC.
    //
    // Measured (antilife seed 555002, turn 1): the single-depth call labels the position 7.00, the
    // ladder 5.667 -- a 1.33-turn error on one row. This is also the whole of the MTG_VALUE_LABEL_BNB
    // anomaly ("pruning found an EARLIER win than the unpruned search", which is impossible for a
    // sound search): B&B's tight cutoff narrows the horizon, which accidentally restores the premise.
    // It is NOT cache order-dependence -- MTG_LABEL_COLD_CACHE=1 leaves the discrepancy intact.
    //
    // So ladder the horizon here the same way FullSearchLine does: pass dd searches dd turns with
    // cutoff turn+dd, and a candidate's win turn is recorded at the FIRST dd that finds one. Every
    // node in a pass shares one horizon edge => the shortcut is sound again. The tight per-pass cutoff
    // is also the free B&B this path never had: earliest_only stops the whole ladder at the first
    // winning pass, and every pass prunes at `turn > cutoff`.
    static const bool s_label_ladder = EnvOn("MTG_LABEL_LADDER", true);
    const bool ladder = s_label_ladder && !rollout_label;
    std::vector<int> ladder_wt;
    if (ladder)
    {
        ladder_wt.assign(pre.size(), max_turns + 1);
        std::vector<char> settled(pre.size(), 0);
        std::size_t unsettled = 0;
        // Pass 0: the cases that need no search at all -- a plan that kills us via its own on-cast
        // triggers, and a plan that wins this turn (the floor; no later pass can beat it).
        for (std::size_t i = 0; i < pre.size(); ++i)
        {
            GameState s = state;
            std::vector<Action> bp;
            ApplyPlanDirect(s, pre[i], true, &bp);
            if (s.ActivePlayer().life <= 0) { settled[i] = 1; continue; }   // -> max_turns+1
            AnimateLandsShared(s, nullptr);
            ActivateTapTokensShared(s, nullptr);
            SimulateCombat(s);
            if (s.Opponent().life <= 0) { ladder_wt[i] = state.turn_number; settled[i] = 1; continue; }
            ++unsettled;
        }
        // dd STARTS AT 0, not 1. Pass 0 is `FSLineTail(s, 0)`, which with second_main enumerates
        // THIS turn's post-combat main and can kill there -- a win at state.turn_number that the
        // pre-loop above (combat only) does not see. For depth >= 2 pass 1 would catch it anyway
        // (FSLineTail checks the second main before recursing), so the omission only bit at
        // depth <= 1, i.e. turn == max_turns, where `dd <= depth - 1` ran NO pass at all and every
        // candidate fell through as a loss. Caught by the earlier/later gate: Hinata seed 555000
        // turn 8 labelled 8 by the old path and 9 by the ladder -- the one and only row that has
        // ever moved LATER.
        for (int dd = 0; dd <= depth - 1 && unsettled > 0; ++dd)
        {
            const int cut = state.turn_number + dd;
            bool any_new = false;
            for (std::size_t i = 0; i < pre.size(); ++i)
            {
                if (settled[i]) { continue; }
                TranspositionTable  tt_cold;
                FSLineCache         lc_cold;
                TranspositionTable& tt_use = s_cold_cache ? tt_cold : tt;
                FSLineCache&        lc_use = s_cold_cache ? lc_cold : lc;
                // Re-apply per pass rather than caching |candidates| GameStates: the apply is cheap
                // next to the search, and a plan-count that can reach the thousands makes holding
                // one board per candidate the expensive choice.
                GameState s = state;
                std::vector<Action> bp;
                ApplyPlanDirect(s, pre[i], true, &bp);
                AnimateLandsShared(s, nullptr);
                ActivateTapTokensShared(s, nullptr);
                SimulateCombat(s);
                const TurnSolver::SearchLine tail =
                    FSLineTail(s, dd, max_turns, cut, second_main, &tt_use, &lc_use, &budget);
                // Exact BECAUSE every shallower pass refuted this candidate: the win is at the
                // horizon edge, so it is this candidate's earliest. A no-win is just "not yet".
                if (tail.win_turn <= cut)
                { ladder_wt[i] = tail.win_turn; settled[i] = 1; --unsettled; any_new = true; }
            }
            // earliest_only: the caller reads report.earliest alone, and the first pass to produce
            // ANY win has produced the minimum over all candidates. Everything still unsettled is a
            // bound from here on, which is exactly what bounded_candidates advertises.
            if (earliest_only && any_new) { break; }
        }
    }

    std::size_t cand_index = 0;
    for (const TurnSolver::Plan& p : pre)
    {
        const std::size_t ci = cand_index++;
        int wt;
        if (ladder)
        {
            wt = ladder_wt[ci];                         // resolved by the pass loop above
        }
        else
        {
        TranspositionTable  tt_cold;
        FSLineCache         lc_cold;
        TranspositionTable& tt_use = s_cold_cache ? tt_cold : tt;
        FSLineCache&        lc_use = s_cold_cache ? lc_cold : lc;
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);

        if (s.ActivePlayer().life <= 0)                 // self-lethal line -> never a win
        {
            wt = max_turns + 1;
        }
        else
        {
            AnimateLandsShared(s, nullptr);
            ActivateTapTokensShared(s, nullptr);
            SimulateCombat(s);
            if (s.Opponent().life <= 0)
            {
                wt = state.turn_number;                 // wins THIS turn
            }
            else if (rollout_label)
            {
                // NON-CLAIRVOYANT label: advance to the next turn (mirroring FSLineTail's
                // pre-FSLineWin step) and play the greedy d0 baseline policy forward. This
                // is what a real non-clairvoyant player achieves from here -- it does NOT
                // over-credit a durdle plan the way the clairvoyant search does (the search
                // recovers with a lucky line it reads from the library; the greedy rollout
                // cannot). Depth 0 => SolveWithLookahead reduces to Solve (the d0 policy).
                GameState r = s;
                if (!SimulateEndAndStartNextTurn(r)) { wt = max_turns + 1; }
                else
                {
                    ExpireStagedCards(r);
                    wt = SimulateToEnd(std::move(r), rollout_depth, max_turns, &budget,
                                       max_turns + 1, second_main, &tt_use);
                }
            }
            else
            {
                // B&B: with earliest_only, a candidate only matters if it BEATS the incumbent, so
                // pass the incumbent as the cutoff. Sound because the cutoff only ever tightens
                // (report.earliest is monotonically non-increasing): a subtree pruned as "no win
                // better than c" stays pruned under any tighter c, and the caches store only
                // genuine wins -- a no-win is never cached, since it may be a cutoff abort rather
                // than a true dead end (see FSLineWin's store guard). A win returned under a tight
                // cutoff is still exact: a better line would have beaten the cutoff and so was not
                // cut. Candidates that lose the race come back as a BOUND, which is why
                // bounded_candidates is set.
                const int cutoff = earliest_only ? report.earliest : (max_turns + 1);
                TurnSolver::SearchLine tail = FSLineTail(s, depth - 1, max_turns,
                                                         cutoff, second_main,
                                                         &tt_use, &lc_use, &budget);
                wt = tail.win_turn;
            }
        }
        }   // !ladder

        EarliestWinCandidate c;
        c.land           = p.land_to_play;
        c.fetch          = p.fetch_target;
        c.searched_order = p.searched_order;
        c.win_turn       = wt;

        // Effective cast order, mirroring apply_plan_actions: a searched plan casts in vector
        // order; otherwise the canonical clean-set order (stable-sort by CastOrderLess). The
        // enabler-first / opaque-set nuance is approximated (a searched_order flag marks the
        // exact-order plans). Sacrifice-land casts are reported separately (they execute last).
        std::vector<int> hand_casts;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[i];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.sacrifice_land) { c.sac_casts.push_back(a.card_name); }
            else                  { hand_casts.push_back(i); }
        }
        if (!p.searched_order)
        {
            std::stable_sort(hand_casts.begin(), hand_casts.end(), [&](int x, int y)
            { return CastOrderLess(state, p.actions[x], p.actions[y]); });
        }
        for (int i : hand_casts) { c.cast_order.push_back(p.actions[i].card_name); }

        if (wt < report.earliest) { report.earliest = wt; }
        report.candidates.push_back(std::move(c));
    }
    // A truncated search returns max_turns+1, which is EXACTLY what a genuine loss returns. The
    // report cannot tell the consumer which one it is holding, so it tells it that it cannot tell.
    // See EarliestWinReport::truncated -- the consumer's job is to drop the position, not to
    // salvage a number from it.
    report.truncated = (g_fs_trunc_events != trunc_at_entry) || budget.Overrun();
    return report;
}

// Reshuffle-averaged non-clairvoyant search as a PLAY policy. See the header. Mirrors the per-
// candidate evaluation of EnumerateEarliestWins' rollout branch, but (a) averages over K reshuffled
// futures with COMMON RANDOM NUMBERS across candidates (same reshuffle per sample k -> low-variance
// comparison), (b) returns the best Plan for the caller to execute against the TRUE library. The
// continuation is honest (g_honest_teacher) at depth>0 so it never reads real draws; depth 0 is a
// pure greedy non-clairvoyant rollout. NO memo/budget (each sample reshuffles a distinct future, so
// the leaf TT's size=>content assumption breaks) -- expensive by design; this measures the ceiling.
TurnSolver::Plan TurnSolver::ReshuffleAvgChoosePlan(const GameState& state, int K, int depth,
                                                    int max_turns, bool second_main, bool is_pre_combat)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    std::vector<TurnSolver::Plan> plans;
    if (is_pre_combat)
    {
        plans = EnumeratePlansWithLand(state, true);
    }
    else
    {
        // Post-combat (second) main: the same candidate set FSLineTail searches -- every castable
        // post-combat play PLUS the idle option (advance without casting). See FSLineTail.
        plans = EnumeratePlans(state, false);
        plans.push_back(TurnSolver::Plan{});
    }
    if (plans.empty())     { return TurnSolver::Plan{}; }
    if (plans.size() == 1) { return plans[0]; }
    if (K < 1) { K = 1; }

    const int turn = state.turn_number;
    std::vector<long long> sum(plans.size(), 0);

    for (int k = 0; k < K; ++k)
    {
        // One sampled future per sample k, SHARED across all candidates (common random numbers).
        const uint64_t rs = state.game_seed
                          + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(k) + 1)
                          + 1000003ULL * static_cast<uint64_t>(turn)
                          + (is_pre_combat ? 0ULL : 7919ULL);   // distinct sample stream per phase
        for (size_t i = 0; i < plans.size(); ++i)
        {
            GameState s = state;
            s.ActivePlayer().library.Shuffle(rs);
            s.shuffle_salt_search = rs;             // honest continuation folds this (varies per k)
            ShuffleEvalGuard   _seg(true);
            HonestTeacherGuard _htg(depth > 0);     // decouple the depth>0 continuation lookahead
            std::vector<Action> bp;
            ApplyPlanDirect(s, plans[i], is_pre_combat, &bp);
            int wt = max_turns + 1;
            if (s.ActivePlayer().life <= 0)         // self-lethal line -> never a win
            {
                wt = max_turns + 1;
            }
            else
            {
                if (is_pre_combat)
                {
                    AnimateLandsShared(s, nullptr);
                    ActivateTapTokensShared(s, nullptr);
                    SimulateCombat(s);
                }
                if (s.Opponent().life <= 0)         // wins THIS turn (library-independent -> all k agree)
                {
                    wt = turn;
                }
                else
                {
                    // This turn's SECOND main (only when evaluating the pre-combat plan of a
                    // second-main deck): search it honestly (reshuffled) so the pre-combat plan's
                    // value ACCOUNTS for the finisher it enables, instead of skipping it. Not greedy.
                    if (is_pre_combat && second_main)
                    {
                        TurnSolver::Plan post = TurnSolver::SolveWithLookahead(
                            s, false, depth > 0 ? depth : 1, max_turns, nullptr, false,
                            second_main, nullptr);
                        ApplyPlanDirect(s, post, false);
                    }
                    if (s.Opponent().life <= 0) { wt = turn; }
                    else
                    {
                        GameState r = s;
                        if (!SimulateEndAndStartNextTurn(r)) { wt = max_turns + 1; }
                        else
                        {
                            ExpireStagedCards(r);
                            wt = SimulateToEnd(std::move(r), depth, max_turns, nullptr,
                                               max_turns + 1, second_main, nullptr);
                        }
                    }
                }
            }
            sum[i] += wt;
        }
    }

    // Pick min average win turn. EnumeratePlansWithLand is value-sorted, so scanning in order and
    // keeping the STRICT minimum breaks ties toward the highest static value (the engine's tiebreak).
    size_t best     = 0;
    long long best_sum = sum[0];
    for (size_t i = 1; i < plans.size(); ++i)
    {
        if (sum[i] < best_sum) { best_sum = sum[i]; best = i; }
    }

    auto makes_land = [](const TurnSolver::Plan& p)
    { return p.land_decided && !p.land_to_play.empty(); };

    // Tempo bonus: the reshuffle averaging shuffles the TRUE library away, so its mean future has NORMAL land
    // density -- it is optimistic about mana and undervalues a land drop as screw-insurance. In a truly
    // land-light game (invisible to the averaging) skipping the drop is a permanent tempo loss (gi11: defer
    // scores 0.5t "better" yet durdles to T8; the land drop wins T5). Reward developing mana: subtract
    // round(bonus*K) from any land-drop plan before picking the min -- breaks decisions the objective
    // considers close without overriding a real win-turn difference > bonus. The bonus (avg-turns) is the
    // ARCHETYPE PROVIDER's call (NcLandDropTempoBonus): GenericProvider = safe gated default, AntiLifegain =
    // aggressive/ungated, land-pitch decks protected by the mana-base gate + PreferHoldLandDrop.
    // MTG_NC_TEMPO(/_LANDS), when set, OVERRIDE the provider with a flat gated bonus (the A/B sweep
    // controls). Provider default 0 for unknown decks + env unset => byte-identical.
    static const bool   s_tempo_env_set = EnvSet("MTG_NC_TEMPO");
    static const double s_env_tempo     = []{ const char* e = std::getenv("MTG_NC_TEMPO");
                                              return (e && *e) ? std::atof(e) : 0.0; }();
    static const int    s_env_lands     = []{ const char* e = std::getenv("MTG_NC_TEMPO_LANDS");
                                              return (e && *e) ? std::atoi(e) : 99; }();
    double tempo_turns = 0.0;
    if (is_pre_combat)
    {
        if (s_tempo_env_set)   // A/B override: flat env bonus, gated by env lands cap
        {
            int lands = 0;
            for (const Permanent& p : state.battlefield)
            {
                if (p.controller_index == state.active_player_index && p.card.IsLand()) { ++lands; }
            }
            if (lands < s_env_lands) { tempo_turns = s_env_tempo; }
        }
        else                   // default: the archetype provider decides (gated internally)
        {
            tempo_turns = ResolveProvider(state).NcLandDropTempoBonus(state, state.active_player_index);
        }
    }
    if (tempo_turns > 0.0)
    {
        const long long bonus = std::llround(tempo_turns * K);
        size_t    bt   = 0;
        long long badj = sum[0] - (makes_land(plans[0]) ? bonus : 0);
        for (size_t i = 1; i < plans.size(); ++i)
        {
            long long a = sum[i] - (makes_land(plans[i]) ? bonus : 0);
            if (a < badj) { badj = a; bt = i; }
        }
        best = bt;
    }

    static const bool s_nc_debug = EnvOn("MTG_NC_DEBUG");
    if (s_nc_debug && is_pre_combat)
    {
        int n_ties = 0, n_land_ties = 0;
        for (size_t i = 0; i < plans.size(); ++i)
        {
            if (sum[i] == best_sum) { ++n_ties; if (makes_land(plans[i])) ++n_land_ties; }
        }
        std::fprintf(stderr,
            "[NCDBG] turn=%d plans=%zu bestsum=%lld(avg%.2f) ties=%d land_in_ties=%d "
            "best_makes_land=%d chosen_land='%s'\n",
            turn, plans.size(), best_sum, (double)best_sum / K, n_ties, n_land_ties,
            (int)makes_land(plans[best]), plans[best].land_to_play.c_str());
        if (turn <= 2)
        {
            for (size_t i = 0; i < plans.size(); ++i)
                std::fprintf(stderr, "        plan[%zu] sum=%lld(avg%.2f) val=%d land='%s' actions=%zu\n",
                    i, sum[i], (double)sum[i] / K, plans[i].value,
                    plans[i].land_to_play.c_str(), plans[i].actions.size());
        }
    }
    return plans[best];
}

// ---- Public API ----

TurnSolver::Plan TurnSolver::SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                                  int depth, int max_turns,
                                                  SearchBudget* budget, bool enforce_budget,
                                                  bool second_main, TranspositionTable* tt,
                                                  int* out_committed_win,
                                                  int* out_committed_sub_depth)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    // Report the committed pass's (win turn, sub_depth) to the caller's optional
    // out-params. Used by the non-convergence detector; every return path calls it.
    auto report = [&](int win, int sub_depth)
    {
        if (out_committed_win)       { *out_committed_win = win; }
        if (out_committed_sub_depth) { *out_committed_sub_depth = sub_depth; }
    };

    if (depth <= 0)
    {
        if (budget) { budget->Consume(1); }
        report(max_turns + 1, 0);   // greedy fallback is not an exhaustively-verified win
        return Solve(state, is_pre_combat);
    }

    // The enforcing top-level call owns the per-decision transposition table and
    // threads it through the whole recursion; rollout sub-searches reuse the table
    // they were handed. local_table is only referenced when we create it here.
    TranspositionTable  local_table;
    if (tt == nullptr && enforce_budget) { tt = &local_table; }

    // Candidate set is land-folded: when a land drop is available this enumerates
    // every (land choice x spell subset) plus a defer option, each plan tagged with
    // its land_to_play. The same fold runs in the rollout (SimulateToEndImpl calls
    // this per turn), so the land choice is searched consistently end to end.
    // Searched-breakpoint fan-out is emitted only here, at the enforcing (committed) decision --
    // see g_bp_root_enum. The rollouts below re-enter this function with the flag clear, so their
    // leaf evaluation keeps the cheap greedy continuation.
    g_bp_root_enum = enforce_budget;
    std::vector<Plan> candidates = EnumeratePlansWithLand(state, is_pre_combat);
    g_bp_root_enum = false;

    // Candidates are sorted highest-value first, so the first winning plan
    // (if any) is also the highest-value winning plan.
    for (const Plan& p : candidates)
    {
        if (p.wins_this_turn) { report(state.turn_number, depth - 1); return p; }
    }

    if (candidates.empty()) { report(max_turns + 1, 0); return Plan{}; }

    // Track the committed pass's win turn / sub_depth for non-convergence reporting.
    int committed_win       = max_turns + 1;
    int committed_sub_depth = 0;

    // Iterative deepening: evaluate EVERY candidate at increasing rollout depth
    // (sub_depth = 0, 1, ... depth-1), and only commit a pass's result once that
    // pass has fully completed. When the budget runs out we fall back to the best
    // plan from the last fully-completed depth, so the decision always reflects a
    // COMPLETE comparison of all candidates (at some depth) rather than a partial
    // ranking. This guarantees a low-ranked but winning line is never starved:
    // even the cheap depth-0 pass plays each rollout out and discovers its win.
    //
    // Fidelity-consistent ranking: each pass ranks candidates at its OWN sub_depth
    // with a FRESH incumbent (we do NOT seed the decision from the previous,
    // shallower pass), and we commit the deepest fully-completed pass's own best.
    // A deeper rollout is the more reliable estimate, so its ranking wins outright —
    // carrying a shallower pass's (often optimistic) win turn as the incumbent would
    // let a stale shallow estimate out-rank a deeper-confirmed equal win. The
    // branch-and-bound cutoff is the WITHIN-pass running best, so SimulateToEnd is
    // only ever compared at one fidelity per pass and the value tiebreak resolves
    // genuinely-equal win turns. (Cost: we lose the cross-pass cutoff's head start,
    // re-discovering the bound each pass — accepted for consistency.)
    //
    // Budget control (only when enforce_budget; the rollout sub-search runs every
    // pass to completion and merely consumes). Two deterministic gates, both keyed
    // on the running work-unit count — never the clock — so a deeper search makes
    // the same start/skip/abort decisions and can never come out worse:
    //   1. START GATE: before pass k, estimate its cost from the measured growth
    //      of the previous passes and skip the whole pass (committing pass k-1) if
    //      it clearly won't fit. No point starting a pass that would be cut off
    //      mid-sweep and discarded anyway.
    //   2. OVERRUN GUARD: once a pass has started, run it PAST the budget to
    //      completion — sunk work near completion shouldn't be thrown away. Abort
    //      and roll back to pass k-1 only when the pass is BOTH well over budget
    //      AND still expensive to finish. Roll-back is free: each candidate runs on
    //      a GameState copy, so a pass only touches pass-local state.
    Plan best_plan = candidates.front();

    const bool   gate         = enforce_budget && budget != nullptr;
    long long    c_prev       = 0;       // cost of pass k-1 (work units)
    long long    c_prev2      = 0;       // cost of pass k-2
    bool         have_prev    = false;
    bool         have_prev2   = false;

    for (int sub_depth = 0; sub_depth <= depth - 1; ++sub_depth)
    {
        long long remaining_at_start = budget ? budget->Remaining() : LLONG_MAX;

        // --- Start gate: skip a pass we estimate won't fit (commit pass k-1) ---
        if (gate && sub_depth > 0)
        {
            double ratio    = (have_prev2 && c_prev2 > 0)
                            ? static_cast<double>(c_prev) / static_cast<double>(c_prev2)
                            : kDefaultGrowth;
            double estimate = static_cast<double>(c_prev) * ratio;
            if (estimate > kStartGateAlpha * static_cast<double>(remaining_at_start))
            {
                break;
            }
        }

        long long used_before     = budget ? budget->Used() : 0;
        Plan      pass_best        = candidates.front();
        int       pass_best_win    = max_turns + 1;
        bool      pass_has_best    = false;
        bool      pass_aborted     = false;
        long long candidates_done  = 0;

        // Trace T1 top-level decisions (enforce_budget=true) AND T2 rollout decisions
        // where depth==3 (fired from the sub_depth=3 pass inside SimulateToEnd).
        const bool trace_t1 = s_trace_solve && enforce_budget
                              && state.turn_number == 1 && is_pre_combat;
        const bool trace_t2 = s_trace_solve && !enforce_budget
                              && state.turn_number == 2 && depth == 3 && is_pre_combat;
        const bool trace_this = trace_t1 || trace_t2;
        if (trace_this)
        {
            std::cerr << "[trace] T" << state.turn_number
                      << (enforce_budget ? " top-level" : " rollout")
                      << " sub_depth=" << sub_depth
                      << "  candidates=" << candidates.size() << "\n";
        }

        // Cost-reframe count-bounder (dominance / resulting-state dedup): the over-optimistic relaxation
        // offers many INFEASIBLE interacting candidates; applied, their unpayable casts STRAND, collapsing
        // them to the SAME resulting state as a smaller feasible candidate. Dedup by post-apply state
        // (BuildSimKey) so each distinct outcome is rolled out ONCE -- the flood no longer dilutes the fixed
        // node budget. Reuses the apply already done below (no extra apply). Off (default) -> set never
        // consulted -> byte-identical. Per sub_depth pass. See docs/design/enumeration-feasibility-via-executor.md.
        std::unordered_set<TranspositionTable::Key, TranspositionTable::KeyHash> reframe_seen;
        // Searched-breakpoint variant dedup -- see the identical guard in FSLineWin. Records every
        // candidate's post-apply state but only SKIPS a bp_choice variant, so runs without variants
        // (and MTG_BP_SEARCH=0) never enter it and stay byte-identical.
        bool bp_variants_here = false;
        for (const Plan& p : candidates) { if (p.bp_choice >= 0) { bp_variants_here = true; break; } }
        std::unordered_set<TranspositionTable::Key, TranspositionTable::KeyHash> bp_seen_states;
        for (const Plan& plan : candidates)
        {
            // --- Overrun guard: finish if almost done, else abort + roll back ---
            if (gate && budget->Exhausted() && candidates_done > 0)
            {
                ++g_fs_trunc_events;
                long long pass_used       = budget->Used() - used_before;
                double    avg_per_cand    = static_cast<double>(pass_used)
                                          / static_cast<double>(candidates_done);
                long long remaining_cands = static_cast<long long>(candidates.size())
                                          - candidates_done;
                double    projected       = avg_per_cand
                                          * static_cast<double>(remaining_cands);
                bool      way_over         = static_cast<double>(pass_used)
                                          > kOverrunBeta * static_cast<double>(remaining_at_start);
                bool      finish_expensive = projected
                                          > static_cast<double>(remaining_at_start);
                if (way_over && finish_expensive)
                {
                    pass_aborted = true;
                    break;
                }
            }

            // One work unit for this candidate's inline first turn (combat + post
            // main); the remaining turns are counted inside SimulateToEnd.
            if (budget) { budget->Consume(1); }

            PROF_INC(gamestate_copies);
            GameState copy = state;
            if (is_pre_combat)
            {
                ApplyPlanDirect(copy, plan, true);
                // Count-bounder: skip this candidate's rollout if its post-apply state was already scored
                // by an earlier candidate this pass (a dominated/strand-equivalent line). Reframe-only.
                if (CostReframeEnabled() && !reframe_seen.insert(BuildSimKey(copy, 0, 0, false)).second) { continue; }
                if (bp_variants_here
                    && !bp_seen_states.insert(BuildSimKey(copy, 0, 0, false)).second
                    && plan.bp_choice >= 0)
                { ++candidates_done; continue; }
                AnimateLandsShared(copy, nullptr);
                ActivateTapTokensShared(copy, nullptr);

                // Combat this turn
                SimulateCombat(copy);
                if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }

                // Post-combat (second) main if this deck wants one (greedy here).
                if (second_main)
                {
                    Plan post = Solve(copy, false);
                    ApplyPlanDirect(copy, post, false);
                    if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }
                }
            }
            else
            {
                // Top-level post-combat (second) main decision: combat already
                // happened this turn, so apply the candidate as a post-combat play
                // and DON'T re-simulate combat (that would be a phantom second one).
                ApplyPlanDirect(copy, plan, false);
                if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return plan; }
                // Count-bounder (post-combat main): dedup by post-apply state AFTER the win check so a
                // unique winner is never skipped. Reframe-only. See the pre-combat branch above.
                if (CostReframeEnabled() && !reframe_seen.insert(BuildSimKey(copy, 0, 0, false)).second) { continue; }
                if (bp_variants_here
                    && !bp_seen_states.insert(BuildSimKey(copy, 0, 0, false)).second
                    && plan.bp_choice >= 0)
                { ++candidates_done; continue; }
            }

            // End of this turn + start of next. The next turn's land drop is searched
            // inside SimulateToEnd's per-turn SolveWithLookahead, so no greedy land
            // play happens here.
            if (!SimulateEndAndStartNextTurn(copy)) { ++candidates_done; continue; }

            // Simulate remaining turns at this pass's sub_depth. The rollout runs to
            // completion (enforce_budget=false inside), only consuming budget; the
            // within-pass running best (pass_best_win) is the branch-and-bound cutoff.
            int win_turn = SimulateToEnd(std::move(copy), sub_depth, max_turns, budget,
                                         pass_best_win, second_main, tt);
            if (trace_this)
            {
                std::cerr << "  " << PlanDesc(plan)
                          << "  val=" << plan.value
                          << "  win=" << win_turn << "\n";
            }
            bool better = !pass_has_best
                       || win_turn < pass_best_win
                       || (win_turn == pass_best_win && plan.value > pass_best.value);
            if (better)
            {
                pass_best_win = win_turn;
                pass_best     = plan;
                pass_has_best = true;
            }
            ++candidates_done;
        }

        // ---- DEFERRED CONTINUATION WAVES (see BpWaveWalker) --------------------------------------
        // The second fan-out site. This loop decides EVERY ROLLOUT TURN (SimulateToEndImpl calls
        // SolveWithLookahead per turn), and the doc's measurement is that the searched-breakpoint
        // gain comes from the rollouts, not the root -- the greedy continuation's real damage is to
        // the LEAF EVALUATOR. So the rank ceiling has to lift here too, or an unbounded search still
        // cannot reach a continuation ranked >= W while scoring a line. Same contract as FSLineWin's:
        // strictly-better win turn only, so an aborted wave leaves the pass exactly as it was.
        // An ABORTED pass is discarded wholesale below, so skip the waves for it.
        if (!pass_aborted && BpWavesHere(budget))
        {
            // A pass that was not aborted scanned every candidate, so no limit applies here.
            BpWaveWalker walker(state, candidates, candidates.size());
            if (walker.Empty())
            {
                if (BpWaveProbeOn()) { g_bp_wave_probe.no_slots.fetch_add(1); }
            }
            else
            {
                if (BpWaveProbeOn())
                {
                    g_bp_wave_probe.nodes.fetch_add(1);
                    g_bp_wave_probe.slots.fetch_add(walker.SlotCount());
                }
                Plan v;
                while (walker.Next(candidates, v))
                {
                    if (budget != nullptr && !budget->Unlimited() && budget->Exhausted())
                    { ++g_fs_trunc_events;
                      if (BpWaveProbeOn()) { g_bp_wave_probe.stopped.fetch_add(1); } break; }
                    if (BpWaveProbeOn())
                    { g_bp_wave_probe.scored.fetch_add(1); BpWaveRank(walker.LastRank()); }

                    if (budget) { budget->Consume(1); }
                    PROF_INC(gamestate_copies);
                    GameState copy = state;
                    g_bp_cands_last = 0;
                    g_bp_seen_last  = 0;
                    if (is_pre_combat)
                    {
                        ApplyPlanDirect(copy, v, true);
                        if (walker.Report(candidates, g_bp_cands_last, g_bp_seen_last)) { continue; }
                        if (!bp_seen_states.insert(BuildSimKey(copy, 0, 0, false)).second) { continue; }
                        if (BpWaveProbeOn()) { g_bp_wave_probe.rolled.fetch_add(1); }
                        AnimateLandsShared(copy, nullptr);
                        ActivateTapTokensShared(copy, nullptr);
                        SimulateCombat(copy);
                        if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return v; }
                        if (second_main)
                        {
                            Plan post = Solve(copy, false);
                            ApplyPlanDirect(copy, post, false);
                            if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return v; }
                        }
                    }
                    else
                    {
                        ApplyPlanDirect(copy, v, false);
                        if (walker.Report(candidates, g_bp_cands_last, g_bp_seen_last)) { continue; }
                        if (copy.Opponent().life <= 0) { report(state.turn_number, depth - 1); return v; }
                        if (!bp_seen_states.insert(BuildSimKey(copy, 0, 0, false)).second) { continue; }
                        if (BpWaveProbeOn()) { g_bp_wave_probe.rolled.fetch_add(1); }
                    }
                    if (!SimulateEndAndStartNextTurn(copy)) { continue; }
                    const int win_turn = SimulateToEnd(std::move(copy), sub_depth, max_turns, budget,
                                                       pass_best_win, second_main, tt);
                    // STRICTLY better only. Wave 0's variants share their base plan's `value`, so the
                    // equal-turn value tiebreak the main loop uses would let a deferred rank displace
                    // an equally-good searched line for no measured reason; requiring a strict
                    // improvement is what makes stopping mid-wave safe.
                    if (pass_has_best && win_turn >= pass_best_win) { continue; }
                    if (BpWaveProbeOn()) { g_bp_wave_probe.improved.fetch_add(1); }
                    pass_best_win = win_turn;
                    pass_best     = v;
                    pass_has_best = true;
                }
            }
        }

        if (pass_aborted)
        {
            if (trace_this)
            {
                std::cerr << "  [T" << state.turn_number << " ABORTED — keeping pass "
                          << (sub_depth-1) << " result]\n";
            }
            break;
        }

        if (pass_has_best)
        {
            best_plan           = pass_best;
            committed_win       = pass_best_win;
            committed_sub_depth = sub_depth;
            if (trace_this)
            {
                std::cerr << "  -> T" << state.turn_number << " COMMITTED sub_depth=" << sub_depth
                          << ": " << PlanDesc(pass_best)
                          << "  win=" << pass_best_win << "\n";
            }
        }

        // Record this completed pass's cost for the next pass's estimate.
        if (budget)
        {
            c_prev2    = c_prev;
            have_prev2 = have_prev;
            c_prev     = budget->Used() - used_before;
            have_prev  = true;
        }
    }

    report(committed_win, committed_sub_depth);
    return best_plan;
}

// --- External-controller hooks (Claude-play / human-play prototype) -------------
// Thin public wrappers around the file-static enumeration + application the solver
// uses internally, so an external decision provider gets the exact same legal plans
// and identical execution. See TurnSolver.h.
std::vector<TurnSolver::Plan> TurnSolver::EnumerateMainPlans(const GameState& state,
                                                             bool is_pre_combat)
{
    return EnumeratePlansWithLand(state, is_pre_combat);
}

// MID-TURN-exact state key for the breakpoint enumeration memo. BuildSimKey alone is NOT sufficient:
// it is a TURN-BOUNDARY key, and GameState documents that floating_mana / spells_cast_this_turn are
// deliberately never folded into it because they are 0 at every boundary. Mid-turn they are exactly
// what distinguishes two breakpoint states -- floating mana decides what is affordable (Hinata's
// Reality Spasm float) and spells_cast_this_turn decides Dragonstorm's storm count. Keying the memo
// on BuildSimKey alone silently changed Hinata's play; folding these three makes it exact.
static TranspositionTable::Key BuildBreakpointKey(const GameState& state, bool is_pre_combat)
{
    TranspositionTable::Key k = BuildSimKey(state, 0, 0, is_pre_combat);
    Fold(k, 0xB9E4);   // section tag: mid-turn scalars
    const ManaPool& f = state.floating_mana;
    Fold(k, static_cast<uint64_t>(f.white));
    Fold(k, static_cast<uint64_t>(f.blue));
    Fold(k, static_cast<uint64_t>(f.black));
    Fold(k, static_cast<uint64_t>(f.red));
    Fold(k, static_cast<uint64_t>(f.green));
    Fold(k, static_cast<uint64_t>(f.colorless));
    Fold(k, static_cast<uint64_t>(f.wild));
    Fold(k, static_cast<uint64_t>(state.spells_cast_this_turn));
    Fold(k, static_cast<uint64_t>(state.casts_remaining_this_turn + 1));   // -1 == unset
    return k;
}

// Probe for the TH depth-curve regression (docs/design/th-d5-five-hour-game.md): is the enum memo
// below THRASHING (its 8192 cap is cleared wholesale on overflow), and how many DISTINCT breakpoint
// states does a game really visit? MTG_BP_ENUM_PROBE=1 prints the tallies at exit; the counters are
// only bumped under the flag, so unset is byte-identical.
namespace
{
    struct BpEnumProbe
    {
        std::atomic<uint64_t> hits{0};      // memo returns
        std::atomic<uint64_t> misses{0};    // full EnumeratePlansWithLand derivations
        std::atomic<uint64_t> clears{0};    // wholesale cache clears (the suspected thrash)
        ~BpEnumProbe()
        {
            if (!EnvOn("MTG_BP_ENUM_PROBE")) { return; }
            std::fprintf(stderr, "[bp-enum] hits=%llu misses=%llu clears=%llu\n",
                         static_cast<unsigned long long>(hits.load()),
                         static_cast<unsigned long long>(misses.load()),
                         static_cast<unsigned long long>(clears.load()));
        }
    };
    BpEnumProbe g_bp_enum_probe;
    inline bool BpEnumProbeOn()
    {
        static const bool on = EnvOn("MTG_BP_ENUM_PROBE");
        return on;
    }
}

std::vector<TurnSolver::Plan> TurnSolver::EnumerateBreakpointPlans(const GameState& state,
                                                                   bool is_pre_combat)
{
    // MEMO. The W variants of one base plan are emitted consecutively and every one reaches the SAME
    // breakpoint state, so without a cache each pays a full EnumeratePlansWithLand -- and that
    // enumeration is NOT a search node, so it never appears in interior_nodes (Dragonstorm at the
    // overnight budget: +5% nodes but multiples of wall clock). Keyed on BuildBreakpointKey, which is
    // mid-turn exact; the first attempt keyed on BuildSimKey alone silently changed Hinata's play.
    // Capped so a long game cannot grow it without bound, and thread_local because the batch runner
    // plays games concurrently. MTG_NO_BP_ENUM_CACHE disables it -- results must be identical either
    // way, and the smoke digests are the check. See docs/design/post-breakpoint-search.md.
    // MTG_BP_ENUM_CACHE_CAP overrides the cap (value-carrying; A/B probe for the memo-thrash
    // question above -- results must be identical at ANY cap, only the wall clock may move).
    static const bool s_cache = !EnvOn("MTG_NO_BP_ENUM_CACHE");
    using BpEnumMap = std::unordered_map<TranspositionTable::Key, std::vector<Plan>,
                                         TranspositionTable::KeyHash>;
    static thread_local BpEnumMap cache;
    static const std::size_t s_bp_enum_cap =
        static_cast<std::size_t>(std::max(1, EnvInt("MTG_BP_ENUM_CACHE_CAP", 8192)));

    TranspositionTable::Key key;
    if (s_cache)
    {
        key = BuildBreakpointKey(state, is_pre_combat);
        BpEnumMap::const_iterator it = cache.find(key);
        if (it != cache.end())
        {
            if (BpEnumProbeOn()) { g_bp_enum_probe.hits.fetch_add(1, std::memory_order_relaxed); }
            return it->second;
        }
    }

    ++g_bp_enum_depth;   // suppress the fan-out: this IS the continuation list, not a new decision
    std::vector<Plan> plans = EnumeratePlansWithLand(state, is_pre_combat);
    --g_bp_enum_depth;

    if (s_cache)
    {
        if (BpEnumProbeOn()) { g_bp_enum_probe.misses.fetch_add(1, std::memory_order_relaxed); }
        if (cache.size() >= s_bp_enum_cap)
        {
            if (BpEnumProbeOn()) { g_bp_enum_probe.clears.fetch_add(1, std::memory_order_relaxed); }
            cache.clear();
        }
        cache.emplace(key, plans);
    }
    return plans;
}

// One-line "land=...; cast: a, b" summary of a plan (for the human-play accept verdict).
static std::string LineSummaryOfPlan(const TurnSolver::Plan& p)
{
    std::string s;
    if (p.land_decided && !p.land_to_play.empty()) { s += "land=" + p.land_to_play + "; "; }
    int le_count = 0;
    std::vector<std::string> cast_names;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::DiscardToLandsEdge) { le_count += a.discard_lands; }
        else if (a.kind == Action::Kind::DigDraw)
        { cast_names.push_back((a.dig_sacrifice ? "sacrifice " : "cycle ") + a.card_name); }
        else { cast_names.push_back(a.card_name); }
    }
    s += "cast: ";
    if (cast_names.empty()) { s += "(nothing)"; }
    else
    {
        for (size_t i = 0; i < cast_names.size(); ++i) { if (i) s += ", "; s += cast_names[i]; }
    }
    if (le_count > 0) { s += "; Land's Edge x" + std::to_string(le_count); }
    return s;
}

// Deduct a KNOWN-PAYABLE cost from an accounting pool (caller checks CanPay first).
// Mirrors ManaPool::CanPay's allocation: colour pips from their own colour then wild;
// generic from leftover specific mana (any colour / {C}) then wild.
static void DeductPayable(ManaPool& p, const ManaCost& cost)
{
    auto pay = [&](int need, int& specific)
    {
        int u = std::min(need, specific); specific -= u; need -= u;
        int w = std::min(need, p.wild);   p.wild   -= w; need -= w;
    };
    pay(cost.white, p.white); pay(cost.blue, p.blue); pay(cost.black, p.black);
    pay(cost.red,   p.red);   pay(cost.green, p.green); pay(cost.colorless, p.colorless);
    int g = cost.generic;
    for (int* src : { &p.colorless, &p.white, &p.blue, &p.black, &p.red, &p.green })
    { int u = std::min(g, *src); *src -= u; g -= u; }
    int w = std::min(g, p.wild); p.wild -= w; g -= w;
}

// Restricted-color line gate (viewer line-check only). Reuses the enumerator's conservative
// ComputeAvailableColors necessary-condition so CheckLine grades a genuinely-unpayable COLORED line
// (e.g. Marshal of Zhalfir {W}{U} off W/R/B Tournament Grounds with no blue source) as Illegal rather
// than LegalNotEnumerated -- the flat AvailableManaPool/CanPay stores every dual as one any-color "wild".
// Default ON; MTG_LINE_COLOR_GATE=0 restores the pre-gate wild behavior. CheckLine is viewer-only, so
// this never affects the search / executor / ground truth.
static bool LineColorGateEnabled()
{
    static const bool v = []{ const char* e = std::getenv("MTG_LINE_COLOR_GATE");
                              return !(e && std::string(e) == "0"); }();
    return v;
}

TurnSolver::LineCheck TurnSolver::CheckLine(const GameState& state, bool is_pre_combat,
                                            const LineSpec& spec)
{
    using V = LineCheck::Verdict;
    LineCheck out;

    // --- 0) Pass / cast-nothing maps to the engine's "idx < 0" pass ------------
    // A line that only activates Land's Edge / deploys via Vial / retraces (no land, no hand
    // casts) is NOT a pass -- those are real actions committed via their own verbs.
    if (spec.pass || (!spec.has_land && spec.casts.empty() && spec.lands_edge == 0 &&
                      spec.vial_deploys.empty() && spec.retrace_casts.empty()))
    {
        out.verdict = V::Accept; out.plan_index = -1;
        out.matched_summary = "pass / cast nothing";
        return out;
    }

    const std::string wantLand = spec.has_land ? spec.land : std::string();
    std::vector<std::string> sortedCasts = spec.casts;
    std::sort(sortedCasts.begin(), sortedCasts.end());
    std::vector<std::string> sortedVial = spec.vial_deploys;
    std::sort(sortedVial.begin(), sortedVial.end());
    std::vector<std::string> sortedRetrace = spec.retrace_casts;
    std::sort(sortedRetrace.begin(), sortedRetrace.end());

    // --- 1) Does the line match a plan (or several variants) the model would play? ----
    // A "match" is same land + same multiset of cast card names. Several enumerated plans can
    // match while differing in a per-spell sub-decision (tutor target / X / Ponder keep /
    // Soulfire count) -- in human-play mode (MTG_UNPRUNED) the search enumerates them all, so we
    // surface the distinct ones for the human to pick among. Pure cast-ORDER duplicates (same
    // sub-decisions, different order) collapse to one representative by their param signature.
    // (Crackle's full target-count range is gated on HumanPlayActive() in CollectActions, which is
    // true here AND in the apply re-run, so the count variants' plan indices stay consistent.)
    std::vector<Plan> plans = EnumerateMainPlans(state, is_pre_combat);

    // Collect every plan matching land + cast-name MULTISET, recording each plan's cast order and
    // a sub-decision signature/label (tutor target / X / Ponder keep / Soulfire count / fetch
    // target). The land + the plan index come along so we can both honour the player's ORDER and
    // surface genuine sub-decision choices.
    struct Cand { int idx; std::vector<std::string> order; std::string sig, label;
                  std::vector<std::string> cards; std::vector<SubChoice> subs; int sacs; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < plans.size(); ++i)
    {
        const Plan& p = plans[i];
        const std::string planLand = p.land_decided ? p.land_to_play : std::string();
        if (planLand != wantLand) { continue; }
        // Actions split by how the human commits them, each matched against its own verb:
        //   DiscardToLandsEdge -> a COUNT vs spec.lands_edge (card_name is "Land's Edge")
        //   ActivateVial       -> creature names vs spec.vial_deploys (free Vial deploy)
        //   CastFromGraveyard  -> spell names vs spec.retrace_casts (retrace)
        //   everything else    -> the plain hand-cast multiset vs spec.casts (order honoured)
        //   SacForMana         -> an IMPLICIT one-shot mana source (Lotus Bloom): tap+sacrifice for a
        //                         float, exactly like auto-tapping a land. The human declares the SPELLS
        //                         they want to cast; the engine sacs Lotus to help pay when (and only
        //                         when) the line needs the mana -- so it is NOT part of the declared cast
        //                         multiset. planSacs counts them so a line affordable WITHOUT saccing
        //                         prefers the no-sac plan (saving the one-shot source; see the pool sort).
        int planLE = 0, planSacs = 0;
        std::vector<std::string> orderNames, vialNames, retraceNames;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::DiscardToLandsEdge) { planLE += a.discard_lands; continue; }
            if (a.kind == Action::Kind::ActivateVial)       { vialNames.push_back(a.card_name); continue; }
            if (a.kind == Action::Kind::CastFromGraveyard)  { retraceNames.push_back(a.card_name); continue; }
            if (a.kind == Action::Kind::SacForMana)         { ++planSacs; continue; }
            orderNames.push_back(a.card_name);
        }
        if (planLE != spec.lands_edge) { continue; }
        std::vector<std::string> sortedNames = orderNames;
        std::sort(sortedNames.begin(), sortedNames.end());
        if (sortedNames != sortedCasts) { continue; }
        std::vector<std::string> sortedVialNames = vialNames;
        std::sort(sortedVialNames.begin(), sortedVialNames.end());
        if (sortedVialNames != sortedVial) { continue; }
        std::vector<std::string> sortedRetraceNames = retraceNames;
        std::sort(sortedRetraceNames.begin(), sortedRetraceNames.end());
        if (sortedRetraceNames != sortedRetrace) { continue; }

        // Per-decision tokens, order-INDEPENDENT (sorted) so plans differing only in cast order
        // share a signature; a real sub-decision difference (target / X / keep / fetch) splits it.
        // Build the sub-decision dimensions structurally (key = dimension the GUI groups by,
        // choice = this plan's value, card = art). `toks` (the sorted `key + choice` strings) is
        // derived from the same subs so the dedup signature/label stays byte-identical to before.
        std::vector<std::string> toks, artCards;
        std::vector<SubChoice> subs;
        // `tok` is the exact sig/label token (unchanged from before); key/choice/card are the
        // structured breakdown the GUI groups by. They are kept separate because the token spacing
        // (e.g. "X=2", "+2 own") is not a plain "key SPACE choice", and sig/label must stay identical.
        auto addSub = [&](const std::string& tok, const std::string& key, const std::string& choice,
                          const std::string& card, const std::string& kind) {
            subs.push_back({ key, choice, card, kind });
            toks.push_back(tok);
            artCards.push_back(card);
        };
        for (const Action& a : p.actions)
        {
            if (!a.tutor_target.empty())   { addSub(a.card_name + " \xE2\x86\x92 " + a.tutor_target, a.card_name + " \xE2\x86\x92", a.tutor_target, a.tutor_target, "tutor"); }
            // Aura enchant TARGET: which creature this Aura attaches to. Emit a sub per legal target so the
            // viewer surfaces a "choose creature" art-grid (mirrors tutor_target) instead of silently taking
            // the heuristic's first-enumerated pick -- the human IS the decision-maker here. Resolve the
            // stable m_number to the creature's name for the grid art; the target is one of the player's
            // creatures on the battlefield (LegalEnchantTargets), so this always resolves.
            if (a.enchant_target > 0)
            {
                std::string etn;
                for (const Permanent& perm : state.battlefield)
                    if (perm.controller_index == state.active_player_index && perm.card.IsCreature()
                        && perm.card.m_number == a.enchant_target)
                    { etn = perm.card.m_name.str(); break; }
                // A same-turn creature target (AppendCreatureTargetAuraCandidates) is still in hand here;
                // resolve its name from hand so the choose grid offers it (else the sub is dropped).
                if (etn.empty())
                    for (const Card& hc : state.ActivePlayer().hand)
                        if (hc.m_number == a.enchant_target) { etn = hc.m_name.str(); break; }
                if (!etn.empty())
                    addSub(a.card_name + " \xE2\x86\x92 " + etn, a.card_name + " \xE2\x86\x92", etn, etn, "enchant");
            }
            if (a.chosen_x > 0)            { addSub(a.card_name + " X=" + std::to_string(a.chosen_x), a.card_name + " X", "X=" + std::to_string(a.chosen_x), a.card_name, "x"); }
            // NOTE: a.ponder_keep is deliberately NOT a variant token. The Ponder reorder (keep-top
            // vs shuffle, and the full ordering) is re-asked at REAL resolution via the look-at-top
            // chooser (g_play_top_chooser), so pre-selecting it here just stacked a redundant "choose
            // how to resolve" dialog ahead of the actual Reorder dialog. Collapsing the keep/shuffle
            // plans to one representative makes a Ponder cast 'accept' and lets the look dialog own it.
            // Soulfire own-target COUNT is a sub-decision. Emit the structured sub for EVERY count,
            // including 0 -- otherwise the count=0 variant carries no sub and the viewer's
            // renderChooseDialog (which falls back to the flat "choose how to resolve" dialog when ANY
            // variant lacks subs) shows the old ugly picker instead of the nice per-count grid. Detect
            // the Soulfire cast by its def (damage_equals_top_mv) rather than by count>0.
            {
                const CardDefinition* adef = CardDatabase::Instance().Lookup(a.card_name);
                if (adef && adef->params.damage_equals_top_mv)
                {
                    addSub(a.card_name + " +" + std::to_string(a.soulfire_own_targets) + " own",
                           a.card_name + " own targets", "+" + std::to_string(a.soulfire_own_targets),
                           a.card_name, "soulfire");
                }
                // Crackle with Power extra-target COUNT: the declared number of targets BEYOND the
                // opponent face (each is {1} off via Hinata and takes 5X). Emit for every count >= 0
                // (the viewer enumerates 0..cap) so the GUI shows a per-count picker; -1 is the
                // autonomous legacy sentinel and carries no sub. Mirrors the Soulfire own-count sub.
                if (adef && IsCrackleCountSpell(adef->params) && a.crackle_targets >= 0)
                {
                    addSub(a.card_name + " +" + std::to_string(a.crackle_targets) + " tgt",
                           a.card_name + " extra targets", "+" + std::to_string(a.crackle_targets),
                           a.card_name, "crackle");
                }
                // Splice (Desperate Ritual, splice_onto_arcane): a base Arcane cast may splice k OTHER
                // copies onto itself (cost + red float scale by k+1; the spliced copies STAY IN HAND, so
                // they do NOT appear as separate casts in orderNames). Without a sub, the k-variants share
                // an empty signature and the dedup below collapses them to the FIRST enumerated (k=0) --
                // silently dropping the human's splice (the same failure the float_color/soulfire subs
                // fix). Emit for EVERY count >= 0 (like Soulfire/Crackle) so a multi-copy hand surfaces a
                // clean per-count picker; a single-copy hand has only k=0 -> one variant -> Accept, no dialog.
                if (adef && adef->params.splice_onto_arcane && a.splice_count >= 0)
                {
                    addSub(a.card_name + " splice+" + std::to_string(a.splice_count),
                           a.card_name + " splice", "+" + std::to_string(a.splice_count),
                           a.card_name, "splice");
                }
            }
        }
        // Fetchland target is a plan-level sub-decision (cracking a fetch chooses what to get).
        if (!p.fetch_target.empty()) { addSub(p.land_to_play + " fetches " + p.fetch_target, p.land_to_play + " fetches", p.fetch_target, p.fetch_target, "fetch"); }
        // MDFC (Pathway) face is a plan-level sub-decision: both faces carry land_to_play == the hand
        // (front) name, so they survive the land-name match above and surface here as a "which face?"
        // choose grid. `card` = the face's real name so the GUI shows the front vs back Scryfall art.
        if (!p.land_face.empty())
        {
            std::string faceName = p.land_to_play;   // "" / "front" -> the front (hand) card
            if (p.land_face == "back")
            {
                const CardDefinition* fd = CardDatabase::Instance().Lookup(p.land_to_play);
                if (fd && !fd->params.mdfc_back_name.empty()) { faceName = fd->params.mdfc_back_name; }
            }
            addSub(p.land_to_play + " face " + faceName, p.land_to_play + " face", faceName, faceName, "face");
        }
        // SIGNATURE (dedup): sort the derived tokens so plans differing only in cast ORDER share a
        // signature; a real sub-decision difference (target / X / splice / fetch) splits it. Kept
        // byte-identical to before (a sorted copy -- `toks` itself stays in cast order for the label).
        std::vector<std::string> sortedToks = toks;
        std::sort(sortedToks.begin(), sortedToks.end());
        std::string sig;
        for (const std::string& t : sortedToks) { sig += "|" + t; }
        // LABEL (displayed in the choose dialog): the FULL ordered line -- land + EVERY cast, including
        // plain casts (Rite of Flame) that carry no sub-decision and so were previously DROPPED -- then
        // the sub-decisions in CAST ORDER (unsorted `toks`), so two Desperate Rituals read
        // "splice+0; splice+1" in the order cast, not alpha-scrambled (viewer issue #8). Empty subs ->
        // just the line summary (unchanged from the old label.empty() fallback).
        std::string label = LineSummaryOfPlan(p);
        if (!toks.empty())
        {
            label += " \xE2\x80\x94 ";   // em dash separating the line from its sub-decisions
            for (size_t t = 0; t < toks.size(); ++t) { label += (t ? "; " : "") + toks[t]; }
        }
        cands.push_back({ static_cast<int>(i), orderNames, sig, label, artCards, subs, planSacs });
    }

    // Honour the player's ORDER: prefer candidates whose cast sequence equals the queued order, so
    // the executed plan (a searched_order variant) plays in that order. If none matches exactly
    // (the only enumerated plan is an end-state-equivalent ordering), fall back to all candidates
    // -- the result is identical, only the displayed order may differ.
    std::vector<const Cand*> pool;
    for (const Cand& c : cands) { if (c.order == spec.casts) { pool.push_back(&c); } }
    if (pool.empty()) { for (const Cand& c : cands) { pool.push_back(&c); } }
    // Choose the representative among cast-multiset matches by (1) PAYABLE first, then (2) FEWER
    // one-shot sacrifices (Lotus Bloom's sac-for-mana). The enumerator OVER-generates: it lists a
    // spell subset WITHOUT the mana actions (a Lotus Bloom sac -- a one-shot "depletion" source) needed
    // to pay it, so a 0-sac candidate can be UNPAYABLE -- applied literally its casts drop and stay in
    // hand (the seed-23 3-ritual + Dragonstorm combo: the no-sac plan can't pay {8}{R}, the Lotus-sac
    // plan can). Because the sac is implicit (not a declared cast) the no-sac and sac plans share a
    // signature; the OLD sacs-only sort kept the no-sac plan, so the viewer committed an unpayable line
    // and rolled it back ("not enough mana"). Now: trial-apply each candidate on a COPY (ApplyPlan ==
    // the commit path, and with default null sinks it has NO global side effects) and keep the ones
    // whose DECLARED casts actually left the hand; prefer those. The sacs tiebreak still SAVES Lotus
    // when the line pays without it (both plans payable -> fewer sacs wins), and spends it when the
    // line needs the mana to go off. CheckLine is viewer-only -> GT-neutral; byte-identical ordering
    // when every candidate is payable (payable-first is then a no-op).
    auto handCountOf = [](const GameState& gs, const std::string& nm) {
        int n = 0; for (const Card& c : gs.ActivePlayer().hand) { if (c.m_name == nm) { ++n; } } return n;
    };
    auto plan_pays = [&](int plan_idx) -> bool {
        GameState sc = state;                       // work on a copy (ApplyPlan mutates)
        {
            // Resolve any sub-decisions (Dragonstorm's Dragon put, a tutor target, ...) via AI defaults
            // rather than the live g_play_* choosers -- otherwise the trial-apply would read the choices
            // stream / EMIT a decision (the combo casts Dragonstorm mid-check, which fires the dragon-put
            // chooser and exits 70). RevealLogPause saves+nulls every g_play_* chooser AND the draw/event
            // sinks for the scope, so the trial-apply is fully side-effect-free; it restores on exit.
            RevealLogPause pause_io;
            ApplyPlan(sc, plans[plan_idx], is_pre_combat);
        }
        for (const std::string& nm : spec.casts)
        {
            int declared = 0; for (const std::string& n2 : spec.casts) { if (n2 == nm) { ++declared; } }
            if (handCountOf(state, nm) - handCountOf(sc, nm) < declared) { return false; }  // a cast dropped
        }
        return true;
    };
    std::vector<int> payableIdx;
    for (const Cand* c : pool) { if (plan_pays(c->idx)) { payableIdx.push_back(c->idx); } }
    auto isPayable = [&](int idx) {
        return std::find(payableIdx.begin(), payableIdx.end(), idx) != payableIdx.end();
    };
    std::stable_sort(pool.begin(), pool.end(),
                     [&](const Cand* a, const Cand* b) {
                         bool pa = isPayable(a->idx), pb = isPayable(b->idx);
                         if (pa != pb) { return pa; }        // payable before unpayable
                         return a->sacs < b->sacs;           // then save one-shot sources when we can
                     });

    // NOTE (viewer issue #1 payability accept-gate): a matched-but-UNPAYABLE plan being accepted lets
    // ApplyPlanDirect resolve only its affordable subset (e.g. Reverent Silence's free alt-cost destroying
    // the player's own enchantment) while dropping the rest -- the partial-state bug. The obvious gate --
    // "drop candidates plan_pays reports unpayable" -- is UNSOUND here: plan_pays trial-applies under
    // RevealLogPause (choosers NULLED), which cannot reproduce a chooser/order-dependent combo (Apex of
    // Power's add-ten-mana + exile-and-cast, storm, dragon-put), so it false-NEGATIVES real combo lines
    // and would reject payable Dragonstorm turns. plan_pays stays advisory (payable-first ORDERING only).
    // The real fix needs an ACCURATE resolution oracle (does the committed plan fully resolve?) -- built
    // in the server-truth resolution workstream; see docs/design/viewer-fixes-2026-07-27.md.
    std::vector<std::string> seenSig;
    for (const Cand* c : pool)
    {
        if (std::find(seenSig.begin(), seenSig.end(), c->sig) != seenSig.end()) { continue; }
        seenSig.push_back(c->sig);
        out.variants.push_back({ c->idx, c->label, c->cards, c->subs });
    }
    if (out.variants.size() == 1)
    {
        out.verdict = V::Accept; out.plan_index = out.variants[0].plan_index;
        out.matched_summary = LineSummaryOfPlan(plans[out.variants[0].plan_index]);
        return out;
    }
    // #7 SPLICE default: the player does NOT get a splice-count picker -- the line just splices as many
    // copies as the mana affords (greedy MAX-affordable) in the committed order. Only when MTG_SPLICE_
    // PROMPT is set does splice stay a Choose so the human dials k. Applies ONLY to a PURE splice fan-out
    // (variants differ SOLELY in the splice dimension); a line with a genuine OTHER sub-decision (tutor /
    // fetch / X / enchant target) still prompts for all of them. Max-affordable = the highest TOTAL
    // splice_count whose trial-apply pays (plan_pays); if NONE pays (a chooser/combo line the nulled-
    // chooser sim under-reports -- see the plan_pays caveat above), fall back to the highest total and let
    // the executor + server-truth dropped_casts surface any real shortfall. CheckLine is viewer-only -> GT-neutral.
    static const bool s_splice_prompt = EnvOn("MTG_SPLICE_PROMPT");
    if (out.variants.size() > 1 && !s_splice_prompt)
    {
        auto nonSpliceSig = [](const std::vector<SubChoice>& subs) -> std::string {
            std::vector<std::string> ks;
            for (const SubChoice& sc : subs) { if (sc.kind != "splice") { ks.push_back(sc.key + "\x1f" + sc.choice); } }
            std::sort(ks.begin(), ks.end());
            std::string r; for (const std::string& k : ks) { r += "\x1e" + k; } return r;
        };
        bool pure_splice = true, any_splice = false;
        std::string base_ns;
        for (size_t i = 0; i < out.variants.size(); ++i)
        {
            bool hs = false;
            for (const SubChoice& sc : out.variants[i].subs) { if (sc.kind == "splice") { hs = true; break; } }
            if (!hs) { pure_splice = false; break; }   // a variant without a splice sub -> not pure splice
            any_splice = true;
            std::string ns = nonSpliceSig(out.variants[i].subs);
            if (i == 0) { base_ns = ns; } else if (ns != base_ns) { pure_splice = false; break; }
        }
        if (any_splice && pure_splice)
        {
            auto totalSplice = [&](int plan_idx) -> int {
                int t = 0;
                for (const Action& a : plans[plan_idx].actions)
                { if (a.kind == Action::Kind::CastFromHand && a.splice_count > 0) { t += a.splice_count; } }
                return t;
            };
            int pick = -1, pick_total = -1;
            for (const LineVariant& v : out.variants)   // highest TOTAL splice among PAYABLE variants
            { if (plan_pays(v.plan_index)) { int t = totalSplice(v.plan_index); if (t > pick_total) { pick_total = t; pick = v.plan_index; } } }
            if (pick < 0)   // none reported payable (combo sim under-report) -> greedy max, trust the human
            { for (const LineVariant& v : out.variants) { int t = totalSplice(v.plan_index); if (t > pick_total) { pick_total = t; pick = v.plan_index; } } }
            out.variants.clear();
            out.verdict = V::Accept; out.plan_index = pick;
            out.matched_summary = LineSummaryOfPlan(plans[pick]);
            return out;
        }
    }
    if (out.variants.size() > 1)
    {
        out.verdict = V::Choose;
        out.reason = "this line resolves several ways -- pick the sub-decisions";
        return out;
    }

    // --- 2) Not enumerated: is it rules-legal (modelling same-turn ramp)? ------
    GameState s = state;   // work on a copy; CheckLine must not mutate the real game

    if (spec.has_land)
    {
        if (!PlayLandByName(s, spec.land))
        {
            out.verdict = V::Illegal;
            out.failed_action = "land=" + spec.land;
            out.reason = "can't play land '" + spec.land +
                         "' (land drop unavailable, or it is not a land in hand)";
            return out;
        }
    }

    // Resolve each named cast to a card definition; bail to Unsupported for the action
    // kinds this v1 check cannot honestly validate (X spells, alt-cost, tutors).
    // A creature-burn's death rider / Blaze's player hit, or any face/any burn, makes the opponent
    // lose life -> turns SPECTACLE on for a later spell in the same line (Light Up the Stage {R}).
    auto deals_opponent_damage = [](const CardDefinition& d) -> bool {
        if (d.tmpl != CardTemplate::DirectDamage) { return false; }
        if (d.params.death_trigger_damage > 0 || d.params.landfall_damage > 0) { return true; }
        return d.params.damage > 0 && d.params.targeting != Targeting::Creature;   // face / any / multi
    };
    struct PendingCast {
        std::string name; const CardDefinition* def; bool rock;
        ManaCost full_cost;               // the EFFECTIVE cost (printed cost minus any Hinata /
                                          // Medallion / affinity reduction in play), or the hard cost
                                          // of an alt-cost spell
        bool has_spectacle;   ManaCost spectacle_cost;
        bool alt_free;                    // alt cost payable (controls the subtype) -> costs no mana
    };
    std::vector<PendingCast> pending;
    for (const std::string& name : spec.casts)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        if (!def)
        {
            out.verdict = V::Illegal; out.failed_action = "cast=" + name;
            out.reason = "unknown card '" + name + "'";
            return out;
        }
        const ManaCost& mc = def->card.m_mana_cost;
        // X and tutor lines still can't be honestly modelled by this affordability check. Alt-cost
        // (Invigorate) IS handled: if we control the required subtype (a Forest), the alt cost is a
        // free payment (opponent gains life), so it costs no mana; else fall back to the hard cost.
        if (mc.has_x || def->params.tutor_to_hand || def->params.tutor_to_top)
        {
            out.verdict = V::Unsupported; out.failed_action = "cast=" + name;
            out.reason = "'" + name + "' uses an action kind (X / tutor) the v1 "
                         "line check cannot validate yet";
            return out;
        }
        const bool alt_free = def->params.alt_lifegain_cost > 0
            && ControlsSubtype(s, s.active_player_index, def->params.alt_cost_requires_subtype);
        PendingCast pc;
        pc.name = name; pc.def = def; pc.rock = def->params.mana_rock && !def->card.IsCreature();
        // Use the EFFECTIVE cost so a Hinata-discounted (or Medallion/affinity-reduced) spell
        // validates at the same reduced generic the enumerator charges -- otherwise a hand-built
        // line casting e.g. Magma Opus with Hinata in play is checked against the full {6}{U}{R}
        // and wrongly rejected as unpayable ("I needed more targets to cast", viewer issue #9).
        // EffectiveCost only touches GENERIC (colored pips preserved, so the color gate above is
        // unaffected) and is an identity when no reducer is in play -> byte-identical for every
        // deck without one. CheckLine is viewer-only (sole caller --validate-line), so GT-neutral.
        pc.full_cost = EffectiveCost(*def, s);
        pc.has_spectacle = def->params.spectacle_cost.has_value();
        pc.spectacle_cost = def->params.spectacle_cost.value_or(mc);
        pc.alt_free = alt_free;
        pending.push_back(pc);
    }

    // Each named card must actually be in hand (multiset-correct for duplicates).
    {
        std::vector<std::string> handNames;
        for (const Card& c : s.ActivePlayer().hand) { handNames.push_back(c.m_name); }
        for (const PendingCast& pc : pending)
        {
            auto it = std::find(handNames.begin(), handNames.end(), pc.name);
            if (it == handNames.end())
            {
                out.verdict = V::Illegal; out.failed_action = "cast=" + pc.name;
                out.reason = "'" + pc.name + "' is not in hand (already cast, or never there)";
                return out;
            }
            handNames.erase(it);
        }
    }

    // Credit in-play untapped SAC-FOR-MANA sources (Lotus Bloom: "{T}, Sacrifice: add three mana of
    // ANY one color"). AvailableManaPool / ComputeAvailableColors omit them -- they're modelled as a sac ACTION
    // (ritual_float credited as wild), not a standing source -- so the affordability sim below would
    // FALSE-REJECT a line they pay for: Dragonlord Kolaghan {4}{B}{R} off a Lotus Bloom's black + five
    // Mountains was reported "illegal: no source of black" (Dragonstorm s21 viewer artifact, issue #9).
    // Amount is credited as WILD (any single pip); the color gate credits all five (Lotus makes any one
    // colour). Contention (one Lotus, two colours needed) is left to the real payment, matching the
    // gate's maximally-conservative design -- and CheckLine only ever classifies here (Stage 1's
    // payability gate is the real accept), so an over-lenient legal_not_enumerated label is harmless.
    int sac_wild = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index || p.tapped) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (pd && pd->params.sac_for_mana_amount > 0) { sac_wild += pd->params.sac_for_mana_amount; }
    }

    // Restricted-color gate: reject a line that needs a colored pip NO untapped source can produce
    // (see LineColorGateEnabled). `s` already has this line's land played, so its color is credited;
    // a mana rock cast in this line also contributes its colors (the greedy below casts rocks first).
    // Amount/contention is left to the greedy CanPay + the executor -- this only prunes the "no source
    // of that color at all" phantom the flat wild pool otherwise lets through (viewer issue #6).
    if (LineColorGateEnabled() && !pending.empty())
    {
        bool have[5];
        ComputeAvailableColors(s, have);
        if (sac_wild > 0) { have[0] = have[1] = have[2] = have[3] = have[4] = true; }  // Lotus Bloom: any one colour
        for (const PendingCast& pc : pending)
        {
            if (!pc.rock || !pc.def) { continue; }
            for (Color c : EffectiveProduces(s, s.active_player_index, *pc.def))
            {
                switch (c)
                {
                    case Color::White: have[0] = true; break;
                    case Color::Blue:  have[1] = true; break;
                    case Color::Black: have[2] = true; break;
                    case Color::Red:   have[3] = true; break;
                    case Color::Green: have[4] = true; break;
                    default: break;
                }
            }
        }
        static const char* kColorName[5] = { "white", "blue", "black", "red", "green" };
        for (const PendingCast& pc : pending)
        {
            if (pc.alt_free) { continue; }   // alt cost pays no mana -> no colored requirement
            const ManaCost& c = pc.full_cost;
            const bool needs[5] = { c.white > 0, c.blue > 0, c.black > 0, c.red > 0, c.green > 0 };
            for (int i = 0; i < 5; ++i)
            {
                if (needs[i] && !have[i])
                {
                    out.verdict = V::Illegal; out.failed_action = "cast=" + pc.name;
                    out.reason = "can't pay for '" + pc.name + "': no untapped source produces " +
                                 kColorName[i] + " mana (a multi-color land makes only its own colors)";
                    return out;
                }
            }
        }
    }

    // Greedy affordability fixpoint: repeatedly cast any affordable not-yet-cast spell,
    // mana producers FIRST so a freshly-cast rock's mana is online for the rest of the
    // line (the same-turn ramp the enumerator's AvailableManaPool does not credit). Order-
    // independent, so it doesn't penalise the human's click order.
    ManaPool avail = AvailableManaPool(s);
    avail.wild += sac_wild;                                // Lotus Bloom & co. (see sac_wild above)
    bool spectacle_on = s.opponent_lost_life_this_turn;   // set as damage spells cast in this line
    // The mana cost to pay for pending[k] RIGHT NOW: free for an available alt cost; the spectacle
    // cost once a same-line burn has turned spectacle on; else the printed cost.
    auto cur_cost = [&](const PendingCast& pc) -> ManaCost {
        if (pc.alt_free)                     { return ManaCost{}; }
        if (pc.has_spectacle && spectacle_on) { return pc.spectacle_cost; }
        return pc.full_cost;
    };
    // Add `amt` mana of colour `col` (one-letter W/U/B/R/G/C, empty = wild) to a working pool --
    // mirrors AddChosenColorFloat but on a local ManaPool; credits a resolved ritual's float.
    auto addColorToPool = [](ManaPool& p, const std::string& col, int amt) {
        if (amt <= 0) { return; }
        if      (col == "W") { p.white     += amt; }
        else if (col == "U") { p.blue      += amt; }
        else if (col == "B") { p.black     += amt; }
        else if (col == "R") { p.red       += amt; }
        else if (col == "G") { p.green     += amt; }
        else if (col == "C") { p.colorless += amt; }
        else                 { p.wild      += amt; }   // empty / unknown -> wild
    };
    // A cast is a mana PRODUCER if it's a rock OR a mana ritual (Seething Song / Pyretic Ritual /
    // Rite of Flame / Irencrag Feat). Producers are cast FIRST (phase 0) and their output credited so
    // a ritual-ramp line into an expensive spell (Dragonstorm) is not wrongly rejected as unpayable --
    // the rituals ADD mana (viewer issue #8). IsManaRitual is the same param test the enumerator /
    // executor use (ApplyRitualFloat on resolution), so the credit matches the real float.
    auto is_producer = [](const PendingCast& pc) -> bool {
        return pc.rock || (pc.def && IsManaRitual(*pc.def));
    };

    // --- Declared-order affordability: honor the human's EXACT cast order first -----------------
    // The producers-first greedy below is order-INDEPENDENT (rituals cast before everything), which
    // is right for a mis-clicked order but WRONG for a same-turn cost reducer: a Ruby Medallion cast
    // early in the line makes the later red spells cheaper, but the greedy casts the rituals BEFORE
    // the Medallion and loses the discount -- wrongly rejecting a payable line (viewer artifact: a
    // Ruby-Medallion + ritual chain into Apex of Power, "can't pay {7}{R}{R}{R}"). The user orders
    // correctly (a ritual for the Medallion -> Medallion -> discounted rituals -> payoff), so an
    // IN-ORDER walk that applies same-turn reduces_spell_color discounts positionally pays for it.
    // If the declared order does NOT pay, fall through to the greedy (order-tolerant). CheckLine is
    // viewer-only (sole caller --validate-line), so this is GT-neutral and only ever ACCEPTS more.
    {
        ManaPool p = AvailableManaPool(s);
        p.wild += sac_wild;                  // Lotus Bloom & co. (see sac_wild above)
        bool spec = s.opponent_lost_life_this_turn;
        std::vector<std::string> reducers;   // reduces_spell_color of Medallions cast so far in-line
        std::vector<std::string> sub_reducers; // reduces_spell_subtype of Warchiefs cast so far in-line
        bool ok = true;
        for (const PendingCast& pc : pending)
        {
            ManaCost cost = pc.alt_free ? ManaCost{}
                          : (pc.has_spectacle && spec) ? pc.spectacle_cost
                          : pc.full_cost;
            if (!cost.has_x && pc.def)   // same-turn Ruby-Medallion discount (generic -1 per matching reducer)
            {
                const ManaCost& mc = pc.def->card.m_mana_cost;   // printed pips
                int disc = 0;
                for (const std::string& rc : reducers)
                {
                    const bool matches =
                          (rc == "W" && mc.white > 0) || (rc == "U" && mc.blue  > 0)
                        || (rc == "B" && mc.black > 0) || (rc == "R" && mc.red   > 0)
                        || (rc == "G" && mc.green > 0);
                    if (matches) { ++disc; }
                }
                // Goblin Warchief same-turn subtype discount (generic -1 per matching Warchief in-line).
                for (const std::string& rs : sub_reducers)
                {
                    for (const std::string& cs : pc.def->card.m_subtypes)
                    {
                        if (cs == rs) { ++disc; break; }
                    }
                }
                cost.generic = std::max(0, cost.generic - disc);
            }
            if (!p.CanPay(cost)) { ok = false; break; }
            DeductPayable(p, cost);
            if (pc.rock && pc.def) { AddSourceToPool(p, s, *pc.def); }
            else if (pc.def && IsManaRitual(*pc.def))
            {
                addColorToPool(p, pc.def->params.ritual_float_color,
                               RitualFloatAmount(s, *pc.def, /*chosen_x=*/0));
            }
            if (pc.def && !pc.def->params.reduces_spell_color.empty())
            {
                reducers.push_back(pc.def->params.reduces_spell_color);
            }
            if (pc.def && !pc.def->params.reduces_spell_subtype.empty())
            {
                sub_reducers.push_back(pc.def->params.reduces_spell_subtype);
            }
            if (pc.def && deals_opponent_damage(*pc.def)) { spec = true; }
        }
        if (ok)
        {
            out.verdict = V::LegalNotEnumerated;
            out.reason  = "rules-legal in your cast order (a same-turn cost reducer makes it "
                          "payable), but the search never enumerated this line";
            return out;
        }
    }

    std::vector<bool> done(pending.size(), false);
    size_t remaining = pending.size();
    bool progress = true;
    while (remaining > 0 && progress)
    {
        progress = false;
        for (int phase = 0; phase < 2 && !progress; ++phase)
        {
            const bool want_producer = (phase == 0);
            for (size_t k = 0; k < pending.size(); ++k)
            {
                if (done[k] || is_producer(pending[k]) != want_producer) { continue; }
                const ManaCost cost = cur_cost(pending[k]);
                if (!avail.CanPay(cost)) { continue; }
                DeductPayable(avail, cost);
                if (pending[k].rock) { AddSourceToPool(avail, s, *pending[k].def); }
                else if (IsManaRitual(*pending[k].def))    // ritual floats mana ON resolution (issue #8)
                {
                    addColorToPool(avail, pending[k].def->params.ritual_float_color,
                                   RitualFloatAmount(s, *pending[k].def, /*chosen_x=*/0));
                }
                if (deals_opponent_damage(*pending[k].def)) { spectacle_on = true; }  // enable later spectacle
                done[k] = true; --remaining; progress = true; break;
            }
        }
    }

    // Filter / ramp-filter lands (Ferrous Lake fed by floating: {1},{T}: Add {U}{R}) are not
    // expressible in the flat AvailableManaPool above, so a legal filter-fed line can leave casts "undone".
    // Before rejecting, retry the WHOLE set with the real payment engine on a copy (mirrors the
    // enumerator's SubsetPayableWithFilters): tap actual sources per cast, persisting taps and freshly-
    // cast rocks across the set, honouring the same rock-first + spectacle ordering. Only runs when the
    // flat check failed AND such a land is present -> filter-less lines are byte-identical.
    if (remaining > 0 && AnyUntappedFilterSource(s))
    {
        GameState cp = s;
        std::vector<bool> paid(pending.size(), false);
        bool spec = s.opponent_lost_life_this_turn;
        size_t left = pending.size();
        bool prog = true;
        while (left > 0 && prog)
        {
            prog = false;
            for (int phase = 0; phase < 2 && !prog; ++phase)
            {
                const bool want_rock = (phase == 0);
                for (size_t k = 0; k < pending.size(); ++k)
                {
                    if (paid[k] || pending[k].rock != want_rock) { continue; }
                    const ManaCost cost = pending[k].alt_free ? ManaCost{}
                                        : (pending[k].has_spectacle && spec) ? pending[k].spectacle_cost
                                        : pending[k].full_cost;
                    const bool for_creature = pending[k].def && pending[k].def->card.IsCreature();
                    if (cost.ManaValue() > 0 && !TapForCostDirect(cp, cost, for_creature)) { continue; }
                    if (pending[k].rock && pending[k].def)   // freshly-cast rock funds later casts
                    {
                        Permanent perm;
                        perm.card             = pending[k].def->card;
                        perm.controller_index = cp.active_player_index;
                        perm.owner_index      = cp.active_player_index;
                        cp.battlefield.push_back(perm);
                    }
                    if (deals_opponent_damage(*pending[k].def)) { spec = true; }
                    paid[k] = true; --left; prog = true; break;
                }
            }
        }
        if (left == 0)
        {
            out.verdict = V::LegalNotEnumerated;
            out.reason  = "rules-legal (a filter-aware affordability simulation can execute it), but "
                          "the search never enumerated this line";
            return out;
        }
    }

    if (remaining == 0)
    {
        out.verdict = V::LegalNotEnumerated;
        out.reason  = "rules-legal (an affordability simulation can execute it), but the "
                      "search never enumerated this line";
        return out;
    }
    for (size_t k = 0; k < pending.size(); ++k)
    {
        if (!done[k])
        {
            out.verdict = V::Illegal; out.failed_action = "cast=" + pending[k].name;
            out.reason  = "can't pay " + cur_cost(pending[k]).ToString() + " for '" +
                          pending[k].name + "' with the mana available this phase";
            break;
        }
    }
    return out;
}

void TurnSolver::ApplyPlan(GameState& state, const Plan& plan, bool is_pre_combat)
{
    ApplyPlanDirect(state, plan, is_pre_combat);
}

// #10 cast-order: the CANONICAL execution order of a plan's non-sacrifice hand casts -- what the
// executor's clean-set branch casts (stable-sort by CastOrderRank; plan order breaks ties). The viewer
// diffs the human's queued order against this to decide whether to emit --cast-order at all (equal =>
// omit => byte-identical / references stay clean). Opaque sets (draw/stage/cascade breakpoint cards)
// keep their plan/breakpoint order in the executor, not this sort; there the diff may over-report a
// reorder, which is benign -- the engine then honours the human's exact queued order via searched_order.
std::vector<std::string> TurnSolver::CanonicalNonSacCastOrder(const GameState& state, const Plan& plan)
{
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
    {
        const Action& a = plan.actions[i];
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { order.push_back(i); }
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(state, plan.actions[x], plan.actions[y]); });
    std::vector<std::string> names;
    names.reserve(order.size());
    for (int i : order) { names.push_back(plan.actions[i].card_name); }
    return names;
}
