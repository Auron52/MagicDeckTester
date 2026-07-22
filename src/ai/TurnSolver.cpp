#include "TurnSolver.h"
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
#include <iostream>
#include <sstream>
#include <fstream>
#include <mutex>
#include <cmath>
#include <unordered_set>
#include <utility>

// When true, SolveWithLookahead prints per-pass per-candidate win turn estimates
// for top-level T1 pre-combat decisions.  Set via TurnSolver::SetTraceSolve().
static bool s_trace_solve = false;

// Diagnostic (env-gated, inert by default): MTG_TRACE_PLAYOUT_SEED/_TURN replay the
// committed plan from one real decision and trace the rollout's believed winning
// line (per-turn opponent life). The companion of the non-convergence detector
// (AIEngine MTG_FLAG_NONCONV): when the detector flags a turn whose earlier-proven
// win the later search can't reproduce, this prints exactly what line the rollout
// thought would win, so the rollout-vs-reality divergence can be pinpointed.
// g_trace_arm makes only the OUTERMOST SimulateToEndImpl print (nested rollout
// sub-searches disarm it).
static const char*     s_tp_seed_env = std::getenv("MTG_TRACE_PLAYOUT_SEED");
static const long long s_tp_seed     = s_tp_seed_env ? std::atoll(s_tp_seed_env) : -1;
static const char*     s_tp_turn_env = std::getenv("MTG_TRACE_PLAYOUT_TURN");
static const int       s_tp_turn     = s_tp_turn_env ? std::atoi(s_tp_turn_env) : -1;
static thread_local bool g_trace_arm = false;

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
static const bool             s_rollout_stats = std::getenv("MTG_ROLLOUT_STATS") != nullptr;
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
static const bool             s_esc_measure = std::getenv("MTG_ESC_MEASURE") != nullptr;
static std::atomic<long long> g_esc_ladder_units{0};
static std::atomic<long long> g_esc_single_units{0};
static std::atomic<long long> g_esc_measure_n{0};
// K-predictor telemetry (MTG_ESC_PREDICT_STATS): predictions, fallbacks (K aborted -> shallower), and the
// predicted-K vs committed-depth histograms. A high fallback rate => the estimate is poor (wasted abort work).
static const bool             s_esc_predict_stats = std::getenv("MTG_ESC_PREDICT_STATS") != nullptr;
static std::atomic<long long> g_pred_n{0};
static std::atomic<long long> g_pred_fallback{0};
static std::array<std::atomic<long long>, 16> g_pred_K{};
static std::array<std::atomic<long long>, 16> g_pred_committed{};
// LOSSLESS audit (MTG_ESC_PREDICT_AUDIT): per escalation, shadow-run the ladder and compare its committed
// depth to the predictor's -- count LOSSY (predict shallower = quality risk) vs deeper vs exact, and dump the
// first few lossy cases so we can see WHY the estimate under-shot. Doubles escalation work (audit only).
static const bool             s_esc_predict_audit = std::getenv("MTG_ESC_PREDICT_AUDIT") != nullptr;
static std::atomic<long long> g_pred_audit_n{0}, g_pred_lossy{0}, g_pred_deeper{0}, g_pred_lossy_dumped{0};
// Amortized-R telemetry (MTG_ESC_PREDICT_STATS): accumulate every measured heuristic-cost-per-probe-leaf sample
// (roll/lv) so we can report the deck's converged R -- the value to FREEZE into value_play.escalation_r for the
// deterministic adopted path. Sum is scaled x1000 to keep it in a long long (R is O(100)).
static std::atomic<long long> g_esc_R_sum_milli{0};
static std::atomic<long long> g_esc_R_n{0};
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
            if (!s_esc_predict_stats) { return; }
            // R telemetry is independent of the K-predictor path (the single-depth eff_single path records R but
            // does not touch g_pred_n), so report it whenever samples exist.
            if (g_esc_R_n.load() > 0)
            {
                const double meanR = (g_esc_R_sum_milli.load() / 1000.0) / g_esc_R_n.load();
                std::cerr << "[esc-predict] converged R (heuristic cost / probe leaf): mean=" << meanR
                          << " over n=" << g_esc_R_n.load() << " samples"
                          << "  -> freeze as value_play.escalation_r\n";
            }
            if (g_pred_n.load() == 0) { return; }
            std::cerr << "[esc-predict] predictions=" << g_pred_n.load()
                      << " fallbacks=" << g_pred_fallback.load()
                      << " (" << (100.0 * g_pred_fallback.load() / g_pred_n.load()) << "% aborted K)\n";
            if (g_pred_audit_n.load() > 0)
            {
                const long long an = g_pred_audit_n.load(), lo = g_pred_lossy.load(), dp = g_pred_deeper.load();
                std::cerr << "[esc-predict] AUDIT vs ladder: n=" << an
                          << " lossy(shallower)=" << lo << " (" << (100.0 * lo / an) << "%)"
                          << " deeper=" << dp << " exact=" << (an - lo - dp) << "\n";
            }
            std::cerr << "[esc-predict] predicted-K / committed-depth:\n";
            for (int d = 0; d < 16; ++d)
            {
                long long pk = g_pred_K[d].load(), pc = g_pred_committed[d].load();
                if (pk == 0 && pc == 0) { continue; }
                std::cerr << "    d" << d << ": predictedK=" << pk << " committed=" << pc << "\n";
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
static const bool s_move_order = std::getenv("MTG_NO_MOVE_ORDER") == nullptr;

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

// ---- Local helpers -------------------------------------------------------

// Build the active player's accounting mana pool from untapped sources. Depletion
// lands contribute 2, multi-color lands 1 wild, filter lands (Cascade Bluffs) 1 wild
// when fed else 1 {C} — see AddSourceToPool, shared with AIEngine::BuildAvailableMana.
// Storage lands (Dwarven Hold / Mercadian Bazaar) yield their LIVE storage_counters via
// PermanentManaYield (0 when uncharged), exactly as BuildAvailableMana does -- passing it here
// keeps the rollout's pool byte-identical to the executor's (a dead sc=0 storage land must add
// nothing, not its static per-tap 1). For non-storage sources PermanentManaYield ==
// ManaProducedPerTap, so every non-storage deck is byte-identical. WITHOUT this the rollout's
// firebreathing pool (SimulateCombat) over-credited dead/low storage lands vs the executor's
// AIEngine::Firebreathe, projecting the combo kill a turn early (Dragonstorm fd-diverge).
static ManaPool BuildPool(const GameState& state)
{
    ManaPool pool;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap()) || def->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, state, *def, PermanentManaYield(p, *def));
    }
    // Turn-scoped reserve (ritual float + retained over-production) is spendable on later
    // same-phase casts, so it counts toward affordability. Empty for non-floating decks ->
    // byte-identical; off (MTG_NO_FLOAT_LEFTOVER) -> not added (legacy board-only pool).
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
    return pool;
}

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
        AddSourceToPool(pool, state, *def, PermanentManaYield(p, *def));
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }  // see BuildPool
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
    }
    // Floating mana (turn-scoped reserve) also satisfies colored pips: a floated {U} pays a {U}
    // pip even when no untapped land produces blue. BuildPool already credits floating into the
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
    for (int j : sel)
    {
        const CardDefinition* d = cands[j].def;
        if (!d || d->params.reduces_spell_color.empty()) { continue; }
        const std::string& rc = d->params.reduces_spell_color;
        if      (rc == "R") { ++red; }   else if (rc == "W") { ++white; }
        else if (rc == "U") { ++blue; }  else if (rc == "B") { ++black; }
        else if (rc == "G") { ++green; }
    }
    if (red + white + blue + black + green == 0) { return 0; }
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
        // A reducer never discounts itself (its own printed cost carries no matching colour pip -- a
        // Ruby Medallion is {2}, colourless), so no self-exclusion is needed here.
        credit += std::min(reducers, cands[j].cost.generic);
    }
    return credit;
}

// Forward decl of the real backtracking mana payment (defined below); used by the filter
// affordability fallback so enumeration can recognize filter/depletion/ramp-land lines.
static bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature);

// Real-payment affordability fallback for filter / ramp lands. BuildPool models a filter land
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
// conversion the flat BuildPool cannot model (so the affordability fallback above is needed).
static bool HasUntappedFilterSource(const GameState& state)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && (d->params.is_filter || d->params.ramp_filter)) { return true; }
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
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        return def.params.spectacle_cost.value();
    }
    ManaCost cost = def.card.m_mana_cost;
    // Desperate Ritual SPLICE: casting ONE base while splicing k OTHER copies pays (k+1) times the
    // printed cost. Scale the RAW cost by copies (=k+1) FIRST, so the Medallion/affinity/Hinata
    // reductions below apply ONCE to the scaled total (a single floor at 0) -- NOT (k+1) separate
    // floors (which would over-subtract the reduction). copies==1 (every non-spliced cast) is an
    // identity multiply -> byte-identical for all other decks.
    if (copies != 1)
    {
        cost.generic   *= copies;
        cost.white     *= copies;
        cost.blue      *= copies;
        cost.black     *= copies;
        cost.red       *= copies;
        cost.green     *= copies;
        cost.colorless *= copies;
    }
    if (def.params.affinity_for_subtype && !def.params.subtypes_affected.empty())
    {
        int reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            for (const std::string& sub : def.params.subtypes_affected)
            {
                bool matches = p.is_animated;
                if (!matches)
                {
                    for (const std::string& cs : p.card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++reduction; break; }
            }
        }
        cost.generic = std::max(0, cost.generic - reduction);
    }
    // Ruby Medallion-style colour cost reduction: each permanent you control whose
    // reduces_spell_color matches a colour in THIS spell's printed cost reduces its GENERIC by 1
    // (floored at 0, stacks per copy). Gated on a reducer being in play, so decks without one are
    // byte-identical. (Same-turn-cast Medallions are handled by ManaPruneBound's bail.)
    {
        int color_reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (!pd || pd->params.reduces_spell_color.empty()) { continue; }
            const std::string& rc = pd->params.reduces_spell_color;
            const ManaCost&    mc = def.card.m_mana_cost;   // printed pips (colour unchanged by discounts)
            const bool spell_has_color =
                  (rc == "W" && mc.white > 0) || (rc == "U" && mc.blue  > 0)
                || (rc == "B" && mc.black > 0) || (rc == "R" && mc.red   > 0)
                || (rc == "G" && mc.green > 0);
            if (spell_has_color) { ++color_reduction; }
        }
        cost.generic = std::max(0, cost.generic - color_reduction);
    }
    // Hinata's per-target cost reduction (fixed-cost spells; {X} spells apply it at the X-cost
    // sites where the whole generic, incl. X, is known -- see the X-enumeration / apply_one).
    if (!def.card.m_mana_cost.has_x)
    {
        cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
    }
    return cost;
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
    // (Hook 15), so the clairvoyant + combo assumptions live in the per-deck file. A deck
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
static bool GroupChoiceOverSplices(const GameState& state,
                                   const std::vector<Action>& cands,
                                   const std::vector<std::vector<int>>& groups,
                                   const std::vector<int>& choice)
{
    auto sel_base = [&](size_t g, int* out_j) -> bool {
        if (choice[g] <= 0) { return false; }
        int j = groups[g][choice[g] - 1];
        const CardDefinition* d = cands[j].def;
        bool is_base = d && d->params.splice_onto_arcane && cands[j].kind == Action::Kind::CastFromHand;
        if (is_base) { *out_j = j; }
        return is_base;
    };
    std::vector<std::string> names;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        int j;
        if (!sel_base(g, &j)) { continue; }
        if (std::find(names.begin(), names.end(), cands[j].card_name) == names.end())
        { names.push_back(cands[j].card_name); }
    }
    if (names.empty()) { return false; }
    const Player& ap = state.ActivePlayer();
    for (const std::string& nm : names)
    {
        std::vector<int> ks;
        for (size_t g = 0; g < groups.size(); ++g)
        {
            int j;
            if (sel_base(g, &j) && cands[j].card_name == nm) { ks.push_back(cands[j].splice_count); }
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
static bool GroupChoiceNonPrefixAccel(const GameState& /*state*/,
                                      const std::vector<Action>& cands,
                                      const std::vector<std::vector<int>>& groups,
                                      const std::vector<int>& group_hand_index,
                                      const std::vector<int>& choice)
{
    struct AccelG { int base_mv; int hand_index; bool cast; };
    std::vector<AccelG> accel;
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
        accel.push_back({ base_mv, group_hand_index[g], choice[g] > 0 });
    }
    if (accel.size() < 2) { return false; }   // 0/1 accelerant -> every selection is trivially a prefix
    std::sort(accel.begin(), accel.end(), [](const AccelG& a, const AccelG& b) {
        if (a.base_mv != b.base_mv) { return a.base_mv < b.base_mv; }
        return a.hand_index < b.hand_index;
    });
    // Cheapest-first prefix: once a cheaper accelerant is left UN-cast, no more-expensive one may be cast.
    bool saw_uncast = false;
    for (const AccelG& a : accel)
    {
        if (a.cast) { if (saw_uncast) { return true; } }
        else        { saw_uncast = true; }
    }
    return false;
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
    static const char* kColorLetters[5] = { "W", "U", "B", "R", "G" };
    std::vector<int> idx;                                      // candidate colour indices, W,U,B,R,G order
    for (int c = 0; c < 5; ++c) { if (open_all || demand[c] > 0) { idx.push_back(c); } }
    // Stable sort by DESCENDING demand -> equal-demand colours keep their W,U,B,R,G tiebreak order.
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return demand[a] > demand[b]; });
    std::vector<std::string> colors;
    for (int c : idx) { colors.push_back(kColorLetters[c]); }
    if (colors.empty()) { colors.push_back("R"); }
    return colors;
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
static std::vector<Action> CollectActions(const GameState& state, bool /*is_pre_combat*/)
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
            ManaPool xpool = BuildPool(state);
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
            std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, state.active_player_index, def.params);
            if (cands.empty()) { cands.push_back(std::string{}); }  // whiff: castable, fetches nothing
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
            for (int k = 0; k <= others; ++k)
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
            for (const std::string& col : ChosenFloatColorCandidates(state))
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
        static const bool s_ponder_search = std::getenv("MTG_PONDER_SEARCH") != nullptr;
        if (def.params.cast_reorder > 0 && (s_ponder_search || DecisionUnpruned(UnprunedGate::Ponder)))
        {
            Action keep_a = a;            keep_a.ponder_keep    = 1;
            a.ponder_keep = 0;            // `a` becomes the shuffle variant
            actions.push_back(std::move(keep_a));
            actions.push_back(std::move(a));
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
    static const bool v = std::getenv("MTG_NO_GROUP_CAP") != nullptr;
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
static int ManaPruneBound(const ManaPool& pool, const std::vector<Action>& cands)
{
    static const bool on = []{ const char* e = std::getenv("MTG_MANA_PRUNE"); return !(e && std::string(e) == "0"); }();
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
    for (const Action& a : cands) { b += a.ritual_float; b += a.rock_mana.Total(); }
    return (b >= std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(b);
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

// ---- TurnSolver::Solve ---------------------------------------------------

TurnSolver::Plan TurnSolver::Solve(const GameState& state, bool is_pre_combat)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    const Player& ap = state.ActivePlayer();
    ManaPool pool             = BuildPool(state);
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

    // Deck-specific reach toward THIS turn's lethal beyond combat + direct damage (the Treasure
    // Hunt / Land's Edge ammunition model) is provider-owned (Hook 14). HasExtraLethalModel()
    // gates the whole thing: a deck without such a model skips building the per-plan cast list
    // entirely, staying byte-identical to the old "all addends 0" path.
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
    bool any_accel = false;
    if (accel_prefix_on)
    {
        for (const Action& ra : cands)
        {
            if (ra.def && ra.def->params.ritual_floating_mana > 0
                && ra.kind == Action::Kind::CastFromHand) { any_accel = true; break; }
        }
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
        // Reject two SacForMana of the same source (its colour variants are mutually exclusive). Inert
        // without a SacForMana action (Lotus Bloom) -> byte-identical.
        if (SubsetHasDuplicateSacSource(cands, sel)) { return; }
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
        if (any_ritual)
        {
            int ritual_credit = 0;
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            // Rite-of-Flame graveyard self-scaling: k copies cast this turn escalate (+0,+1,...,+k-1)
            // as each prior copy hits the graveyard; the flat per-cast stamp misses that triangular
            // term. Single self-scaling name in-deck, so count-by-flag == count-by-name.
            int gy_self = 0;
            for (int j : sel) { if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++gy_self; } }
            ritual_credit += gy_self * (gy_self - 1) / 2;
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
            int acred = SameTurnAffinityGenericCredit(state, cands, sel);
            if (acred > 0)
            {
                combined.generic             = std::max(0, combined.generic - acred);
                noncreature_combined.generic = std::max(0, noncreature_combined.generic - acred);
            }
        }
        // (No same-turn reducer credit here -- see the note at the any_affinity flag above; the
        // Medallion credit is EnumeratePlans-only so Solve's greedy/leaf stays byte-identical.)
        const bool mana_ok = credited ? (eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined))
                                       : (pool.CanPay(combined) && pool_noncreature.CanPay(noncreature_combined));
        // Filter/ramp-land color conversion the flat pool can't express -> real-payment fallback.
        if (!mana_ok && !(any_filter && SubsetPayableWithFilters(state, cands, sel))) { return; }
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

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
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
        for (int j : sel) { best.actions.push_back(cands[j]); }
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
    static const bool s_no_combo_line = std::getenv("MTG_NO_COMBO_LINE") != nullptr;
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

    // The default enumeration replaces the 2^m action powerset with the PRODUCT of per-hand-card
    // choices {skip, cast, deploy-via-Vial} (same-charge Vial deploys collapse to one
    // representative, bounded by an aggregate per-charge capacity) crossed with the 2^independent
    // powerset of non-hand actions (graveyard retrace). This visits exactly the powerset's feasible
    // subsets -- the same invariant EnumeratePlans relies on -- in O(prod(1+choices)*2^independent)
    // instead of O(2^m), which is the wide-board (slivers/knights) hot path. MTG_LEGACY_SOLVE keeps
    // the reference powerset for A/B (the two must produce byte-identical game results).
    static const bool s_legacy_solve = std::getenv("MTG_LEGACY_SOLVE") != nullptr;
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
    for (int j = 0; j < m; ++j)
    {
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

    const int mana_bound = ManaPruneBound(pool, cands);   // total-mana prune (byte-identical); see helper
    std::vector<int> choice(num_groups, 0);
    bool done = false;
    while (!done)
    {
        // Group-selection total mana cost (once per odometer position). If it already exceeds the
        // ramp-credited bound, every extension (independent actions only add cost) is unpayable, so
        // skip the whole inner loop -- the combos skipped are exactly those consider()'s CanPay rejects.
        int gcost = 0;
        for (int g = 0; g < num_groups; ++g)
        { if (choice[g] > 0) { gcost += cands[groups[g][choice[g] - 1]].cost.ManaValue(); } }
        // Likewise skip an over-splicing group choice (byte-identical: consider()'s SubsetHasIllegalSplice
        // would reject every extension anyway; splice legality is fixed by the group selection). any_splice
        // gates it off for every non-splice deck. Kills the Dragonstorm illegal-over-splice majority.
        const bool splice_ok = !any_splice || !GroupChoiceOverSplices(state, cands, groups, choice);
        // Dragonstorm acceleration-prefix collapse (HEURISTIC, gated by accel_prefix_on/any_accel above):
        // reject a group choice whose cast accelerants are not a cheapest-first prefix. Off/absent -> true.
        const bool accel_ok = !(accel_prefix_on && any_accel)
                           || !GroupChoiceNonPrefixAccel(state, cands, groups, group_hand_index, choice);
        if (gcost <= mana_bound && splice_ok && accel_ok)
        {
            for (int imask = 0; imask < (1 << num_ind); ++imask)
            {
                sel.clear();
                int icost = 0;
                for (int g = 0; g < num_groups; ++g)
                {
                    if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); }
                }
                for (int b = 0; b < num_ind; ++b)
                {
                    if (imask & (1 << b)) { sel.push_back(independent[b]);
                                            icost += cands[independent[b]].cost.ManaValue(); }
                }
                if (!sel.empty() && gcost + icost <= mana_bound && vial_ok(sel)) { consider(sel); }
            }
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

    return best;
}

// ============================================================
// Multi-turn lookahead
// ============================================================

// Tap mana sources in-place to pay a cost. Returns false if mana is unavailable.
// for_creature: if false, skip creature-only mana sources (e.g. Ancient Ziggurat)
//               since non-creature spells cannot be paid with that mana.
// Single payment attempt honouring `reserved_mask` (active-player battlefield indices to HOLD --
// never tapped here). The public TapForCostDirect wrapper (below) runs this first with the reserved
// specials held, then again with reserved_mask=0 if that missed. reserved_mask=0 is byte-identical
// to the pre-reservation code.
static bool TapForCostDirectOnce(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 std::uint64_t reserved_mask)
{
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed

    // Spend any turn-scoped RESERVE mana (a ritual's floating output) before tapping. No-op when
    // empty -> byte-identical for non-ritual decks. Mirrors AIEngine::TapForCost so the rollout
    // and the real executor realise a ritual's floating mana identically (lockstep).
    const ManaPool reserve_pre = state.floating_mana;
    ManaCost cost = cost_in;
    SpendFloatingTowardCost(state.floating_mana, cost);

    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        if (reserved_mask)   // reservation audit: a held source is not tappable this attempt
        {
            const std::size_t idx = static_cast<std::size_t>(&p - state.battlefield.data());
            if (idx < 64 && (reserved_mask & (1ull << idx))) { return false; }
        }
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def.params.mana_rock;
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        if (!StorageSourceLive(p, def)) { return false; }   // uncharged storage land makes no mana
        return true;
    };

    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        p.tapped = true;
        DecrementDepletionOnTap(p);
        if (def.params.tap_self_damage > 0) { state.players[active].life -= def.params.tap_self_damage; }
        // Grove of the Burnwillows: the COLOURED tap ({R}/{G}) makes the opponent gain 1 (-> 1 damage
        // with Tainted Remedy out). A `col == Colorless` tap is the painless "{T}: Add {C}" mode --
        // no drip (see DripLandAnyPipColor: a generic pip absent a Remedy routes here as Colorless).
        // Mirrored in AIEngine::TapForCost and TapForCostBacktrack.
        if (def.params.tap_opponent_lifegain > 0 && col != Color::Colorless)
        { OpponentGainsLife(state, active, def.params.tap_opponent_lifegain, def.card.m_name.str()); }
        // A Karoo bounce land (Izzet Boilerworks) makes TWO mana of DIFFERENT colours from one tap
        // ({U}{R}). Crediting `amt` of the single matched colour loses the second colour, so a lone
        // bounce land could not pay a two-colour cost (Expressive Iteration {U}{R}) even though
        // BuildPool/AddSourceToPool count it as `amt` wild -- the spell was enumerated but unpayable,
        // a silent no-op (mana tapped, spell stuck in hand). Produce one mana of EACH colour it makes
        // so the floating pool actually holds {U}{R}. Single-colour sources (incl. single-tap duals,
        // amt 1) keep `amt` of the matched colour -> every deck without such a land is byte-identical.
        // Mirrored in AIEngine::tap_source -- keep the two in lockstep.
        //
        // Storage-counter land (Dwarven Hold / Mercadian Bazaar) BURST-ALL: one tap floats ALL live
        // storage counters (the land is committed for the turn). The planner credits the land its full
        // PermanentManaYield (= storage_counters) and marks the whole count consumed on tap, so the
        // executor must deliver all of them. The old per-spell PARTIAL burst (need = cost.ManaValue() -
        // produced_total) delivered LESS than the planner promised, silently dropping a legal cast on a
        // tight multi-spell plan when an earlier spell under-burst and stranded a counter (the burst
        // amount shifted with irrelevant cast order). See docs/design/dragonstorm-plan-execution-
        // fidelity-bug.md. Bank-the-rest is via the RESERVE (an unneeded storage land is held untapped),
        // not a partial burst. ManaSourceRank taps storage LAST. Mirrors AIEngine::tap_source (lockstep).
        int amt;
        if (def.params.storage_land)
        {
            amt = p.storage_counters;
            p.storage_counters = 0;
        }
        else { amt = ManaProducedPerTap(def); }
        const std::vector<Color>& prod = EffectiveProduces(state, active, def);
        if (amt > 1 && prod.size() > 1) { for (Color c : prod) { floating.Add(c, 1); } }
        else                            { floating.Add(col, amt); }
    };

    // allow_ramp: may a ramp filter (Ferrous Lake) be used? false when feeding a ramp
    // filter's {1} so ramp filters never feed each other. Mirrors AIEngine::TapForCost.
    std::function<bool(Color,bool,bool)> produce = [&](Color needed, bool any, bool allow_ramp) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // Scarcity-first source selection (default ON; MTG_TAP_LEGACY opts OUT to the battlefield-order
        // 4-step path below, a byte-identical A/B baseline): pick the LEAST-flexible qualifying source
        // for this pip (via ManaSourceRank, lower = earlier) so rainbow sources stay up; filters rank
        // between duals and tri and are candidates only when feedable now. Ramp filters (rare) are left
        // to the legacy path / backtracker, the complete fallback. MUST stay byte-for-byte identical to
        // AIEngine::TapForCost (this solver just omits the `available` accounting AIEngine keeps).
        if (TapScarcityEnabled())
        {
            const int bn = static_cast<int>(state.battlefield.size());
            int best_i = -1, best_rank = 1 << 30, best_kind = 0;  // 1 direct, 2 filter-colour, 3 filter-{C}
            for (int i = 0; i < bn; ++i)
            {
                Permanent& p = state.battlefield[i];
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !usable(p, *def)) { continue; }
                int kind = 0;
                if (def->params.is_filter)
                {
                    if (any || needed == Color::Colorless) { kind = 3; }   // {C} mode covers generic/{C}
                    else
                    {
                        bool makes = false;
                        for (Color c : def->params.produces) { if (c == needed) { makes = true; break; } }
                        bool feedable = false;
                        if (makes)
                        {
                            for (Color c : def->params.produces)
                            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { feedable = true; break; } }
                            if (!feedable) { feedable = HasUntappedNonFilterSourceProducing(state, def->params.produces); }
                        }
                        if (makes && feedable) { kind = 2; } else { continue; }
                    }
                }
                else if (def->params.ramp_filter) { continue; }
                else
                {
                    const std::vector<Color>& prod = EffectiveProduces(state, active, *def);
                    bool makes = false;
                    if (any) { makes = !prod.empty(); }
                    else { for (Color c : prod) { if (c == needed) { makes = true; break; } } }
                    if (!makes) { continue; }
                    kind = 1;
                }
                const int rank = ResolveProvider(state).ManaSourceRank(state, *def);
                if (rank < best_rank) { best_rank = rank; best_i = i; best_kind = kind; }
            }
            if (best_i < 0) { return false; }
            Permanent& bp = state.battlefield[best_i];
            const CardDefinition* bdef = CardDatabase::Instance().LookupCached(bp.card);
            if (best_kind == 1)
            {
                const std::vector<Color>& prod = EffectiveProduces(state, active, *bdef);
                tap_source(bp, *bdef, any ? DripLandAnyPipColor(state, active, *bdef, prod[0]) : needed);
                return true;
            }
            if (best_kind == 3)
            {
                bp.tapped = true;
                floating.Add(Color::Colorless, 1);
                return true;
            }
            // kind 2: filter coloured mode -- feed one of its colours (least-flexible feeder), yield 2.
            const Color out = needed;
            bool have_input = false;
            for (Color c : bdef->params.produces)
            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { have_input = true; break; } }
            if (!have_input)
            {
                int fi = -1, frank = 1 << 30; Color fcol = Color::Colorless;
                for (int i = 0; i < bn; ++i)
                {
                    Permanent& s = state.battlefield[i];
                    if (s.controller_index != active || s.tapped) { continue; }
                    const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                    if (!sd || sd->params.is_filter || sd->params.ramp_filter || !usable(s, *sd)) { continue; }
                    bool m = false; Color match = Color::Colorless;
                    for (Color pc : EffectiveProduces(state, active, *sd))
                    { for (Color ic : bdef->params.produces) { if (pc == ic) { m = true; match = ic; break; } } if (m) { break; } }
                    if (!m) { continue; }
                    const int r = ResolveProvider(state).ManaSourceRank(state, *sd);
                    if (r < frank) { frank = r; fi = i; fcol = match; }
                }
                if (fi < 0) { return false; }
                Permanent& fs = state.battlefield[fi];
                tap_source(fs, *CardDatabase::Instance().LookupCached(fs.card), fcol);
            }
            for (Color c : bdef->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            bp.tapped = true;
            floating.Add(out, 2);
            return true;
        }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || def->params.is_filter || def->params.ramp_filter || !usable(p, *def)) { continue; }
            const std::vector<Color>& prod = EffectiveProduces(state, active, *def);  // RP-aware
            Color col;
            if (any)
            {
                if (prod.empty()) { continue; }
                col = DripLandAnyPipColor(state, active, *def, prod[0]);  // Grove {C} mode for generic
            }
            else
            {
                bool match = false;
                for (Color c : prod) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                col = needed;
            }
            tap_source(p, *def, col);
            return true;
        }

        // 2) Filter land colourless mode ({T}: Add {C}) — for a generic or {C} pip.
        if (any || needed == Color::Colorless)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
                p.tapped = true;
                floating.Add(Color::Colorless, 1);
                return true;
            }
        }

        // 3) Filter mode for a coloured pip: feed one of the filter's colours, yield 2.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
            Color out;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                out = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                out = needed;
            }
            bool have_input = false;
            for (Color c : def->params.produces)
            {
                ManaPool probe = floating;
                if (ConsumeFloating(probe, c)) { have_input = true; break; }
            }
            if (!have_input)
            {
                bool fed = false;
                for (Color ic : def->params.produces)
                {
                    for (Permanent& s : state.battlefield)
                    {
                        if (s.controller_index != active || s.tapped) { continue; }
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                        if (!sd || sd->params.is_filter || sd->params.ramp_filter || !usable(s, *sd)) { continue; }
                        bool m = false;
                        for (Color c : EffectiveProduces(state, active, *sd)) { if (c == ic) { m = true; break; } }  // RP feeder
                        if (!m) { continue; }
                        tap_source(s, *sd, ic);
                        fed = true; break;
                    }
                    if (fed) { break; }
                }
                if (!fed) { continue; }
            }
            for (Color c : def->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            p.tapped = true;
            floating.Add(out, 2);
            return true;
        }

        // 4) Ramp filter (e.g. Ferrous Lake: {1},{T}: Add {U}{R}). Pay {1} generic from any
        //    other untapped source (incl. a filter's {C}), then yield one of each produces
        //    colour. No free mode; allow_ramp=false in the feed call prevents ramp chains.
        if (allow_ramp)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.ramp_filter || !usable(p, *def)) { continue; }
                if (!any)
                {
                    bool match = false;
                    for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                    if (!match) { continue; }
                }
                else if (def->params.produces.empty()) { continue; }
                if (floating.Total() == 0 && !produce(Color::Colorless, true, false)) { continue; }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                for (Color c : def->params.produces) { floating.Add(c, 1); }
                return true;
            }
        }
        return false;
    };

    auto pay = [&](Color needed, bool any) -> bool
    {
        if (!produce(needed, any, true)) { return false; }
        if (any) { Color took; return ConsumeFloatingAny(floating, took); }
        return ConsumeFloating(floating, needed);
    };

    // Greedy-first, then a backtracking fallback for filter chains the greedy strands
    // (e.g. Throes of Chaos via a Cascade Bluffs + Ferrous Lake chain). Snapshot so the
    // greedy's success path is byte-identical (no GT churn) and only previously-FAILING
    // casts gain the chain solution. See TapForCostBacktrack.
    const std::vector<Permanent> bf_pre = state.battlefield;
    const int life_pre = state.players[active].life;
    const int opp_pre = state.players[1 - active].life;
    const bool oll_pre = state.opponent_lost_life_this_turn;
    // Retain over-produced mana (forced filter/depletion over-tap) into the turn-scoped
    // reserve so a later same-(main-)phase cast can spend it (CR 500.4). state.floating_mana
    // already holds the un-spent reserve after SpendFloatingTowardCost; add the leftover on top.
    // Off (MTG_NO_FLOAT_LEFTOVER) -> no-op. Mirrored byte-for-byte in AIEngine::TapForCost.
    auto commit_leftover = [&](const ManaPool& lo)
    { if (FloatLeftoverManaEnabled()) { state.floating_mana.AddPool(lo); } };
    auto greedy = [&]() -> bool
    {
        for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) return false; }
        for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) return false; }
        for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) return false; }
        for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) return false; }
        for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) return false; }
        for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) return false; }
        for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) return false; }
        return true;
    };
    if (greedy()) { commit_leftover(floating); return true; }
    // Greedy failed: try the backtracking solver from a clean board.
    state.battlefield        = bf_pre;
    state.players[active].life = life_pre;
    ManaPool bt_leftover;
    if (TapForCostBacktrack(state, cost, for_creature, ManaPool{}, nullptr, nullptr, &bt_leftover,
                            /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
    { commit_leftover(bt_leftover); return true; }
    // Floating-fed filter retry: a filter / ramp-filter land (Ferrous Lake {1},{T}: Add {U}{R}) can be
    // FED by turn-scoped floating (a ritual's output, a depletion over-tap). SpendFloatingTowardCost
    // above spent that floating on the cost DIRECTLY, stranding the filter (no feeder left) -- so the
    // first backtracker, run on the REDUCED cost with an empty float pool, could not chain it. Retry
    // the backtracker with the ORIGINAL cost and the ORIGINAL reserve as feed, letting it choose
    // feed-vs-spend. Guarded by a non-empty reserve AND an untapped filter/ramp source, so it is only
    // reached in exactly that stranded-feeder case: a non-floating or filter-less board never enters it
    // (byte-identical), and any cast the greedy/first backtracker already paid never reaches a fallback.
    if (reserve_pre.Total() > 0 && AnyUntappedFilterSource(state))
    {
        state.battlefield          = bf_pre;
        state.players[active].life  = life_pre;
        ManaPool bt2_leftover;
        if (TapForCostBacktrack(state, cost_in, for_creature, reserve_pre, nullptr, nullptr,
                                &bt2_leftover, /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
        {
            state.floating_mana = ManaPool{};   // the whole reserve was re-allocated by the backtracker
            commit_leftover(bt2_leftover);
            return true;
        }
    }
    // Total failure: a cast that cannot be paid must leave the game exactly as it found it
    // (atomic rollback) -- restore the full pre-payment snapshot, not the greedy's partial-tap
    // end-state. Callers (cycling/sac loops, ill-ordered plans) rely on a failed payment being
    // side-effect-free; the old greedy-fail restore leaked tapped lands / spent counters.
    state.battlefield                  = bf_pre;
    state.players[active].life         = life_pre;
    state.players[1 - active].life     = opp_pre;
    state.opponent_lost_life_this_turn = oll_pre;
    state.floating_mana                = reserve_pre;   // payment failed -> return the reserve untouched
    return false;
}

// Public payment entry. Mana-source RESERVATION ("leaving sources up", see ReserveEnabled): FIRST
// try to pay while HOLDING the special sources (dorks / {C}-manlands / depletion) untapped; only if
// the cost cannot be met without them fall through to the normal payment. Slack-only, so it is
// weakly dominant (a held source you did not need never hurts and keeps its attack / animate /
// depletion-counter value). Default ON; MTG_NO_RESERVE -> mask 0 -> a single normal attempt, byte-
// identical to the pre-reservation code. Mirrored byte-for-byte in AIEngine::TapForCost (lockstep).
static bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature)
{
    const std::uint64_t rmask = ReservableSpecialMask(state);
    if (rmask != 0)
    {
        // Snapshot everything a payment can touch so a reserved MISS restores byte-identically
        // before the normal attempt (which must reproduce the pre-reservation payment exactly).
        const int a = state.active_player_index;
        const std::vector<Permanent> bf_snap  = state.battlefield;
        const ManaPool               fm_snap  = state.floating_mana;
        const int  la  = state.players[a].life;
        const int  lo  = state.players[1 - a].life;
        const bool oll = state.opponent_lost_life_this_turn;
        if (TapForCostDirectOnce(state, cost_in, for_creature, rmask)) { return true; }
        state.battlefield                  = bf_snap;
        state.floating_mana                = fm_snap;
        state.players[a].life              = la;
        state.players[1 - a].life          = lo;
        state.opponent_lost_life_this_turn = oll;
    }
    return TapForCostDirectOnce(state, cost_in, for_creature, /*reserved_mask=*/0);
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
                           const std::string& fetch_target = "", bool allow_shock_pay = true);
static std::string SimulateLandPlay(GameState& state);

// Provider cast-order rank for a hand cast by name (lower = cast earlier). Thin lookup
// wrapper around DecisionProvider::CastOrderRank; mirrored byte-for-byte in AIEngine so the
// rollout's canonical cast order and the real executor's stay in lockstep. Unknown card
// (should not happen for a planned cast) falls to the noncreature rank.
static int CastRankOf(const GameState& state, const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    return d ? ResolveProvider(state).CastOrderRank(state, *d) : 20;
}

// A cast whose resolution triggers a mid-turn re-solve breakpoint (draw / staging / cascade
// / retrace): the rest of the turn re-solves from the post-draw state, so the optimal cast
// ORDER around it is situation-dependent (mana left, what is revealed) -- a static rank
// can't capture it. The CastOrderRank reordering is therefore SKIPPED for any set that
// contains such a card; that set keeps its canonical plan/breakpoint order (the search owns
// the ambiguous ordering). Mirrored in AIEngine.
static bool OrderingOpaque(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawUntilNonland
        || d->params.stages_cards
        || d->params.cascade_max_mv > 0
        || d->params.retrace
        || d->params.expressive_iteration
        || d->params.impulse_exile > 0   // Apex of Power: staged exile -> search-owned breakpoint order
        || d->params.draw > 0;
}

bool TurnSolver::BatchPrepayMainCasts(GameState& state, const std::vector<Action>& acts)
{
    static const bool s_enabled = std::getenv("MTG_NO_BATCH_PAY") == nullptr;
    if (!s_enabled) { return false; }
    // A non-empty float (e.g. a ritual's output) would be clobbered by the pre-load; producer turns
    // are declined below regardless, but guard here too so the reserve stays byte-identical there.
    if (state.floating_mana.Total() != 0) { return false; }

    const int active = state.active_player_index;
    ManaCost combined;
    int eligible = 0;
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
        ok = TapForCostBacktrack(state, combined, /*for_creature=*/false, ManaPool{},
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
        ok = TapForCostBacktrack(state, combined, /*for_creature=*/false, ManaPool{},
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
                            std::vector<Action>* out_breakpoint = nullptr)
{
    PROF_INC(applyplan_calls);
    Player& ap  = state.ActivePlayer();
    int opp_idx = 1 - state.active_player_index;

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
    static const bool s_defer_cantrip = std::getenv("MTG_NO_DEFER_CANTRIP") == nullptr;
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
    static const bool s_karoo_defer = std::getenv("MTG_NO_KAROO_DEFER") == nullptr;
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
                    PlayLandByName(state, plan.land_to_play, plan.fetch_target, allow_shock_pay);
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
        static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        std::string played = SimulateLandPlay(state);
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
        static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
        if (!s_fd || !is_pre_combat) { return; }
        if (karoo_deferred) { return; }   // the drop is reserved for the deferred Karoo
        Player& lp = state.ActivePlayer();
        if (lp.lands_played_this_turn >= lp.LandDropsAvailable()) { return; }   // drop already used

        // Hold the drop entirely when the lands in hand are the marginal Land's Edge ammo for a
        // lethal this turn: playing one would push the count below lethal and the fire-count
        // heuristic (below, in this same ApplyPlanDirect) would then hold the rest, slipping the
        // win a turn (s1 gi0 T4-vs-T3). Provider-owned (Hook 21); default off for every other deck.
        if (ResolveProvider(state).HoldDeferredDropForLethal(state, state.active_player_index)) { return; }

        // The keep-ammo land CHOICE is deck logic -> ask the provider (Hook 13); the engine
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
        // No flood-keep land to play -> play the best normal land (the deferred drop), recorded.
        play_breakpoint_land(sink);
    };


    // One-shot flag: when set, the NEXT apply_one cast skips its mana cost (a free
    // cascade cast). Consumed at the top of apply_one so it applies to exactly one cast.
    bool cascade_free = false;
    std::function<void(const std::string&, bool, bool, int, bool, int, const std::string&, int, int, int, int, int, const std::string&)> apply_one;
    apply_one = [&](const std::string& name, bool is_sacrifice, bool from_graveyard, int discard_lands,
                    bool alt_cost, int alt_lifegain, const std::string& tutor_target, int chosen_x,
                    int own_targets, int ponder_keep, int crackle_targets, int splice_count,
                    const std::string& chosen_float_color)
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
        if (!free_cast && !alt_cost && !TapForCostDirect(state, ec, is_creature)) { return; }
        // Apex of Power cast-from-hand gate (captured BEFORE the erase invalidates `it`): a hand copy
        // has m_is_staged == false -> cast_from_hand true (adds Apex's 10-colour float); an Apex cast off
        // another Apex's staged exile has m_is_staged == true -> false (float withheld). Inert otherwise.
        const bool cast_from_hand = !it->m_is_staged;
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
                int pick = lands.front();   // heuristic default: first land in hand order
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
                SoulfireResult sr = SoulfireDig(state, state.active_player_index, own_targets, &def);
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
                    // Magma Opus faithful spread (opt-in): the opponent's face takes only the plan's face
                    // damage (1 spreading / `dmg` concentrating; see MagmaFaithfulPlan), lockstep with
                    // ResolveDirectDamage + the discount. Every other burn deals its full dmg to the face.
                    int face = IsMagmaFaithful(def.params) ? MagmaFaithfulPlan(def, state).opp_face_damage : dmg;
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
                    ManaPool rem = BuildPool(scratch);
                    while (rem.CanPay(rep_cost) && TapForCostDirect(scratch, rep_cost, true))
                    { ++max_count; rem = BuildPool(scratch); }
                    int k = (*g_play_replicate_chooser)(state, state.active_player_index,
                                                        def.card.m_name.str(), max_count);
                    cap = k < 0 ? 0 : (k > max_count ? max_count : k);
                }
                int made = 0;
                ManaPool remaining = BuildPool(state);
                while ((cap < 0 || made < cap) && remaining.CanPay(rep_cost))
                {
                    if (!TapForCostDirect(state, rep_cost, true)) { break; }
                    Permanent token = perm;
                    token.card.m_number = 0;
                    state.battlefield.push_back(token);
                    remaining = BuildPool(state);
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
                play_breakpoint_land(my_bp_sink);
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
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
                play_drawn_flood_keep_land(my_bp_sink);
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
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
                    apply_one(cname, false, false, 0, false, 0, std::string{}, 0, 0, -1, -1, 0, std::string{});
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
                play_breakpoint_land(my_bp_sink);
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
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
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            if (def.params.etb_opponent_lifegain > 0)
            {
                OpponentGainsLife(state, state.active_player_index, def.params.etb_opponent_lifegain);
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
            std::vector<int> lands;
            int idx = -1;
            bool locked = false;   // once a tapped land is chosen as the default, keep it (orig `break`)
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
                lands.push_back(i);
                if (!locked)
                {
                    if (idx < 0)  { idx = i; }
                    if (p.tapped) { idx = i; locked = true; }   // first tapped land -> default (was break)
                }
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
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color);
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
                { if (is_enabler(a)) { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color); } }
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
                        apply_one(a.card_name, true, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color);
                        spec_hoisted_sac.insert(ai);
                    }
                }
                for (const Action& a : acts)
                {
                    if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !is_enabler(a))
                    { apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color); }
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
                { return CastRankOf(state, acts[x].card_name) < CastRankOf(state, acts[y].card_name); });
                for (int i : order)
                {
                    const Action& a = acts[i];
                    apply_one(a.card_name, false, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color);
                }
            }
        }
        for (size_t ai = 0; ai < acts.size(); ++ai)
        {
            if (spec_hoisted_sac.count(ai)) { continue; }   // already cast by the Spectacle hoist
            const Action& a = acts[ai];
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            {
                apply_one(a.card_name, true, false, 0, a.alt_cost, a.alt_lifegain, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color);
            }
        }
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromGraveyard)
            {
                apply_one(a.card_name, false, true, a.discard_lands, false, 0, std::string{}, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color);
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
            std::vector<Card> keep;
            int fired = 0;
            for (Card& c : le_ap.hand)
            {
                auto cdef    = CardDatabase::Instance().LookupCached(c);
                bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
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
            apply_one(nm, false, false, 0, true, amt, std::string{}, 0, 0, -1, -1, 0, std::string{});
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
        { ApplySacForMana(state, state.active_player_index, a.sac_source_id, a.chosen_float_color, a.ritual_float); }
        else if (a.kind == Action::Kind::Suspend)
        { ApplySuspend(state, state.active_player_index, a.card_name); }
    }

    // Whole-turn batch pre-payment: tap for the combined cost of the main hand casts and pre-load
    // floating (see BatchPrepayMainCasts). The main casts below then drain the pool -- scarce colours
    // allocated jointly, filters fed, unneeded sources left up -- instead of the stranding per-cast
    // greedy. Declined turns leave state untouched and fall through to the identical greedy path.
    TurnSolver::BatchPrepayMainCasts(state, plan.actions);

    apply_plan_actions(plan.actions, plan.searched_order);

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
        play_breakpoint_land(out_breakpoint);
        TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
        apply_plan_actions(extra.actions, false);
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
            ManaPool pool = BuildPool(state);
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
                TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat);
                apply_plan_actions(extra.actions, false);
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
            static const bool s_fd = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
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
    int opp_idx = 1 - state.active_player_index;
    int active  = state.active_player_index;

    // Legend rule before declaring attackers (mirror GameEngine::CombatPhase): a duplicate
    // legendary lord cannot double-count its buff. No-op without legendaries.
    EnforceLegendRule(state, active);

    // Eligible attacker indices BEFORE any token creation (push_back keeps indices stable).
    std::vector<int> atk_idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active)) { continue; }
        if (!ResolveProvider(state).ShouldAttackWith(state, p)) { continue; }
        atk_idx.push_back(i);
    }

    // Attack-trigger tokens (Adeline), tapped and attacking this combat, then persist.
    if (!atk_idx.empty())
    {
        int tok_start = FireAttackCreateTokens(state, active);
        for (int i = tok_start; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            atk_idx.push_back(i);
        }
    }

    // Exalted (Ignoble Hierarch): +1/+1 per Exalted ability to a creature attacking ALONE.
    int exalted_bonus = (static_cast<int>(atk_idx.size()) == 1)
                        ? CountExalted(state.battlefield, active) : 0;

    // Firebreathing (Scourge {R}:+1/+0 self, Lathliss {1}{R}: Dragons +1/+0 team): spend LEFTOVER
    // combat mana on attacker pumps BEFORE the damage loop reads their power. Shared with the
    // executor (AIEngine::Firebreathe) on the byte-identical BuildAvailableMana pool -> lockstep.
    // Inert unless a firebreathing param is present -> other decks byte-identical.
    if (!atk_idx.empty() && ControlsFirebreathingSource(state, active))
    { ApplyFirebreathing(state, active, atk_idx, BuildPool(state)); }

    std::vector<const Permanent*> attackers;
    attackers.reserve(atk_idx.size());
    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            p.card, state.battlefield, active, animated, &p);
        bool ds = animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active));
        int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        int power = base_pw * (ds ? 2 : 1);
        state.players[opp_idx].life -= power;
        if (power > 0) { state.opponent_lost_life_this_turn = true; }
        if (!p.card.HasKeyword(Keyword::Vigilance)) { p.tapped = true; }
        attackers.push_back(&p);
    }
    int trigger_life_loss = CountAttackTriggerLifeLoss(state.battlefield, active, attackers);
    if (trigger_life_loss > 0)
    {
        state.players[opp_idx].life -= trigger_life_loss;
        state.opponent_lost_life_this_turn = true;
    }

    // Utvara Hellkite: per attacking Dragon, create a 6/6 Dragon token (untapped, summoning-sick;
    // NOT added to this combat). Each token entering fires OnDragonEnters (Scourge ping / Lathliss
    // token) via CreateToken. Mirrors GameEngine::CombatPhase (executor). `attackers` still holds
    // the pre-token attacker pointers (FireUtvaraAttackTokens reads them before any CreateToken).
    FireUtvaraAttackTokens(state, active, attackers);
}


// Activate tap-and-pay token abilities (e.g. Sliver Hive) with any spare mana.
static void SimulateTapTokens(GameState& state)
{
    int active = state.active_player_index;
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != active
            || state.battlefield[i].tapped) { continue; }
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!def || !def->params.tap_token_cost.has_value()) { continue; }

        if (!def->params.tap_token_requires_subtypes.empty())
        {
            bool found = false;
            for (int j = 0; j < bf_size; ++j)
            {
                if (state.battlefield[j].controller_index != active) { continue; }
                for (const std::string& req : def->params.tap_token_requires_subtypes)
                    for (const std::string& cs : state.battlefield[j].card.m_subtypes)
                        if (cs == req) { found = true; break; }
                if (found) { break; }
            }
            if (!found) { continue; }
        }

        const ManaCost& add_cost = def->params.tap_token_cost.value();
        state.battlefield[i].tapped = true;  // {T} cost; tap before building pool
        ManaPool remaining = BuildPool(state);
        if (!remaining.CanPay(add_cost)) { state.battlefield[i].tapped = false; continue; }
        TapForCostDirect(state, add_cost, true);

        // CreateToken appends to battlefield — never use stale refs after this point.
        CreateToken(state, active,
                    def->params.tap_token_power,
                    def->params.tap_token_toughness,
                    def->params.tap_token_subtypes);
    }
}

// Animate all animatable lands (e.g. Mutavault) if the active player has spare mana.
// Called after spells are cast, before combat, so the animated land can attack.
static void SimulateAnimateLands(GameState& state)
{
    int active = state.active_player_index;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped || p.is_animated) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.can_animate || !def->params.animate_cost.has_value()) { continue; }
        if (TapForCostDirect(state, def->params.animate_cost.value(), false))
        {
            p.is_animated = true;
        }
    }
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
    static const bool s_fd_discard = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
    while (!unlimited_hand && ap.hand.size() > 7)
    {
        // Default (commit-the-line): use the SHARED selector so the rollout sheds exactly the
        // card the real engine's ChooseDiscard would -- required-piece protection + land-outlet
        // ammo, reading the deck's pieces from state.m_required_pieces. Without this the rollout
        // shed high-MV spells and hoarded lands, predicting a phantom Land's Edge flood (gi=220).
        // Legacy (MTG_LEGACY_SEARCH): the frozen highest-MV-only rule (held-out ground truth).
        std::vector<Card>::iterator victim;
        if (s_fd_discard)
        {
            int idx = SelectCleanupDiscardIndex(state, state.m_required_pieces);
            victim = ap.hand.begin() + idx;
        }
        else
        {
            victim = std::max_element(ap.hand.begin(), ap.hand.end(),
                [](const Card& a, const Card& b)
                {
                    return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
                });
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
    static const bool s_fd_opp_spawns = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
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
                           const std::string& fetch_target, bool allow_shock_pay)
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

    for (auto it = pick; it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        if (it->m_impulse_no_land) { continue; }                   // Apex-exiled land: never played
        auto def = CardDatabase::Instance().LookupCached(*it);
        if (!def || !def->card.IsLand()) { continue; }

        // Fetchland: the land drop sacrifices the fetchland to search out a real land.
        // fetch_target names the searched choice (Pass 2); empty -> PerformFetch falls back
        // to FetchCandidates' top heuristic pick (Pass 1 / single-candidate).
        if (!def->params.fetch_land_types.empty())
        {
            Card fetchland = *it;
            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            ap.graveyard.push_back(fetchland);
            PerformFetch(state, state.active_player_index, def->params, fetch_target);
            return true;
        }

        // Resolve "as this land enters" choices while the card is still in hand. Human play
        // (g_play_land_entry_chooser set, and a real choice present) lets the user pick whether to
        // pay the shock life / reveal to enter untapped; otherwise the autonomous heuristic stands
        // (byte-identical for the search, which nulls the chooser via RevealLogPause).
        bool tapped;
        if (g_play_land_entry_chooser && LandEntryHasChoice(state, *def))
        {
            bool heur_untapped = !LandWouldEnterTapped(state, *def, allow_shock_pay);
            bool untapped = (*g_play_land_entry_chooser)(
                state, state.active_player_index, def->card.m_name.str(),
                def->params.etb_pay_life_to_untap,
                def->params.etb_untap_reveal_subtypes, heur_untapped);
            if (untapped) { ApplyLandUntapPayment(state, *def); }
            tapped = !untapped;
        }
        else
        {
            tapped = LandEntersTapped(state, *def, allow_shock_pay);
        }
        Permanent perm;
        perm.card              = def->card;
        perm.controller_index  = state.active_player_index;
        perm.owner_index       = state.active_player_index;
        perm.entered_this_turn = true;
        perm.tapped            = tapped;
        if (def->params.enters_tapped_with_depletion > 0)
        {
            Counter dep;
            dep.type  = Counter::Type::Depletion;
            dep.count = def->params.enters_tapped_with_depletion;
            perm.counters.push_back(dep);
        }
        state.battlefield.push_back(perm);

        ap.hand.erase(it);
        ++ap.lands_played_this_turn;
        if (def->params.etb_scry > 0)    { ScryTop(state, def->params.etb_scry); }
        if (def->params.etb_surveil > 0) { SurveilTop(state, def->params.etb_surveil); }
        if (def->params.etb_bounce_land) { BounceKarooLand(state, state.active_player_index, static_cast<int>(state.battlefield.size()) - 1); }
        // Forbidden Orchard played this turn -> tapped this turn -> opponent Spirit now (the
        // turn-start spawn only covers copies already in play). Mirrors AIEngine::TryPlaySpecificLand;
        // gated like the turn-start spawn so MTG_LEGACY_SEARCH keeps the old model.
        if (IsForbiddenOrchard(def))
        {
            static const bool s_orchard_onplay = std::getenv("MTG_LEGACY_SEARCH") == nullptr;
            if (s_orchard_onplay) { SpawnOpponentSpirit(state); }
        }
        return true;
    }
    return false;
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
    static const bool global = (std::getenv("MTG_SEARCH_ORDER") != nullptr) || DecisionUnpruned(UnprunedGate::SearchOrder);
    // Archetype opt-in (Hook 28): Dragonstorm searches its combo cast order by default. Provider-scoped
    // so every other deck stays byte-identical (base hook returns false). Cheap per-call vtable check --
    // the real cost is the k! applies below, gated behind this.
    return global || ResolveProvider(state).WantsCastOrderingSearch();
}

// Targeted cast-ORDERING candidates for Dragonstorm (Hook 28; see docs/design/dragonstorm-cast-order-search.md).
// The combo is a CHEAPEST-FIRST self-funding ritual chain with only a few real degrees of freedom, so we
// enumerate a handful of principled orderings instead of all k! permutations (which the caller caps at 120,
// SKIPPING the biggest go-off hands). The rules (user-specified):
//   * mana rituals -> cheapest-first (each funds the next); never searched among themselves;
//   * Irencrag Feat ("cast only one more spell") -> immediately before the finisher;
//   * finisher (Dragonstorm / Apex / a closing Dragon) -> last;
//   * multiple Desperate Ritual vs Seething Song -> two variants: splice-AFTER (preferred, splice the
//     Desperates once Seething's mana is up) and interleaved-by-cost (fallback, cast individually);
//   * Ruby Medallion -> the one genuinely SEARCHED position: tried at every slot so the rollout keeps the
//     earliest that still goes off (earlier discounts more red rituals).
// The identity (given) order is always included so the search can never do worse than the canonical line.
// Returns index-orderings over `reorder`; the caller applies + dedups-by-end-state + scores each.
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
    inline bool Enabled() { static const bool v = std::getenv("MTG_BRANCH_STATS") != nullptr; return v; }
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
    ManaPool      pool            = BuildPool(state);
    ManaPool      pool_noncreature = BuildNonCreaturePool(state);
    int           total_lands     = CountLands(state);
    int           pending_atk     = PendingAttackDamage(state);
    int           prowess_attackers    = CountProwessAttackers(state);
    std::vector<TriggerSource> trigger_sources = CollectTriggerSources(state);

    // Shared enumeration of all action sources (hand casts + Vial + retrace; LE in a
    // later phase). The subset machinery below reads the per-Action valuation scalars.
    std::vector<Action> cands = CollectActions(state, is_pre_combat);
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
    bool any_accel = false;
    if (accel_prefix_on)
    {
        for (const Action& ra : cands)
        {
            if (ra.def && ra.def->params.ritual_floating_mana > 0
                && ra.kind == Action::Kind::CastFromHand) { any_accel = true; break; }
        }
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
    for (int j = 0; j < m; ++j)
    {
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
        // Reject two SacForMana of the same source (mutually-exclusive colour variants). Inert
        // without a SacForMana action -> byte-identical.
        if (SubsetHasDuplicateSacSource(cands, sel)) { return; }
        // Reject physically-impossible Desperate Ritual over-splice. Inert without a splice base.
        if (SubsetHasIllegalSplice(state, cands, sel)) { return; }
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
        if (any_ritual)
        {
            int ritual_credit = 0;
            for (int j : sel) { ritual_credit += cands[j].ritual_float; }
            // Rite-of-Flame graveyard self-scaling: k copies cast this turn escalate (+0,+1,...,+k-1)
            // as each prior copy hits the graveyard; the flat per-cast stamp misses that triangular
            // term. Single self-scaling name in-deck, so count-by-flag == count-by-name.
            int gy_self = 0;
            for (int j : sel) { if (cands[j].def && cands[j].def->params.ritual_float_gy_self_bonus) { ++gy_self; } }
            ritual_credit += gy_self * (gy_self - 1) / 2;
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
            int acred = SameTurnAffinityGenericCredit(state, cands, sel);
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
            int rcred = SameTurnReducerGenericCredit(state, cands, sel);
            if (rcred > 0)
            {
                combined.generic             = std::max(0, combined.generic - rcred);
                noncreature_combined.generic = std::max(0, noncreature_combined.generic - rcred);
            }
        }
        const bool mana_ok = credited ? (eff.CanPay(combined) && eff_nc.CanPay(noncreature_combined))
                                       : (pool.CanPay(combined) && pool_noncreature.CanPay(noncreature_combined));
        // Filter/ramp-land color conversion the flat pool can't express -> real-payment fallback.
        if (!mana_ok && !(any_filter && SubsetPayableWithFilters(state, cands, sel))) { return; }
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

        int projected_atk = pending_atk + vial_haste_atk + noncreature_count * prowess_attackers;
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
        TurnSolver::Plan plan;
        plan.value          = total_eval;
        plan.wins_this_turn = wins;
        for (int j : sel) { plan.actions.push_back(cands[j]); }
        plans.push_back(std::move(plan));
    };

    // Odometer over per-card choices (0 = skip the card, v >= 1 selects
    // groups[g][v-1]), crossed with the 2^num_ind powerset of independent actions.
    // The empty combination (skip everything) is not a plan and is dropped.
    const int mana_bound = ManaPruneBound(pool, cands);   // total-mana prune (byte-identical); see helper
    std::vector<int> choice(num_groups, 0);
    std::vector<int> sel;   // reused across subset iterations (clear keeps capacity, avoids per-combo alloc)
    bool done = false;
    while (!done)
    {
        // Group-selection total mana cost (once per odometer position). If it already exceeds the
        // ramp-credited bound, every extension is unpayable, so skip the inner loop -- the combos
        // skipped are exactly those eval_and_push()'s CanPay would reject. Byte-identical, same order.
        int gcost = 0;
        for (int g = 0; g < num_groups; ++g)
        { if (choice[g] > 0) { gcost += cands[groups[g][choice[g] - 1]].cost.ManaValue(); } }
        // Over-splice group choices are likewise skipped (byte-identical: eval_and_push()'s
        // SubsetHasIllegalSplice rejects every extension; splice legality is fixed by the group choice).
        const bool splice_ok = !any_splice || !GroupChoiceOverSplices(state, cands, groups, choice);
        // Dragonstorm acceleration-prefix collapse (HEURISTIC, gated by accel_prefix_on/any_accel above;
        // mirrors Solve): reject a group choice whose cast accelerants are not a cheapest-first prefix.
        const bool accel_ok = !(accel_prefix_on && any_accel)
                           || !GroupChoiceNonPrefixAccel(state, cands, groups, group_hand_index, choice);
        if (gcost <= mana_bound && splice_ok && accel_ok)
        {
            for (int imask = 0; imask < (1 << num_ind); ++imask)
            {
                sel.clear();
                int icost = 0;
                for (int g = 0; g < num_groups; ++g)
                {
                    if (choice[g] > 0) { sel.push_back(groups[g][choice[g] - 1]); }
                }
                for (int b = 0; b < num_ind; ++b)
                {
                    if (imask & (1 << b)) { sel.push_back(independent[b]);
                                            icost += cands[independent[b]].cost.ManaValue(); }
                }
                if (!sel.empty() && gcost + icost <= mana_bound) { eval_and_push(sel); }
            }
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
                                  + "#" + std::to_string(act.sac_source_id)); break;
                case Action::Kind::DigDraw:  break;  // human-play only; not a plan.actions signature key
                case Action::Kind::PlayLand: break;  // never appears in plan.actions
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
            }
            std::sort(sub.begin(), sub.end());
            for (const std::string& x : sub) { sig += '#'; sig += x; }
            if (p.land_decided)            { sig += "|land="  + p.land_to_play; }
            if (!p.fetch_target.empty())   { sig += "|fetch=" + p.fetch_target; }
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

        // Candidate orderings to try. Dragonstorm (Hook 28) uses the TARGETED generator (cheapest-first
        // chain + Irencrag-before-finisher + Medallion-position + Desperate/Seething splice variants) --
        // O(k) principled orderings, and it covers the big go-off hands the k! cap below would skip. The
        // global A/B knob (MTG_SEARCH_ORDER on a non-Dragonstorm deck) keeps the full-permutation search.
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
        return s;
    };

    // Human play (the play GUI) enumerates one plan per distinct land NAME rather than per static
    // signature: the player chose a SPECIFIC land and expects that exact card played (not a
    // signature-equivalent representative), and a different-but-equivalent land must never read as
    // a reject. Gated on MTG_HUMAN_PLAY -> byte-identical for every autonomous goldfish/search run,
    // which keeps deduping by signature for enumeration economy.
    const bool s_human_play_lands = HumanPlayActive();
    std::vector<std::string>        land_names;   // representatives, in hand order
    std::unordered_set<std::string> seen_key;
    for (const Card& c : ap.hand)
    {
        if (c.m_impulse_no_land) { continue; }   // Apex-exiled land: castable as a SPELL only, not enumerable as a land play
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || !def->card.IsLand()) { continue; }
        const std::string key = s_human_play_lands ? c.m_name : land_sig(def->params);
        if (seen_key.insert(key).second) { land_names.push_back(c.m_name); }
    }

    // Greedy land the heuristic (AIEngine::TryPlayLand) would play from this hand.
    // Used ONLY as the last-resort ordering tiebreak below: when the search is
    // genuinely indifferent between land lines (equal win-turn AND equal first-turn
    // value), we default to the proven heuristic rather than to hand order. The
    // clairvoyant rollout often rates two land choices identically at the horizon,
    // and letting an arbitrary order decide picks a land that plays out marginally
    // worse than greedy in the realized game (the small fold-vs-greedy regressions).
    // Mirrors TryPlayLand's TH pre-pass + four-pass (untapped/tapped x multi/any).
    auto greedy_land_name = [&]() -> std::string
    {
        bool has_draw_until_nonland = false;
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && d->tmpl == CardTemplate::DrawUntilNonland) { has_draw_until_nonland = true; break; }
        }
        if (has_draw_until_nonland)
        {
            for (const Card& c : ap.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (d && d->card.IsLand() && d->params.no_max_hand_size) { return c.m_name; }
            }
        }
        for (int pass = 0; pass < 4; ++pass)
        {
            bool want_untapped = (pass < 2);
            bool want_multi    = (pass == 0 || pass == 2);
            for (const Card& c : ap.hand)
            {
                if (c.m_impulse_no_land) { continue; }   // Apex-exiled land: never played
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (!d || !d->card.IsLand()) { continue; }
                bool is_tapped = d->params.enters_tapped;
                bool is_multi  = d->params.produces.size() > 1;
                if (want_untapped == is_tapped) { continue; }
                if (want_multi && !is_multi)    { continue; }
                return c.m_name;
            }
        }
        return std::string();
    }();

    std::vector<TurnSolver::Plan> all;

    auto add_for_land = [&](const std::string& land_name, const std::string& fetch_target)
    {
        PROF_INC(gamestate_copies);
        GameState copy = state;
        if (!land_name.empty() && !PlayLandByName(copy, land_name, fetch_target)) { return; }

        // "Play this land, cast nothing" baseline (neutral value 0).
        TurnSolver::Plan idle;
        idle.value        = 0;
        idle.land_decided = true;
        idle.land_to_play = land_name;
        idle.fetch_target = fetch_target;
        all.push_back(std::move(idle));

        std::vector<TurnSolver::Plan> plans = EnumeratePlans(copy, is_pre_combat);
        for (TurnSolver::Plan& p : plans)
        {
            p.land_decided = true;
            p.land_to_play = land_name;
            p.fetch_target = fetch_target;
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
    static const bool s_color_blind_tiebreak = std::getenv("MTG_COLOR_BLIND_TIEBREAK") != nullptr;
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
    static const bool s_develop_tiebreak = std::getenv("MTG_NO_DEVELOP_TIEBREAK") == nullptr;

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

    TRACE("plans", "T%d EnumeratePlansWithLand -> %zu plans (lands=%zu, hand=%zu)",
          state.turn_number, all.size(), land_names.size(), ap.hand.size());
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

static TranspositionTable::Key BuildSimKey(const GameState& state, int depth, int max_turns,
                                           bool second_main)
{
    TranspositionTable::Key k;

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

        Fold(k, 0x4A00 + static_cast<uint64_t>(pi)); // sub-section: hand (ordered)
        Fold(k, static_cast<uint64_t>(p.hand.size()));
        for (const Card& c : p.hand)
        {
            Fold(k, c.m_name_hash);
            // A staged card's expiry changes when it can still be played, so two hands
            // with identical names but different staged expiries are different rollout
            // states. Folded ONLY when staged, so non-staging decks keep their exact
            // prior key (byte-identical results).
            if (c.m_is_staged) { Fold(k, static_cast<uint64_t>(c.m_staged_expiry)); }
        }

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

    Fold(k, 0xB1F1); // section tag: battlefield (ordered)
    Fold(k, static_cast<uint64_t>(state.battlefield.size()));
    for (const Permanent& perm : state.battlefield)
    {
        Fold(k, perm.card.m_name_hash);
        Fold(k, static_cast<uint64_t>(perm.controller_index));
        Fold(k, perm.tapped ? 1u : 0u);
        Fold(k, perm.entered_this_turn ? 1u : 0u);
        Fold(k, perm.is_animated ? 1u : 0u);
        Fold(k, static_cast<uint64_t>(perm.charge_counters));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_power_bonus)));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.temp_tough_bonus)));
        Fold(k, static_cast<uint64_t>(static_cast<int64_t>(perm.damage)));
        for (const Counter& ctr : perm.counters)
        {
            Fold(k, static_cast<uint64_t>(ctr.type));
            Fold(k, static_cast<uint64_t>(static_cast<int64_t>(ctr.count)));
        }
    }

    return k;
}

// Simulate from the current state (at the START of a pre-combat main phase,
// land already played) to game end. Uses SolveWithLookahead(depth) for
// pre-combat decisions. A post-combat (second) main is played only when
// second_main is set (greedy, via Solve) — see AIEngine::TakeTurn for why it is
// otherwise skipped. Returns the win turn, or max_turns+1 if not won in time.
static int SimulateToEndImpl(GameState& state, int depth, int max_turns,
                             SearchBudget* budget, int cutoff_turn,
                             bool second_main, TranspositionTable* tt)
{
    const bool trace_pl = g_trace_arm;   // only the outermost diagnostic playout prints
    g_trace_arm = false;
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
        if (budget && budget->Overrun()) { return max_turns + 1; }

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

        if (trace_pl)
        {
            std::cerr << "  [pl] >>> turn=" << state.turn_number << " hand_before=[";
            for (const Card& c : state.ActivePlayer().hand)
            { std::cerr << c.m_name << (c.m_is_staged ? "*" : "") << "; "; }
            std::cerr << "] lib_top=";
            const Player& tp = state.ActivePlayer();
            std::cerr << (tp.library.empty() ? std::string("(none)") : tp.library.front().m_name.str()) << "\n";
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
        SimulateAnimateLands(state);
        SimulateTapTokens(state);

        // Combat
        SimulateCombat(state);
        if (trace_pl)
        {
            std::cerr << "  [pl] turn=" << state.turn_number
                      << " opp " << life_before_pl << "->" << state.Opponent().life
                      << "  " << PlanDesc(pre_plan) << "  hand_after=[";
            for (const Card& c : state.ActivePlayer().hand)
            { std::cerr << c.m_name << (c.m_is_staged ? "*" : "") << "; "; }
            std::cerr << "]\n";
        }
        if (state.Opponent().life <= 0)
        {
            if (trace_pl) { std::cerr << "  [pl] WIN at turn " << state.turn_number << "\n"; }
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
            static const bool s_leaf_verify = std::getenv("MTG_LEAF_VERIFY") != nullptr;
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
    }

    int result = SimulateToEndImpl(state, depth, max_turns, budget, cutoff_turn, second_main, tt);

    if (tt != nullptr && result <= max_turns) { tt->Store(key, result); }
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
// content -- the cached line's draw-dependent breakpoint_actions stay valid) and
// caches ONLY genuine wins (win_turn <= max_turns): a winning line is cutoff-
// independent (pruning never removes a strictly-earlier win, and selection replaces
// only on strict improvement), whereas a no-win may be a branch-and-bound abort.
using FSLineCache = std::unordered_map<TranspositionTable::Key, TurnSolver::SearchLine,
                                       TranspositionTable::KeyHash>;

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
    if (budget && budget->Overrun()) { return { max_turns + 1, {} }; }
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
            if (beam_here && _beam_i++ >= g_esc_beam_width) { break; }   // value-guided beam (near-leaf only)
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
    // Mid-pass overrun: this pass has blown far past its budget estimate; bail out with a
    // no-win so FullSearchLine rolls back to the last completed pass (see SetOverrunLimit).
    if (budget && budget->Overrun())   { return { max_turns + 1, {} }; }
    if (depth <= 0)
    {
        ++g_fs_leaf_evals;   // count leaf evaluations (value-leaf or rollout) for the K-predictor's leaf-count model
        // Learned leaf VALUE (MTG_VALUE_MODEL): distil the deep search into an O(1) win-turn estimate
        // that REPLACES the horizon rollout -- the rollout is the weak, slow link (greedy play-out, not
        // searched). The model's Score is a fixed-point WIN TURN (x1000); round + clamp to the legal
        // window [this turn, loss]. nullptr / empty / flag-off -> the exact rollout below (byte-identical).
        // Hybrid: the caller forces the exact heuristic leaf (g_force_heuristic_leaf) on the re-run pass
        // when value-leaf committed too shallow; otherwise use the learned leaf as before.
        const MidGameEvaluator* vm = (!g_force_heuristic_leaf && UseValueModel()
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
        if (it != lc->end()) { return it->second; }
    }

    // No projected-`wins_this_turn` shortcut: lethality is decided by actually
    // simulating each plan below, so the committed line's win turn always matches
    // replaying it (the projection can over-count vs ApplyPlanDirect+SimulateCombat).
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);
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

    TurnSolver::SearchLine best;
    best.win_turn = max_turns + 1;
    int _beam_i = 0;
    for (const TurnSolver::Plan& p : pre)
    {
        // Value-guided escalation beam: expand only the top-W value-ranked plans (g_esc_beam_width), but only at
        // near-leaf nodes (beam_here); the top plies keep full exploration so the committed play is never pruned.
        // 0 = unlimited = byte-identical. pre is value-ordered above, so this keeps the best W lines.
        if (beam_here && _beam_i++ >= g_esc_beam_width) { break; }
        if (budget) { budget->Consume(1); }   // one interior node (plan applied)
        if (s_rollout_stats)
        {
            g_interior_nodes.fetch_add(1, std::memory_order_relaxed);
            if (g_force_heuristic_leaf) { g_interior_nodes_esc.fetch_add(1, std::memory_order_relaxed); }
        }
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);
        // A plan that kills the active player via its own on-cast triggers (Eidolon of
        // the Great Revel) or self-damage cannot win: those triggers go on top of the
        // spell and resolve BEFORE it (CR 603.3), so we die to them before our spell or
        // combat deals any damage. Skip the line entirely -- mirrors the baseline plan
        // guard (`self_damage >= ap.life`) so commit-the-line never commits a suicide
        // (burn gi=492: two Eidolons + an extra Goblin Guide = 8 self-damage at 6 life).
        if (s.ActivePlayer().life <= 0) { if (rec_vals) { node_vals.push_back(max_turns + 1); } continue; }
        SimulateAnimateLands(s);
        SimulateTapTokens(s);
        SimulateCombat(s);
        if (s.Opponent().life <= 0)  // win this turn -> floor
        {
            TurnSolver::Plan p_rec = p;
            p_rec.breakpoint_actions = std::move(bp);
            TurnSolver::SearchLine win = { state.turn_number, { { true, std::move(p_rec) } } };
            // A this-turn win is the earliest possible from here, so it is the final
            // optimal line for this node -- cache it (cutoff-independent).
            if (lc != nullptr) { lc->emplace(key, win); }
            return win;
        }

        TurnSolver::SearchLine tail =
            FSLineTail(s, depth - 1, max_turns, std::min(cutoff, best.win_turn), second_main, tt, lc, budget);
        if (rec_vals) { node_vals.push_back(tail.win_turn); }   // value-rank for the beam reorder
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
                if (lc != nullptr) { lc->emplace(key, best); }
                return best;
            }
        }
    }

    // Store the probe's per-plan value ranks for this fully-evaluated node (loop completed = every plan
    // scored), so the escalation's beam can reorder by them. Win-node early-returns above skip this (they
    // don't need reordering). Overwrites are harmless (depth is folded into the key, so no cross-pass clash).
    if (rec_vals && !node_vals.empty()) { (*g_probe_plan_vals)[key] = std::move(node_vals); }

    // Cache only a genuine win; a no-win (best.win_turn > max_turns) may be a
    // cutoff abort rather than a true dead end, so it is never stored (mirrors
    // SimulateToEnd / the leaf table).
    if (lc != nullptr && best.win_turn <= max_turns) { lc->emplace(key, best); }
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
    static const bool s_esc_jump_env = std::getenv("MTG_ESC_JUMP") != nullptr;
    const bool s_esc_jump = s_esc_jump_env && g_force_heuristic_leaf;

    for (int pass_depth = (depth >= 1 ? 1 : depth); pass_depth <= depth; ++pass_depth)
    {
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

    static const bool fd_trace = std::getenv("MTG_FD_TRACE") != nullptr;
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
                ApplyPlanDirect(copy, pp.plan, true);
                SimulateAnimateLands(copy);
                SimulateTapTokens(copy);
                SimulateCombat(copy);
            }
            else
            {
                ApplyPlanDirect(copy, pp.plan, false);
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
        bool enabled = std::getenv("MTG_HYBRID_STATS") != nullptr;
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
        // Budget-ADAPTIVE threshold (MTG_ESCALATION_GATE_T_LOW + _BUDGET_CUT, opt-in): use the base (higher,
        // conservative) `threshold` when the escalation still has budget headroom, and t_low (lower, more
        // aggressive skip) when the probe already spent most of it. Enabled only when T_LOW is set; otherwise
        // EffectiveThreshold == threshold (fixed, unchanged). See hinata-escalation-budget-restore memory.
        double t_low      = -1.0;
        double budget_cut = 0.5;
        bool   adaptive   = false;
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
            const char* tl = std::getenv("MTG_ESCALATION_GATE_T_LOW");
            if (tl && *tl) { try { t_low = std::stod(tl); adaptive = true; } catch (...) {} }
            const char* bc = std::getenv("MTG_ESCALATION_GATE_BUDGET_CUT");
            if (bc && *bc) { try { budget_cut = std::stod(bc); } catch (...) {} }
            enabled = true;
        }
        // Effective skip threshold for THIS escalation: adaptive lowers it (skip more) once the remaining
        // decision budget falls below budget_cut of its limit. Fixed == threshold when not adaptive.
        double EffectiveThreshold(const SearchBudget* b) const
        {
            if (!adaptive || b == nullptr || b->Unlimited()) { return threshold; }
            const double rf = static_cast<double>(b->Remaining()) / static_cast<double>(b->Limit());
            return (rf >= budget_cut) ? threshold : t_low;
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
    // MTG_ESC_RESTORE=f : ADD a fresh f*budget_ms to the escalation on top of the reserve (a partial restore;
    //                     THIS is the part that costs extra time -- keep it small).
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
    static const double s_esc_restore = []{ const char* e = std::getenv("MTG_ESC_RESTORE");
                                            return (e && *e) ? std::atof(e) : 0.0; }();
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
    static const bool s_esc_predict = std::getenv("MTG_ESC_PREDICT") != nullptr;
    static const bool s_esc_single_predict = std::getenv("MTG_ESC_SINGLE_PREDICT") != nullptr;
    static const bool s_esc_single_env     = std::getenv("MTG_ESC_SINGLE") != nullptr;
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
    static const bool s_esc_beam_env_set = std::getenv("MTG_ESC_BEAM") != nullptr;
    static const int s_esc_beam = []{ const char* e = std::getenv("MTG_ESC_BEAM");
                                      return (e && *e) ? std::atoi(e) : 0; }();
    // MTG_ESC_BEAM_LEAFDEPTH=D: apply the beam only to nodes within D plies of the leaf (the top plies keep full
    // exploration so the committed PLAY is never beamed out). Unset => INT_MAX => uniform beam (original).
    static const int s_esc_beam_leafdepth = []{ const char* e = std::getenv("MTG_ESC_BEAM_LEAFDEPTH");
                                                return (e && *e) ? std::atoi(e) : 2147483647; }();
    static const bool s_esc_beam_static = std::getenv("MTG_ESC_BEAM_STATIC") != nullptr;  // prune by static order
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
            if (g_escalation_gate.PNoOp(raw) > g_escalation_gate.EffectiveThreshold(budget))
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
        static const bool   s_fresh_frac_env_set = std::getenv("MTG_ESCALATION_FRESH_FRAC") != nullptr;
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
            const double frac = std::max(0.0, 1.0 - s_esc_split) + std::max(0.0, s_esc_restore);
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
        static const bool s_esc_predict_warm = std::getenv("MTG_ESC_PREDICT_WARM") != nullptr;
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
            // EMA weight for the amortized R update (MTG_ESC_PREDICT_RALPHA, default 0.4).
            static const double s_r_alpha = []{ const char* e = std::getenv("MTG_ESC_PREDICT_RALPHA");
                                                return (e && *e) ? std::atof(e) : 0.4; }();
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
            if (s_esc_predict_stats)
            {
                g_pred_n.fetch_add(1, std::memory_order_relaxed);
                g_pred_K[std::clamp(K, 0, 15)].fetch_add(1, std::memory_order_relaxed);
                g_pred_committed[std::clamp(hcommitted, 0, 15)].fetch_add(1, std::memory_order_relaxed);
                if (aborts > 0) { g_pred_fallback.fetch_add(1, std::memory_order_relaxed); }
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
                        // COST-CURVE probe (MTG_ESC_PREDICT_COSTCURVE): run a COLD single heuristic pass at
                        // each depth 1..committed and dump the actual cost so we can see the real per-depth
                        // cost vs the leaf-count model and the rollout-length-decay model. Expensive; only on
                        // the first few lossy cases. H = horizon turns from now (rollout-length scale).
                        if (std::getenv("MTG_ESC_PREDICT_COSTCURVE"))
                        {
                            const int H = max_turns - state.turn_number;
                            std::cerr << "    [costcurve] H=" << H << " (max_turns=" << max_turns
                                      << " turn=" << state.turn_number << ")\n";
                            for (int d = 1; d <= committed; ++d)
                            {
                                TranspositionTable cc_tt;
                                FSLineCache        cc_cache;
                                SearchBudget       cc_budget;   // unlimited: just count Used()
                                (void)FSLineWin(state, d, max_turns, max_turns + 1, second_main,
                                                &cc_tt, &cc_cache, &cc_budget);
                                long long cumL_d = 0;
                                for (int k = 1; k <= d; ++k) { cumL_d += std::max<long long>(0, g_probe_leaves[k]); }
                                std::cerr << "      d" << d << " cold_cost=" << cc_budget.Used()
                                          << " leaves(d)=" << g_probe_leaves[d]
                                          << " cumL=" << cumL_d
                                          << " probe_cost=" << g_probe_cost[d]
                                          << " H-d=" << (H - d) << "\n";
                            }
                        }
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
            // measured antilife 4-seed dLP=0.0000 at ~45% work, light decks dLP=0.0000 + faster). The env research
            // path (MTG_ESC_SINGLE) keeps the fixed-depth knobs for A/B: MTG_ESC_SINGLE_ABS=D pins an ABSOLUTE
            // target D; else committed-offset. Overrun-guarded with depth fallback (below): a pass that cannot
            // complete falls back a depth; if none fit, the value-leaf line is kept (hcommitted=0).
            static const int s_esc_single_abs = []{ const char* e = std::getenv("MTG_ESC_SINGLE_ABS");
                                                    return (e && *e) ? std::atoi(e) : 0; }();
            // `cap` = the CONVERGENCE cap (heuristic gains ~0 past it). Per-deck path (adopted): the profile's
            // escalation_cap. Env research path: MTG_ESC_SINGLE_ABS, else committed-offset. Never search deeper.
            const int cap = eff_single_deck
                          ? std::clamp(escalation_cap, 1, depth)
                          : (s_esc_single_abs > 0
                              ? std::clamp(s_esc_single_abs, 1, depth)
                              : std::clamp(committed - s_esc_single_off, 1, depth));
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
            //   2 = SPARSE-LADDER seed: a cheap shallow FSLineWin at MTG_ESC_SINGLE_SEED (default 2), reusing
            //       the SAME tt+cache (both keyed by (state, remaining-depth), so cross-depth reuse is sound).
            //       Its win turn is a REAL achievable bound (a genuine line at that depth). Tighter bound, but
            //       costs a shallow search. (Conceptually the same family as MTG_ESC_JUMP.)
            //   3 = ROLLOUT bound: a greedy playout win turn from the CURRENT state (real + achievable, cheapest
            //       to obtain; looser than (2) since the greedy policy is suboptimal).
            // Back-compat: MTG_ESC_SINGLE_WARM (with BOUND unset) selects mode 1.
            static const bool s_esc_single_warm  = std::getenv("MTG_ESC_SINGLE_WARM") != nullptr;
            static const int  s_esc_single_bound = []{ const char* e = std::getenv("MTG_ESC_SINGLE_BOUND");
                                                       return (e && *e) ? std::atoi(e) : -1; }();
            static const int  s_esc_single_seed  = []{ const char* e = std::getenv("MTG_ESC_SINGLE_SEED");
                                                       return (e && *e) ? std::atoi(e) : 2; }();
            const int bound_mode = (s_esc_single_bound >= 0) ? s_esc_single_bound : (s_esc_single_warm ? 1 : 0);
            int single_cut = max_turns + 1;                                     // 0: loose (cold)
            if (bound_mode == 1) { single_cut = old_wt; }                       // 1: value-leaf (optimistic)
            else if (bound_mode == 2)                                           // 2: sparse-ladder seed
            {
                const int sd = std::clamp(s_esc_single_seed, 1, std::max(1, target - 1));
                if (sd < target)
                {
                    SearchLine seed = FSLineWin(state, sd, max_turns, max_turns + 1, second_main,
                                                single_tt, &single_cache, esc_budget);
                    if (seed.win_turn <= max_turns) { single_cut = seed.win_turn; }   // real achievable bound
                }
            }
            else if (bound_mode == 3)                                           // 3: ONE deep rollout bound
            {
                // ONE heuristic rollout from the current state with STRUCTURED lookahead to
                // MTG_ESC_SINGLE_ROLLDEPTH (default = search depth => a deep, value-leaf-depth-ish playout that
                // follows good play rather than greedy). Its win turn is a REAL achievable bound at depth -- the
                // cheap incumbent a lone deep pass otherwise lacks, WITHOUT the ladder's shallow rework. This is
                // the user's "one rollout at the value-leaf's max depth" made concrete.
                static const int s_esc_single_rolldepth = []{ const char* e = std::getenv("MTG_ESC_SINGLE_ROLLDEPTH");
                                                              return (e && *e) ? std::atoi(e) : -1; }();
                const int rd = std::clamp(s_esc_single_rolldepth < 0 ? depth : s_esc_single_rolldepth, 0, depth);
                GameState rs = state;
                const int rwt = SimulateToEnd(std::move(rs), rd, max_turns, esc_budget,
                                              max_turns + 1, second_main, single_tt);
                if (rwt <= max_turns) { single_cut = rwt; }
            }
            // BUDGET-LIMITED single pass with DEPTH FALLBACK (MTG_ESC_SINGLE_FALLBACK): try `target`; if it
            // OVERRUNS the budget, retry one depth SHALLOWER (reusing the warmed tt/cache, so the d3 attempt's
            // exploration primes the d2 retry) -- the user's "single pass, budget-limited: fall back to d2 if d3
            // doesn't fit," down to d1. A depth that never fits keeps the value-leaf line (hcommitted=0). Off =>
            // legacy single behaviour (one attempt at target; abort => keep value-leaf).
            // Depth-fallback defaults ON for the adopted per-deck path (it IS the budget-limited fallback); the env
            // research path keeps it opt-in (MTG_ESC_SINGLE_FALLBACK) for back-compat A/B.
            static const bool s_esc_single_fallback = std::getenv("MTG_ESC_SINGLE_FALLBACK") != nullptr;
            const bool eff_fallback = eff_single_deck || s_esc_single_fallback;
            // UP-CLIMB (adopted path default ON; env research: MTG_ESC_SINGLE_CLIMB; off-switch MTG_ESC_SINGLE_NOCLIMB).
            // The frozen-R hint picks the START depth cheaply (no d1/d2 tax). After that pass, use its LIVE measured
            // cost x the probe's leaf-expansion ratio to test whether ONE deeper still fits the remaining budget; if
            // so, climb (capped at `cap`). This corrects a too-shallow (pessimistic-hint) target from THIS decision's
            // own measurement -- deterministic + adaptive, no cross-decision state. It only fires with real budget
            // headroom, so the common case (hint ~right => little budget left => estimate exceeds remaining) stays a
            // single pass. Combined with the existing overrun fallback (corrects a too-DEEP hint), the hint need only
            // be roughly right: R is a hint, the live estimate makes the final depth reliable.
            static const bool s_esc_single_climb   = std::getenv("MTG_ESC_SINGLE_CLIMB") != nullptr;
            static const bool s_esc_single_noclimb = std::getenv("MTG_ESC_SINGLE_NOCLIMB") != nullptr;
            const bool eff_climb = eff_single_deck ? !s_esc_single_noclimb : s_esc_single_climb;
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
                        // Telemetry (behind MTG_ESC_PREDICT_STATS): accumulate the measured cost-per-leaf so we can
                        // report the converged R to freeze into value_play.escalation_r.
                        if (s_esc_predict_stats)
                        {
                            g_esc_R_sum_milli.fetch_add(static_cast<long long>(Rsample * 1000.0),
                                                        std::memory_order_relaxed);
                            g_esc_R_n.fetch_add(1, std::memory_order_relaxed);
                        }
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
                                                                bool honest)
{
    RevealLogPause _rlp;  // planning: suppress scry/dig reveal logging (real play only)
    ShuffleEvalGuard _seg(true);  // decoupling instrument: planning shuffles use shuffle_salt_search
    // Full-strength honest teacher: decouple the rollout continuation's per-turn lookahead from the
    // real draw order (see g_honest_teacher). Only meaningful with a depth>0 rollout label.
    HonestTeacherGuard _htg(honest && rollout_label && rollout_depth > 0);
    EarliestWinReport report;
    report.turn     = state.turn_number;
    report.earliest = max_turns + 1;

    // Same candidate set the search ranks (cast ORDERINGS included iff MTG_SEARCH_ORDER /
    // MTG_UNPRUNED is set -- EnumeratePlansWithLand expands them there).
    std::vector<TurnSolver::Plan> pre = EnumeratePlansWithLand(state, true);

    // Deep enough to reach any win up to max_turns from this turn; NO cross-candidate B&B
    // (cutoff = max_turns+1) so each candidate gets its TRUE earliest win, not a pruned bound.
    int depth = max_turns - state.turn_number + 1;
    if (depth < 1) { depth = 1; }

    // Shared tail memo across candidates (downstream states transpose). Budget is never armed
    // for overrun here, so FSLineTail runs to completion -- this is an offline tool.
    TranspositionTable tt;
    FSLineCache        lc;
    SearchBudget       budget = SearchBudget::FromVirtualMs(1000000);

    for (const TurnSolver::Plan& p : pre)
    {
        GameState s = state;
        std::vector<Action> bp;
        ApplyPlanDirect(s, p, true, &bp);

        int wt;
        if (s.ActivePlayer().life <= 0)                 // self-lethal line -> never a win
        {
            wt = max_turns + 1;
        }
        else
        {
            SimulateAnimateLands(s);
            SimulateTapTokens(s);
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
                                       max_turns + 1, second_main, &tt);
                }
            }
            else
            {
                TurnSolver::SearchLine tail = FSLineTail(s, depth - 1, max_turns,
                                                         max_turns + 1, second_main,
                                                         &tt, &lc, &budget);
                wt = tail.win_turn;
            }
        }

        EarliestWinCandidate c;
        c.land           = p.land_to_play;
        c.fetch          = p.fetch_target;
        c.searched_order = p.searched_order;
        c.win_turn       = wt;

        // Effective cast order, mirroring apply_plan_actions: a searched plan casts in vector
        // order; otherwise the canonical clean-set order (stable-sort by CastRankOf). The
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
            { return CastRankOf(state, p.actions[x].card_name)
                   < CastRankOf(state, p.actions[y].card_name); });
        }
        for (int i : hand_casts) { c.cast_order.push_back(p.actions[i].card_name); }

        if (wt < report.earliest) { report.earliest = wt; }
        report.candidates.push_back(std::move(c));
    }
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
                    SimulateAnimateLands(s);
                    SimulateTapTokens(s);
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

    // Tempo bonus: the reshuffle averaging shuffles the TRUE library away, so its mean future has
    // NORMAL land density -- it is optimistic about mana and undervalues a land drop as screw-insurance.
    // In a truly land-light game (invisible to the averaging) skipping the drop is a permanent tempo
    // loss (gi11: defer scores 0.5t "better" yet durdles to T8; the land drop wins T5). Reward
    // developing mana: subtract round(bonus*K) from any land-drop plan before picking the min -- breaks
    // decisions the objective considers close without overriding a real win-turn difference > bonus.
    // The bonus (avg-turns) is the ARCHETYPE PROVIDER's call (Hook 22): GenericProvider = safe gated
    // default, AntiLifegain = aggressive/ungated, land-pitch decks protected by the mana-base gate +
    // PreferHoldLandDrop. MTG_NC_TEMPO(/_LANDS), when set, OVERRIDE the provider with a flat gated bonus
    // (the A/B sweep controls). Provider default 0 for unknown decks + env unset => byte-identical.
    static const bool   s_tempo_env_set = std::getenv("MTG_NC_TEMPO") != nullptr;
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

    static const bool s_nc_debug = std::getenv("MTG_NC_DEBUG") != nullptr;
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
    std::vector<Plan> candidates = EnumeratePlansWithLand(state, is_pre_combat);

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

        for (const Plan& plan : candidates)
        {
            // --- Overrun guard: finish if almost done, else abort + roll back ---
            if (gate && budget->Exhausted() && candidates_done > 0)
            {
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
                SimulateAnimateLands(copy);
                SimulateTapTokens(copy);

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

    // Diagnostic (MTG_TRACE_PLAYOUT_*): replay the committed plan and trace the
    // rollout's believed winning line. Inert unless the env seed/turn match.
    if (enforce_budget && s_tp_seed >= 0
        && static_cast<long long>(state.game_seed) == s_tp_seed
        && state.turn_number == s_tp_turn)
    {
        std::cerr << "[playout] seed=" << state.game_seed << " turn=" << state.turn_number
                  << " committed_win=" << committed_win
                  << " sub_depth=" << committed_sub_depth
                  << "  best_plan=" << PlanDesc(best_plan) << "\n";
        GameState dbg = state;
        int life0 = dbg.Opponent().life;
        ApplyPlanDirect(dbg, best_plan, is_pre_combat);
        SimulateAnimateLands(dbg);
        SimulateTapTokens(dbg);
        SimulateCombat(dbg);
        std::cerr << "  [pl] turn=" << dbg.turn_number << " opp " << life0 << "->"
                  << dbg.Opponent().life << " (committed turn)\n";
        if (dbg.Opponent().life > 0 && SimulateEndAndStartNextTurn(dbg))
        {
            g_trace_arm = true;
            int w = SimulateToEndImpl(dbg, committed_sub_depth, max_turns,
                                      nullptr, max_turns + 1, second_main, nullptr);
            g_trace_arm = false;
            std::cerr << "  [pl] rollout returned win=" << w << "\n";
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
// than LegalNotEnumerated -- the flat BuildPool/CanPay stores every dual as one any-color "wild".
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
        // Sort the derived token strings so plans differing only in cast order share a signature.
        // The label preserves the old " → "/" X="/" +N own"/" fetches " spacing (key already ends
        // with the operator, choice follows a single space) so displayed labels are unchanged.
        std::sort(toks.begin(), toks.end());
        std::string sig, label;
        for (size_t t = 0; t < toks.size(); ++t) { sig += "|" + toks[t]; label += (t?"; ":"") + toks[t]; }
        if (label.empty()) { label = LineSummaryOfPlan(p); }
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

    // Restricted-color gate: reject a line that needs a colored pip NO untapped source can produce
    // (see LineColorGateEnabled). `s` already has this line's land played, so its color is credited;
    // a mana rock cast in this line also contributes its colors (the greedy below casts rocks first).
    // Amount/contention is left to the greedy CanPay + the executor -- this only prunes the "no source
    // of that color at all" phantom the flat wild pool otherwise lets through (viewer issue #6).
    if (LineColorGateEnabled() && !pending.empty())
    {
        bool have[5];
        ComputeAvailableColors(s, have);
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
    // line (the same-turn ramp the enumerator's BuildPool does not credit). Order-
    // independent, so it doesn't penalise the human's click order.
    ManaPool avail = BuildPool(s);
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
        ManaPool p = BuildPool(s);
        bool spec = s.opponent_lost_life_this_turn;
        std::vector<std::string> reducers;   // reduces_spell_color of Medallions cast so far in-line
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
    // expressible in the flat BuildPool above, so a legal filter-fed line can leave casts "undone".
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
