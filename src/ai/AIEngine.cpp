#include "ValueArm.h"
#include "../core/EnvFlags.h"
#include "AIEngine.h"
#include "DecisionProviders.h"
#include "Dominance.h"   // ModelFeatureMask (stamped onto GameState::m_model_feat_mask)
#include "ManaPayment.h"
#include "LandPlay.h"
#include "Combat.h"
#include "EngineFlags.h"
#include "TurnSolver.h"
#include "TranspositionTable.h"
#include "SearchBudget.h"
#include "Profiler.h"
#include "../cards/CardDatabase.h"
#include "../core/GameEngine.h"
#include "../core/EffectHandler.h"   // PushCastTriggers (real-stack cascade/demonstrate)
#include "../core/SpellEffects.h"
#include "../core/RolloutTouch.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <mutex>
#include <fstream>
#include <map>
#include <stdexcept>

// Non-convergence detector gate, read once. When set (MTG_FLAG_NONCONV in the
// environment), TakeTurn checks each committed decision and prints a [nonconv]
// record whenever a later turn's verified win turn exceeds one proved earlier.
static const bool s_flag_nonconv = EnvOn("MTG_FLAG_NONCONV");

// #10 cast-order side-channel: reorder a committed plan's non-sacrifice hand casts to the
// human's pinned name order and flag searched_order so ApplyPlanDirect executes them in vector
// order (no CastOrderRank re-sort). Only the non-sac CastFromHand actions move -- sac casts,
// graveyard casts, vial activations and the land keep their positions (they run in separate
// canonical loops regardless). Names in `order` are matched greedily (duplicate copies match in
// listed order); any non-sac cast NOT named in `order` keeps its original relative position,
// appended after the pinned ones. Empty `order` (the common / no-reorder case) is a no-op, so
// existing references -- which pass no --cast-order -- stay byte-identical.
static void ReorderPlanCasts(TurnSolver::Plan& plan, const std::vector<std::string>& order)
{
    if (order.empty()) { return; }
    // Positions in plan.actions that hold a reorderable (non-sac hand) cast.
    std::vector<size_t> slots;
    for (size_t i = 0; i < plan.actions.size(); ++i)
    {
        const auto& a = plan.actions[i];
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
        { slots.push_back(i); }
    }
    if (slots.size() < 2) { plan.searched_order = true; return; }  // nothing to reorder, but honour order
    // Greedily pick, for each name in `order`, the first not-yet-used slot whose card matches.
    std::vector<size_t> remaining = slots;   // slot positions still to place
    std::vector<Action> seq;                 // reordered actions
    for (const std::string& name : order)
    {
        for (auto it = remaining.begin(); it != remaining.end(); ++it)
        {
            if (plan.actions[*it].card_name == name)
            { seq.push_back(plan.actions[*it]); remaining.erase(it); break; }
        }
    }
    // Any non-sac casts the human didn't name keep their original relative order, after the pinned ones.
    for (size_t pos : remaining) { seq.push_back(plan.actions[pos]); }
    // Write the reordered actions back into the same slot positions.
    for (size_t k = 0; k < slots.size(); ++k) { plan.actions[slots[k]] = seq[k]; }
    plan.searched_order = true;
}

// Route pre-combat (and second-main) decisions through the full-depth
// commit-the-line search instead of SolveWithLookahead, so "depth N" means fully
// searching N complete turns. This is now the DEFAULT engine (validated by the
// overnight A/B: better-or-equal on every case, 0 regressions). Set MTG_LEGACY_SEARCH
// to opt back into the old SolveWithLookahead baseline -- the held-out reference kept
// reproducible for future A/Bs. (The old MTG_FULL_DEPTH opt-in is gone; setting it is
// harmless as full depth is the default now.)
static const bool s_full_depth = !EnvOn("MTG_LEGACY_SEARCH");

// Commit-the-line fidelity oracle (MTG_FD_ORACLE): when a recomputed line's searched
// win exceeds an earlier line's, the committed line we just replayed did NOT realise
// its predicted win — a rollout/real-execution divergence. Flag the seed + turn so it
// can be traced. Only meaningful with s_full_depth.
static const bool s_fd_oracle = EnvOn("MTG_FD_ORACLE");

// Breakpoint lockstep trace: BpTraceEnabled() in EngineFlags.h (shared with TurnSolver).

// Enumerate-all-earliest-wins rule-miner (MTG_DUMP_EWINS): at each REAL pre-combat main,
// emit one JSON line ({"ewins":...}) scoring every candidate top-level play by the earliest
// full-game win it leads to (TurnSolver::EnumerateEarliestWins). Feeds the analyzer's
// heuristic-grounding pattern analysis (scripts/analyze_earliest_wins.py). EXPENSIVE -- run
// single-threaded on a few games. MTG_DUMP_EWINS_TURN limits it to one decision turn (default
// 1 = opening only, to bound cost; 0 = every turn). Set MTG_SEARCH_ORDER=1 to also expand
// cast orderings. Inert (zero overhead) unless MTG_DUMP_EWINS is set.
static const bool s_dump_ewins = EnvOn("MTG_DUMP_EWINS");
static const int  s_dump_ewins_turn = []{
    const char* e = std::getenv("MTG_DUMP_EWINS_TURN"); return e ? std::atoi(e) : 1;
}();

// Learned-eval LABEL GENERATION (MTG_DUMP_EVAL_ROWS=<file>, MTG_EVAL_ROWS_K=<K>): at each REAL
// pre-combat main, run EnumerateEarliestWins K times under RESHUFFLED remaining libraries and emit
// one row per candidate plan for the offline eval-model trainer. This de-clairvoys the label: the
// search reads the real library order (clairvoyant), so averaging its earliest-win over K independent
// future draw orders marginalises the draw out -- the plan's EXPECTED win turn, which is what a
// non-clairvoyant policy should predict (the same "blind rollout" trick the mulligan trainer uses).
// Features are the NON-CLAIRVOYANT ExtractMidGameFeatures of the ORIGINAL state (reshuffle-invariant).
// Row: "<label> <feat0> ... <featN> <seed> <turn>". Losses count as max_turns+1 (the primary metric).
// EXPENSIVE (K x full search per decision) -- run on a bounded set of games. Inert unless set.
// See docs/design/learned-d0-policy.md.
static const char* s_eval_rows_path = std::getenv("MTG_DUMP_EVAL_ROWS");
// Leaf VALUE label dump (MTG_DUMP_VALUE_ROWS=<file>): one row per POSITION whose label is the
// de-clairvoyed DEEP-SEARCH win turn from here (avg over K reshuffles of EnumerateEarliestWins'
// report.earliest = the best achievable win turn). Features are the POSITION only (null plan). This
// is the training target for the value model that replaces the search's horizon rollout with the
// searched result. Shares the same K-reshuffle loop as the eval dump. See learned-d0-policy.md.
static const char* s_value_rows_path = std::getenv("MTG_DUMP_VALUE_ROWS");
// MTG_VALUE_LABEL_BNB: DEFAULT ON; =0 disables. The value row consumes ONLY report.earliest (the MIN
// over candidates), so carrying the running incumbent as the cross-candidate cutoff is lossless for
// that label while cutting the work -- the same bound FSLineTail already carries between siblings.
// Under the horizon ladder it also stops the ladder at the first winning pass.
//
// This flag shipped OFF for a day because it measured as producing EARLIER labels than the unpruned
// arm (6->4, 7->6.33) -- impossible for a sound search, so it was quarantined as evidence of a latent
// bug rather than adopted. It was evidence of exactly that, but the bug was in the UNPRUNED arm: the
// labeller called FSLineWin outside the ladder its first-win shortcut requires, so it returned *a*
// win rather than *the earliest*. B&B's tight cutoff narrowed the horizon and accidentally restored
// the premise, which is why the "pruned" arm looked better -- it was the less broken one.
//
// With MTG_LABEL_LADDER (default ON) the premise holds in both arms and B&B is measured LOSSLESS:
// identical labels on all 7 suite decks, 0.99x-1.72x faster (test/label_bnb_check.sh).
// See docs/design/label-horizon-ladder.md.
static const bool  s_value_label_bnb = EnvOn("MTG_VALUE_LABEL_BNB", true);
static const int   s_eval_rows_k    = []{ const char* e = std::getenv("MTG_EVAL_ROWS_K");
                                          int v = (e && *e) ? std::atoi(e) : 8; return v < 1 ? 1 : v; }();
// MTG_EVAL_ROWS_ROLLOUT: label candidates by a non-clairvoyant greedy d0 rollout instead of the
// clairvoyant earliest-win search (stops the oracle over-crediting durdle lines a real d0 can't
// realise). Affects the label from EnumerateEarliestWins -> use for EVAL-row dumps only, NOT value
// dumps (the value model wants the searched label). See learned-d0-policy.md (antilife d0).
static const bool  s_eval_rows_rollout = EnvOn("MTG_EVAL_ROWS_ROLLOUT");
// MTG_EVAL_ROLLOUT_DEPTH: per-turn lookahead of the rollout policy (default 0 = greedy d0 = imitate
// baseline; >0 distils a stronger policy, for weak-baseline decks like hinata). See learned-d0-policy.md.
static const int   s_eval_rollout_depth = []{ const char* e = std::getenv("MTG_EVAL_ROLLOUT_DEPTH");
                                              return (e && *e) ? std::atoi(e) : 0; }();
// MTG_EVAL_ROWS_HONEST: with MTG_EVAL_ROLLOUT_DEPTH>0, make the rollout continuation a full-strength
// NON-clairvoyant teacher -- its per-turn lookahead plans against a reshuffled unseen library and
// resolves against the true order (see g_honest_teacher). Without this, rollout_depth>0 is a
// clairvoyant deep search (reads the real future) and is WORSE than greedy. See learned-d0-policy.md.
static const bool  s_eval_rows_honest = EnvOn("MTG_EVAL_ROWS_HONEST");
// MTG_SEARCHED_DISCARD: make the REAL cleanup discard a lookahead search (roll out each candidate
// discard, keep the one that preserves the earliest clairvoyant win; heuristic breaks ties) instead
// of the highest-MV heuristic. DEFAULT OFF (heuristic) => byte-identical. Measured (smoke 1001):
// neutral on every deck except Treasure Hunt, which got slightly WORSE (d3 +0.007, d5 +0.013 avg) --
// a train/serve mismatch (rollouts assume heuristic discards later, the real game searches them) plus
// clairvoyance, at real compute cost -- so it ships OFF pending a reproduced combo-discard win (the
// Dragonstorm "don't pitch Apex/Dragonstorm" case is absent from seed 1001). See ChooseDiscard.
// The cleanup discard is SEARCHED by default (MTG_SEARCHED_DISCARD=0 restores the pure heuristic).
// It shipped off for a year because it measured worse; both reasons turned out to be defects in how
// candidates were EVALUATED, not in searching them -- a trial state rolled out from mid-cleanup
// (fixed: RolloutWinTurnFrom / ResumeAt::Cleanup) and a committed line left stale by the deviation
// (fixed: s_discard_reline). With both fixed it is monotone-better: held-out overnight 39 games
// faster / 0 slower. See docs/design/searched-cleanup-discard.md.
static const bool  s_searched_discard = EnvOn("MTG_SEARCHED_DISCARD", true);
// MTG_REFUTED_FOLLOW -- ADOPTED DEFAULT ON 2026-09-03 (USER: "We should turn it on if it
// improves performance (and has no regressions)"); `=0` restores per-turn re-search. USER
// design, same day: "Once we have searched fully to the end of max_turn we should stop...
// choosing a line that seems the best and following it out". When a top-level search covers
// the FULL remaining horizon with zero truncation events (TurnSolver::TruncEvents() delta) and
// finds no win, the game is PROVEN unwinnable -- later turns' searches explore a subset of the
// refuted space, so re-proving the doom every turn is pure waste (historically the slowest
// games are no-wins: the five-hour TH game was 100% no-win leaf lookups). The engine commits
// the WHOLE best-graded lost line, follows it out, and answers uncovered phases with the
// greedy plan; rollouts untouched. Outcome-identical by construction AND measured: 0 outcome
// moves in 2000 paired games (th 1000 / hinata 500 / mirrorwing 500, seed 5500001), identical
// averages; unwon games are <4% of games but were 19-46% of deck wall (indicative contended
// wall: hinata -19% / th -33% / mw -46%). Unwon-game digests move (a followed line differs in
// actions from a re-searched one) => GT rebaselined at adoption. Also bounds DEEP (d8 b0)
// instrument runs: refutation fires as early as the horizon allows.
static const bool  s_refuted_follow    = EnvOn("MTG_REFUTED_FOLLOW", true);
// Drop the committed line when the searched discard deviates from the heuristic pick (the line was
// searched assuming the heuristic shed). MTG_DISCARD_RELINE=0 keeps replaying the stale line.
static const bool  s_discard_reline    = EnvOn("MTG_DISCARD_RELINE", true);
// MTG_DISCARD_NODE (docs/design/searched-discard-as-search-node.md): the probe is RETIRED -- the
// cleanup shed is decided IN-SEARCH (Plan::discard_choice, replayed by the lockstep pin above) or
// by the provider's top pick; the out-of-band trial games never run. DEFAULT ON (user, 2026-08-06:
// the probe is an oracle replacing search judgment -- neither of the two sanctioned roles; its
// measured value, hinata +0.005..0.010 / antilife +0.007..0.008 on train seeds, is accepted as
// the price of removing the class). =0 is the exact legacy hatch (probe + reline restored).
static const bool  s_discard_node      = EnvOn("MTG_DISCARD_NODE", true);
// MTG_SEARCHED_VIAL: the Aether Vial upkeep charge is a real BRANCH (charge / hold), not a rule.
// Holding at the current count keeps this turn's free deploy of an MV-k creature; charging trades it
// for an MV-(k+1) deploy next turn. The heuristic (WantVialCharge: hold while a creature of the
// current MV is in hand, else climb) is a good default and an excellent tie-break, but it cannot see
// which side actually wins the game -- so it is now the DEFAULT + TIE-BREAK of a searched decision,
// exactly like the cleanup discard. MTG_SEARCHED_VIAL=0 restores the pure heuristic.
// The trial resumes at ResumeAt::UpkeepTail (the charge is mid-upkeep -- resuming at Draw would
// skip this turn's upkeep tokens); vials AFTER this one in the same loop are charged on the
// heuristic first, or the trial would drop their counters entirely.
// DEFAULT FLIPPED TO OFF 2026-08-30 alongside MTG_VIAL_AXIS (see EngineFlags.h for the measurement
// and the user direction). This probe was already dead on the shipped path -- the axis owned the
// decision and the design doc's follow-up marks the probe for deletion -- so leaving it default-ON
// while the axis went default-OFF would have silently REVIVED a retired out-of-band probe as the
// shipping decider, which is not what was measured. The measured configuration is both OFF: the
// pure hand-aware heuristic.
static const bool  s_searched_vial     = EnvOn("MTG_SEARCHED_VIAL", false);
// MTG_DIVERGENCE_LOG=<file>: DIAGNOSTIC (diagnosis only, no play change). On the search-driven path,
// at each real pre-combat main decision, ALSO compute the greedy d0 plan (TurnSolver::Solve) for the
// SAME untouched state and append one JSONL record {seed,turn,diverge,search_land,search,greedy,feat[]}.
// A state where the search and greedy plans differ is one the greedy rollout policy would misplay --
// the raw material for classifying the d0/rollout gap as rule-shaped (state-determined) vs lookahead-
// bound (draw-dependent). The game CONTINUES on the search plan (state is untouched here). Features are
// the non-clairvoyant ExtractMidGameFeatures for clustering. Run SINGLE-THREADED for readable output.
// Inert unless set. See docs/design/dragonstorm-d0-divergence-digest.md.
static const char* s_divergence_log = std::getenv("MTG_DIVERGENCE_LOG");

// Positions dropped because their label search was cut short (see EarliestWinReport::truncated).
// A bounded run that silently emits fewer rows reads as "covered everything" when it did not, so
// this is reported at exit whenever it is non-zero -- the repo's no-silent-caps rule.

namespace execgreedy
{
inline bool Enabled() { static const bool v = EnvOn("MTG_M2_YIELD_STATS"); return v; }
inline std::atomic<unsigned long long> g_exec[12] = {};
inline std::atomic<unsigned long long> g_rollout{0}, g_bp{0};
// CAUSE split of the executor breakpoint fallback (g_bp), because the two kinds differ in what
// they mean: BASE (the committed plan carries no searched continuation, bp_choice < 0) is
// realized-equals-scored -- the scoring rollout ran the SAME greedy Solve at the same state
// through the twin applier; MISMATCH (a searched continuation existed but the executor could
// not replay it: bp_seen counting drifted or bp_choice overran the re-enumerated list) is a
// realized-vs-scored DIVERGENCE and must read zero.
inline std::atomic<unsigned long long> g_bp_base{0}, g_bp_mismatch{0};
inline void Record(int depth, bool in_rollout)
{
    if (!Enabled()) { return; }
    if (depth < 0) { g_bp.fetch_add(1, std::memory_order_relaxed); return; }   // executor BREAKPOINT fallback
    if (in_rollout) { g_rollout.fetch_add(1, std::memory_order_relaxed); return; }
    if (depth >= 0 && depth < 12) { g_exec[depth].fetch_add(1, std::memory_order_relaxed); }
}
inline void RecordBpCause(bool had_choice)
{
    if (!Enabled()) { return; }
    (had_choice ? g_bp_mismatch : g_bp_base).fetch_add(1, std::memory_order_relaxed);
}
struct Dumper
{
    ~Dumper()
    {
        if (!Enabled()) { return; }
        std::fprintf(stderr, "=== EXECUTOR GREEDY Solve(): in-rollout=%llu breakpoint-fallback=%llu"
                     " (base=%llu MISMATCH=%llu)"
                     " | REAL main-phase decisions by depth:", g_rollout.load(), g_bp.load(),
                     g_bp_base.load(), g_bp_mismatch.load());
        bool any = false;
        for (int i = 0; i < 12; ++i)
        {
            const unsigned long long c = g_exec[i].load();
            if (c) { std::fprintf(stderr, "  d%d=%llu", i, c); any = true; }
        }
        if (!any) { std::fprintf(stderr, "  NONE"); }
        std::fprintf(stderr, "  ===\n");
    }
};
inline Dumper g_dumper;
}

static std::atomic<long long> g_label_positions_truncated{0};
static std::atomic<long long> g_label_positions_total{0};
struct LabelTruncationReport
{
    ~LabelTruncationReport()
    {
        const long long t = g_label_positions_truncated.load();
        if (t <= 0) { return; }
        const long long n = g_label_positions_total.load();
        std::fprintf(stderr,
            "[label] DROPPED %lld of %lld positions (%.1f%%): label search hit the budget ceiling "
            "(MTG_VALUE_LABEL_BUDGET_MS). Their true win turn is unknown, so no row was written -- "
            "raise the budget to keep them, or accept the gap.\n",
            t, n, n ? (100.0 * static_cast<double>(t) / static_cast<double>(n)) : 0.0);
    }
};
static LabelTruncationReport g_label_trunc_report;

static void EmitEvalRows(const GameState& state, int max_turns, bool second_main)
{
    g_label_positions_total.fetch_add(1, std::memory_order_relaxed);
    // Per-candidate accumulator, keyed by a canonical plan string (stable across reshuffles).
    struct Acc { long long sum = 0; int count = 0; std::vector<std::string> casts, sac; std::string land; };
    std::map<std::string, Acc> acc;
    long long earliest_sum = 0; int earliest_n = 0;   // for the position VALUE label (avg report.earliest)
    for (int k = 0; k < s_eval_rows_k; ++k)
    {
        GameState s = state;
        // Reshuffle the REMAINING library so the label averages over future draw orders (de-clairvoy).
        // Deterministic per (seed, turn, k); k==0 also reshuffles, so the mean is over K independent
        // futures. EnumerateEarliestWins itself sets g_shuffle_eval, so its internal shuffles decouple.
        const uint64_t rs = state.game_seed
                          + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(k) + 1)
                          + 1000003ULL * static_cast<uint64_t>(state.turn_number);
        s.ActivePlayer().library.Shuffle(rs);
        // Honest-teacher per-turn reshuffle (g_honest_teacher) folds shuffle_salt_search; vary it per
        // k so the K outer samples draw independent decoupled futures (else all k share one future).
        // Guarded to honest mode: leaves the non-honest rollout/searched dump path byte-unchanged.
        if (s_eval_rows_honest) { s.shuffle_salt_search = rs; }
        // B&B only when the per-candidate numbers are genuinely unused: value rows read
        // report.earliest alone, eval rows read every candidate. With BOTH dumps on, the eval rows
        // win and we pay the unpruned price -- a bounded candidate silently mislabels its row.
        const bool earliest_only = s_value_label_bnb && s_value_rows_path && !s_eval_rows_path;
        const TurnSolver::EarliestWinReport rep =
            TurnSolver::EnumerateEarliestWins(s, max_turns, second_main, s_eval_rows_rollout,
                                              s_eval_rollout_depth, s_eval_rows_honest,
                                              earliest_only);
        // TRUNCATED => DISCARD, never salvage. A budget-cut search reports max_turns+1, which is
        // byte-identical to "this position is unwinnable" -- so keeping the number would teach the
        // model that a position we could not AFFORD to solve is a position we cannot WIN from, and
        // that lie is worst exactly on the hardest positions, which are the ones worth learning.
        // Dropping the whole position (all K samples) rather than the offending sample keeps the
        // average over an unbiased K: an expensive position tends to truncate on most of its
        // samples, so averaging the survivors would quietly re-introduce the same bias.
        if (rep.truncated) { g_label_positions_truncated.fetch_add(1, std::memory_order_relaxed); return; }
        const int e = (rep.earliest > 0 && rep.earliest <= max_turns) ? rep.earliest : (max_turns + 1);
        earliest_sum += e; ++earliest_n;
        // Belt-and-braces against the one way B&B could corrupt training data: under B&B a losing
        // candidate's win_turn is an upper bound, so accumulating it would write a wrong eval label.
        // The gate above already excludes that combination; this makes it structurally impossible
        // rather than a property of a condition written two screens away.
        if (rep.bounded_candidates) { continue; }
        for (const TurnSolver::EarliestWinCandidate& c : rep.candidates)
        {
            std::string key = c.land + "|" + c.fetch + "|";
            for (const std::string& n : c.cast_order) { key += n; key += ","; }
            key += "#";
            for (const std::string& n : c.sac_casts)  { key += n; key += ","; }
            Acc& a = acc[key];
            const int wt = (c.win_turn > 0 && c.win_turn <= max_turns) ? c.win_turn : (max_turns + 1);
            a.sum += wt; ++a.count;
            if (a.count == 1) { a.casts = c.cast_order; a.sac = c.sac_casts; a.land = c.land; }
        }
    }

    static std::mutex s_mtx;
    std::lock_guard<std::mutex> lk(s_mtx);

    // Position VALUE row: features are the board only (null plan), label = de-clairvoyed best win turn.
    if (s_value_rows_path && earliest_n > 0)
    {
        static std::ofstream v_out(s_value_rows_path, std::ios::app);
        static bool v_header = false;
        if (v_out.good())
        {
            if (!v_header)
            {
                v_out << "# label";
                for (int i = 0; i < static_cast<int>(MidGameFeature::Count); ++i)
                { v_out << ' ' << MidGameFeatureName(static_cast<MidGameFeature>(i)); }
                v_out << " seed turn\n";
                v_header = true;
            }
            const std::vector<int> feat = ExtractMidGameFeatures(state, MidGamePlanSummary{});
            v_out << (static_cast<double>(earliest_sum) / earliest_n);
            for (int vv : feat) { v_out << ' ' << vv; }
            v_out << ' ' << state.game_seed << ' ' << state.turn_number << '\n';
            v_out.flush();
        }
    }

    if (!s_eval_rows_path || acc.empty()) { return; }

    // Per-candidate eval rows (features reshuffle-invariant -> from the ORIGINAL state).
    static std::ofstream s_out(s_eval_rows_path, std::ios::app);
    static bool          s_header = false;
    if (!s_out.good()) { return; }
    if (!s_header)
    {
        s_out << "# label";
        for (int i = 0; i < static_cast<int>(MidGameFeature::Count); ++i)
        { s_out << ' ' << MidGameFeatureName(static_cast<MidGameFeature>(i)); }
        s_out << " seed turn\n";
        s_header = true;
    }
    for (const auto& kv : acc)
    {
        const Acc& a = kv.second;
        std::vector<std::string> names = a.casts;
        names.insert(names.end(), a.sac.begin(), a.sac.end());
        MidGamePlanSummary sum  = SummarizePlanByNames(names, !a.land.empty());
        sum.baseline_eval       = TurnSolver::PlanBaselineEval(state, names);  // lockstep w/ the ranking seam
        const std::vector<int>   feat = ExtractMidGameFeatures(state, sum);
        s_out << (static_cast<double>(a.sum) / a.count);
        for (int v : feat) { s_out << ' ' << v; }
        s_out << ' ' << state.game_seed << ' ' << state.turn_number << '\n';
    }
    s_out.flush();
}

// Trajectory probe: when MTG_NONCONV_TRACE_SEED matches a game's seed, dump every
// real pre-combat decision (turn, committed_win, opp life/creatures, hand, plan).
static const char*     s_trace_seed_env = std::getenv("MTG_NONCONV_TRACE_SEED");
static const long long s_trace_seed     = s_trace_seed_env ? std::atoll(s_trace_seed_env) : -1;

static double ComputeHandScore(const std::vector<Card>& hand,
    const std::map<std::string, std::vector<double>>& card_scores)
{
    std::map<std::string, int> counts;
    for (const Card& c : hand) { ++counts[c.m_name]; }

    double score = 0.0;
    for (const auto& kv : counts)
    {
        auto it = card_scores.find(kv.first);
        if (it == card_scores.end()) { continue; }
        const std::vector<double>& marginals = it->second;
        int copies = std::min(kv.second, static_cast<int>(marginals.size()));
        // Clamp to zero: negative scores arise from selection-bias confounds
        // (e.g. Aether Vial looks slower because Vial hands have fewer creatures
        // on average), not from the card being a liability in the opening hand.
        // We only want to score hands DOWN for lacking good cards, not UP for
        // having support cards that test negatively in goldfishing.
        for (int k = 0; k < copies; ++k) { score += std::max(0.0, marginals[k]); }
    }
    return score;
}

AIEngine::AIEngine(MulliganProfile profile, int lookahead_depth, int budget_ms)
    : m_profile(std::move(profile)), m_lookahead_depth(lookahead_depth), m_budget_ms(budget_ms)
{
    // Run-level: searched play (depth > 0) enables cantrip-first ordering in BOTH the searched
    // plans and the rollout leaf policy. See TurnSolver::SetSearchedPlay.
    TurnSolver::SetSearchedPlay(lookahead_depth > 0);
}

void AIEngine::OnGameEnd(const GameState& state, int win_turn)
{
    if (!s_fd_oracle) { return; }
    // win_turn <= 0 means the game ended without a win (loss / timeout).
    const int realized = win_turn > 0 ? win_turn : m_max_turns + 1;
    if (m_fd_best_win <= m_max_turns && realized > m_fd_best_win)
    {
        std::cerr << "[fd-diverge] seed=" << state.game_seed
                  << " realized_win=" << realized
                  << " predicted_win=" << m_fd_best_win
                  << " proven_at_turn=" << m_fd_best_turn
                  // SIMULATED vs PREDICTED: leaf_est == predicted_win means the "verified" win was the
                  // value leaf's guess landing inside the horizon, which fd_verified cannot tell apart
                  // from a real simulated win. "none" => the leaf claimed no win, so the divergence is a
                  // genuine commit-the-line replay failure.
                  << " leaf_est="
                  << (m_fd_best_leafest == LLONG_MAX ? std::string("none")
                                                     : std::to_string(m_fd_best_leafest))
                  << (m_fd_best_leafest == static_cast<long long>(m_fd_best_win)
                          ? "  [WIN WAS A LEAF ESTIMATE, NOT SIMULATED]" : "")
                  << "\n";
    }
}

// ============================================================
// Mulligan
// ============================================================

void AIEngine::HandleMulligan(GameState& state, int max_turns)
{
    Player& ap = state.ActivePlayer();

    // Stamp this engine's required combo pieces onto the state so the search rollout's
    // shared cleanup-discard selector protects the same pieces ChooseDiscard does. The
    // pointer is non-owning (m_profile outlives the game) and propagates through every
    // deep copy / rollout trial (each a copy of this live state). See GameState::m_required_pieces.
    state.m_required_pieces = &m_profile.required_pieces;
    state.m_discard_protect = m_profile.discard_protect;
    // ... and the deck's learned per-card marginals, same non-owning / deep-copy propagation, so a
    // provider heuristic can consult them at a decision the profile itself does not make. Read
    // GameState::m_card_scores' comment first: these are OPENING-HAND marginals, castability-
    // confounded, and are not a drop-in mid-game value.
    state.m_card_scores     = m_profile.card_scores.empty() ? nullptr : &m_profile.card_scores;

    // Attach the deck's learned mid-game play evaluator (nothing to do with the mulligan itself --
    // this is just the first per-game hook that has BOTH the live state and the profile). Non-owning
    // (m_profile outlives the game); propagates through every deep copy. Presence + MTG_EVAL_MODEL
    // gate its actual use in TurnSolver::Solve. See GameState::m_evaluator.
    state.m_evaluator = m_profile.eval_model.empty() ? nullptr : &m_profile.eval_model;
    // ... and the deck's learned leaf value model (replaces the search's horizon rollout when
    // MTG_VALUE_MODEL is set). Same non-owning / deep-copy propagation. See GameState::m_value_model.
    state.m_value_model = m_profile.value_model.empty() ? nullptr : &m_profile.value_model;
    // Fold both models' branchable-feature masks once, here, where they are attached (see
    // GameState::m_model_feat_mask -- deriving it on demand from the pointers is an ABA hazard).
    state.m_model_feat_mask = dominance::ModelFeatureMask(state.m_evaluator)
                            | dominance::ModelFeatureMask(state.m_value_model);

    ap.library.DrawN(7, ap.hand);

    // New game: reset the per-game non-convergence baseline.
    m_nonconv_best_win  = max_turns + 1;
    m_nonconv_best_turn = 0;

    // New game: drop any committed full-depth line from a previous game.
    m_committed_line.clear();
    m_refuted_follow = false;
    m_discard_choice_pin = -1;
    m_vial_choice_pin    = -1;
    m_atk_release_pin    = -1;
    m_fd_best_win  = max_turns + 1;
    m_fd_best_turn = 0;

    // New game: reset the game-persistent leaf cache (MTG_LEAF_CACHE) so a reused batch
    // worker's AIEngine does not accumulate/cross-hit across games. See m_leaf_cache.
    if (m_leaf_cache_enabled) { m_leaf_cache.Clear(); }

    // New game: same rule, same reason, for the thread_local PLAN memos (see
    // TurnSolver::ClearPerGameCaches). Their entries are epoch-scoped to one decision and so can
    // never be hit across a game boundary -- but they still occupy the shared cap, which made the
    // eviction schedule (and therefore a game's work-unit count) depend on which games shared the
    // worker thread. Play was never affected; the work METER's determinism was.
    TurnSolver::ClearPerGameCaches();

    m_last_bottomed_numbers.clear();
    int mulligan_count = 0;
    while (true)
    {
        // Forced-mulligan replay (see SetForcedMulligan): keep at EXACTLY the recorded depth,
        // ignoring the keep heuristic, so the reconstructed hand is engine-version-independent.
        // Otherwise the play path first consults the archetype keep-floor (DecisionProvider::KeepFloor,
        // e.g. the TH payoff-hand force-keep behind MTG_TH_KEEPFLOOR): a ForceKeep/ForceMulligan there
        // overrides the exhaustive table, Undecided (the default for every deck) falls through to the
        // stop_at floor + table unchanged, so this is byte-identical wherever no provider opts in.
        bool keep;
        if (m_forced_mull_active)
        {
            keep = (mulligan_count >= m_forced_mull_count);
        }
        else
        {
            const KeepGuard floor =
                ResolveProvider(state).KeepFloor(ap.hand, mulligan_count, state.on_the_play);
            keep = (floor == KeepGuard::ForceKeep)     ? true
                 : (floor == KeepGuard::ForceMulligan) ? false
                 : (static_cast<int>(ap.hand.size()) <= m_profile.stop_at
                    || KeepHand(ap.hand, mulligan_count, state.on_the_play));
        }

        // External controller (claude-play / human-play) may keep/mulligan differently. It sees the
        // engine's own decision (`keep`) as the AI hint and returns its own keep/mulligan. Never
        // consulted during forced replay. Inert (engine decision unchanged) when no chooser is set.
        if (m_external_mulligan_chooser && !m_forced_mull_active)
        {
            keep = m_external_mulligan_chooser(ap.hand, mulligan_count, state.on_the_play, keep);
        }

        if (m_logger)
        {
            std::vector<int>         nums;
            std::vector<std::string> names;
            for (const Card& c : ap.hand)
            {
                nums.push_back(c.m_number);
                names.push_back(c.m_name);
            }
            m_logger->LogMulliganAttempt(mulligan_count, nums, names, keep);
        }

        if (keep) { break; }

        for (Card& c : ap.hand) { ap.library.push_back(c); }
        ap.hand.clear();
        ap.library.Shuffle(SaltSeed(state.game_seed + static_cast<uint64_t>(mulligan_count),
                                    state.shuffle_salt_opening));
        ++mulligan_count;
        ap.library.DrawN(7, ap.hand);
    }

    m_last_mulligan_count = mulligan_count;
    if (mulligan_count > 0) { BottomCards(state, mulligan_count, max_turns); }

    // Confounded-bottoming A/B (MTG_CONFOUND_BOTTOM): after the bottoming DECISION is made, reshuffle the
    // remaining library with a seed DECORRELATED from the per-mulligan order the clairvoyant lookahead
    // bottoming peeked at (line above uses game_seed + mulligan_count). The playout then draws a fresh
    // sequence that no longer matches what lookahead optimized the removal against, so the peek is
    // worthless -- isolating bottoming DECISION quality from the value of seeing the exact library. The
    // blind exhaustive table (chosen as the argmin over reshuffled continuations -- exactly this
    // distribution) is unaffected; a clairvoyant pick becomes miscalibrated. Reshuffling the whole
    // remaining library mirrors how the exhaustive V labels were built (fresh continuations). OFF (unset
    // or "0") => no reshuffle => byte-identical to the normal path. Only meaningful when mulligan>0.
    static const bool confound_bottom = EnvOn("MTG_CONFOUND_BOTTOM");
    if (confound_bottom && mulligan_count > 0)
    {
        ap.library.Shuffle(state.game_seed + 0x9E3779B97F4A7C15ULL);
    }

    m_kept_opening_hand.clear();
    for (const Card& c : ap.hand)
    {
        m_kept_opening_hand.push_back(c.m_name);
    }
}

bool AIEngine::KeepHand(const std::vector<Card>& hand, int mulligan_count, bool on_the_play) const
{
    int effective_size = static_cast<int>(hand.size()) - mulligan_count;
    if (effective_size <= 1) { return true; }   // hard floor: a 1-card hand is always kept

    // Exhaustive bucketed keep policy: when present and the hand resolves to a tabled bucket
    // composition, it is the EXACT optimal keep decision for the objective and overrides everything
    // below. A composition not in the table (or an unbucketed card) leaves present=false and falls
    // through to the keep_model / static path, so decks without an exhaustive table are unaffected.
    if (m_profile.HasExhaustiveKeep())
    {
        std::vector<std::string> names;
        names.reserve(hand.size());
        for (const Card& c : hand) { names.push_back(c.m_name.str()); }
        bool present = false;
        bool keep = m_profile.exhaustive_keep->Decide(names, mulligan_count, on_the_play, present);
        if (present) { return keep; }
    }

    // Analyzer-generated keep model: when present it OWNS the keep/mulligan decision at EVERY level
    // above the size-1 floor -- there is NO separate stop_at short-circuit, because the model was
    // trained at every hand size (7..2) and learned where to stop mulliganing itself. It REPLACES the
    // static-filter + linear-score path below. The durable human constraints (separately loaded, never
    // regenerated) act as a hard guard wrapping it; then the decision tree decides on the named feature
    // vector (which includes on-the-play and mulligan depth -- the axes the legacy path ignores). An
    // empty model falls through to the legacy path (with its stop_at floor), so every deck without a
    // generated model is byte-identical.
    if (!m_profile.keep_model.empty())
    {
        switch (ApplyKeepConstraints(hand, m_profile.keep_constraints))
        {
            case KeepGuard::ForceMulligan: return false;
            case KeepGuard::ForceKeep:     return true;
            case KeepGuard::Undecided:     break;
        }
        const std::vector<int> feats =
            ComputeKeepFeatures(hand, mulligan_count, on_the_play, m_profile.keep_model);
        return m_profile.keep_model.Keep(feats);
    }

    // Legacy static path retains the grid-tuned stop_at floor (keep-model decks bypass it above).
    if (effective_size <= m_profile.stop_at) { return true; }

    int land_count = 0;
    for (const Card& c : hand)
    {
        auto def = CardDatabase::Instance().LookupCached(c);
        bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (is_land) { ++land_count; }
    }
    int non_land_count = static_cast<int>(hand.size()) - land_count;

    if (land_count < m_profile.min_lands) { return false; }
    if (land_count > m_profile.max_lands) { return false; }
    if (non_land_count == 0)              { return false; }

    if (!m_profile.required_pieces.empty())
    {
        bool found = false;
        for (const std::string& piece : m_profile.required_pieces)
        {
            for (const Card& c : hand)
            {
                if (c.m_name == piece) { found = true; break; }
            }
        }
        if (!found) { return false; }
    }

    if (!m_profile.min_color_sources.empty())
    {
        for (const std::pair<const Color, int>& req : m_profile.min_color_sources)
        {
            int sources = 0;
            for (const Card& c : hand)
            {
                auto def = CardDatabase::Instance().LookupCached(c);
                if (!def) { continue; }
                bool is_mana_source = (def->tmpl == CardTemplate::BasicLand)
                                   || (def->tmpl == CardTemplate::ManaDork)
                                   || (def->params.mana_rock);
                if (!is_mana_source) { continue; }
                for (Color produced : EffectiveProducesInHand(hand, *def))   // RP -> union of other hand lands
                {
                    if (produced == req.first) { ++sources; break; }
                }
            }
            if (sources < req.second) { return false; }
        }
    }

    if (m_profile.min_playable > 0)
    {
        // Build a pool from lands in hand (one mana of each color they produce).
        // Each non-land card is evaluated independently against the full pool —
        // we're asking "is this card castable at all," not "can we cast everything."
        // Build a simplified pool: single-color lands add their color;
        // multi-color lands add 1 wild (can be any one color).
        std::map<Color, int> pool;
        int total_land_mana = 0;
        int wild_mana = 0;
        for (const Card& c : hand)
        {
            auto def = CardDatabase::Instance().LookupCached(c);
            if (!def || !def->card.IsLand()) { continue; }
            ++total_land_mana;
            const std::vector<Color>& prod = EffectiveProducesInHand(hand, *def);  // RP-aware
            if (prod.size() == 1)
            {
                ++pool[prod[0]];
            }
            else if (!prod.empty())
            {
                ++wild_mana;
            }
        }

        int playable = 0;
        for (const Card& c : hand)
        {
            auto def = CardDatabase::Instance().LookupCached(c);
            if (!def || def->card.IsLand()) { continue; }
            const ManaCost& cost = def->card.m_mana_cost;
            if (total_land_mana < cost.ManaValue()) { continue; }
            // Check each colored pip; wild_mana can cover any shortfall.
            int deficit = std::max(0, cost.white     - pool[Color::White])
                        + std::max(0, cost.blue      - pool[Color::Blue])
                        + std::max(0, cost.black     - pool[Color::Black])
                        + std::max(0, cost.red       - pool[Color::Red])
                        + std::max(0, cost.green     - pool[Color::Green])
                        + std::max(0, cost.colorless - pool[Color::Colorless]);
            if (deficit > wild_mana) { continue; }
            ++playable;
        }
        if (playable < m_profile.min_playable) { return false; }
    }

    if (m_profile.curve_check != CurveCheck::None && mulligan_count < 2)
    {
        int count_mv1 = 0;  // spells with MV <= 1
        int count_mv2 = 0;  // spells with MV <= 2 (includes MV <= 1)
        for (const Card& c : hand)
        {
            auto def     = CardDatabase::Instance().LookupCached(c);
            bool is_land = def ? def->card.IsLand() : c.IsLand();
            if (is_land) { continue; }
            int mv = def ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
            if (mv <= 1) { ++count_mv1; }
            if (mv <= 2) { ++count_mv2; }
        }

        switch (m_profile.curve_check)
        {
            case CurveCheck::TwoDrop:
                if (land_count < 2 || count_mv2 == 0) { return false; }
                break;
            case CurveCheck::OneDrop:
                if (count_mv1 == 0) { return false; }
                break;
            case CurveCheck::OneAndTwo:
                if (count_mv1 == 0 || count_mv2 < 2 || land_count < 2) { return false; }
                break;
            default:
                break;
        }
    }

    if (!m_profile.card_scores.empty())
    {
        double score = ComputeHandScore(hand, m_profile.card_scores);
        if (score < m_profile.hand_score_threshold) { return false; }
    }

    return true;
}

double AIEngine::CardScore(const std::string& name, int copy_index) const
{
    std::map<std::string, std::vector<double>>::const_iterator it =
        m_profile.card_scores.find(name);
    if (it == m_profile.card_scores.end() || it->second.empty())
    {
        return 0.0;
    }
    // Value the specific copy being bottomed: index 0 = first copy's marginal,
    // index 1 = second copy's (typically smaller — diminishing returns). A hand
    // holding a redundant 2nd copy of a lord thus scores that copy at [1], so it
    // bottoms before a unique card scored at [0]. Clamp the index to the recorded
    // vector (some cards only have a first-copy sample).
    int idx = std::min(std::max(0, copy_index),
                       static_cast<int>(it->second.size()) - 1);
    // Clamp negatives to zero: they are selection-bias artifacts (e.g. Aether
    // Vial tests slow because Vial hands hold fewer creatures), not a signal to
    // bottom the card preferentially. See ComputeHandScore for the same rationale.
    return std::max(0.0, it->second[idx]);
}

int AIEngine::HeuristicBottomPick(const std::vector<Card>& hand,
                                 const std::vector<char>& allowed) const
{
    // Recompute from the passed hand — the caller bottoms one card at a time, so
    // the composition changes between calls.
    std::map<Color, int> pool;
    int land_count = 0;
    for (const Card& c : hand)
    {
        auto def = CardDatabase::Instance().LookupCached(c);
        if (!def || !def->card.IsLand()) { continue; }
        ++land_count;
        for (Color produced : EffectiveProducesInHand(hand, *def)) { ++pool[produced]; }  // RP-aware
    }

    // Helper: single-spell deficit given a pool.
    auto one_deficit = [](const ManaCost& cost,
                          const std::map<Color, int>& p, int mana) -> int
    {
        auto get = [&](Color c) -> int
        {
            auto it = p.find(c);
            return it != p.end() ? it->second : 0;
        };
        int needed = 0;
        needed += std::max(0, cost.white     - get(Color::White));
        needed += std::max(0, cost.blue      - get(Color::Blue));
        needed += std::max(0, cost.black     - get(Color::Black));
        needed += std::max(0, cost.red       - get(Color::Red));
        needed += std::max(0, cost.green     - get(Color::Green));
        needed += std::max(0, cost.colorless - get(Color::Colorless));
        needed += std::max(0, cost.ManaValue() - (mana + needed));
        return needed;
    };

    // Number of copies of a named card currently in the passed hand. Bottoming a
    // candidate removes the n-th copy, whose marginal keep-value is the (n-1)-th
    // entry of card_scores (0-based) — so a redundant 2nd copy is valued by its
    // smaller second-copy marginal, not the headline first-copy one.
    auto copy_count = [&](const std::string& name) -> int
    {
        int n = 0;
        for (const Card& c : hand)
        {
            if (c.m_name == name) { ++n; }
        }
        return n;
    };

    int chosen = -1;

    if (land_count > m_profile.min_lands + 1)
    {
        // Excess land: bottom the one that is least needed by the spells in hand.
        //
        // Primary key:   total spell deficit after removing the land (lower = prefer).
        //   Avoids bottoming a land that would leave a spell uncastable.
        //
        // Secondary key: usefulness = max colour demand the land can satisfy (lower = prefer).
        //   Demand for colour C = total pips of C across all spells in hand.
        //   A Forest in an all-red hand has usefulness 0; a Mountain has usefulness = red demand.
        //   Among equally safe removals, this bottoms the land producing the least-needed colour.

        // Compute per-colour demand from spells in hand.
        std::map<Color, int> demand;
        for (const Card& hc : hand)
        {
            auto sdef = CardDatabase::Instance().LookupCached(hc);
            if (!sdef || sdef->card.IsLand()) { continue; }
            const ManaCost& cost = sdef->card.m_mana_cost;
            demand[Color::White]     += cost.white;
            demand[Color::Blue]      += cost.blue;
            demand[Color::Black]     += cost.black;
            demand[Color::Red]       += cost.red;
            demand[Color::Green]     += cost.green;
            demand[Color::Colorless] += cost.colorless;
        }

        int    best_total_deficit  = std::numeric_limits<int>::max();
        int    best_uncastable     = std::numeric_limits<int>::max();
        int    best_usefulness     = std::numeric_limits<int>::max();
        double best_land_score     = 0.0;

        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (!allowed[j]) { continue; }
            auto def     = CardDatabase::Instance().LookupCached(hand[j]);
            bool is_land = def ? def->card.IsLand() : hand[j].IsLand();
            if (!is_land) { continue; }

            // Build pool without this land.
            std::map<Color, int> tmp_pool = pool;
            if (def)
            {
                for (Color c : EffectiveProducesInHand(hand, *def))   // RP-aware (mirror of pool build)
                {
                    auto it = tmp_pool.find(c);
                    if (it != tmp_pool.end()) { --it->second; }
                }
            }

            // Three-key score for the remaining hand:
            //   1. Uncastable count — number of spells with any deficit > 0
            //   2. Total deficit    — total additional lands needed across all spells
            //   3. Usefulness       — max colour demand the removed land could satisfy
            // Bottom the land that minimises (1), then (2), then (3).
            // Count-first because immediately playable spells matter more than minimising
            // total damage — having 2 castable spells + 1 brick is better than having
            // 3 spells each one draw away from castable.
            int total_deficit  = 0;
            int uncastable_cnt = 0;
            for (const Card& hc : hand)
            {
                auto sdef = CardDatabase::Instance().LookupCached(hc);
                if (!sdef || sdef->card.IsLand()) { continue; }
                int d = one_deficit(sdef->card.m_mana_cost, tmp_pool, land_count - 1);
                total_deficit += d;
                if (d > 0) { ++uncastable_cnt; }
            }

            int usefulness = 0;
            if (def)
            {
                for (Color c : EffectiveProducesInHand(hand, *def))   // RP-aware
                {
                    auto it = demand.find(c);
                    usefulness = std::max(usefulness,
                                          it != demand.end() ? it->second : 0);
                }
            }

            bool first_better, second_better;
            if (m_profile.bottom_order == BottomOrder::CountFirst)
            {
                first_better  = uncastable_cnt < best_uncastable;
                second_better = uncastable_cnt == best_uncastable
                             && total_deficit  <  best_total_deficit;
            }
            else
            {
                first_better  = total_deficit  <  best_total_deficit;
                second_better = total_deficit  == best_total_deficit
                             && uncastable_cnt <  best_uncastable;
            }
            double land_score = CardScore(hand[j].m_name,
                                          copy_count(hand[j].m_name) - 1);

            bool prefer = (chosen == -1)
                       || first_better
                       || second_better
                       || (uncastable_cnt == best_uncastable
                           && total_deficit == best_total_deficit
                           && usefulness   <  best_usefulness)
                       || (uncastable_cnt == best_uncastable
                           && total_deficit == best_total_deficit
                           && usefulness   == best_usefulness
                           && land_score   <  best_land_score);

            if (prefer)
            {
                best_total_deficit = total_deficit;
                best_uncastable    = uncastable_cnt;
                best_usefulness    = usefulness;
                best_land_score    = land_score;
                chosen             = j;
            }
        }
    }
    else
    {
        // Bottom the worst non-land spell using a two-key score:
        //   Primary:   lands deficit — how many more lands are needed to cast this card.
        //              Colour gaps are counted first (they require specific lands);
        //              any remaining generic shortfall is added on top.
        //              A 4-drop with 3 on-colour lands has deficit 1 (any land helps).
        //              An off-colour 1-drop with 1 wrong-colour land also has deficit 1
        //              (needs a specific colour), but is worth keeping because MV is lower.
        //   Secondary: analysis score ascending — among equally castable spells,
        //              bottom the lowest-scored card (keep the lords/payload). Inert
        //              when the profile carries no card_scores (all scores 0.0).
        //   Tertiary:  MV descending — among equal score, bottom the more expensive card.
        int    best_deficit = -1;
        double best_score   = 0.0;
        int    best_mv      = -1;

        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (!allowed[j]) { continue; }
            auto def     = CardDatabase::Instance().LookupCached(hand[j]);
            bool is_land = def ? def->card.IsLand() : hand[j].IsLand();
            if (is_land) { continue; }

            int    deficit = def ? one_deficit(def->card.m_mana_cost, pool, land_count) : 0;
            double score   = CardScore(hand[j].m_name,
                                       copy_count(hand[j].m_name) - 1);
            int    mv      = def ? def->card.m_mana_cost.ManaValue()
                                 : hand[j].m_mana_cost.ManaValue();

            bool prefer = (chosen == -1)
                       || (deficit > best_deficit)
                       || (deficit == best_deficit && score < best_score)
                       || (deficit == best_deficit && score == best_score && mv > best_mv);

            if (prefer)
            {
                chosen       = j;
                best_deficit = deficit;
                best_score   = score;
                best_mv      = mv;
            }
        }
    }

    // Fallback: the branch found no eligible card of its preferred type (e.g. the
    // allowed set is all lands while we're in the spell branch). Under lookahead
    // these are all win-equal, so bottom the lowest-scored among them (keep the
    // payload); with no card_scores this reduces to the first allowed card.
    if (chosen == -1)
    {
        double best_score = 0.0;
        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (!allowed[j]) { continue; }
            double score = CardScore(hand[j].m_name,
                                     copy_count(hand[j].m_name) - 1);
            if (chosen == -1 || score < best_score)
            {
                chosen     = j;
                best_score = score;
            }
        }
    }
    return chosen;
}

void AIEngine::ResolveEchoUpkeep(GameState& state)
{
    bool echo_processed = false;   // did an echo obligation actually come due THIS pass?
    for (std::size_t i = 0; i < state.battlefield.size(); )
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index || p.echo_resolved)
        { ++i; continue; }
        const CardDefinition* edef = CardDatabase::Instance().LookupCached(p.card);
        if (!edef || !edef->params.echo_cost) { ++i; continue; }
        p.echo_resolved = true;   // obligation resolved this upkeep, whatever the outcome
        echo_processed  = true;
        const CardParams& ep = edef->params;
        // Affordability decided up front so the human-play chooser is offered only a REAL choice
        // (paying is possible); if unaffordable the creature is simply sacrificed (no decision).
        // AvailableManaPool is a read-only snapshot (taps nothing), so computing it unconditionally
        // is behaviourally identical to the old non-self_token-only path.
        ManaPool avail = AvailableManaPool(state);
        const bool affordable = avail.CanPay(*ep.echo_cost);
        // Pay-vs-decline JUDGEMENT is provider-owned (PayEchoToKeep) so the executor and the rollout
        // (TurnSolver) share one decision -> lockstep. Default reproduces the old fixed heuristic
        // (self-replacing body declines, else pays); GoblinsProvider adds the Mogg lethal/no-gas keep.
        bool pay = affordable && ResolveProvider(state).PayEchoToKeep(state, p);
        // Human play (--claude-play/viewer): let the player decide pay-vs-sacrifice when affordable.
        // Guarded to the REAL executor (never a clairvoyant rollout, which must play autonomously so
        // the kept hand reproduces the search) -> autonomous play + ground truth are byte-identical.
        if (m_external_echo_chooser && !m_in_rollout && affordable)
        { pay = m_external_echo_chooser(state, p, pay); }
        bool kept = false;
        if (pay) { kept = TapForCost(state, *ep.echo_cost, avail, /*for_creature=*/false); }
        if (kept) { ++i; continue; }
        // Declined or unaffordable -> sacrifice; fire OnCreatureDies (Mogg's death token, etc.).
        const Card dead = p.card;
        const int  ctrl = p.controller_index;
        const bool tok  = p.is_token;
        const int  m1   = MinusCountersOn(p);
        state.players[ctrl].graveyard.push_back(dead);
        state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
        OnCreatureDies(state, ctrl, dead, tok, m1);   // may append a token at the end -> safe (no echo_cost)
        // Do not advance i: the erased slot now holds the next permanent.
    }
    // END OF THE UPKEEP STEP (CR 500.4): the pool empties. Echo is resolved here rather than in
    // GameEngine::UpkeepStep (see the note above) purely so it can tap through AIEngine's mana
    // API -- but its TIMING is still the upkeep, so mana over-produced paying an echo cost must
    // NOT survive into this main phase. Lockstep partner: TurnSolver::SimulateEndAndStartNextTurn.
    // See UpkeepFloatClearEnabled (viewer issue #6, Goblins s19 gi18 T4 floated {R}x5).
    //
    // Gated on echo_processed, and that gate is LOAD-BEARING, not caution: TakeTurn can be
    // called a SECOND time with is_pre_combat_main still true (the staged-draw / depth-0 second
    // pass -- see the `return true` sites below), so an unconditional clear here fires again
    // MID-main-phase and wipes a ritual float the first pass built up. Measured: it cost
    // Dragonstorm d0 85 slower / 37 play-changed games (gi11 6->loss) while Goblins, the deck
    // this fixes, was untouched. The echo loop is idempotent via Permanent::echo_resolved, so
    // keying on "an obligation actually came due in THIS pass" inherits that once-per-turn
    // latch. Nothing else floats mana during upkeep, so this is exactly the echo leftover.
    if (echo_processed && UpkeepFloatClearEnabled()) { state.floating_mana = ManaPool{}; }
}

void AIEngine::FlagNonConvergence(const GameState& state, const TurnSolver::Plan& plan,
                                  int committed_win, int committed_sub_depth)
{
    const int turn = state.turn_number;

    // A win is VERIFIED when it is within the committing pass's own branched horizon (real
    // simulation reached it) and within the game bound. The old extra conjunct
    // `committed_sub_depth == m_lookahead_depth - 1` limited this to full-depth passes, which
    // let every SHALLOWER ladder commit evade the detector -- under iterative deepening a d2
    // pass commits only after d1's complete refutation, so its in-horizon win carries the same
    // proof, and goblins gi=149's d2-pass win-3 that realized as 5 was exactly the class this
    // flag exists to announce (2026-08-15 dig). Diagnostic-only (MTG_FLAG_NONCONV, default off).
    const bool verified = committed_win <= m_max_turns
                       && committed_win - turn <= committed_sub_depth;
    if (!verified) { return; }

    // First verified win of the game, or an even earlier proof: adopt it.
    if (committed_win <= m_nonconv_best_win)
    {
        m_nonconv_best_win  = committed_win;
        m_nonconv_best_turn = turn;
        return;
    }

    // Later turn's verified win EXCEEDS one already proved earlier => non-convergence.
    std::ostringstream os;
    os << "[nonconv] seed=" << state.game_seed
       << " turn=" << turn
       << " verified_win_now=" << committed_win
       << " EXCEEDS earlier verified_win=" << m_nonconv_best_win
       << " proven_at_turn=" << m_nonconv_best_turn;

    os << " | hand=";
    bool first = true;
    for (const Card& c : state.ActivePlayer().hand)
    {
        os << (first ? "" : ", ") << c.m_name;
        first = false;
    }

    os << " | plan=";
    if (plan.land_decided && !plan.land_to_play.empty()) { os << "[land " << plan.land_to_play << "] "; }
    if (plan.actions.empty()) { os << "(idle)"; }
    first = true;
    for (const Action& a : plan.actions)
    {
        const char* kind = a.kind == Action::Kind::ActivateVial      ? "vial:"
                         : a.kind == Action::Kind::CastFromGraveyard ? "retrace:"
                         : a.kind == Action::Kind::DiscardToLandsEdge ? "LE:"
                         : "";
        os << (first ? "" : ", ") << kind << a.card_name;
        first = false;
    }
    os << "\n";
    std::cerr << os.str();
}

int AIEngine::RolloutWinTurn(GameState trial, int max_turns, int* lands_out)
{
    return RolloutWinTurnFrom(std::move(trial), max_turns, GameEngine::ResumeAt::NewTurn, lands_out);
}

// `from` is where in the CURRENT turn the rollout resumes (see GameEngine::ResumeAt). Every
// between-turns caller passes NewTurn (= the old PlayOut behaviour); a caller that captured its
// trial state part-way through a turn MUST name its own step, or the rest of that turn is skipped
// and the label describes a state the real game cannot reach.
//
// TWO RULES FOR A MID-GAME TRIAL CALLER, both learned the hard way. `from` above is the first.
// The second: this function runs the trial on an EMPTY committed line (see the stash below), so a
// trial's label is always the win turn of a FRESHLY RE-SEARCHED continuation. If you act on that
// label by DEVIATING from what the heuristic -- and therefore what TurnSolver::ApplyPlanDirect,
// and therefore what the search that built m_committed_line -- assumed, the queued line is stale
// by construction: it plans around resources the deviation just spent, and replaying it is a
// train/serve split (label from a fresh search, realised game from a stale plan). Every such
// caller must `m_committed_line.clear()` on the deviating branch. There are exactly three today
// (the searched cleanup discard, the searched Vial charge, and the Land's Edge fire count) and it
// took three separate bug hunts -- the last of them costing two full turns on TH s3304 gi301,
// unrecoverable at depth 8 / budget 60000 -- to fix them one at a time. A fourth site added later
// needs the same clear. See docs/design/th-colourless-first-s3003-gi301.md §7.6.
int AIEngine::RolloutWinTurnFrom(GameState trial, int max_turns,
                                 GameEngine::ResumeAt from, int* lands_out)
{
    RevealLogPause _rlp;  // rollout: suppress scry/dig reveal logging (real play only)
    HumanPlaySuppress _hps;  // rollout: play autonomously even under --claude-play (bottoming/keep parity)
    GameLogger* saved = m_logger;
    m_logger          = nullptr;
    m_in_rollout      = true;
    // Attach the deck's learned play models to the rollout state so a DIRECT rollout (keep/bottom
    // generator, MULL-EV, Land's Edge probe -- all of which call PlayOut without going through
    // RunGame::HandleMulligan) plays like the SHIPPED deck. HandleMulligan is the normal hook that
    // sets these (see there); the rollout bypasses it (the hand is pre-built), so without this the
    // value/eval model is null throughout the rollout and every full-depth leaf falls back to the
    // slow SimulateToEnd horizon rollout instead of the O(1) learned value leaf -- a ~train/serve
    // MISMATCH (the gen played a value-less policy the deck never serves) AND the dominant cost of
    // the exhaustive-keep generator (measured: the value leaf was 100% inert; MTG_VALUE_MODEL=0 was
    // byte-for-byte the same speed). Non-owning; m_profile outlives every deep copy the search makes.
    trial.m_value_model = m_profile.value_model.empty() ? nullptr : &m_profile.value_model;
    trial.m_evaluator   = m_profile.eval_model.empty()  ? nullptr : &m_profile.eval_model;
    trial.m_model_feat_mask = dominance::ModelFeatureMask(trial.m_evaluator)
                            | dominance::ModelFeatureMask(trial.m_value_model);
    ShuffleEvalGuard  _seg(true);   // decoupling instrument: rollout shuffles use shuffle_salt_search
    // The rollout PlayOut shares this AIEngine by reference, so isolate its committed
    // full-depth line: stash the real game's line, run the rollout on a fresh empty
    // line, then restore. Otherwise the rollout would consume/overwrite the line the
    // real game is mid-way through replaying.
    std::deque<TurnSolver::PhasePlan> saved_line = std::move(m_committed_line);
    m_committed_line.clear();
    const int saved_discard_pin = m_discard_choice_pin;
    m_discard_choice_pin = -1;
    const int saved_vial_pin = m_vial_choice_pin;
    m_vial_choice_pin = -1;
    const int saved_atk_pin = m_atk_release_pin;
    m_atk_release_pin = -1;
    GameEngine engine(*this);
    int win_turn = engine.PlayOutFrom(trial, max_turns, from);
    if (lands_out)
    {
        int n = 0;
        for (const Permanent& p : trial.battlefield)
        { if (p.controller_index == 0 && p.card.IsLand()) { ++n; } }
        *lands_out = n;
    }
    m_committed_line = std::move(saved_line);
    m_discard_choice_pin = saved_discard_pin;
    m_vial_choice_pin    = saved_vial_pin;
    m_atk_release_pin    = saved_atk_pin;
    m_in_rollout = false;
    m_logger     = saved;
    return win_turn > 0 ? win_turn : max_turns + 1;
}

int AIEngine::RolloutKeepWinTurn(GameState trial, int mulligan_count, int max_turns,
                                 std::vector<char>* out_hit, int* lands_out)
{
    // The keep-model generator's oracle: evaluate the value of KEEPING this opening hand at
    // the given mulligan depth. Bottom `mulligan_count` cards exactly as HandleMulligan would
    // (lookahead bottoming, on at depth > 0), then play the game out clairvoyantly. The result
    // is the same win turn the deck would realise if it kept this hand -- so a label derived
    // from it (keep iff this beats the expected value of mulliganing) matches real play.
    SetMaxTurns(max_turns);
    if (mulligan_count > 0) { BottomCards(trial, mulligan_count, max_turns); }
    // Execution-trace: if a touch index + output vector are supplied, record which cards' effects run
    // during this rollout's playout (docs/design/execution-trace-carry.md). off => byte-identical.
    if (m_touch_index && out_hit)
    {
        rollout_touch::Sink sink; sink.index = m_touch_index; sink.hit = out_hit;
        rollout_touch::Sink* saved = rollout_touch::g_sink;
        rollout_touch::g_sink = &sink;
        int wt = RolloutWinTurn(std::move(trial), max_turns, lands_out);
        rollout_touch::g_sink = saved;
        return wt;
    }
    return RolloutWinTurn(std::move(trial), max_turns, lands_out);
}

void AIEngine::BottomCards(GameState& state, int count, int max_turns)
{
    Player& ap = state.ActivePlayer();

    // Blind exhaustive-policy bottoming: when the deck carries an exhaustive keep/bottom table and it
    // covers this hand's bucket composition, bottom toward the table's optimal kept-subcomposition
    // (expected over library continuations -- no clairvoyance), overriding the lookahead/heuristic.
    // Falls through when the policy is absent, has no bottoming table, or doesn't cover the hand, so
    // decks without an exhaustive policy are byte-identical. Consistent with KeepHand's Decide consult.
    //
    // Driven by the PROFILE's bottoming_enabled flag, which generation ALWAYS bakes true
    // (analyzer/main.cpp) -- there is no off switch and no per-deck judgment to make. A table that
    // ships has bottoming on; every deck in the repo reads true. (This comment used to say "off for
    // low-R/noise-limited profiles, on for validated high-R ones". That was the ORIGINAL 2026-07
    // policy from docs/design/exhaustive-keep-policy.md and it has not been true since generation
    // started baking the flag on; it survived long enough to make an agent hedge a shipped adoption
    // as though enabling bottoming were a decision it had made. It is not.)
    //
    // MTG_EXHAUSTIVE_BOTTOM is a 3-state override that exists FOR THE A/B HARNESS ONLY: unset =>
    // follow the profile flag; "0" => force off; else => force on. test/keepmodel_exhaustive_ab.sh
    // uses it to isolate the two halves -- KM_MODE=keep pins it to 0 on BOTH arms so the keep effect
    // is measured with bottoming held identical, and KM_MODE=bottom toggles it as the axis under
    // test. Nothing outside that harness sets it. Keep is presence-gated and independent.
    static const int bottom_override = []
    { const char* e = std::getenv("MTG_EXHAUSTIVE_BOTTOM");
      if (!e || !*e) { return -1; } return std::string(e) == "0" ? 0 : 1; }();
    const bool exhaustive_bottom = (bottom_override >= 0) ? (bottom_override == 1)
                                        : (m_profile.exhaustive_keep && m_profile.exhaustive_keep->bottoming_enabled);
    // Defer to a forced-mulligan replay (must bottom the EXACT recorded cards) and to an external
    // bottom chooser (claude-play / human-play drives the pick) -- the exhaustive table is an
    // AUTONOMOUS bottomer, so it stands down whenever a replay or a human is in control, exactly as the
    // lookahead/heuristic path below guards itself with !m_forced_mull_active.
    if (exhaustive_bottom && m_profile.HasExhaustiveKeep()
        && !m_forced_mull_active && !m_external_bottom_chooser)
    {
        std::vector<std::string> names;
        names.reserve(ap.hand.size());
        for (const Card& c : ap.hand) { names.push_back(c.m_name.str()); }
        std::vector<int> target;
        if (m_profile.exhaustive_keep->DecideBottom(names, count, state.on_the_play, target))
        {
            // Bottom `count` cards, each time removing one physical card from a bucket that is over
            // its target count. Members of a bucket are equivalent by construction, so any over-target
            // card is an equally-optimal removal; picking the first is deterministic.
            const auto& n2b = m_profile.exhaustive_keep->name_to_bucket;
            for (int i = 0; i < count && !ap.hand.empty(); ++i)
            {
                std::vector<int> comp(m_profile.exhaustive_keep->buckets.size(), 0);
                for (const Card& c : ap.hand)
                { auto it = n2b.find(c.m_name.str()); if (it != n2b.end()) { comp[it->second]++; } }
                int pick = -1;
                for (int j = 0; j < static_cast<int>(ap.hand.size()); ++j)
                {
                    auto it = n2b.find(ap.hand[j].m_name.str());
                    if (it != n2b.end() && comp[it->second] > target[it->second]) { pick = j; break; }
                }
                if (pick < 0) { break; }   // already at target (shouldn't happen for a tabled hand)
                if (m_logger) { m_logger->LogBottomed(ap.hand[pick].m_number, ap.hand[pick].m_name); }
                ap.library.push_back(ap.hand[pick]);
                ap.hand.erase(ap.hand.begin() + pick);
            }
            return;
        }
    }

    // One transposition table shared across every candidate rollout of this whole
    // bottoming pass: each RolloutWinTurn plays a full lookahead game over the same
    // fixed library, so later turns/candidates reuse memoised exact win turns rather
    // than rebuilding a fresh table ~(count*hand_size) times. Scoped to this loop only
    // (m_shared_tt restored to its prior value on return), so the real game and any
    // enclosing rollout are unaffected and byte-identical. See m_shared_tt.
    TranspositionTable  shared_tt;
    TranspositionTable* saved_tt = m_shared_tt;
    m_shared_tt = LookaheadBottoming() ? &shared_tt : saved_tt;

    // LEGAL-SIZE SUBSET TABLE (MTG_BOTTOM_LEGAL, default ON, adopted 2026-08-26 (=0 disables) -- with the
    // overhaul flip's rebaseline). The shipped greedy scores each candidate removal on a hand
    // that is still (count - i - 1) cards TOO BIG, so a line that needs EVERY remaining card can
    // score win-N at step i yet be unrealisable by any legal keep -- st374's mull-3: "bottom Sol
    // Ring" tied at 4 off a 5-card-only line (the chip + Terastodon line needs all 3 Forests +
    // Llanowar + Natural Order), killing the only real 4-win keep (Sol Ring -> T2 Natural
    // Order). Fix: roll out every LEGAL removal SUBSET once (C(h,count) <= 35 trials, shared-TT
    // amortised, vs the greedy's h+(h-1)+... oversized trials), then run the SAME per-step
    // greedy where a candidate's score is the best win over subsets consistent with the bottoms
    // so far -- a candidate is allowed iff SOME legal completion achieves the best win. Later
    // steps reuse the table (no further rollouts) and the heuristic tiebreak among allowed
    // candidates is unchanged. Clairvoyant path only (the blind bottomer keeps its own model).
    static const bool s_legal_trials = EnvOn("MTG_BOTTOM_LEGAL", true);
    std::vector<int>  subset_win;    // win turn per removal mask over the initial hand (0 = not a legal mask)
    std::vector<int>  nums0;         // initial hand card numbers, in mask-bit order
    std::uint32_t     done_mask = 0; // bottoms committed so far, as initial-hand bits
    bool subset_table = false;
    {
        static const bool s_rollouts_on = EnvOn("MTG_BOTTOM_ROLLOUTS", true);
        static const bool s_blind       = EnvOn("MTG_NC_BLIND_BOTTOM");
        const int h0 = static_cast<int>(ap.hand.size());
        if (s_legal_trials && count > 1 && count < h0 && h0 <= 16
            && LookaheadBottoming() && !m_forced_mull_active && s_rollouts_on && !s_blind)
        {
            auto popcnt = [](std::uint32_t v) { int n = 0; while (v) { v &= v - 1; ++n; } return n; };
            subset_win.assign(std::size_t{1} << h0, 0);
            nums0.reserve(h0);
            for (const Card& c : ap.hand) { nums0.push_back(c.m_number); }
            for (std::uint32_t m = 0; m < (1u << h0); ++m)
            {
                if (popcnt(m) != count) { continue; }
                GameState trial = state;
                Player& tp = trial.ActivePlayer();
                // Bottom the masked cards in ascending hand order (the order the step greedy
                // would send them down), erasing descending so indices stay valid.
                for (int idx = 0; idx < h0; ++idx)
                { if (m & (1u << idx)) { tp.library.push_back(tp.hand[idx]); } }
                for (int idx = h0 - 1; idx >= 0; --idx)
                { if (m & (1u << idx)) { tp.hand.erase(tp.hand.begin() + idx); } }
                subset_win[m] = RolloutWinTurn(std::move(trial), max_turns);
            }
            subset_table = true;
        }
    }

    // When forced, bottom EXACTLY the listed cards (its size, not `count`): a full list (size ==
    // count) is a faithful replay; an empty/short list is a deliberate probe that exposes the
    // pre-bottom depth-N hand (used to derive the bottomed set when patching old references).
    const int stop = m_forced_mull_active ? static_cast<int>(m_forced_bottom_numbers.size()) : count;
    for (int i = 0; i < stop && !ap.hand.empty(); ++i)
    {
        int hand_size = static_cast<int>(ap.hand.size());
        std::vector<char> allowed(hand_size, 1);

        // SIZING PROBE (MTG_BOTTOM_ROLLOUTS=0), default ON => byte-identical. Bottoming is 90.4% of
        // FiveColour's runtime at shipped play (callgrind, 2026-08-10) because this block runs a FULL
        // game rollout per candidate per bottom step and the deck has no exhaustive bottom table to
        // short-circuit it. Turning the rollouts off leaves `allowed` all-ones, so HeuristicBottomPick
        // below decides alone -- which is the K=0 end of the "pre-filter candidates before rolling
        // out" lever, and therefore an upper bound on what that lever can save and on what it costs
        // in play quality. A probe, not a proposal: measure both arms before designing anything.
        static const bool s_bottom_rollouts = EnvOn("MTG_BOTTOM_ROLLOUTS", true);
        if (LookaheadBottoming() && !m_forced_mull_active && s_bottom_rollouts)
        {
            // Clairvoyant greedy: roll out a full game for removing each candidate
            // card (it goes to the bottom of the library, so the draws the rollout
            // sees are the real future draws). The best removal is the one that
            // preserves the earliest win. Restrict the heuristic tiebreak below to
            // those win-optimal removals, so lookahead only ever overrides the
            // heuristic to secure a strictly earlier win.
            //
            // BLIND (non-clairvoyant) bottoming, MTG_NC_BLIND_BOTTOM (default off => byte-identical):
            // the lookahead bottomer above reads the REAL post-bottom draws (each rollout keeps the true
            // library order), so its removal choice is CLAIRVOYANT -- which contaminates the otherwise
            // non-clairvoyant NC play policy's pipeline (the decouple instrument on Hinata attributes the
            // whole coupled->decoupled drop to THIS stage, not to ReshuffleAvgChoosePlan). When on, each
            // candidate's trial library is RESHUFFLED to an unseen future (game_seed-derived salt, so it
            // is executor-order-INDEPENDENT) and averaged over K samples, matching the NC turn policy's
            // reshuffle-averaged evaluation. The removal is then judged on EXPECTED win turn over unknown
            // draws, not the one true draw sequence. See ReshuffleAvgChoosePlan / learned-d0-policy.md.
            static const bool s_blind_bottom = EnvOn("MTG_NC_BLIND_BOTTOM");
            static const int  s_blind_k      = []{ const char* e = std::getenv("MTG_NC_BLIND_BOTTOM_K");
                                                   return (e && *e) ? std::max(1, std::atoi(e)) : 4; }();
            std::vector<int> win_turn(hand_size, 0);
            int best_win = std::numeric_limits<int>::max();
            for (int j = 0; j < hand_size; ++j)
            {
                if (subset_table)
                {
                    // Legal-subset score: the best win over legal removal sets that contain the
                    // bottoms committed so far plus this candidate.
                    std::uint32_t bit = 0;
                    for (std::size_t idx = 0; idx < nums0.size(); ++idx)
                    {
                        if (nums0[idx] == ap.hand[j].m_number && !((done_mask >> idx) & 1u))
                        { bit = 1u << idx; break; }
                    }
                    int best = max_turns + 1;
                    const std::uint32_t need = done_mask | bit;
                    for (std::uint32_t m = 0; m < subset_win.size(); ++m)
                    {
                        if (subset_win[m] != 0 && (m & need) == need && subset_win[m] < best)
                        { best = subset_win[m]; }
                    }
                    win_turn[j] = best;
                }
                else if (s_blind_bottom)
                {
                    // Average the removal's value over K reshuffled unseen futures (non-clairvoyant).
                    long long acc = 0;
                    for (int k = 0; k < s_blind_k; ++k)
                    {
                        GameState trial = state;
                        Player& trial_ap = trial.ActivePlayer();
                        Card bottomed = trial_ap.hand[j];
                        trial_ap.hand.erase(trial_ap.hand.begin() + j);
                        const uint64_t rs = state.game_seed
                                          + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(k) + 1)
                                          + 1000003ULL * static_cast<uint64_t>(i)      // per bottom step
                                          + 7919ULL   * static_cast<uint64_t>(j);      // per candidate
                        trial_ap.library.Shuffle(rs);                   // unseen future draw order
                        trial_ap.library.push_back(std::move(bottomed)); // the removed card truly bottoms
                        trial.shuffle_salt_search = rs;   // rollout mid-game shuffles fold this too
                        acc += RolloutWinTurn(std::move(trial), max_turns);
                    }
                    win_turn[j] = static_cast<int>((acc + s_blind_k / 2) / s_blind_k);  // rounded mean
                }
                else
                {
                    GameState trial = state;
                    Player& trial_ap = trial.ActivePlayer();
                    trial_ap.library.push_back(trial_ap.hand[j]);
                    trial_ap.hand.erase(trial_ap.hand.begin() + j);
                    win_turn[j] = RolloutWinTurn(std::move(trial), max_turns);
                }
                if (win_turn[j] < best_win) { best_win = win_turn[j]; }
            }
            for (int j = 0; j < hand_size; ++j)
            {
                allowed[j] = (win_turn[j] == best_win) ? 1 : 0;
            }
            if (TurnSolver::GetTraceSolve())
            {
                std::cerr << "[bottom_trace depth=" << m_lookahead_depth
                          << " bottom#" << (i + 1) << "]\n";
                for (int j = 0; j < hand_size; ++j)
                {
                    std::cerr << "  bottom " << ap.hand[j].m_name
                              << " -> win=" << win_turn[j]
                              << (allowed[j] ? " *" : "") << "\n";
                }
            }
        }

        int pick = HeuristicBottomPick(ap.hand, allowed);
        if (pick < 0) { pick = 0; }

        // External controller (claude-play / human-play) picks which card to bottom; it sees the
        // engine's pick as the AI hint and the win-optimal removal flags. Never during forced replay.
        // Inert (engine pick unchanged) when no chooser is set -> autonomous bottoming byte-identical.
        if (m_external_bottom_chooser && !m_forced_mull_active)
        {
            int human = m_external_bottom_chooser(ap.hand, pick, allowed, i, stop);
            if (human >= 0 && human < hand_size) { pick = human; }
        }

        // Forced-mulligan replay: bottom exactly the recorded card (by m_number) at this step,
        // overriding the heuristic pick, so the reconstructed hand matches regardless of how the
        // bottoming heuristic has since changed. Falls back to the heuristic pick if the recorded
        // number isn't present (shouldn't happen for a faithful reference).
        if (m_forced_mull_active && i < static_cast<int>(m_forced_bottom_numbers.size()))
        {
            for (int j = 0; j < hand_size; ++j)
            {
                if (ap.hand[j].m_number == m_forced_bottom_numbers[i]) { pick = j; break; }
            }
        }

        if (subset_table)
        {
            // Record the pick in initial-hand bits so later steps score only completions of it.
            for (std::size_t idx = 0; idx < nums0.size(); ++idx)
            {
                if (nums0[idx] == ap.hand[pick].m_number && !((done_mask >> idx) & 1u))
                { done_mask |= 1u << idx; break; }
            }
        }
        m_last_bottomed_numbers.push_back(ap.hand[pick].m_number);
        if (m_logger)
        {
            m_logger->LogBottomed(ap.hand[pick].m_number, ap.hand[pick].m_name);
        }
        ap.library.push_back(ap.hand[pick]);
        ap.hand.erase(ap.hand.begin() + pick);
    }

    m_shared_tt = saved_tt;
}

// ============================================================
// TakeTurn
// ============================================================

// Charge every Vial at or after `from_index` on the pure heuristic. Used to finish the upkeep loop a
// searched charge trial interrupted: the trial resumes at UpkeepTail, which is PAST the loop, so
// without this the later Vials silently lose the counter they would really have gained.
static void ChargeRemainingVialsHeuristic(GameState& s, int from_index)
{
    for (int i = from_index; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        Permanent& p = s.battlefield[i];
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->params.upkeep_adds_charge) { continue; }
        if (ResolveProvider(s).WantVialCharge(s, p)) { ++p.charge_counters; }
    }
}

bool AIEngine::DecideVialCharge(const GameState& state, const Permanent& vial)
{
    // Hand-aware charge policy (shared with the rollout): hold while a creature of the
    // current MV is in hand to deploy, otherwise climb toward a bigger creature in hand
    // (up to Haytham's MV 4), else pre-charge toward the deck's dominant MV. See
    // WantVialCharge. This is the DEFAULT, the FALLBACK and the TIE-BREAK of the searched pass below.
    bool heuristic = ResolveProvider(state).WantVialCharge(state, vial);
    // An external controller (claude-play / human-play) may decide differently -- but NOT inside the
    // engine's clairvoyant rollouts (bottoming / keep eval), which must play autonomously so the kept
    // hand reproduces the real search's game. Without the m_in_rollout guard the external chooser
    // would fire (exit 70 / consume a --choices token) from within a bottoming rollout for a Vial deck.
    if (m_external_vial_chooser && !m_in_rollout) { return m_external_vial_chooser(state, vial, heuristic); }

    // LOCKSTEP (MTG_VIAL_AXIS): the executing plan's searched charge (Plan::vial_charge_choice via
    // m_vial_choice_pin) decides the FIRST vial of this upkeep -- the search already chose between
    // hold and charge under the same assumptions the committed line encodes, so re-deciding here
    // (probe or heuristic) would deviate from the scored line. Consume-and-clear on first vial,
    // byte-for-byte the rollout's semantics (TurnSolver::SimulateBeginningPhase). Never inside a
    // rollout: a shared-engine playout saves/restores the pin and charges heuristically, as before.
    //
    // This RETIRES the probe on the searched path -- the axis is the decision, so the out-of-band
    // trial games below must not also run and override it (the probe is an oracle replacing search
    // judgment; see docs/design/searched-discard-as-search-node.md for the same ruling on the
    // cleanup discard). With MTG_VIAL_AXIS off, nothing writes the pin and the probe path is
    // reached exactly as before -> byte-identical.
    if (!m_in_rollout && m_vial_choice_pin >= 0)
    {
        const bool pinned = (m_vial_choice_pin != 0);
        m_vial_choice_pin = -1;
        return pinned;
    }
    if (VialAxisEnabled()) { return heuristic; }   // axis owns the decision; the probe stays retired

    // SEARCHED charge (mirrors the searched cleanup discard): roll the game out under BOTH answers
    // and take the one that wins soonest, heuristic first so it owns every tie.
    //   * Only with lookahead (depth > 0); at d0 this is inert => byte-identical greedy.
    //   * NEVER inside a rollout (m_in_rollout): the trial's PlayOutFrom reaches this same upkeep,
    //     so a nested searched pass would blow up exponentially. Rollout upkeeps use the heuristic.
    if (!s_searched_vial || !LookaheadBottoming() || m_in_rollout) { return heuristic; }

    // The Permanent is a reference INTO state.battlefield (GameEngine's upkeep loop hands us the
    // live element), so its index is recoverable -- and needed, because the trial is a deep copy.
    int vi = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    { if (&state.battlefield[i] == &vial) { vi = i; break; } }
    if (vi < 0) { return heuristic; }   // not a live battlefield element -> nothing to search

    int win[2] = { 0, 0 };
    const bool order[2] = { heuristic, !heuristic };   // heuristic FIRST: ties go to it
    for (int k = 0; k < 2; ++k)
    {
        GameState trial = state;
        if (order[k]) { ++trial.battlefield[vi].charge_counters; }
        ChargeRemainingVialsHeuristic(trial, vi + 1);
        win[k] = RolloutWinTurnFrom(std::move(trial), m_max_turns, GameEngine::ResumeAt::UpkeepTail);
    }
    static const bool s_vial_trace = EnvOn("MTG_VIAL_TRACE");
    if (s_vial_trace)
    {
        std::cerr << "[vial_trace turn=" << state.turn_number << " counters=" << vial.charge_counters
                  << " heur=" << (heuristic ? "charge" : "hold")
                  << " win(heur)=" << win[0] << " win(alt)=" << win[1] << "]\n";
    }
    if (win[1] < win[0])
    {
        // DEVIATION from the heuristic: every plan in the committed line was searched (and this
        // trial was rolled out) assuming the heuristic charge, so the line is now stale. Same
        // reasoning as s_discard_reline -- drop it and let next turn re-search what we created.
        if (s_discard_reline) { m_committed_line.clear(); }
        return order[1];
    }
    return heuristic;
}

bool AIEngine::TakeTurn(GameState& state, bool is_pre_combat_main,
                        const std::function<void(GameState&)>& resolve_stack)
{
    // MTG_EDF_TURN_TRACE (diagnostic, no-op unless set): dump the blink loop as it stands on the
    // REAL board, before the search is entered. Taken here rather than inside the recognizer because
    // every call site it has is already under a RevealLogPause -- see EdfTurnTrace.
    if (is_pre_combat_main) { EdfTurnTrace(state, state.active_player_index); }
    // Resolve the stack after a cast when a resolver was supplied (real game path);
    // a no-op when batched (no resolver) or when the stack is empty (e.g. Vial).
    auto resolve_now = [&]()
    {
        if (resolve_stack && !state.stack.empty()) { resolve_stack(state); }
    };
    bool cast_draw_engine = false;
    // Merge any unexpired staged cards into hand so the solver and casting logic
    // can treat them as playable. They are marked m_is_staged = true so we can
    // identify and restore unplayed ones afterward. The expiry is preserved in
    // staged_snapshot so it can be restored if the card is not played.
    Player& ap_ref = state.ActivePlayer();
    std::vector<StagedCard> staged_snapshot = ap_ref.staged_cards;
    ap_ref.staged_cards.clear();
    for (StagedCard& sc : staged_snapshot)
    {
        if (sc.expiry_turn < state.turn_number) { continue; }  // end-of-next-turn expiry (CR 406)
        sc.card.m_is_staged     = true;
        sc.card.m_staged_expiry = sc.expiry_turn;  // travels with the card so the rollout can expire it
        ap_ref.hand.push_back(sc.card);
    }

    // Echo upkeep resolution -- IDEMPOTENT BACKSTOP. The real resolution now runs at true upkeep
    // timing (GameEngine::UpkeepTail -> ResolveEchoUpkeep, BEFORE the draw step): resolving it here
    // at the top of the pre-combat main -- AFTER the draw -- de-synced the executor from the rollout
    // (which resolves echo pre-draw in SimulateEndAndStartNextTurn) whenever a lapse's death trigger
    // consumes library cards. Goblins gi=149 (seed 3152, 2026-08-15): Mogg's declined echo dies into
    // a live Rundvelt Hordemaster, whose impulse exile eats the library top -- pre-draw it ate the
    // Warchief and the draw was a Mountain (the committed win-3 line's 3rd land + staged Muxus);
    // post-draw the Warchief was drawn and the Mountain exiled, leaving Muxus one mana short. This
    // call remains for resume paths that skip the upkeep (echo_resolved makes it a no-op after the
    // upkeep-timed pass ran).
    if (is_pre_combat_main)
    {
        ResolveEchoUpkeep(state);
        // The ORDER-CONDEMNATION stamp used to sit here (pre-solve); it moved to after the
        // plan is chosen -- pre-land, pre-cast -- because the split-turn affordability test
        // needs the chosen plan's cast costs. See the stamp call below fold_land's block and
        // TurnSolver::StampM1Hand.
    }

    // Enumerate-all-earliest-wins dump (offline rule-miner; inert unless MTG_DUMP_EWINS).
    // Emitted here -- after the staged merge, before any play -- so the candidate set matches
    // what the search sees. One compact JSON line per real pre-combat decision (or only the
    // MTG_DUMP_EWINS_TURN turn). See scripts/analyze_earliest_wins.py.
    if (s_dump_ewins && !m_in_rollout && is_pre_combat_main
        && (s_dump_ewins_turn <= 0 || state.turn_number == s_dump_ewins_turn))
    {
        TurnSolver::EarliestWinReport rep =
            TurnSolver::EnumerateEarliestWins(state, m_max_turns, m_search_post_combat);
        auto esc = [](const std::string& s) { return s; };   // names are already JSON-safe
        std::ostringstream js;
        js << "{\"ewins\":{\"seed\":" << state.game_seed
           << ",\"turn\":" << rep.turn << ",\"earliest\":" << rep.earliest
           << ",\"candidates\":[";
        for (size_t i = 0; i < rep.candidates.size(); ++i)
        {
            const TurnSolver::EarliestWinCandidate& c = rep.candidates[i];
            if (i) { js << ","; }
            js << "{\"win\":" << c.win_turn
               << ",\"land\":\"" << esc(c.land) << "\""
               << ",\"fetch\":\"" << esc(c.fetch) << "\""
               << ",\"searched\":" << (c.searched_order ? "true" : "false")
               << ",\"casts\":[";
            for (size_t k = 0; k < c.cast_order.size(); ++k)
            { js << (k ? "," : "") << "\"" << esc(c.cast_order[k]) << "\""; }
            js << "],\"sac\":[";
            for (size_t k = 0; k < c.sac_casts.size(); ++k)
            { js << (k ? "," : "") << "\"" << esc(c.sac_casts[k]) << "\""; }
            js << "]}";
        }
        js << "]}}\n";
        static std::mutex s_ewins_mtx;   // one whole line at a time (workers share cerr)
        std::lock_guard<std::mutex> lk(s_ewins_mtx);
        std::cerr << js.str();
    }

    // Learned-eval / leaf-value label generation (inert unless MTG_DUMP_EVAL_ROWS or MTG_DUMP_VALUE_ROWS
    // set): emit de-clairvoyed per-candidate (eval) and/or per-position (value) rows at this REAL decision.
    if ((s_eval_rows_path || s_value_rows_path) && !m_in_rollout && is_pre_combat_main)
    {
        EmitEvalRows(state, m_max_turns, m_search_post_combat);
    }

    // The post-combat (second) main phase does NOTHING unless post-combat search
    // is explicitly enabled (see SetSearchPostCombat). In a clairvoyant goldfish
    // combat creates no new resources, so everything castable was already cast in
    // the first main (casting is timing-neutral there) and the lookahead rollout
    // likewise plays no second main. An unsearched greedy second main would be an
    // off-model action the search never evaluated — worse, it could execute a cast
    // the rollout merely assumed, so model and reality would diverge. Decks whose
    // combat enables genuine second-main plays (Bear Umbra / Hidden Strings
    // untapping lands; spectacle costs unlocked by combat damage) turn it on.
    // Human play (claude-play) additionally plays the post-combat (second) main so the player can
    // cast a spell unlocked by combat -- a Spectacle cost (Light Up the Stage) becomes available
    // only after combat damage set opponent_lost_life_this_turn. The empty-second-main is skipped
    // below (only prompted when a cast is actually available), so this is not per-turn decision
    // spam. m_external_chooser is null for the autonomous search -> play_this_phase unchanged there.
    // The external chooser is a HUMAN decision point, so it must NOT fire inside the engine's own
    // clairvoyant rollouts (bottoming / keep evaluation): those must play autonomously so the kept
    // hand reproduces the real search's game. m_in_rollout gates it off there (the rollout then
    // follows the normal autonomous search path, exactly like a goldfish rollout).
    const bool use_external = m_external_chooser != nullptr && !m_in_rollout;
    const bool play_this_phase =
        is_pre_combat_main || m_search_post_combat || use_external;

    // External-controller intercept (Claude-play / human-play prototype, opt-in via
    // SetExternalChooser; inert otherwise so the normal autonomous AI path is
    // unchanged). Offer the SAME candidate plans the solver would search and execute
    // the chosen one, bypassing the search. Self-contained: ApplyPlan is the rollout-
    // style direct application -- it applies the same life/board/draw effects and fires
    // the same on-cast triggers that decide win/loss, so win-turn outcomes are faithful
    // (log fidelity is lower than the stack-based real path, fine for a flag-generating
    // sweep). Combat and cleanup discard stay on the engine heuristics.
    if (use_external && play_this_phase)
    {
        // Segment loop for DRAW BREAKPOINTS (human play): execute one committed plan, and if it
        // DREW cards (a Treasure Hunt / dig / cantrip resolved -> library shrank), re-enumerate
        // from the post-draw state and ask the chooser again so the human plays the revealed
        // cards (a land, Land's Edge, another Treasure Hunt). A plan that draws nothing ends the
        // phase (one decision, as before). The guard bounds a pathological chain; a real game
        // terminates when the library empties or the human passes. In the autonomous (search) AI
        // path ApplyPlan resolves draws inline and never shrinks the library across a no-draw
        // plan, so non-human external choosers still see exactly one decision per phase.
        // "COMMIT LINE STOPS AT THE LINE, NOT AT THE PHASE" (viewer issue #3, user-directed
        // 2026-08-08). Committing a line applies it and then RE-ASKS, for as long as there is
        // anything left to do; only an explicit PASS (idx < 0) ends the main phase. That is what
        // makes resource-generating plays usable by the SAME phase that generated them, which the
        // old draw-only / new-sac-source breakpoints could not express:
        //   * tap Krenko for N Goblin tokens, THEN sacrifice those tokens to Skirk Prospector,
        //   * Vial in a Mogg War Marshal, THEN sac it + its token for the mana to cast Goblin King,
        //   * Crop Rotation a land into play, THEN cast off the mana it makes.
        // Each of those needs a board state that does not exist when the line is being assembled,
        // so no single atomic commit can contain it. The old rule ended the phase the moment a
        // committed plan neither drew a card nor put a new sac-to-draw source into play, which is
        // exactly why those lines were unreachable by hand.
        //
        // THE ONLY STOP CONDITIONS ARE THE HUMAN PASSING AND A DEAD OPPONENT (user, 2026-09-04:
        // "I don't ever want to continue to the next turn or phase", "Commit Line literally means
        // let me play more things"). "There is genuinely nothing to do" used to be one too, and it
        // is NOT a fact -- it is the enumerator's opinion, and a phase that ends on it takes the
        // board away at exactly the moment a human wants to look at it and say which card they
        // expected to be able to cast. Skipping to the next turn is what "Commit turn" is for.
        //
        // Human-play only (this whole branch is inside use_external && !m_in_rollout) -> GT-neutral;
        // the autonomous search never enters here. Saved references gain one PASS frame per main
        // phase, which viewer_protocol_check.py already absorbs (an unaligned main_phase frame is
        // answered -1, consuming no recorded pick) -> they replay as `repaired`, not drift.
        //
        // MTG_PLAY_SEGMENT_ALWAYS=0 restores the previous behaviour (re-prompt only after a DRAW or
        // a newly-played, affordable sac-to-draw source) for anyone who finds the extra pass click
        // worse than the reach. The two signals that rule used:
        //   inplay_sac(state) -- sac-to-draw sources IN PLAY and UNTAPPED (Horizon Canopy, Fiery Islet).
        //     A source appears here the segment you PLAY it untapped; a STANDING one is present at
        //     phase start (so it never counts as "new"), which is what stops per-turn re-prompt spam.
        //   offer_sac(plans)  -- sac-to-draw digs actually OFFERED among the enumerated plans
        //     (AppendHumanPlayDigPlans additionally gates on the {1}-style sac cost being affordable
        //     THIS phase). This is the affordability signal.
        static const bool s_segment_always = EnvOn("MTG_PLAY_SEGMENT_ALWAYS", true);
        auto inplay_sac = [](const GameState& gs) -> std::set<std::string>
        {
            std::set<std::string> s;
            for (const Permanent& perm : gs.battlefield)
            {
                if (perm.controller_index != gs.active_player_index || perm.tapped) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(perm.card);
                if (d && d->params.sacrifice_draw_cost.has_value())
                { s.insert(perm.card.m_name.str()); }
            }
            return s;
        };
        auto offer_sac = [](const std::vector<TurnSolver::Plan>& plans) -> std::set<std::string>
        {
            std::set<std::string> s;
            for (const TurnSolver::Plan& p : plans)
            { for (const Action& a : p.actions)
              { if (a.kind == Action::Kind::DigDraw && a.dig_sacrifice)
                { s.insert(a.card_name); } } }
            return s;
        };
        // MAELSTROM ARCHANGEL free cast -- a ONE-TIME triggered choice, asked before the plan menu.
        // The trigger is "you MAY cast a spell from your hand without paying its mana cost" on combat
        // damage; the approved model banks a charge (free_casts_available) and spends it in the
        // post-combat main, because against a passive opponent nothing happens in between. What it
        // must NOT do is behave like a standing menu option: offering a #FREE variant of every hand
        // card throughout the phase let the player cast anything at any moment, and when they simply
        // queued a card the paid and free variants shared a signature so the dedup kept the PAID one
        // and the charge silently evaporated. So ask once, here, exactly like the Goblin Lackey put:
        // which spell, or decline. The chosen cast is applied through the ordinary commit path
        // (ApplyPlan on the enumerated single-action free plan) so ETBs/triggers/logging are the real
        // ones, and the bank is zeroed either way -- the trigger resolves once. Afterwards
        // free_casts_available is 0, so no #FREE variants reach the plan menu at all.
        //
        // Human play only (this whole branch is use_external && !m_in_rollout, and the chooser is
        // nulled by RevealLogPause everywhere else), so the autonomous search keeps the plan-variant
        // path and every deck without a live bank is byte-identical.
        if (!is_pre_combat_main && state.free_casts_available > 0 && g_play_free_cast_chooser)
        {
            // One enumerated plan per castable hand card, each a single free CastFromHand.
            std::vector<TurnSolver::Plan> fc_plans = TurnSolver::EnumerateMainPlans(state, is_pre_combat_main);
            std::vector<int>  fc_idx;      // plan index per candidate
            std::vector<Card> fc_cards;    // the hand card each one casts
            for (size_t pi = 0; pi < fc_plans.size(); ++pi)
            {
                const TurnSolver::Plan& fp = fc_plans[pi];
                if (!fp.land_to_play.empty() || fp.actions.size() != 1) { continue; }
                const Action& fa = fp.actions[0];
                if (!fa.free_cast || fa.kind != Action::Kind::CastFromHand) { continue; }
                if (fa.hand_index < 0
                    || fa.hand_index >= static_cast<int>(state.ActivePlayer().hand.size())) { continue; }
                const Card& hc = state.ActivePlayer().hand[fa.hand_index];
                bool dup = false;   // one entry per CARD; sub-decisions stay the plan menu's job
                for (const Card& seen : fc_cards) { if (seen.m_number == hc.m_number) { dup = true; break; } }
                if (dup) { continue; }
                fc_idx.push_back(static_cast<int>(pi));
                fc_cards.push_back(hc);
            }
            if (!fc_cards.empty())
            {
                // Default = the highest mana value (the charge is worth most on what you could least
                // afford), mirroring the Lackey put's highest-MV heuristic.
                int heur = 0, best_mv = -1;
                for (size_t ci = 0; ci < fc_cards.size(); ++ci)
                {
                    const CardDefinition* cd = CardDatabase::Instance().LookupCached(fc_cards[ci]);
                    const int mv = cd ? cd->card.m_mana_cost.ManaValue() : 0;
                    if (mv > best_mv) { best_mv = mv; heur = static_cast<int>(ci); }
                }
                const int picked = (*g_play_free_cast_chooser)(state, state.active_player_index,
                                                               "Maelstrom Archangel",
                                                               fc_cards, heur,
                                                               /*walked=*/{});   // from-hand: no library walk
                if (picked >= 0 && picked < static_cast<int>(fc_idx.size()))
                { TurnSolver::ApplyPlan(state, fc_plans[fc_idx[picked]], is_pre_combat_main); }
            }
            state.free_casts_available = 0;   // the trigger resolved (cast or declined) -- once only
        }

        std::set<std::string> prev_inplay;  // untapped in-play sac sources before the last applied plan
        bool drew_last = false;             // did the last applied plan draw (library shrank)?
        for (int seg = 0; seg < 64; ++seg)
        {
            // #6 storage-land TAP-vs-CHARGE, POST-DRAW variant (Mercadian Bazaar, storage_charge_mode
            // "tap"): its "{T}: put a counter" is an active MAIN-PHASE tap, so the human decides hold-vs-
            // burst AFTER the draw, with full information. Consulted once per turn at the START of the pre-
            // combat main and BEFORE plan enumeration (so the offered plans respect the hold). Dwarven Hold
            // ("upkeep_if_tapped") is EXCLUDED here -- its commitment is pre-draw, surfaced in
            // GameEngine::UpkeepStep. A hold flags the land not-live for the turn -> never tapped for mana ->
            // stays untapped -> charges. Human-play only (chooser null autonomously / in rollout) ->
            // byte-identical for the search and every non-storage deck.
            if (seg == 0 && is_pre_combat_main)
            {
                for (Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index) { continue; }
                    if (p.tapped || p.storage_counters <= 0) { continue; }
                    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                    if (!d || !d->params.storage_land
                        || d->params.storage_charge_mode == "upkeep_if_tapped") { continue; }
                    // Provider owns the AUTONOMOUS answer (base: never hold == the historical
                    // hardcoded behaviour, so this is byte-identical); a human chooser, when one is
                    // attached, still overrides it. Previously the whole block was gated on the
                    // chooser being non-null, which meant the autonomous engine had no say at all.
                    bool hold = ResolveProvider(state).StorageLandHold(
                        state, state.active_player_index, *d, p.storage_counters);
                    if (g_play_storage_hold_chooser)
                    { hold = (*g_play_storage_hold_chooser)(state, p, p.storage_counters); }
                    if (hold) { p.storage_hold_this_turn = true; }
                }
            }
            std::vector<TurnSolver::Plan> plans =
                TurnSolver::EnumerateMainPlans(state, is_pre_combat_main);
            // AN EMPTY MENU IS STILL A FRAME. "cast nothing is always enumerated" was not true:
            // EnumerateMainPlans returns an EMPTY vector when it believes there is nothing to do,
            // and breaking on that skipped the phase before the always-prompt rule below could see
            // it -- which is what actually ended the turn the moment a Living Wish resolved, and
            // what silently swallowed every post-combat main. Synthesize the pass-only menu instead
            // and let the human look at the board. Committing it is an empty plan, which the break
            // further down already reads as the pass it is, so this cannot loop.
            const bool menu_was_empty = plans.empty();
            if (menu_was_empty)
            {
                if (!s_segment_always) { break; }   // legacy rule keeps its old exit
                plans.emplace_back();               // one entry: "cast nothing" == pass
            }
            std::set<std::string> cur_inplay = inplay_sac(state);
            std::set<std::string> cur_offer  = offer_sac(plans);
            // Is there any real play on offer (a cast, an activation, or a land drop)? "Cast nothing"
            // is always enumerated, so a menu with no action is a menu with nothing in it.
            bool any_play = false;
            for (const TurnSolver::Plan& p : plans)
            { if (!p.actions.empty() || !p.land_to_play.empty()) { any_play = true; break; } }

            if (s_segment_always)
            {
                // ALWAYS PROMPT. A phase ends when the HUMAN passes -- committing the empty line,
                // which the break below treats as the pass it is -- or when the opponent is dead.
                // Nothing else may end it, and in particular the engine's own opinion that there is
                // nothing left to do may not.
                //
                // USER, 2026-09-04, after Living Wish resolved and the turn jumped straight to the
                // next one: "I would like commit line to actually stop skipping to the next turn in
                // all situations", "If I click Commit Line, I mean it literally", "I don't ever want
                // to continue to the next turn or phase."
                //
                // The rule this replaces re-prompted only when the turn had DRAWN or the enumerator
                // still offered a real play (`any_play`). That reads the engine's own enumeration as
                // ground truth for whether the human has anything to do, which is precisely the
                // assumption a bug breaks: a card the enumerator wrongly believes is uncastable
                // produces an empty menu, the phase ends silently, and the human never sees the
                // board that would have shown them why. The user's reason for wanting the frame is
                // exactly that -- "I want to instead see what the state is and if I can't cast what I
                // want to there, then I can report exactly what happened." A frame with no plays in
                // it is not noise; on this viewer it is the bug report.
                //
                // Cost is one extra click per phase that had nothing to offer. MTG_PLAY_SEGMENT_
                // ALWAYS=0 still restores the old draw/sac-source rule for anyone who wants it.
                //
                // References only ever GAIN frames, never lose one, which is the direction that is
                // safe: viewer_protocol_check.py answers an unaligned main_phase frame -1, consuming
                // no recorded pick, so a saved game replays as `repaired` rather than drifting.
                // Human-play only (use_external && !m_in_rollout) -> GT-neutral.
                //
                // ...with ONE thing that is not free, and it is the reason g_play_frame_no_ordinal
                // exists: an added frame must not RENUMBER the frames after it. main_ordinal is the
                // key a cast-order pin is recorded under, so a reference whose pin sat at ordinal 4
                // replays it against whatever decision is 4th now. Measured, not guessed:
                // StompySurprise/claude_s1_gi0's recorded turn-4 win replayed as turn 5 with nothing
                // else changed, and the baseline binary reproduces it at turn 4 with the identical
                // stale-index repair. So an added frame consumes NO ordinal (below).
            }
            else
            {
                // Legacy: phase complete once a committed plan neither DREW nor put a NEW, AFFORDABLE
                // sac source into play. seg==0 is the initial prompt, shown.
                if (seg > 0 && !drew_last)
                {
                    bool new_activation = false;
                    for (const std::string& name : cur_inplay)
                    { if (!prev_inplay.count(name) && cur_offer.count(name)) { new_activation = true; break; } }
                    if (!new_activation) { break; }
                }
                // Post-combat (second) main: on the FIRST entry, only prompt when there is a real play
                // available -- with nothing to do the second main is a no-op, so skip it silently
                // rather than asking the human to "pass" every single turn.
                if (!is_pre_combat_main && seg == 0 && !any_play) { break; }
            }
            size_t lib_before = state.ActivePlayer().library.size();
            // A frame the OLD rule would have stopped on is one this change ADDED, and it takes no
            // ordinal (see g_play_frame_no_ordinal). Every such frame is pass-only by construction --
            // `menu_was_empty` synthesized it, and `!any_play` means every enumerated plan is empty --
            // so it can never be the one a cast order was pinned to. The flag rides to the harness
            // that writes the decision JSON so both counters skip in lockstep.
            const bool first_pre   = (seg == 0 && is_pre_combat_main);
            const bool added_frame = s_segment_always
                                  && (menu_was_empty || (!first_pre && !drew_last && !any_play));
            const int this_main_ordinal = added_frame ? -1 : m_ext_main_ordinal++;   // #10 key
            g_play_frame_no_ordinal = added_frame;
            int idx = m_external_chooser(state, plans, is_pre_combat_main);
            g_play_frame_no_ordinal = false;
            if (idx < 0 || idx >= static_cast<int>(plans.size())) { break; }  // pass / done
            // An EMPTY committed plan ("land=none; cast: (nothing)") means exactly what a pass
            // means, so it must END the phase like one. Applying it changes nothing, and under the
            // commit-the-line rule above the loop then re-enumerates and offers the identical menu
            // -- so choosing it repeated the SAME decision forever (turn frozen, only main_ordinal
            // advancing; reproduced at seed 8800 gi19 by the Dragons Stage-5d sweep, where an agent
            // that picks "do nothing" hangs the game instead of passing). The autonomous search
            // never reaches this branch (use_external && !m_in_rollout), so this is GT-neutral; the
            // plan stays in the menu with its index intact, so no saved reference is renumbered.
            if (plans[idx].actions.empty() && plans[idx].land_to_play.empty()) { break; }
            // #10: honour a human-pinned cast order for this main-phase decision (empty / unset =>
            // no-op => canonical). Copy the chosen plan so the enumerated menu stays untouched.
            TurnSolver::Plan chosen = plans[idx];
            if (g_play_cast_order_chooser && this_main_ordinal >= 0)
            {
                std::vector<std::string> ord = (*g_play_cast_order_chooser)(this_main_ordinal);
                ReorderPlanCasts(chosen, ord);
            }
            TurnSolver::ApplyPlan(state, chosen, is_pre_combat_main);
            // The opponent is DEAD -- stop asking. Under the commit-the-line rule the loop would
            // otherwise re-enumerate and keep prompting inside an already-won turn (the Dragons
            // sweep saw three Scourge ETB pings take the opponent to -1 and then still be offered a
            // land drop and a bounce). The recorded win turn was always correct, so this is
            // human-play ergonomics, not a scoring fix -- and it is the same shape as the empty-plan
            // break above. Autonomous play never reaches this branch, so it is GT-neutral.
            if (state.players[1 - state.active_player_index].life <= 0) { break; }
            drew_last = state.ActivePlayer().library.size() < lib_before;
            prev_inplay = std::move(cur_inplay);
        }

        // Grove of the Burnwillows drip -- the same end-of-pre-combat-main sweep the autonomous
        // executor (below, ~3716) and the rollout (ApplyPlanDirect) both run. This external-chooser
        // path returns before reaching that call, so human play never swept leftover drip lands;
        // the gap was masked while payment over-tapped Groves as a side effect, and surfaced when
        // MTG_PREPAY_SHRINK removed the over-tap (Anti-Lifegain s5/gi4: the recorded T4 kill needed
        // the two Grove drips and slipped to T5).
        if (is_pre_combat_main) { TapDripLandsIfUseful(state, state.active_player_index); }

        // Restore unplayed staged cards (mirror the normal end-of-TakeTurn restore):
        // cards cast were removed from hand; the rest, still flagged m_is_staged, go
        // back to staged_cards so they expire correctly (CR 406).
        Player& ap_after = state.ActivePlayer();
        std::vector<Card> regular_hand;
        for (Card& c : ap_after.hand)
        {
            if (c.m_is_staged)
            {
                c.m_is_staged = false;
                StagedCard sc;
                sc.card        = c;
                sc.expiry_turn = c.m_staged_expiry;
                ap_after.staged_cards.push_back(sc);
            }
            else { regular_hand.push_back(c); }
        }
        ap_after.hand = std::move(regular_hand);
        return false;  // ApplyPlan resolved draw-engine re-solves inline; no second pass
    }

    TurnSolver::Plan plan;  // empty plan == do nothing this phase
    bool fd_plan_committed = false;  // full-depth: plan came from the committed line
                                     // (carries a recorded breakpoint script to replay)

    // Karoo play-at-end timing -- mirror of ApplyPlanDirect's karoo_deferred. A planned Karoo
    // bounce land (etb_bounce_land, enters tapped) is NOT played at the fold_land step; it is
    // deferred to AFTER the main cast loop so BounceKarooLand returns a spent (tapped) land
    // rather than an untapped one we still need. MTG_NO_KAROO_DEFER restores the old land-first.
    static const bool s_karoo_defer = !EnvOn("MTG_NO_KAROO_DEFER");
    bool        karoo_deferred = false;
    std::string karoo_land_name;
    std::string karoo_fetch;

    if (play_this_phase)
    {
        // The land drop is searched (folded into SolveWithLookahead) ONLY for the
        // depth>0 first main. Every other case keeps the pre-fold greedy land play:
        //   - depth 0 (fast greedy runner): the search needs a rollout, so depth 0
        //     uses the 4-pass heuristic plus the Treasure-Hunt defer special-case;
        //   - the second main at any depth: a land may still be playable post-combat
        //     (e.g. one revealed by Light Up the Stage), played greedily as before.
        // Main2DropEnabled (MTG_MAIN2_DROP, EngineFlags.h): the depth>0 SECOND main is now
        // searched-land too -- EnumeratePlansWithLand folds the still-open drop post-combat,
        // so the executor follows the plan's land exactly like the first main (the greedy
        // suppression note below remains true for the flag-off world it describes).
        const bool fold_land = (m_lookahead_depth > 0
                                && (is_pre_combat_main || Main2DropEnabled()
                                    // Reconsider mode (MTG_M2_RECONSIDER): m2 land handling is
                                    // plan-driven -- the solver's M2DropLive opens the land
                                    // dimension per-state and the executor follows the plan's
                                    // land exactly; the depth>0 greedy m2 fallback stays
                                    // suppressed (gi=141 lockstep), same as flag-off today.
                                    || M2ReconsiderEnabled()));

        if (!fold_land)
        {
            if (is_pre_combat_main && ap_ref.lands_played_this_turn == 0)
            {
                // "TH before land drop" heuristic: when Treasure Hunt is castable and no
                // enabler (RT/LE) is in play, defer the land drop to the second TakeTurn
                // pass so a land drawn by TH (possibly Reliquary Tower) can be used.
                bool defer_land = false;
                bool has_enabler = false;
                for (const Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index) { continue; }
                    auto def = CardDatabase::Instance().LookupCached(p.card);
                    if (!def) { continue; }
                    if (def->params.no_max_hand_size || def->params.discard_land_damage > 0)
                    { has_enabler = true; break; }
                }
                if (!has_enabler)
                {
                    ManaPool avail = AvailableManaPool(state);
                    ManaCost th_cost;
                    th_cost.generic = 1;
                    th_cost.blue    = 1;
                    for (const Card& c : ap_ref.hand)
                    {
                        auto def = CardDatabase::Instance().LookupCached(c);
                        if (!def || def->tmpl != CardTemplate::DrawUntilNonland) { continue; }
                        if (avail.CanPay(th_cost)) { defer_land = true; }
                        break;
                    }
                }
                // "Sylvan Scrying before the land drop" (USER, Creature Giving review
                // 2026-08-19): the ONE cast allowed to precede the drop -- it fetches a land
                // to hand (another Forbidden Orchard), so deferring lets the fetched land BE
                // the drop, played right after this main's casts (NOT the second-main pass:
                // a uses_second_main=no deck never runs one, and the first CG arm measured
                // the drop simply LOST, d0 +0.32). Provider-gated (payable-from-board-mana
                // fallback lives in the hook); default false -> byte-identical elsewhere.
                if (!defer_land
                    && ResolveProvider(state).LandDropAfterHandLandTutor(
                           state, state.active_player_index))
                { defer_land = true; m_tutor_deferred_drop = true; }
                if (!defer_land) { TryPlayLand(state); }
            }
            else
            {
                // Second main: play a land if a drop still remains.
                //
                // ONLY at depth 0. At depth>0 the SEARCH owns every land decision: the
                // pre-combat main folds the land into the search (incl. a deliberate
                // DEFER), and any land revealed mid-turn (Light Up the Stage / Treasure
                // Hunt) is replayed from the committed line's recorded breakpoint actions
                // (replay_recorded). The search's second main never plays a land
                // (FSLineTail enumerates via EnumeratePlans, no land fold), so an
                // autonomous greedy land here OVERRIDES the search's deliberate deferral
                // and diverges from the committed line: gi=141 (d5 s2002, seed 2143) the
                // executor played a deferred fetchland a turn early, fetching Overgrown
                // Tomb OUT of the library while the committed line kept it to draw -- the
                // in-window rollout/executor divergence that made the search "verify" an
                // uncastable Plague Drone line (predicted T5, realised T7). Suppressing it
                // keeps the executor in lockstep with the committed line. Opt-out restores
                // the old greedy behaviour for A/B (MTG_LEGACY_2ND_MAIN_LAND).
                static const bool s_legacy_2nd_main_land =
                    EnvOn("MTG_LEGACY_2ND_MAIN_LAND");
                if (m_lookahead_depth == 0 || s_legacy_2nd_main_land) { TryPlayLand(state); }
            }
        }

        if (m_lookahead_depth > 0)
        {
            // Lookahead. When fold_land is set the land drop is FOLDED INTO the search:
            // SolveWithLookahead enumerates each (land choice x spell subset) plus a
            // defer option and searches them together, and the same fold runs in its
            // rollout, so the land decision is modelled identically in the real game
            // and the rollout. We then play the chosen land before executing spells.
            // m_shared_tt is non-null only during the bottoming loop (BottomCards), so
            // every TakeTurn of that loop's rollouts shares one table; nullptr in normal
            // play, where SolveWithLookahead keeps its own per-decision table as before.
            SearchBudget budget = SearchBudget::FromVirtualMs(m_budget_ms);
            int committed_win       = m_max_turns + 1;
            int committed_sub_depth = 0;
            // EXPERIMENTAL (MTG_HONEST_PLAY, default off): run the search's forward model
            // DRAW-DECOUPLED -- each rollout turn plans against a reshuffled unseen library and
            // resolves against the true order (see g_honest_teacher). A cheap 1-sample proxy for
            // the reshuffle-averaged NON-clairvoyant policy (the untested goal-#1 lever). Byte-
            // identical when unset. Kept as an instrument, not a shipped play mode.
            static const bool s_honest_play = EnvOn("MTG_HONEST_PLAY");
            HonestTeacherGuard _htp(s_honest_play);
            // EXPERIMENTAL (MTG_NC_SEARCH, default off): the non-clairvoyant CEILING policy --
            // reshuffle-averaged search (each real decision ranks plans by K-reshuffle-averaged
            // honest depth-D win turn; execute the best against the true library, re-decide next
            // turn, no committed line). MTG_NC_K / MTG_NC_DEPTH tune the averaging / lookahead
            // (depth 0 = greedy non-clairvoyant). Byte-identical when unset. See
            // TurnSolver::ReshuffleAvgChoosePlan and learned-d0-policy.md.
            static const bool s_nc_search = EnvOn("MTG_NC_SEARCH");
            static const int  s_nc_k     = []{ const char* e = std::getenv("MTG_NC_K");
                                               return (e && *e) ? std::atoi(e) : 8; }();
            static const int  s_nc_depth = []{ const char* e = std::getenv("MTG_NC_DEPTH");
                                               return (e && *e) ? std::atoi(e) : 2; }();
            if (s_nc_search)
            {
                // Both mains use the reshuffle-averaged non-clairvoyant search -- NO greedy play on
                // a searched turn (the executed second main is searched, not Solve'd).
                plan = TurnSolver::ReshuffleAvgChoosePlan(state, s_nc_k, s_nc_depth,
                                                          m_max_turns, m_search_post_combat,
                                                          is_pre_combat_main);
            }
            else if (s_full_depth)
            {
                // Full-depth commit-the-line path: searches up to m_lookahead_depth
                // complete turns (no reduced rollout / greedy second main) via iterative
                // deepening under `budget`'s start gate, and REPLAYS the committed line
                // phase by phase, so the realised win equals the searched win. The whole
                // line is computed once at a pre-combat main when exhausted; each phase
                // then pops its plan. No non-convergence accounting yet (committed_win
                // left unset).
                // Refuted-follow: the game is proven unwinnable (full-coverage no-win, below), so
                // skip the full search entirely -- the else-branch fallback plays the greedy plan.
                // Never in a rollout: the PlayOut shares this engine and must search normally.
                if (is_pre_combat_main && m_committed_line.empty()
                    && !(s_refuted_follow && m_refuted_follow && !m_in_rollout))
                {
                    // m_shared_tt is non-null only during the bottoming loop, where
                    // the shared table lets sibling FullSearchLine calls reuse each
                    // other's tail rollouts; otherwise FullSearchLine owns a per-call
                    // table. Either way the greedy tail leaves are now memoized — the
                    // deep search no longer re-rolls identical leaf states. Lossless:
                    // SimulateToEnd is a pure function of its key.
                    // Hybrid value-leaf: run the cheap value-leaf, and escalate an UNVERIFIED line committed
                    // below the trust depth to the heuristic (see FullSearchLineHybrid). The escalate-below
                    // depth is, in priority order: the MTG_VALUE_MIN_DEPTH env override (experiments; 0 =>
                    // pure value-leaf, no escalation), else the deck's per-model value_trust_depth (knights/
                    // slivers = 5, where their leaf matches the heuristic -- verified-win-dominated), else
                    // m_lookahead_depth+1 (escalate at ANY committed depth up to the user depth: the raw leaf
                    // is weak everywhere below convergence, so default to fixing it whenever affordable).
                    // Inert unless a value model is attached (UseValueModel + <deck>.value.json).
                    // See TurnSolver / learned-d0-policy.md.
                    static const int s_vmd_env = []{ const char* e = std::getenv("MTG_VALUE_MIN_DEPTH");
                                                     return (e && *e) ? std::atoi(e) : -1; }();
                    // Per-job override (see ValueArm.h) so a pooled batch can carry both arms; -2 =
                    // unset => the env static, i.e. byte-identical off the batch path.
                    const int s_vmd_override = (valuearm::t_arm.value_min_depth >= -1)
                                             ? valuearm::t_arm.value_min_depth : s_vmd_env;
                    const int escalate_below =
                        (s_vmd_override >= 0)             ? s_vmd_override
                      : (m_profile.value_trust_depth > 0) ? m_profile.value_trust_depth
                                                          : m_lookahead_depth + 1;
                    int searched_depth = m_lookahead_depth;
                    // Leaf-cache source (MTG_LEAF_CACHE): the game-persistent rollout cache on the real
                    // top-level path (never in a rollout PlayOut or the bottoming loop). nullptr (flag off)
                    // => per-call local_table, byte-identical. See m_leaf_cache.
                    TranspositionTable* fd_tt =
                        (m_leaf_cache_enabled && !m_in_rollout && m_shared_tt == nullptr)
                            ? &m_leaf_cache : m_shared_tt;
                    // The value_play play-levers (budget renewal + beam) were tuned for the block's OWN search
                    // (its target_depth). They apply only when this search IS that one -- i.e. the resolved depth
                    // equals target_depth. In real play the block LOCKS the depth to target_depth so this always
                    // holds; only a harness case that pins a different depth (d3/d0 sanity, via
                    // --ignore-play-profile) runs the block's model at an off-policy depth, and there the levers
                    // must NOT fire (they'd over-prune a shallow search / change an off-production sanity result).
                    const bool vp_here = m_profile.value_play.drives()
                                      && m_lookahead_depth == m_profile.value_play.target_depth;
                    // The escalation BEAM is DEPTH-ADAPTIVE (FullSearchLineHybrid picks the deep vs shallow
                    // regime by search depth), so it fires whenever the block drives -- not only on-policy. At the
                    // deck's own (deep) depth this is the ADOPTED config (byte-identical); at a shallow off-policy
                    // depth it switches to the wide static leaf beam (measured neutral+faster). Budget renewal
                    // (fresh_frac) stays ON-POLICY-only (vp_here): it was tuned for the deck's deep search, and the
                    // shallow beam was measured on the legacy budget. -1 beam_width => no block => beam off => byte-
                    // identical; d0/d1/d2 are too shallow for any regime so the beam stays off there too.
                    const bool vp_beam = m_profile.value_play.drives();
                    // Per-decision reset for the fd-oracle's leaf-estimate diagnostic (no-op unless
                    // MTG_FD_ORACLE, which is the only thing that writes it).
                    if (s_fd_oracle) { TurnSolver::ResetLeafEstimate(); }
                    // Refuted-follow precondition: a zero before/after delta means nothing was
                    // truncated anywhere beneath this decision (budget skips, beams, wave skips
                    // all count) -- required to read its no-win as full-coverage proof.
                    const unsigned long long rf_trunc_before =
                        s_refuted_follow ? TurnSolver::TruncEvents() : 0;
                    TurnSolver::SearchLine line = TurnSolver::FullSearchLineHybrid(
                        state, m_lookahead_depth, m_max_turns, m_search_post_combat,
                        fd_tt, &budget, &searched_depth, escalate_below, m_budget_ms,
                        m_profile.value_no_fallback, m_profile.value_fallback_take_at,
                        // Per-deck escalation budget renewal; only the block's own on-policy search overrides the
                        // env static (-2.0 sentinel => use env => byte-identical otherwise).
                        vp_here ? m_profile.value_play.escalation_fresh_frac : -2.0,
                        // Per-deck escalation beam; fires whenever the block drives (depth-adaptive inside the
                        // hybrid). beam_leafdepth protects the top plies (the committed play). -1 => use env.
                        vp_beam ? m_profile.value_play.beam_width : -1,
                        vp_beam ? m_profile.value_play.beam_leafdepth : 2,
                        // Per-deck single-depth escalation cap; on-policy only (vp_here) -- the predicted-
                        // affordable single pass was tuned/measured at the deck's own depth. 0 => use env / off.
                        vp_here ? m_profile.value_play.escalation_cap : 0,
                        // Per-deck FROZEN cost-per-leaf R for the predicted walk (determinism). <=0 => 120 prior.
                        vp_here ? m_profile.value_play.escalation_r : -1.0);

                    // Oracle: track the EARLIEST win the search actually FOUND this game --
                    // i.e. a win VERIFIED inside the searched horizon (win_turn within
                    // turn + searched_depth - 1), the SAME condition that decides to commit
                    // the whole line (verified_win, below). A beyond-horizon leaf ESTIMATE is
                    // NOT a win the search found: it is never committed (the line is truncated
                    // to this turn), so a realised game that comes in later than an optimistic
                    // estimate is not a commit-the-line divergence -- it is just the leaf
                    // estimator being optimistic, which the engine correctly declines to trust.
                    // Gating on the verified horizon makes the oracle flag ONLY genuine
                    // "committed a verified win, did not realise it" divergences (rollout vs
                    // real execution), never leaf-estimate optimism (a budget/search-depth
                    // matter, measured elsewhere). The realised win is compared at game end
                    // (OnGameEnd) -- NOT per recompute, because a pre-combat recompute happens
                    // before that turn's combat and so can't see a win that arrives via combat.
                    const bool fd_verified =
                        line.win_turn <= state.turn_number + searched_depth - 1;
                    if (s_fd_oracle && !m_in_rollout && fd_verified
                        && line.win_turn < m_fd_best_win)
                    {
                        m_fd_best_win  = line.win_turn;
                        m_fd_best_turn = state.turn_number;
                        // Was this "verified" win SIMULATED, or merely PREDICTED by the value leaf?
                        // fd_verified only asks whether the win turn falls inside the searched horizon.
                        // That was a sound proxy while the leaf was SimulateToEnd (a real rollout to game
                        // end); the learned value leaf returns a bare estimate, and one landing INSIDE
                        // the horizon is indistinguishable here -- SearchLine carries a win_turn and
                        // nothing else. Record whether the committed win turn is exactly the leaf's best
                        // guess, so a flagged divergence can say which of the two it was.
                        m_fd_best_leafest = TurnSolver::MinLeafEstimate();
                    }

                    // Commit-only-verified: keep the WHOLE line only when it reaches a
                    // win VERIFIED inside the SEARCHED horizon (win turn within turn +
                    // searched_depth - 1, found by real simulation, not the greedy tail).
                    // searched_depth is the depth FullSearchLine actually reached -- the
                    // budget start gate can commit a pass shallower than m_lookahead_depth,
                    // so using the nominal depth here would misjudge a shallow greedy-tail
                    // estimate as verified and lock in an unverified line (turning a
                    // baseline win into a loss). When the win is only an estimate beyond
                    // the searched horizon, commit just THIS turn and re-search next turn,
                    // like baseline's per-turn re-deciding. (We still RANK this turn's play
                    // with the search; we just don't commit future turns on an estimate.)
                    // EXPERIMENT (env-gated, default off => byte-identical): always
                    // re-search every turn -- commit only THIS turn's phases and recompute
                    // from the realised state next turn, even when a win is verified in
                    // horizon. This is per-turn re-deciding driven by the full-depth search
                    // (search-primary): it lets the line adapt to each draw, recovering the
                    // gi252-class lines commit-the-line locks a turn slower. Perf cost = a
                    // FullSearchLine every turn instead of once per committed line.
                    static const bool s_fd_always_research =
                        EnvOn("MTG_FD_ALWAYS_RESEARCH");
                    const bool verified_win =
                        !s_fd_always_research
                        && line.win_turn <= state.turn_number + searched_depth - 1;
                    // FULL-COVERAGE REFUTATION (MTG_REFUTED_FOLLOW): no win exists within the
                    // game cap, the committed pass covered every remaining turn (the recursion
                    // hits the turn cap before the depth runs out, so leaves are terminal states,
                    // not estimates), and nothing anywhere was truncated. Every later turn's
                    // search explores a subset of this space: stop searching, keep the WHOLE
                    // best-graded lost line and follow it out.
                    const bool refuted_full =
                        s_refuted_follow && !m_in_rollout
                        && line.win_turn > m_max_turns
                        && state.turn_number + searched_depth - 1 >= m_max_turns
                        && TurnSolver::TruncEvents() == rf_trunc_before;
                    if (refuted_full) { m_refuted_follow = true; }
                    if (!verified_win && !refuted_full && !line.phases.empty())
                    {
                        // Keep the current turn only: its pre-combat phase plus any
                        // immediate second main (everything before the next pre-combat).
                        size_t keep = 1;
                        while (keep < line.phases.size()
                               && !line.phases[keep].is_pre_combat) { ++keep; }
                        line.phases.resize(keep);
                    }

                    for (const TurnSolver::PhasePlan& pp : line.phases)
                    {
                        m_committed_line.push_back(pp);
                    }
                }

                if (!m_committed_line.empty()
                    && m_committed_line.front().is_pre_combat == is_pre_combat_main)
                {
                    plan = m_committed_line.front().plan;
                    m_committed_line.pop_front();
                    fd_plan_committed = true;
                }
                else
                {
                    // No committed play for this phase: the search verified no win in
                    // horizon (even the greedy tail found none), so there is no line to
                    // commit. Rank this turn with the SAME full lookahead baseline uses
                    // -- on a FRESH budget so it is exactly the baseline decision -- not
                    // the static depth-0 Solve, which under-develops multi-turn combo
                    // setups: on a Treasure Hunt game (gi=129) static Solve idled ~10
                    // turns and won at 15 where the lookahead develops and wins at 6.
                    // This makes full-depth a strict superset of baseline -- it plays the
                    // baseline turn whenever it has no verified win to commit -- so it can
                    // never be worse than baseline when no win is in sight. Re-searches
                    // next turn; once a win enters the horizon the verified line is
                    // committed as usual. This plan carries no recorded breakpoint, so a
                    // draw engine in it re-solves (below).
                    // Refuted-follow: the game is proven unwinnable, so the full-lookahead
                    // fallback would re-prove the doom at full price -- play the greedy plan
                    // instead (the follow-out policy for phases the committed line no longer
                    // covers, e.g. after a re-line).
                    if (s_refuted_follow && m_refuted_follow && !m_in_rollout)
                    {
                        plan = TurnSolver::Solve(state, is_pre_combat_main);
                    }
                    else
                    {
                    SearchBudget fallback_budget = SearchBudget::FromVirtualMs(m_budget_ms);
                    plan = TurnSolver::SolveWithLookahead(state, is_pre_combat_main,
                                                          m_lookahead_depth, m_max_turns,
                                                          &fallback_budget, true,
                                                          m_search_post_combat, m_shared_tt,
                                                          &committed_win, &committed_sub_depth);
                    }
                }
            }
            else
            {
                plan = TurnSolver::SolveWithLookahead(state, is_pre_combat_main,
                                                      m_lookahead_depth, m_max_turns,
                                                      &budget, true, m_search_post_combat,
                                                      m_shared_tt,
                                                      &committed_win, &committed_sub_depth);
            }
            PROF_ADD_NODES(budget.Used());
            PROF_RECORD_DECISION(state.turn_number, is_pre_combat_main, budget.Used());
            // Committed-depth telemetry (MTG_ROLLOUT_STATS): what iterative deepening actually
            // reached under this decision's budget. Inert unless the flag is set.
            TurnSolver::RecordCommittedDepth(committed_sub_depth);

            // Cleanup-discard lockstep: pin the executing plan's searched shed choice for this
            // turn's real cleanup (ChooseDiscard). Write-when->=0, exactly like ApplyPlanDirect's
            // scripted_discard_choice write on the rollout side, so a second main's plan without
            // the axis leaves a pre-combat pin standing -- the same last-writer-wins the scored
            // line saw. Inert while every provider's axis width is 1 (no plan carries the field).
            if (plan.discard_choice >= 0) { m_discard_choice_pin = plan.discard_choice; }
            // Vial-charge lockstep: same write-when->=0 rule, but consumed at NEXT turn's upkeep
            // (DecideVialCharge) rather than this turn's cleanup -- the pin rides across the boundary.
            if (plan.vial_charge_choice >= 0) { m_vial_choice_pin = plan.vial_charge_choice; }
            // Searched dork attack/hold lockstep (MTG_DORK_ATK_SEARCH): the committed m1 plan's
            // combat-variant choice pins this turn's DeclareAttackers, so the realised combat is
            // the one the scored line simulated. Write-when->=0; m2 plans never carry the field.
            if (plan.atk_dork_release >= 0) { m_atk_release_pin = plan.atk_dork_release; }

            // Divergence log (MTG_DIVERGENCE_LOG): on the search-driven path, compare the search's
            // committed plan to what greedy d0 would do at this SAME untouched state (diagnosis only;
            // the game continues on `plan`). See s_divergence_log / dragonstorm-d0-divergence-digest.md.
            if (s_divergence_log && !m_in_rollout && is_pre_combat_main)
            {
                const TurnSolver::Plan greedy = TurnSolver::Solve(state, is_pre_combat_main);
                auto casts_of = [](const TurnSolver::Plan& p) {
                    std::vector<std::string> v;
                    for (const Action& a : p.actions)
                    {
                        const char* k = a.kind == Action::Kind::ActivateVial       ? "vial:"
                                      : a.kind == Action::Kind::CastFromGraveyard  ? "retrace:"
                                      : a.kind == Action::Kind::DiscardToLandsEdge ? "LE:"
                                      : "";
                        std::string t = std::string(k) + a.card_name;
                        if (!a.tutor_target.empty()) { t += ">" + a.tutor_target; }
                        if (a.chosen_x > 0)          { t += "@X" + std::to_string(a.chosen_x); }
                        v.push_back(t);
                    }
                    return v;
                };
                std::vector<std::string> sc = casts_of(plan), gc = casts_of(greedy);
                std::vector<std::string> scs = sc, gcs = gc;
                std::sort(scs.begin(), scs.end()); std::sort(gcs.begin(), gcs.end());
                const bool diverge = (scs != gcs);
                auto join = [](const std::vector<std::string>& v) {
                    std::string s;
                    for (size_t i = 0; i < v.size(); ++i) { if (i) { s += ", "; } s += v[i]; }
                    return s.empty() ? std::string("(idle)") : s;
                };
                const std::vector<int> feat = ExtractMidGameFeatures(state, MidGamePlanSummary{});
                static std::mutex s_dv_mtx;
                std::lock_guard<std::mutex> lk(s_dv_mtx);
                static std::ofstream dv_out(s_divergence_log, std::ios::app);
                static bool dv_header = false;
                if (dv_out.good())
                {
                    if (!dv_header)
                    {
                        dv_out << "# featnames:";
                        for (int i = 0; i < static_cast<int>(MidGameFeature::Count); ++i)
                        { dv_out << (i ? "," : " ") << MidGameFeatureName(static_cast<MidGameFeature>(i)); }
                        dv_out << "\n";
                        dv_header = true;
                    }
                    dv_out << "{\"seed\":" << state.game_seed
                           << ",\"turn\":" << state.turn_number
                           << ",\"diverge\":" << (diverge ? 1 : 0)
                           << ",\"search_land\":\"" << (plan.land_decided ? plan.land_to_play : std::string())
                           << "\",\"search\":\"" << join(sc) << "\""
                           << ",\"greedy\":\"" << join(gc) << "\",\"feat\":[";
                    for (size_t i = 0; i < feat.size(); ++i) { dv_out << (i ? "," : "") << feat[i]; }
                    dv_out << "]}\n";
                    dv_out.flush();
                }
            }

            // Non-convergence detection: only meaningful for real-game pre-combat
            // decisions (not the rollout's own searches, not second mains).
            if (s_flag_nonconv && !m_in_rollout && is_pre_combat_main)
            {
                FlagNonConvergence(state, plan, committed_win, committed_sub_depth);
            }

            // Trajectory probe for one game (MTG_NONCONV_TRACE_SEED).
            if (!m_in_rollout && is_pre_combat_main
                && static_cast<long long>(state.game_seed) == s_trace_seed)
            {
                int opp_creatures = 0;
                int my_creatures  = 0;
                for (const Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index && p.card.IsCreature())
                    { ++opp_creatures; }
                    if (p.controller_index == state.active_player_index && p.card.IsCreature())
                    { ++my_creatures; }
                }
                std::ostringstream os;
                os << "[traj] seed=" << state.game_seed
                   << " turn=" << state.turn_number
                   << " committed_win=" << committed_win
                   << " sub_depth=" << committed_sub_depth
                   << " opp_life=" << state.Opponent().life
                   << " opp_creatures=" << opp_creatures
                   << " my_creatures=" << my_creatures
                   << " | hand=";
                bool tfirst = true;
                for (const Card& c : state.ActivePlayer().hand)
                { os << (tfirst ? "" : ", ") << c.m_name << (c.m_is_staged ? "*" : ""); tfirst = false; }
                os << " | staged=";
                for (const StagedCard& sc : state.ActivePlayer().staged_cards)
                { os << sc.card.m_name << "(exp" << sc.expiry_turn << ") "; }
                os << " | libtop=";
                {
                    const Player& tp = state.ActivePlayer();
                    for (int li = 0; li < 12 && li < static_cast<int>(tp.library.size()); ++li)
                    { os << tp.library[li].m_name << "#" << tp.library[li].m_number << "; "; }
                }
                os << " | plan=";
                if (plan.land_decided && !plan.land_to_play.empty())
                { os << "[land " << plan.land_to_play << "] "; }
                if (plan.actions.empty()) { os << "(idle)"; }
                tfirst = true;
                for (const Action& a : plan.actions)
                {
                    const char* k = a.kind == Action::Kind::ActivateVial      ? "vial:"
                                  : a.kind == Action::Kind::CastFromGraveyard ? "retrace:"
                                  : a.kind == Action::Kind::DiscardToLandsEdge ? "LE:"
                                  : "";
                    os << (tfirst ? "" : ", ") << k << a.card_name; tfirst = false;
                }
                os << "\n";
                std::cerr << os.str();
            }

            // ORDER-CONDEMNATION stamp (executor half of the lockstep pair -- see
            // GameState::m1_hand): the plan is chosen, nothing has executed -- the same
            // post-plan / pre-land / pre-cast point ApplyPlanDirect stamps, so the split-turn
            // affordability test sees the same pool and the same plan costs in both worlds.
            if (is_pre_combat_main)
            {
                static const bool s_condemn_trace = EnvOn("MTG_CONDEMN_TRACE");
                TurnSolver::StampM1Hand(state, &plan.actions,
                                        s_condemn_trace && !m_in_rollout);
            }

            // What this line still owes, seeded at the SAME point ApplyPlanDirect seeds it -- plan
            // chosen, nothing executed -- so the executor's mid-line replicate gate and the rollout's
            // ask the identical question. Each cast decrements it as it pays (CastSpellFromHand).
            // Zero for every plan with no hand cast, and inert for every deck with no mid-line sink.
            LineUnpaidCostScope _luc(LineCastCostTotal(plan.actions));

            // MTG_LANDDROP_STATS (diagnostic): does the COMMITTED plan carry a searched land drop?
            // TurnSolver's counter cannot answer this -- every land play it sees is search-internal
            // by construction, so splitting it on g_real_resolution reads 100% rollout on every
            // deck and means nothing. m_in_rollout is the executor's own committed/rollout flag and
            // is the discriminator that actually separates the two.
            //
            // A committed plan with land_decided=false, while a playable land sits in hand, is the
            // case where the greedy ranker (SimulateLandPlay: first multi-colour land in HAND
            // ORDER, blind to yield) decides the real drop. That is the number that scopes the
            // USER's "no greedy in the searched window" directive for the land drop.
            if (is_pre_combat_main && !m_in_rollout && EnvOn("MTG_LANDDROP_STATS"))
            {
                static std::atomic<unsigned long long> s_committed{0}, s_searched{0}, s_greedy{0};
                struct Dump
                {
                    ~Dump()
                    {
                        if (!EnvOn("MTG_LANDDROP_STATS")) { return; }
                        std::fprintf(stderr,
                            "=== COMMITTED m1 plans: %llu | land SEARCHED %llu | "
                            "no searched land WITH one playable in hand %llu ===\n",
                            s_committed.load(), s_searched.load(), s_greedy.load());
                    }
                };
                static Dump s_dump;
                s_committed.fetch_add(1, std::memory_order_relaxed);
                if (plan.land_decided && !plan.land_to_play.empty())
                { s_searched.fetch_add(1, std::memory_order_relaxed); }
                else
                {
                    const Player& lap = state.ActivePlayer();
                    bool playable = lap.lands_played_this_turn < lap.LandDropsAvailable()
                        && std::any_of(lap.hand.begin(), lap.hand.end(), [](const Card& c)
                           { return !c.m_impulse_no_land && c.IsLand(); });
                    if (playable) { s_greedy.fetch_add(1, std::memory_order_relaxed); }
                }
            }

            if (fold_land && plan.land_decided && !plan.land_to_play.empty())
            {
                const CardDefinition* ld = CardDatabase::Instance().Lookup(plan.land_to_play);
                if (s_karoo_defer && ld && ld->params.etb_bounce_land)
                {
                    // Reserve the drop; play it after the main cast loop (see karoo_deferred).
                    karoo_deferred  = true;
                    karoo_land_name = plan.land_to_play;
                    karoo_fetch     = plan.fetch_target;
                }
                else
                {
                    // Same pin the search applied when it scored this plan (Plan::scry_choice), so
                    // the realised land ETB disposes of the top card exactly as the line assumed.
                    ScriptedTopChoice _stc(plan.scry_choice);
                    TryPlaySpecificLand(state, plan.land_to_play, plan.fetch_target, plan.land_face,
                                        plan.rad_mode);
                }
            }
        }
        else
        {
            // INSTRUMENT (MTG_M2_YIELD_STATS): does the EXECUTOR ever decide greedily at a
            // production depth? A greedy EVALUATION inside the search is the accepted design
            // (rollouts beyond the horizon / beyond what budget allows); a greedy DECISION here
            // would be a defect. Counted so the question is settled by measurement.
            execgreedy::Record(m_lookahead_depth, m_in_rollout);
            plan = TurnSolver::Solve(state, is_pre_combat_main);
        }
    }

    // Deploy a creature from hand via Aether Vial (lords boost subsequent spell evals).
    auto deploy_via_vial = [&](const std::string& name)
    {
        const CardDefinition* copt = CardDatabase::Instance().Lookup(name);
        if (!copt || !copt->card.IsCreature()) { return; }
        int mv = copt->card.m_mana_cost.ManaValue();
        Player& ap_v = state.ActivePlayer();
        auto hand_it = std::find_if(ap_v.hand.begin(), ap_v.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (hand_it == ap_v.hand.end()) { return; }
        int bf_sz = static_cast<int>(state.battlefield.size());
        for (int vi = 0; vi < bf_sz; ++vi)
        {
            Permanent& vp = state.battlefield[vi];
            if (vp.controller_index != state.active_player_index || vp.tapped) { continue; }
            const CardDefinition* vdef =
                CardDatabase::Instance().LookupCached(vp.card);
            if (!vdef || !vdef->params.upkeep_adds_charge) { continue; }
            if (vp.charge_counters != mv) { continue; }
            if (m_logger) { m_logger->LogCastSpell(hand_it->m_number, name, "Vial"); }
            Permanent perm;
            perm.card              = copt->card;
            perm.card.m_number     = hand_it->m_number;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            ap_v.hand.erase(hand_it);
            state.battlefield[vi].tapped = true;  // index access — safe after push_back
            // ETB dig / legend rule for Vial-deployed creatures (mirrors the rollout's
            // apply_vial; Vial is not a cast so no on-cast trigger fires).
            if (copt->params.etb_dig_count > 0)
            {
                PerformEtbDig(state, state.active_player_index, copt->params,
                              &state.battlefield.back());
            }
            // UNIVERSAL ETB cascade (USER 2026-08-27, Minotaur seed 1: a Vial-put Fanatic of
            // Mogis dealt NO devotion damage, missing a T5 lethal). A Vial put IS an
            // enters-the-battlefield event -- every ETB trigger fires (CR 603.6a); only CAST
            // triggers don't. This path fired only the dig + legend rule, silently skipping the
            // whole ETB suite (devotion burn, ETB burn, tokens, tutors, debuffs) -- and the
            // per-deck audit missed it because it hand-checked CASTS. Same pair the attack-dig
            // put path calls; lockstep twin in TurnSolver's apply_vial.
            {
                const int slot = static_cast<int>(state.battlefield.size()) - 1;
                FireEtbWatchers(state, state.active_player_index, slot);
                FireOwnEtbTriggers(state, state.active_player_index, slot);
            }
            if (copt->card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }
            break;
        }
    };

    // Same pin the search applied when it scored this plan (Plan::etbdig_choice), so the realised
    // ETB dig takes the card the line assumed. Executor/rollout lockstep: without it the search
    // would score one Knight and the real game would put a different one into hand. Scoped to the
    // rest of the phase; the first dig consumes it. -1 (no variant chosen) is inert.
    ScriptedEtbDig _sed_exec(plan.etbdig_choice);
    ScriptedReorder _sr_exec(plan.ponder_choice);   // executor/rollout lockstep for the Ponder disposition
    ScriptedTutor _stut_exec(plan.tutor_choice);    // executor/rollout lockstep for the searched tutor
                                                    // pick (index resolved at the true state); -1 inert
    ScriptedSacLand _ssac_exec(plan.sac_pins);      // executor/rollout lockstep for the searched
                                                    // sac-land picks (one entry per sac ordinal,
                                                    // resolved at the true state); empty inert
    ScriptedFreshMode _sfm_exec(plan.freshmode_choice);  // executor/rollout lockstep for the
                                                    // fresh-spend branch: the committed lethal's
                                                    // payment may crack the same-turn mint, so the
                                                    // replay must pay in the same world; 0 inert
    ScriptedTapMode _stm_exec(plan.tapmode_choice); // executor/rollout lockstep for the tapmode
                                                    // (TapReserve) axis -- the searched-choice
                                                    // audit's §C latent gap: the solver pinned
                                                    // this in ApplyPlanDirect but the executor
                                                    // re-priced under default rules, so any
                                                    // TapReserve sweep measured a chimera; 0 inert

    // Same for the searched Lackey put -- executor/rollout lockstep: without it the search would
    // score one Goblin entering and the real game would put a different one.
    if (plan.lackey_choice >= 0) { state.scripted_cheat_choice = plan.lackey_choice; }

    // Cast a spell from hand by name.
    // PRE-DRAW hand for the next resolve_draw_breakpoint -- lockstep twin of ApplyPlanDirect's
    // deferred_hand_before. Written by cast_by_name (the only point that reliably precedes the
    // cantrip's own draw) and bound by the scope inside resolve_draw_breakpoint. Declared here
    // because cast_by_name captures it. Empty is the SAFE value: every card then reads as new, so
    // both consumers stand down. See docs/design/breakpoint-phase-classification.md.
    std::vector<int> rdb_hand;
    // Name hashes of the committed plan's hand casts -- lockstep twin of ApplyPlanDirect's
    // plan_cast_names. A card the plan casts is never "declined", so the breakpoint filter keeps it.
    std::vector<std::uint64_t> rdb_plan_casts;

    auto cast_by_name = [&](const std::string& name, const std::string& tutor_target = "",
                            int chosen_x = 0, int own_targets = 0, int ponder_keep = -1,
                            int crackle_targets = -1,   // -1 = legacy auto-max discount (see Action::crackle_targets)
                            int splice_count = 0,       // Desperate Ritual splice count k (0 = plain cast)
                            const std::string& chosen_float_color = "",  // Apex of Power: searched float colour
                            int enchant_target = 0,      // Aura: searched creature to enchant (0 = none)
                            bool free_cast = false,      // Archangel: spend a banked free cast
                            bool bestow = false,         // Gnarled Scarhide: cast the AURA mode
                            int replicate_count = -1,    // Replicate: the count the PLAN pinned
                                                         // (-1 = greedy-max sink, the autonomous default)
                            int convoke_green = 0,       // Chord of Calling: committed convoke taps
                            int convoke_other = 0,
                            int phyrexian_life = 0,      // Phyrexian ({G/P}): life paid for pips
                            bool evoke = false)          // Evoke (Reveillark): alternate-cost self-sac cast
    {
        Player& ap = state.ActivePlayer();
        // PRE-CAST hand snapshot for the breakpoint drawn-card exemption (lockstep twin of
        // ApplyPlanDirect's apply_one capture). It MUST be taken here rather than where rdb_site is
        // armed: the arming sites run after `cast_by_name(...); resolve_now();`, so by then the
        // cantrip has already drawn and the new card would read as old -- the exact case the
        // exemption exists for. Gated on the levers that read it => ship pays nothing.
        if (TurnSolver::BreakpointHandSnapshotWanted(state))
        { rdb_hand = TurnSolver::HandCardNumbers(state); }
        // Prefer an expiring STAGED copy (Light Up the Stage etc.) over a persistent hand copy, so a
        // committed line that spends the staged copy this turn -- freeing the drawn copy for a later
        // turn -- replays exactly. Mirrors the land-play fix (TryPlaySpecificLand): the burn 6225
        // fd-diverge cast the drawn Skullcrack on T3 and lapsed the staged one, so the committed T4
        // second-Skullcrack line was never realised (predict T4, realise T6). Byte-identical when no
        // staged copy of `name` is in hand.
        auto it = ap.hand.end();
        for (auto c = ap.hand.begin(); c != ap.hand.end(); ++c)
        {
            if (c->m_name != name) { continue; }
            if (it == ap.hand.end()) { it = c; continue; }       // first match (fallback)
            // Prefer a staged (expiring) copy over a persistent hand copy, and among staged copies the
            // EARLIEST-EXPIRING one (kept in lockstep with ApplyPlanDirect's apply_one). Byte-identical
            // when no staged copy of `name` is in hand.
            if (c->m_is_staged && (!it->m_is_staged || c->m_staged_expiry < it->m_staged_expiry))
            {
                it = c;
            }
        }
        if (it == ap.hand.end()) { return; }
        ManaPool available = AvailableManaPool(state);
        CastSpellFromHand(state, *it, available, 0, tutor_target, chosen_x, own_targets, ponder_keep,
                          crackle_targets, splice_count, chosen_float_color, enchant_target, free_cast,
                          bestow, replicate_count, convoke_green, convoke_other, phyrexian_life, evoke);
    };

    // Cast a spell from hand via its alternative cost (Invigorate / Skyshroud Cutter /
    // Reverent Silence): no mana, the opponent gains alt_lifegain (-> damage with Remedy).
    auto cast_alt = [&](const std::string& name, int alt_lifegain)
    {
        Player& ap = state.ActivePlayer();
        auto it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == ap.hand.end()) { return; }
        // Cast-time guard (mirror of the rollout's): a risky alt payload (Reverent Silence)
        // committed by the search may realize on a board where its enabler diverged away
        // (commit-the-line non-convergence, gi=212). Re-check the gate on the realized board;
        // if no enabler survives the wipe and it isn't lethal, SKIP it rather than self-brick.
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(*it);
        if (adef && adef->params.destroy_all_enchantments
            && !ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, *adef, /*at_cast_time=*/true))
        {
            return;
        }
        ManaPool available = AvailableManaPool(state);
        CastSpellFromHand(state, *it, available, alt_lifegain);
    };

    // Cast a Retrace card from the graveyard, discarding `discard_lands` lands as the
    // additional cost. The card is removed from the graveyard onto the stack; on
    // resolution EffectHandler::MoveToGraveyard returns it (Retrace does not exile),
    // so it remains available to retrace again on a later turn.
    auto cast_from_graveyard = [&](const std::string& name, int discard_lands)
    {
        Player& ap = state.ActivePlayer();
        auto git = std::find_if(ap.graveyard.begin(), ap.graveyard.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (git == ap.graveyard.end()) { return; }
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        if (!def) { return; }

        // Pay the mana cost first; abort cleanly (graveyard untouched) if unpayable.
        ManaPool available = AvailableManaPool(state);
        ManaCost effective = EffectiveCost(*def, state);
        if (AffordAuditOn()) { g_afford_real_attempts.fetch_add(1, std::memory_order_relaxed); }
        if (!available.CanPay(effective))
        { if (AffordAuditOn()) { g_afford_real_fails.fetch_add(1, std::memory_order_relaxed); } return; }
        if (!TapForCost(state, effective, available, def->card.IsCreature()))
        { if (AffordAuditOn()) { g_afford_real_fails.fetch_add(1, std::memory_order_relaxed); } return; }

        // Additional cost: discard `discard_lands` land cards from hand to the graveyard.
        // WHICH land is provider-owned (RetraceDiscardCandidates), the SAME hook the rollout asks
        // in TurnSolver's retrace path. This site used to walk the hand and take the first land
        // itself, which was byte-identical only because the base rule is also first-in-hand-order:
        // the instant a provider ranked these, the rollout would have planned against one land and
        // the executor discarded another, and the projection would silently stop matching the game
        // it was projecting. Same lockstep requirement as Land's Edge's pitch order.
        // (claude-play executes retrace via TurnSolver::ApplyPlan, where the human chooser lives.)
        for (int discarded = 0; discarded < discard_lands; ++discarded)
        {
            std::vector<int> lands;
            for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
            {
                const CardDefinition* hdef = CardDatabase::Instance().Lookup(ap.hand[i].m_name);
                if (hdef ? hdef->card.IsLand() : ap.hand[i].IsLand()) { lands.push_back(i); }
            }
            if (lands.empty()) { break; }
            const std::vector<int> ranked =
                ResolveProvider(state).RetraceDiscardCandidates(state, state.active_player_index, lands);
            const int pick = ranked.empty() ? lands.front() : ranked.front();
            // MTG_TRACE=retrace: how often this decision happens and how CONTENDED it is. `cands`
            // is the width the ranking has to justify; `diff` says the ranking actually moved the
            // pick off hand order. Without these a null A/B cannot be told apart from a rule that
            // never fires -- the trap this deck's Land's Edge pitch fell into (97.6% of pitches
            // burn the whole hand, so the order was unobservable).
            TRACE("retrace", "T%d cands=%zu diff=%d -> %s", state.turn_number, lands.size(),
                  pick != lands.front() ? 1 : 0, ap.hand[pick].m_name.str().c_str());
            if (m_logger) { m_logger->LogDiscard(ap.hand[pick].m_number, ap.hand[pick].m_name); }
            ap.graveyard.push_back(ap.hand[pick]);
            ap.hand.erase(ap.hand.begin() + pick);
        }

        // Remove the source from the graveyard (re-find: push_back above may reallocate).
        git = std::find_if(ap.graveyard.begin(), ap.graveyard.end(),
            [&name](const Card& c) { return c.m_name == name; });
        int number = (git != ap.graveyard.end()) ? git->m_number : 0;
        if (git != ap.graveyard.end()) { ap.graveyard.erase(git); }

        StackEntry entry;
        entry.type             = StackEntry::EntryType::Spell;
        entry.source           = def->card;
        entry.source.m_number  = number;
        entry.controller_index = state.active_player_index;
        // Retrace cards in this set (Throes of Chaos) target nothing; if a future
        // retrace card needs targets, mirror CastSpellFromHand's targeting switch here.

        if (m_logger) { m_logger->LogCastSpell(number, name, effective.ToString() + " (retrace)"); }
        state.stack.push_back(std::move(entry));
        FireOnCastTriggers(state, *def);
        FireProwess(state, *def);
        // A retrace cast is a cast: its cascade fires exactly as a hand cast's does (this is
        // Throes of Chaos's ONLY effect -- since cascade moved to cast-trigger entries, this
        // call is what keeps retrace-cast Throes cascading at all).
        EffectHandler::PushCastTriggers(state, *def, state.active_player_index);
    };

    // Note whether an action casts a draw-engine spell (DrawUntilNonland / cascade /
    // a staging draw like Light Up the Stage); the caller gives a second pass so the
    // AI can play the newly drawn/staged cards. stages_cards is included so a spell
    // like Light Up the Stage gets the same draw-breakpoint the rollout's
    // ApplyPlanDirect already models (cast the draw spell, then re-solve and cast the
    // freshly revealed cards with the remaining mana). See stage_draw_break below.
    auto note_draw_engine = [&](const std::string& name)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (d && (d->tmpl == CardTemplate::DrawUntilNonland || d->params.cascade_max_mv > 0
                  || d->params.shuffle_reveal_freecast || d->params.etb_exile_until_nonland
                  || d->params.stages_cards || d->params.impulse_exile > 0
                  // Zada/Mirrorwing trick with a draw payload -- or a Treasure payload (Gold
                  // Rush), whose tokens are same-turn mana: a magnet fan-out mass-draws (or
                  // mass-mints Treasures), so the depth-0 executor gets a second pass to cast the
                  // freshly drawn/funded cards -- mirroring the rollout's deferred (site-3)
                  // post-cast re-solve. At depth>0 the committed line's recorded breakpoint
                  // script covers it (TakeTurn returns false under full-depth regardless of this
                  // flag).
                  || (d->params.solo_target_trick
                      && (d->params.cast_draw > 0 || d->params.creates_treasures > 0))
                  // MTG_ACQ_RESOLVE (mid-phase acquisition family): a tutor-to-hand fetch or a
                  // staged-exile dig acquires same-phase-castable material, so the depth-0
                  // executor gets the same second pass the rollout's deferred re-solve models.
                  // This clause was MISSING while is_draw_engine below had it -- depth-0 never
                  // re-solved after a tutor, so the fetched card (Sylvan Scrying -> Forbidden
                  // Orchard as the deferred drop) waited a turn (found in the Creature Giving
                  // review, 2026-08-19).
                  || (AcqResolveEnabled()
                      && (d->params.damage_equals_top_mv || d->params.tutor_to_hand))
                  // MTG_TOP_RESOLVE: a tutor-to-top (Worldly Tutor) re-arms the top-of-library
                  // consumers, so the depth-0 executor gets the same second pass the rollout's
                  // deferred re-solve arms -- the USER's "cast Worldly Tutor -> now activations
                  // and Turntimber can be cast", always taken at d0 when affordable.
                  || (TopResolveEnabled() && d->params.tutor_to_top)
                  // MTG_ACQ_DIG: an ETB dig (Acclaimed Contender) puts a same-phase-castable
                  // card in hand at resolution, so the depth-0 executor gets the same second
                  // pass the rollout's deferred re-solve arms. Cast path only (see flag note).
                  || (AcqDigEnabled() && d->params.etb_dig_count > 0)
                  // BREAKPOINT SITE 6 -- an Equipment entering under a Puresteel Paladin draws,
                  // and until this clause existed the drawn card could not be cast in the phase
                  // that drew it (0 of 150 games ever did). Depth 0 gets the second pass here;
                  // depth > 0 replays the committed continuation through the post-loop catch-all
                  // (`fd_plan_committed && !bp_replayed` below), which is the same point in the
                  // turn the rollout's deferred re-solve runs at -- after the trailing Equip /
                  // Stoneforge / Balan passes. Deliberately NOT added to is_draw_engine: that hook
                  // fires INLINE at the cast, i.e. BEFORE those passes, and equip costs move with
                  // metalcraft, so the two worlds would price the continuation differently.
                  // State-keyed, unlike every other clause here, because the draw is the WATCHER's
                  // -- see TurnSolver::EquipmentDrawBreakpoint. Evaluated pre-resolution, which is
                  // right: the Paladin has to be out ALREADY for its trigger to see this enter.
                  || TurnSolver::EquipmentDrawBreakpoint(state, *d)))
        { cast_draw_engine = true; }
    };

    // True for a draw spell that stages cards (e.g. Light Up the Stage). After casting
    // one we must STOP executing the rest of this pass's plan and defer it to the
    // second pass: the staged cards are revealed only after the spell resolves, and
    // they compete for the same mana as the plan's remaining spells. Continuing the
    // plan here would spend that mana (the rollout instead re-solves post-draw and
    // lets the planned spell fall away when the freshly revealed cards are better),
    // so the second pass re-solves from the post-draw state with the mana intact.
    auto stage_draw_break = [&](const std::string& name) -> bool
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        // Apex of Power stages 7 this-turn exiles that compete for the same mana as the plan's
        // remaining spells -> break here and re-solve post-exile, exactly like a stages_cards spell.
        return d && (d->params.stages_cards || d->params.impulse_exile > 0);
    };

    // A spell whose resolution reveals new cards to play (Light Up the Stage staging,
    // Treasure Hunt's DrawUntilNonland, cascade) -- the same set ApplyPlanDirect
    // re-solves after.
    auto is_draw_engine = [&](const std::string& name) -> bool
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        return d && (d->tmpl == CardTemplate::DrawUntilNonland || d->params.cascade_max_mv > 0
                     || d->params.shuffle_reveal_freecast || d->params.etb_exile_until_nonland
                     || d->params.stages_cards || d->params.expressive_iteration
                     || d->params.impulse_exile > 0
                     // Soulfire's staged exile / tutor fetch (MTG_ACQ_RESOLVE): the rollout arms a
                     // deferred re-solve after these, so the executor must classify them the same
                     // way or the committed continuation replays at the wrong breakpoint.
                     || (AcqResolveEnabled()
                         && (d->params.damage_equals_top_mv || d->params.tutor_to_hand))
                     // MTG_TOP_RESOLVE: the tutor-to-top reset arms the rollout's deferred
                     // re-solve (TurnSolver twin), so full-depth replay must classify it the
                     // same way or the committed continuation replays at the wrong breakpoint.
                     || (TopResolveEnabled() && d->params.tutor_to_top)
                     // MTG_ACQ_DIG is deliberately ABSENT here: the lever is depth-0-only
                     // (note_draw_engine above) -- the rollout never arms a dig breakpoint,
                     // so classifying one here would desync full-depth replay. See the
                     // rejected searched-depth arm at TurnSolver's PerformEtbDig site.
                     // SITE 6 is present only in the INLINE (partition) mode, and for exactly the
                     // reason the ACQ_DIG note gives: in that mode the rollout DOES arm at the cast,
                     // so the executor must classify it the same way or the committed continuation
                     // replays at the wrong breakpoint. In the deferred mode the rollout arms after
                     // every main cast instead, and the end-of-turn catch-all is the matching hook.
                     || (TurnSolver::EquipmentDrawBreakpointInline()
                         && TurnSolver::EquipmentDrawBreakpoint(state, *d)));
    };

    // SCRIPTED draw breakpoint for COMMIT-THE-LINE replay (MTG_FULL_DEPTH): cast the
    // EXACT cards the search recorded (plan.breakpoint_actions / Action::breakpoint_casts)
    // after a draw engine revealed them, instead of RE-SOLVING from the post-draw state.
    // The earlier re-solve diverged from the search on land-drop/mana state (it could
    // play a phantom extra land, or fail to afford a card the search had), so the realised
    // turn missed wins the search had verified within the horizon (e.g. Treasure Hunt +
    // Land's Edge: the search's breakpoint cast Land's Edge and discarded the drawn lands
    // for lethal, but the real re-solve left Land's Edge in hand). Replaying the verbatim
    // script keeps the real game in lockstep with the committed line. Recurses on each
    // recorded cast's own nested breakpoint_casts (a recorded draw engine that revealed
    // further cards). See project-full-depth-search (TH oracle class).
    std::function<void(const std::vector<Action>&)> replay_recorded =
        [&](const std::vector<Action>& recs)
    {
        if (EnvOn("MTG_FD_TRACE"))
        {
            std::fprintf(stderr, "[replay-bp] turn=%d recs=%d:", state.turn_number, (int)recs.size());
            for (const Action& a : recs) { std::fprintf(stderr, " %s", a.card_name.c_str()); }
            std::fprintf(stderr, "\n");
        }
        // The just-resolved staging spell put its revealed cards into staged_cards (the
        // real resolution path), but the cast helpers only see the hand. Merge unexpired
        // staged cards into hand first (mirroring the top-of-TakeTurn merge and
        // ApplyPlanDirect staging directly into hand) so cast_by_name can find them.
        Player& rp = state.ActivePlayer();
        std::vector<StagedCard> snap = rp.staged_cards;
        rp.staged_cards.clear();
        for (StagedCard& sc : snap)
        {
            if (sc.expiry_turn < state.turn_number) { continue; }
            sc.card.m_is_staged     = true;
            sc.card.m_staged_expiry = sc.expiry_turn;
            rp.hand.push_back(sc.card);
        }

        // CONTINUATION TRAITS (lockstep with ApplyPlanDirect's deferred-continuation apply): pay
        // the recorded continuation under PlanTraits computed from ITS actions, not the main
        // plan's still-open scopes (the mirrorwing gi43 divergent-payment class -- see
        // resolve_draw_breakpoint's twin install). Null scope (levers off) = unchanged.
        PlanTraits _rec_traits;
        if (PlanTraitsWanted()) { _rec_traits = TurnSolver::ComputePlanTraits(state, recs); }
        PlanTraitsScope  _rec_scope(PlanTraitsWanted() ? &_rec_traits : nullptr);
        TapKeepLastScope _rec_keep(PumpTargetHoldEnabled() ? _rec_traits.pump_target_card : 0);

        for (const Action& a : recs)
        {
            if (a.kind == Action::Kind::PlayLand)
            { TryPlaySpecificLand(state, a.card_name); }
            else if (a.kind == Action::Kind::ActivateVial)
            { deploy_via_vial(a.card_name); resolve_now(); }
            else if (a.kind == Action::Kind::CastFromHand)
            { if (a.convoke_green > 0 || a.convoke_other > 0)
              { ApplyConvokeTaps(state, state.active_player_index, a.convoke_green, a.convoke_other); }
              if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); } else { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); } resolve_now(); }
            else if (a.kind == Action::Kind::CastFromGraveyard)
            { cast_from_graveyard(a.card_name, a.discard_lands); resolve_now(); }
            else if (a.kind == Action::Kind::SacForMana)
            { ApplySacForMana(state, state.active_player_index, a.sac_source_id,
                              TurnSolver::SacFloatColorFor(state, plan.actions, a), a.ritual_float, a.sac_victim_id); }
            else if (a.kind == Action::Kind::Suspend)
            { ApplySuspend(state, state.active_player_index, a.card_name); }
            else if (a.kind == Action::Kind::DigDraw)
            { PerformDig(state, a.card_name, a.dig_sacrifice); }
            // Nested breakpoint casts this recorded draw engine (or dug Treasure Hunt) revealed.
            if (!a.breakpoint_casts.empty()) { replay_recorded(a.breakpoint_casts); }
        }
    };

    // Fallback draw breakpoint for the NON-committed full-depth plan (the develop-when-
    // stuck Solve plan, which carries no recorded script): re-solve from the post-draw
    // state and cast revealed cards, recursing on further draws. This is the rare
    // no-win-found path, so the re-solve's land/mana drift is harmless (no win to miss).
    // Depth bound: this fallback RE-SOLVES and re-casts after each draw engine, recursing on the
    // next draw engine it casts. A cost-reduced cantrip/dig chain (Hinata makes its spells cheap and
    // Reality Spasm refloats mana) can chain dozens deep in one turn -- enough to overflow the stack,
    // since each frame holds a full TurnSolver::Plan (a real game crashed at ~241 levels). No real
    // turn re-solves this many times after draws, and this is the no-win greedy-pilot fallback whose
    // drift is already documented as harmless, so capping the chain only stops a pathological loop --
    // it never abandons a win (the win paths use the bounded committed-line replay, not this).
    constexpr int  kMaxDrawBreakpointDepth = 40;
    // The depth cap alone does NOT bound this recursion against a NO-PROGRESS loop. Observed at
    // Dragonstorm d0: the greedy re-solve returns an UNREALIZABLE draw-engine line (cast Apex of Power off
    // ritual float the board can't actually pay for), the executor no-ops it, and the state is left
    // FROZEN (hand/mana/float identical every level). But the recursion at line ~1943 fires on
    // is_draw_engine(NAME) regardless of whether the cast happened, so it re-solves the same frozen state
    // -> same phantom plan -> recurses forever (re-entered from the outer cast loop -> millions of no-op
    // calls, a hang). A TOTAL per-main-phase invocation budget bounds it. This is the harmless no-win
    // greedy fallback (wins replay the committed line, not this path); a real draw chain is deep at most
    // (each real cast consumes its card), well under the budget, so the cap only stops the phantom spin.
    constexpr long kMaxDrawBreakpointCalls = 4096;
    long rdb_calls = 0;
    // SEARCHED breakpoint continuation (TurnSolver::Plan::bp_choice / bp_at): consumed at the
    // breakpoint whose INDEX equals bp_at, mirroring ApplyPlanDirect exactly. Counting here (rather
    // than a used-flag) is what keeps a nested searched continuation in lockstep -- the rollout
    // scored bp_at, so the executor must apply it at that same breakpoint, not the first one.
    int bp_seen_exec = 0;
    // MTG_CANTRIP_ORDER site for the NEXT resolve_draw_breakpoint call (set right before each
    // call; the lambda binds it at entry). Lockstep twin of the rollout's deferred_cantrip_site
    // binding in ApplyPlanDirect -- both worlds must enumerate the continuation under the same
    // watermark or the searched bp_choice indexes a different list. Inert when the lever is off.
    const CardDefinition* rdb_site = nullptr;
    std::function<void(int)> resolve_draw_breakpoint = [&](int bp_depth)
    {
        if (bp_depth >= kMaxDrawBreakpointDepth || ++rdb_calls > kMaxDrawBreakpointCalls) { return; }
        // karoo_deferred: the executor reserves a Karoo drop for after the main cast loop exactly as
        // ApplyPlanDirect does, so it must tell the breakpoint enumeration the same thing -- a
        // RESERVED drop is not a declined one (MTG_BP_CONDEMN_LAND). Lockstep pair.
        TurnSolver::CantripOrderScope _cos(rdb_site, &rdb_hand, &rdb_plan_casts,
                                           ResolveProvider(state).CondemnsConsideredAtBreakpoint(),
                                           karoo_deferred,
                                           TurnSolver::ManaSourceCount(state));
        // Executor twin of the rollout's marker (MTG_CONDEMN_M1_BP) -- the lockstep pair. Without
        // it the executor would re-offer at a breakpoint what the rollout condemned there.
        TurnSolver::BpContinuationScope _cbs;
        Player& rp = state.ActivePlayer();
        std::vector<StagedCard> snap = rp.staged_cards;
        rp.staged_cards.clear();
        for (StagedCard& sc : snap)
        {
            if (sc.expiry_turn < state.turn_number) { continue; }
            sc.card.m_is_staged     = true;
            sc.card.m_staged_expiry = sc.expiry_turn;
            rp.hand.push_back(sc.card);
        }
        // Honour the plan's SEARCHED breakpoint continuation. The search ranked this turn assuming
        // candidate k of the breakpoint's own plan list, so the executor must play candidate k --
        // re-solving greedily here would realise a turn the search never scored (a rollout/execution
        // divergence, exactly the class of bug the HoldDeferredDrop* mirrors below exist to prevent).
        // EnumerateBreakpointPlans is the SHARED list (fan-out suppressed) ApplyPlanDirect indexed.
        // Inert when bp_choice < 0, i.e. for every plan under MTG_BP_SEARCH=0.
        TurnSolver::Plan extra;
        bool bp_searched_here = false;
        const int bp_idx = (plan.bp_choice >= 0) ? bp_seen_exec++ : -1;
        if (bp_idx == plan.bp_at)
        {
            const std::vector<TurnSolver::Plan> cands =
                TurnSolver::EnumerateBreakpointPlans(state, is_pre_combat_main);
            if (plan.bp_choice < static_cast<int>(cands.size()))
            {
                extra            = cands[plan.bp_choice];
                bp_searched_here = true;
                // The continuation's land drop is part of the searched decision; play it first so
                // its mana funds the casts (mirrors bp_play_searched_land in ApplyPlanDirect).
                // karoo_deferred guard (audit §6.8, MTG_KAROO_BP_LOCKSTEP=0 to restore): the
                // rollout's bp_play_searched_land SUPPRESSES the continuation land while the drop
                // is reserved for the deferred Karoo; without the same guard here the executor
                // consumed the drop and never played the Karoo the rollout scored -- a plain
                // rollout/executor divergence (the class the HoldDeferredDrop mirrors prevent).
                static const bool s_karoo_lockstep = EnvOn("MTG_KAROO_BP_LOCKSTEP", true);
                if (extra.land_decided && !extra.land_to_play.empty()
                    && !(s_karoo_lockstep && karoo_deferred))
                { TryPlaySpecificLand(state, extra.land_to_play, extra.fetch_target, extra.land_face); }
            }
        }
        if (!bp_searched_here) { execgreedy::Record(-1, m_in_rollout);
                                 execgreedy::RecordBpCause(plan.bp_choice >= 0);
                                 extra = TurnSolver::Solve(state, is_pre_combat_main); }
        // Lockstep trace (MTG_BP_TRACE): the EXECUTOR's breakpoint sequence, to be diffed against
        // ApplyPlanDirect's [bp-apply] lines for the same committed line. Diagnosis only.
        if (BpTraceEnabled())
        {
            std::fprintf(stderr, "[bp-exec]  turn=%d idx=%d bp_at=%d bp_choice=%d searched=%d\n",
                         state.turn_number, bp_idx, plan.bp_at, plan.bp_choice,
                         bp_searched_here ? 1 : 0);
        }
        // CONTINUATION TRAITS (lockstep with ApplyPlanDirect's deferred-continuation apply): the
        // continuation is its own mini-plan -- pay it under PlanTraits computed from ITS actions,
        // not the main plan's still-open scopes (which the rollout's continuation never saw; the
        // mirrorwing gi43 divergent-Draught-payment class). Null scope (levers off) = unchanged.
        PlanTraits _cont_traits;
        if (PlanTraitsWanted()) { _cont_traits = TurnSolver::ComputePlanTraits(state, extra.actions); }
        PlanTraitsScope  _cont_scope(PlanTraitsWanted() ? &_cont_traits : nullptr);
        TapKeepLastScope _cont_keep(PumpTargetHoldEnabled() ? _cont_traits.pump_target_card : 0);
        // Lotus Bloom: apply any SacForMana (float the chosen colour) / Suspend this re-solve chose BEFORE
        // the casts, mirroring TakeTurn's top-of-turn pre-pass. Without this a mid-turn Lotus sac was
        // silently dropped in the fallback breakpoint re-solve, so the floated mana never materialised and
        // the staged Dragonstorm/rituals it was meant to pay for no-op'd. Empty (no SacForMana) -> unchanged.
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::SacForMana)
            { ApplySacForMana(state, state.active_player_index, a.sac_source_id,
                              TurnSolver::SacFloatColorFor(state, extra.actions, a), a.ritual_float, a.sac_victim_id); }
            else if (a.kind == Action::Kind::Suspend)
            { ApplySuspend(state, state.active_player_index, a.card_name); }
            else if (a.kind == Action::Kind::CastFromHand
                     && (a.convoke_green > 0 || a.convoke_other > 0))
            { ApplyConvokeTaps(state, state.active_player_index, a.convoke_green, a.convoke_other); }
        }
        for (const Action& a : extra.actions)
        { if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); } }
        // Continuation casts in the SAME canonical order the rollout's apply_plan_actions
        // realises (ordering-audit 2026-08-15, item 2: this loop ran in RAW plan order, so a
        // continuation holding more than one cast could execute a different sequence than the
        // one the search scored -- the classic lockstep failure, previously masked because
        // continuations were near-singletons). searched_order pins the vector order; otherwise
        // an OPAQUE set (cantrip/staging present) applies enablers first (CastOrderRank-stable,
        // rest in plan order) and a CLEAN set stable-sorts by CastOrderLess -- the exact branch
        // pair of the main cast loop below / TurnSolver's apply.
        std::vector<int> cont_order;
        for (int i = 0; i < static_cast<int>(extra.actions.size()); ++i)
        {
            const Action& a = extra.actions[i];
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { cont_order.push_back(i); }
        }
        if (!extra.searched_order && cont_order.size() > 1)
        {
            bool cont_opaque = false;
            for (int i : cont_order)
            { if (OrderingOpaque(extra.actions[i].card_name)) { cont_opaque = true; break; } }
            if (cont_opaque)
            {
                std::vector<int> ena, rest;
                for (int i : cont_order)
                {
                    const Action& a = extra.actions[i];
                    const bool is_ena = !a.alt_cost
                        && ResolveProvider(state).CastEnablerFirst(state, a.card_name);
                    (is_ena ? ena : rest).push_back(i);
                }
                std::stable_sort(ena.begin(), ena.end(), [&](int x, int y)
                {
                    const CardDefinition* dx = CardDatabase::Instance().Lookup(extra.actions[x].card_name);
                    const CardDefinition* dy = CardDatabase::Instance().Lookup(extra.actions[y].card_name);
                    if (!dx || !dy) { return false; }
                    return ResolveProvider(state).CastOrderRank(state, *dx)
                         < ResolveProvider(state).CastOrderRank(state, *dy);
                });
                // Mirror of the main opaque loop / apply_plan_actions (lockstep): with the opaque
                // rank-sort active (global arm or a provider's adopted order, e.g. Mirrorwing's
                // MTG_MW_ORDERED), the non-enabler rest is rank-ordered + laddered too, so a
                // continuation realises the same sequence the rollout scored.
                if (OpaqueCastOrderActive(state))
                {
                    std::stable_sort(rest.begin(), rest.end(), [&](int x, int y)
                    { return CastOrderLess(state, extra.actions[x], extra.actions[y]); });
                    ApplyCastOrderRangeLadder(state, extra.actions, rest);
                    ApplyEnablerWipeRecheck(state, extra.actions, rest);
                }
                cont_order = std::move(ena);
                cont_order.insert(cont_order.end(), rest.begin(), rest.end());
            }
            else
            {
                std::stable_sort(cont_order.begin(), cont_order.end(), [&](int x, int y)
                { return CastOrderLess(state, extra.actions[x], extra.actions[y]); });
            }
        }
        for (int ci : cont_order)
        {
            const Action& a = extra.actions[ci];
            {
                cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); resolve_now();
                if (is_draw_engine(a.card_name))
                {
                    rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                    if (TurnSolver::BreakpointHandSnapshotWanted(state))
                    {
                        rdb_plan_casts.clear();
                        for (const Action& pa : extra.actions)
                        {
                            if (pa.kind != Action::Kind::CastFromHand) { continue; }
                            rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                        }
                    }
                    resolve_draw_breakpoint(bp_depth + 1);
                }
            }
        }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); resolve_now(); }
        }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromGraveyard)
            { cast_from_graveyard(a.card_name, a.discard_lands); resolve_now(); }
        }
        // Flood-keep (fallback path): if the draw overfilled the hand and the land drop is
        // still open (deferred before Treasure Hunt), play it now -- TryPlayLand prioritizes a
        // drawn Reliquary Tower when flooding (see its pre-pass), keeping the whole draw as
        // Land's Edge ammo instead of discarding it at cleanup (gi=65). Only when flooding, so
        // non-flood draw turns keep their normal land timing. HOLD the drop when the hand is the
        // marginal Land's Edge ammo for a lethal this turn (HoldDeferredDropForLethal, mirrors
        // the search's
        // play_drawn_flood_keep_land) so the deferred drop isn't spent out of the ammo pool.
        // HoldDeferredDropForFurtherDig is mirrored here for the SAME reason as
        // HoldDeferredDropForLethal: the rollout applies it in
        // play_drawn_flood_keep_land, so an executor that developed the drop anyway would play a land
        // the proved line does not -- a rollout/execution divergence, not a heuristic disagreement.
        // The keep-land case (PostDrawKeepLandName) still goes through TryPlayLand's own Reliquary pre-pass.
        // Skipped entirely when the continuation was SEARCHED: the drop is then part of the scored
        // plan (played above, or deliberately deferred), and a static flood-keep play on top would
        // be exactly the un-searched land choice this whole mechanism removes.
        if (!bp_searched_here
            && is_pre_combat_main
            && static_cast<int>(state.ActivePlayer().hand.size()) > 7
            && !ResolveProvider(state).HoldDeferredDropForLethal(state, state.active_player_index))
        {
            const int          apx  = state.active_player_index;
            const std::string  keep = ResolveProvider(state).PostDrawKeepLandName(state, apx);
            if (!keep.empty() || !ResolveProvider(state).HoldDeferredDropForFurtherDig(state, apx))
            { TryPlayLand(state); }
        }
    };

    // Canonical execution order: Vial deployments first (lords live before spell casts),
    // then regular spells (their lands tap first), then sacrifice-land spells, then
    // graveyard (Retrace) casts last. Each cast is resolved before the next (when a
    // resolver was supplied) so same-phase interactions (prowess, lords, spectacle,
    // on-cast triggers) see the up-to-date board/life, matching the lookahead rollout.
    // Set once a staging draw spell is cast: defer the rest of the plan to the second
    // pass (which re-solves from the post-draw state with the remaining mana), so the
    // real game executes the same draw-breakpoint line the rollout searches.
    bool staged_break = false;
    bool bp_replayed  = false;  // commit-the-line: recorded breakpoint replayed once
    // PARTITION truncation (MTG_EQUIP_DRAW_BP_INLINE) -- executor twin of ApplyPlanDirect's
    // bp_truncate. Once a site-6 continuation has run at the cast that drew, the rest of this
    // plan's casts belong to that continuation's section and the rollout did NOT apply them here;
    // executing them anyway would realise a turn the search never scored. Separate from
    // staged_break on purpose: that flag also suppresses the alt-payload auto-fire and the
    // end-of-turn catch-all replay, and borrowing it would silently change those too.
    bool bp_trunc_exec = false;
    // Does casting `nm` end our section? Same predicate the rollout arms on, so the two worlds
    // truncate at exactly the same cast. Evaluated post-resolution, where the Equipment and the
    // watcher are both on the battlefield.
    auto equip_bp_truncates = [&](const std::string& nm) -> bool
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        if (d == nullptr) { return false; }
        // THE PARTITION SHAPE for plain cantrips (MTG_BP_PARTITION_CANTRIP) -- the executor half.
        // ApplyPlanDirect truncates its section the moment a plain cantrip's continuation has run;
        // if the executor kept casting the plan's tail here it would realise a turn the search
        // never scored, which is the divergence the deferred shape was originally chosen to avoid.
        // EXACT mirror of the rollout's `plain_cantrip`, and all three clauses are load-bearing.
        // The rollout reaches that branch only inside `def.tmpl == CardTemplate::DrawSpell`; an
        // earlier version of this predicate dropped that clause and so returned true for EVERY
        // cast, because equip_bp_truncates is consulted for every action in the executor's cast
        // loop rather than only for draws. The executor then truncated after the FIRST cast of any
        // kind: measured on hold gi=18, T1 cast Sol Ring and dropped Ornithopter, T2 cast Preordain
        // and dropped Hinata, turning a turn-3 win into a turn-6 one, with 46 of 60 games worse.
        // MTG_BP_NODE truncates the SAME cast, but only for a plan CARRYING a continuation
        // choice (bp_choice >= 0, incl. the empty sentinel) -- the exact mirror of the rollout's
        // shape-(2) condition. A committed plan WITHOUT a choice (a tranche/group-wave plan the
        // search scored full-tail-greedy) must execute its tail, and a non-committed greedy plan
        // (bp_choice < 0 always) is untouched.
        if ((TurnSolver::PartitionCantrip() || (TurnSolver::BpNodeSearch() && plan.bp_choice >= 0))
            && d->tmpl == CardTemplate::DrawSpell
            && !d->params.expressive_iteration && !d->params.stages_cards)
        { return true; }
        // MTG_BP_NODE_D56: the node hosts the other two DEFERRED classes too, and each one's
        // partition needs its executor twin here for exactly the reason the cantrip clause above
        // spells out -- a committed plan carrying a continuation choice was SCORED with its tail
        // truncated at this cast, so executing that tail realises a turn the search never scored.
        // Both clauses mirror ApplyPlanDirect's arming conditions cast-for-cast; the PUT-armed
        // site-6 case is deliberately absent on BOTH sides (see the note at that arming point).
        if (TurnSolver::BpNodeSearch() && plan.bp_choice >= 0)
        {
            const int hosted = TurnSolver::BpNodeHostedSites();
            // Site 5 -- solo-target trick with a draw or Treasure payload (Gold Rush, Mirrorwing).
            if ((hosted & (1 << 5)) != 0 && d->params.solo_target_trick
                && (d->params.cast_draw > 0 || d->params.creates_treasures > 0))
            { return true; }
            // Site 6 -- an Equipment cast under a live ETB-draw watcher. Only the DEFERRED shape
            // is new here; the inline one already truncates through the clause below.
            if ((hosted & (1 << 6)) != 0 && !TurnSolver::EquipmentDrawBreakpointInline()
                && TurnSolver::EquipmentDrawBreakpoint(state, *d))
            { return true; }
        }
        if (!TurnSolver::EquipmentDrawBreakpointInline()) { return false; }
        return TurnSolver::EquipmentDrawBreakpoint(state, *d);
    };

    // Order trace (MTG_ORDER_TRACE, inert by default): print the committed hand-cast
    // sequence per pre-combat main, tagged with searched_order, so a heuristic-vs-search
    // (MTG_SEARCH_ORDER) A/B can see WHICH reorder the search chose. The skill's
    // heuristic-accuracy process uses this to author a provider ordering heuristic that
    // reproduces the search's pick. Single-thread + --game-index N for a clean per-game read.
    static const bool s_order_trace = EnvOn("MTG_ORDER_TRACE");
    if (s_order_trace && is_pre_combat_main && !m_in_rollout)
    {
        // Print the ACTUAL executed order of non-sacrifice hand casts: rank-sorted for a
        // clean set, plan order for an opaque (draw/staging) set or a searched_order plan.
        std::vector<int> ns;
        bool opaque = false;
        for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
        {
            const Action& a = plan.actions[i];
            if (a.kind != Action::Kind::CastFromHand || a.sacrifice_land) { continue; }
            ns.push_back(i);
            if (OrderingOpaque(a.card_name)) { opaque = true; }
        }
        if (!opaque && !plan.searched_order)
        {
            std::stable_sort(ns.begin(), ns.end(), [&](int x, int y)
            { return CastOrderLess(state, plan.actions[x], plan.actions[y]); });
        }
        std::string seq;
        for (int i : ns)
        {
            if (!seq.empty()) { seq += ", "; }
            seq += plan.actions[i].card_name;
            if (plan.actions[i].alt_cost) { seq += "(alt)"; }
        }
        std::fprintf(stderr, "[ord] turn=%d searched=%d opaque=%d casts: %s\n",
                     state.turn_number, plan.searched_order ? 1 : 0, opaque ? 1 : 0,
                     seq.empty() ? "(none)" : seq.c_str());
    }

    // Audit-only: a fresh per-plan dropped-cast list, so the stranded-equip detector below cannot
    // see a drop from an earlier plan on this worker thread. No-op unless the audit is on.
    if (AffordAuditOn()) { ResetDroppedCastNumbers(); }

    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); }
    }
    // Lotus Bloom: apply SacForMana (float the chosen colour) and Suspend BEFORE the batch pre-pay /
    // casts, exactly as the rollout's ApplyPlanDirect does at this same logical point -> lockstep. Both
    // loops are empty for every deck without a Lotus (no SacForMana/Suspend action) -> byte-identical.
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::SacForMana)
        { ApplySacForMana(state, state.active_player_index, a.sac_source_id,
                          TurnSolver::SacFloatColorFor(state, plan.actions, a), a.ritual_float, a.sac_victim_id); }
        else if (a.kind == Action::Kind::Suspend)
        { ApplySuspend(state, state.active_player_index, a.card_name); }
        // Convoke (Chord of Calling): tap the chosen bodies BEFORE the batch pre-pay / casts, so
        // AvailableManaPool no longer counts anything convoke consumed; the action's cost was
        // reduced at enumeration by exactly their contribution. Same deterministic body order in
        // both worlds (ApplyConvokeTaps) -> lockstep.
        else if (a.kind == Action::Kind::CastFromHand
                 && (a.convoke_green > 0 || a.convoke_other > 0))
        { ApplyConvokeTaps(state, state.active_player_index, a.convoke_green, a.convoke_other); }
    }
    // PLAN TRAITS -- executor mirror of ApplyPlanDirect (lockstep, same builder): in scope over the
    // prepay and every cast payment below. Null scope (levers off) changes nothing.
    PlanTraits _plan_traits;
    if (PlanTraitsWanted()) { _plan_traits = TurnSolver::ComputePlanTraits(state, plan.actions); }
    PlanTraitsScope _plan_traits_scope(PlanTraitsWanted() ? &_plan_traits : nullptr);
    TapKeepLastScope _keep_last(PumpTargetHoldEnabled() ? _plan_traits.pump_target_card : 0);
    // Whole-turn batch pre-payment -- mirror of ApplyPlanDirect (lockstep): tap for the combined
    // cost of the main hand casts and pre-load floating so the casts below drain the pool instead of
    // the stranding per-cast greedy. Same (state, plan.actions) inputs as the rollout at the same
    // logical point (after the land drop + Vial deploys) -> identical prepay. Declined -> greedy.
    TurnSolver::BatchPrepayMainCasts(state, plan.actions);
    // Indices of sac-land casts hoisted ahead of the Spectacle spell (mirrors ApplyPlanDirect);
    // the trailing sac loop skips them so they are not double-cast. Empty unless a Spectacle
    // enabler is hoisted below.
    std::set<size_t> spec_hoisted_sac;
    // MANA-UNLOCK equip -- executor mirror of ApplyPlanDirect (lockstep, same two functions): a
    // haste-granting Equipment onto a still-locked mana dork is what pays for a later cast in this
    // plan, so it fires the moment both pieces are on the battlefield rather than in the trailing
    // equip pass below. Once up front (both pieces may already be out) and once after each cast.
    // The reserve scope is the other half -- it stops the enabler casts from spending the payoff's
    // scarce colour before the unlock lands. Both no-ops for every plan without that pairing.
    // See TurnSolver::ApplyManaUnlockEquips / ::ManaUnlockColorReserve.
    PlanSourceReserveScope _unlock_reserve(TurnSolver::PlanReserveSources(state, plan.actions));
    auto fire_unlock = [&]() { TurnSolver::ApplyManaUnlockEquips(state, plan.actions); };
    fire_unlock();
    // Cast-ordering search (C): a committed plan with searched_order set carries an
    // EXPLICIT interleaving the search scored (e.g. enabler/destroy-all-payload rebuild);
    // replay the non-sacrifice hand casts in plan.actions VECTOR ORDER so the executor
    // realises the same line ApplyPlanDirect's explicit-order path produced. Without this
    // the executor would re-bucket enabler-first and diverge from the committed ordering.
    if (plan.searched_order)
    {
        for (const Action& a : plan.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.sacrifice_land) { continue; }
            if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); resolve_now(); continue; }
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else
                {
                    rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                    if (TurnSolver::BreakpointHandSnapshotWanted(state))
                    {
                        rdb_plan_casts.clear();
                        for (const Action& pa : plan.actions)
                        {
                            if (pa.kind != Action::Kind::CastFromHand) { continue; }
                            rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                        }
                    }
                    resolve_draw_breakpoint(0);
                }
            }
            else if (stage_draw_break(a.card_name)) { staged_break = true; break; }
            // PARTITION truncation: the continuation just decided the rest of this phase, so the
            // plan's remaining casts are not ours (see bp_trunc_exec). Mirrors ApplyPlanDirect.
            if (equip_bp_truncates(a.card_name)) { bp_trunc_exec = true; break; }
        }
    }
    else
    {
    // Reorder by CastOrderRank, EXCEPT when the set has a re-solve breakpoint card
    // (draw/staging/cascade): its ordering is search-owned, so keep the canonical
    // enabler-first + plan order (with the breakpoint/staging handling). Mirrors
    // ApplyPlanDirect's gate (lockstep).
    bool opaque = false;
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land
            && OrderingOpaque(a.card_name)) { opaque = true; break; }
    }
    if (opaque)
    {
    // Enabler-first: cast lifegain_to_loss spells (Tainted Remedy / Plague Drone) before any
    // other hand cast so a same-turn payload fires with the enabler active. Then the rest in
    // plan order, with the draw-engine breakpoint / staging handling.
    // Enablers apply in CastOrderRank order (stable; equal ranks keep plan order -- byte-
    // identical unless a provider ranks its enablers apart). Mirror of ApplyPlanDirect's
    // opaque path: Mirrorwing needs magnet(5) -> Twinflame(8) -> pump tricks.
    {
        std::vector<int> ena;
        for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
        {
            const Action& a = plan.actions[i];
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !a.alt_cost
                && ResolveProvider(state).CastEnablerFirst(state, a.card_name))
            { ena.push_back(i); }
        }
        std::stable_sort(ena.begin(), ena.end(), [&](int x, int y)
        {
            const CardDefinition* dx = CardDatabase::Instance().Lookup(plan.actions[x].card_name);
            const CardDefinition* dy = CardDatabase::Instance().Lookup(plan.actions[y].card_name);
            if (!dx || !dy) { return false; }
            return ResolveProvider(state).CastOrderRank(state, *dx)
                 < ResolveProvider(state).CastOrderRank(state, *dy);
        });
        for (int i : ena)
        {
            const Action& a = plan.actions[i];
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock();
        }
    }
    // Spectacle hoist (mirror of ApplyPlanDirect): a sac-land damage source (Shard Volley) would
    // otherwise be cast in the trailing sac loop AFTER the non-sac Spectacle spell (Light Up),
    // leaving Spectacle un-triggered. When the set holds a not-yet-active Spectacle spell, cast
    // such sac-land damage enablers here so Light Up unlocks its reduced cost. Only the 2-card
    // {burn, Light Up} spectacle plans pair a sac-land burn with Light Up, so this touches no
    // other line. Inert unless a Spectacle spell is present -> non-burn byte-identical.
    bool spec_needed = !state.opponent_lost_life_this_turn;
    if (spec_needed)
    {
        bool has_spec = false;
        for (const Action& a : plan.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.has_spectacle) { has_spec = true; break; } }
        spec_needed = has_spec;
    }
    for (size_t ai = 0; spec_needed && ai < plan.actions.size(); ++ai)
    {
        const Action& a = plan.actions[ai];
        if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land && a.direct_damage > 0)
        {
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock();
            spec_hoisted_sac.insert(ai);
        }
    }
    // ORDER within the opaque set (MTG_ORDER_OPAQUE, step 3 of cast-order-ideal-with-ranges.md):
    // the bail-out's premise is that a re-solve breakpoint makes the order situation-dependent, but
    // USER principle 1 answers the situation -- the draw goes FIRST, so the land drop and the rest
    // of the line are chosen with what it found. The breakpoint / staging handling in the body is
    // untouched; only the sequence changes, and the range ladder decides how far the promotion
    // survives payment. Off -> `ord` is plan order -> byte-identical. Mirrored in ApplyPlanDirect.
    std::vector<int> ord;
    for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
    {
        const Action& a = plan.actions[i];
        // MTG_GARTH_ORDERED: the activation IS the copy's cast, so it joins the ordered
        // sequence at the copy's rank (mirrors ApplyPlanDirect -- lockstep).
        if (GarthOrderedEnabled() && a.kind == Action::Kind::GarthActivate)
        { ord.push_back(i); continue; }
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (!a.alt_cost && (a.sacrifice_land
                            || ResolveProvider(state).CastEnablerFirst(state, a.card_name)))
        { continue; }
        ord.push_back(i);
    }
    if (OpaqueCastOrderActive(state))
    {
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y)
        { return CastOrderLess(state, plan.actions[x], plan.actions[y]); });
        ApplyCastOrderRangeLadder(state, plan.actions, ord);
        ApplyEnablerWipeRecheck(state, plan.actions, ord);
    }
    for (int oi : ord)
    {
        const Action& a = plan.actions[oi];
        if (a.kind == Action::Kind::GarthActivate)   // only present under MTG_GARTH_ORDERED
        {
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/a.tutor_target == "Shivan Dragon"))
            {
                ApplyGarthActivate(state, state.active_player_index, a.sac_source_id, a.tutor_target, a.chosen_x);
                // Acquisition second pass (d0; depth>0 replays the plan's recorded breakpoint
                // script): Braingeyser's draws / Regrowth's return are same-turn castable.
                if (a.tutor_target == "Braingeyser" || a.tutor_target == "Regrowth")
                { cast_draw_engine = true; }
                if (m_logger)
                { m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                       "conjure + cast " + a.tutor_target.str()
                                       + (a.chosen_x > 0 ? " (X=" + std::to_string(a.chosen_x) + ")" : "")); }
            }
        }
        else if (a.kind == Action::Kind::CastFromHand && a.alt_cost)
        {
            cast_alt(a.card_name, a.alt_lifegain); resolve_now();
        }
        else if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land
                 && !ResolveProvider(state).CastEnablerFirst(state, a.card_name))
        {
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else
                {
                    rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                    if (TurnSolver::BreakpointHandSnapshotWanted(state))
                    {
                        rdb_plan_casts.clear();
                        for (const Action& pa : plan.actions)
                        {
                            if (pa.kind != Action::Kind::CastFromHand) { continue; }
                            rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                        }
                    }
                    resolve_draw_breakpoint(0);
                }
            }
            else if (stage_draw_break(a.card_name)) { staged_break = true; break; }
            // PARTITION truncation: the continuation just decided the rest of this phase, so the
            // plan's remaining casts are not ours (see bp_trunc_exec). Mirrors ApplyPlanDirect.
            if (equip_bp_truncates(a.card_name)) { bp_trunc_exec = true; break; }
        }
    }
    }
    else
    {
    // Clean set: stable-sort the non-sacrifice hand casts by DecisionProvider::CastOrderRank
    // (enabler-first, prowess creatures before noncreature spells, on-cast self-damage
    // sources last). Stable => plan order breaks ties. Mirrors ApplyPlanDirect's canonical
    // branch (the shared CastOrderLess in ManaPayment.cpp) so the executor realises the same line
    // the rollout scored. No draw engine here, so no breakpoint handling is needed.
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
    {
        const Action& a = plan.actions[i];
        if ((a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
            || (GarthOrderedEnabled() && a.kind == Action::Kind::GarthActivate))
        { order.push_back(i); }
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(state, plan.actions[x], plan.actions[y]); });
    // RANGE ladder (MTG_ORDER_RANGE): re-place the ranged spells at their IDEAL end and walk them
    // back only as far as paying for the line requires. Inert with the lever off / no ranged spell
    // in the set. Mirrored in ApplyPlanDirect (lockstep).
    ApplyCastOrderRangeLadder(state, plan.actions, order);
    ApplyEnablerWipeRecheck(state, plan.actions, order);
    for (int oi : order)
    {
        const Action& a = plan.actions[oi];
        if (a.kind == Action::Kind::GarthActivate)   // only present under MTG_GARTH_ORDERED
        {
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/a.tutor_target == "Shivan Dragon"))
            {
                ApplyGarthActivate(state, state.active_player_index, a.sac_source_id, a.tutor_target, a.chosen_x);
                // Acquisition second pass (d0; depth>0 replays the plan's recorded breakpoint
                // script): Braingeyser's draws / Regrowth's return are same-turn castable.
                if (a.tutor_target == "Braingeyser" || a.tutor_target == "Regrowth")
                { cast_draw_engine = true; }
                if (m_logger)
                { m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                       "conjure + cast " + a.tutor_target.str()
                                       + (a.chosen_x > 0 ? " (X=" + std::to_string(a.chosen_x) + ")" : "")); }
            }
            continue;
        }
        if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); resolve_now(); continue; }
        cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock();
        // SITE 6 (MTG_EQUIP_DRAW_BP_INLINE) is the first breakpoint class that can appear in a
        // CLEAN set: the branch comment above ("No draw engine here, so no breakpoint handling is
        // needed") held only because every other class carries an OrderingOpaque param and an
        // Equipment cast carries none -- the draw belongs to the watcher. Without this the rollout
        // would search a continuation the executor never plays. Inert in every other config.
        //
        // Gated on equip_bp_truncates (site 6 + inline mode) and NOT on is_draw_engine, which was
        // measured: is_draw_engine also covers the MTG_ACQ_RESOLVE tutor family, and tutor_to_hand
        // is NOT one of OrderingOpaque's params -- so a tutor set reaches this CLEAN branch, and
        // hooking the broad predicate here armed breakpoints those decks never had (smoke went
        // 26/36 with 2 searched slower and 26 play-changed). Site 6 is the only class that both
        // lands in a clean set and has a rollout twin arming at the cast.
        if (s_full_depth && equip_bp_truncates(a.card_name))
        {
            if (fd_plan_committed)
            { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
            else
            {
                rdb_site = CardDatabase::Instance().Lookup(a.card_name);
                if (TurnSolver::BreakpointHandSnapshotWanted(state))
                {
                    rdb_plan_casts.clear();
                    for (const Action& pa : plan.actions)
                    {
                        if (pa.kind != Action::Kind::CastFromHand) { continue; }
                        rdb_plan_casts.push_back(std::hash<std::string>{}(pa.card_name));
                    }
                }
                resolve_draw_breakpoint(0);
            }
        }
        if (equip_bp_truncates(a.card_name)) { bp_trunc_exec = true; break; }
    }
    }
    }
    for (size_t ai = 0; ai < plan.actions.size(); ++ai)
    {
        if (staged_break || bp_trunc_exec) { break; }
        if (spec_hoisted_sac.count(ai)) { continue; }   // already cast by the Spectacle hoist
        const Action& a = plan.actions[ai];
        if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
        { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target, a.free_cast, a.bestow, a.replicate_count, a.convoke_green, a.convoke_other, a.phyrexian_life, a.evoke); note_draw_engine(a.card_name); resolve_now(); fire_unlock(); }
    }
    for (const Action& a : plan.actions)
    {
        if (staged_break || bp_trunc_exec) { break; }
        if (a.kind == Action::Kind::CastFromGraveyard)
        { cast_from_graveyard(a.card_name, a.discard_lands); note_draw_engine(a.card_name); resolve_now(); }
    }

    // Deferred-for-tutor drop (LandDropAfterHandLandTutor, depth-0 only): the pre-combat land
    // block held the drop so a hand-land tutor (Sylvan Scrying) could resolve first; play it now
    // with the fetched land (Forbidden Orchard) in hand. In-main1, NOT the second-main pass -- a
    // uses_second_main=no deck never runs one, and losing the drop outright measured d0 +0.32 on
    // the first CG arm. Consume-and-clear so the flag never leaks across turns.
    // ALWAYS request the second pass after playing a held drop: this pass's plan was solved
    // WITHOUT the land's mana, so a re-solve must pick up what it could not afford (gi40: a T2
    // Enlightened Tutor silently dropped because the defer fired but Scrying was not in the
    // plan, so no tutor second pass ever ran -- the pass-1-plans-short residual).
    if (m_tutor_deferred_drop)
    {
        m_tutor_deferred_drop = false;
        if (m_lookahead_depth == 0
            && state.ActivePlayer().lands_played_this_turn
                   < state.ActivePlayer().LandDropsAvailable())
        {
            TryPlayLand(state);
            cast_draw_engine = true;
        }
    }

    // Auto-fire safe alt payloads (Invigorate / Skyshroud) deterministically once a Remedy is
    // live -> free face damage. Mirrors the rollout's FireSafeAltPayloads pass (so the realised
    // turn matches the searched line without any recording). Re-scan after each cast because it
    // mutates the hand. No-op for decks without alt-cost cards.
    if (!staged_break)
    {
        for (;;)
        {
            Player& rp2 = state.ActivePlayer();
            int target = -1; int amt = 0;
            for (int i = 0; i < static_cast<int>(rp2.hand.size()); ++i)
            {
                auto d = CardDatabase::Instance().LookupCached(rp2.hand[i]);
                if (d && ResolveProvider(state).CanAutoFireAltPayload(state, state.active_player_index, *d))
                { target = i; amt = d->params.alt_lifegain_cost; break; }
            }
            if (target < 0) { break; }
            std::string nm = rp2.hand[target].m_name;
            size_t before = rp2.hand.size();
            cast_alt(nm, amt); resolve_now();
            if (state.ActivePlayer().hand.size() >= before) { break; }   // didn't consume -> stop
        }
    }

    // Krenko, Mob Boss taps AFTER the main casts (executor mirror of ApplyPlanDirect's trailing
    // TapForTokens pass): X = Goblins you control counts this turn's developed board. Free ({T}).
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::TapForTokens)
        { ApplyTapForTokens(state, state.active_player_index, a.sac_source_id); }
    }

    // Costed sac outlets (Siege-Gang / Pashalik) + Twinshot channel: executor mirror of the rollout
    // trailing pass. Pay the mana from the pool left after casts (BuildAvailableMana + TapForCost,
    // the byte-identical mirror of TapForCostDirect), then realise the effect; a stranded outlet is
    // a no-op in both worlds -> lockstep.
    // Viewer/digest visibility for the equipment-deck ability applies below (2026-08-14):
    // Equip / Jitte mode / Stoneforge put / Balan attach-all were applied silently -- the game
    // log showed the equipment appear via CAST_SPELL and damage change, but never WHERE it went
    // or WHICH ability fired, so the play viewer could not render an equipment deck's turns and
    // the play digest was blind to equip destinations (two different hosts -> same digest).
    // Logged via LogAbility (folds into the digest -> a deliberate fingerprint improvement;
    // digest-moving for decks that equip, flagged for GT rebaseline).
    auto bf_name = [&](int num) -> std::string {
        // Own side first (card numbers are unique per deck; the opponent's spawns share
        // m_number 0), then the opponent's -- a Jitte -1/-1 target is an opponent creature.
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index == state.active_player_index
                && p.card.m_number == num) { return p.card.m_name.str(); }
        }
        for (const Permanent& p : state.battlefield)
        { if (p.card.m_number == num) { return p.card.m_name.str(); } }
        return "#" + std::to_string(num);
    };
    // A LAMBDA (recursive, hence std::function) rather than a bare loop, mirroring
    // ApplyPlanDirect's apply_trailing_activations: the pod-chain breakpoint twin (site 7,
    // inside the ActivatePod branch) applies its continuation's activations through this same
    // dispatcher -- the chain itself -- and a continuation Pod activation re-enters the site
    // (bounded: every activation taps a Pod). Called with plan.actions exactly where the loop
    // stood -- byte-identical for every plan that opens no pod site.
    std::function<void(const std::vector<Action>&)> exec_trailing_activations =
        [&](const std::vector<Action>& trailing_acts)
    {
    for (const Action& a : trailing_acts)
    {
        if (a.kind == Action::Kind::SacCreatureOutlet)
        {
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                if (a.sac_count > 1)
                { if (a.sac_victim_id != 0)
                  { ApplyPersistLoop(state, state.active_player_index, a.sac_source_id, a.sac_victim_id, a.sac_count); }
                  else
                  { ApplySacCreatureOutletBurst(state, state.active_player_index, a.sac_source_id, a.sac_count); } }
                else
                { ApplySacCreatureOutlet(state, state.active_player_index, a.sac_source_id, a.sac_victim_id); }
            }
        }
        else if (a.kind == Action::Kind::GraveyardExileAbility)
        {
            // Deathrite abilities 2/3 (executor mirror of the rollout's trailing pass).
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            { ApplyGraveyardExileAbility(state, state.active_player_index, a.sac_source_id, a.gy_exile_mode); }
        }
        else if (a.kind == Action::Kind::GraveyardReturnAbility)
        {
            // Haven of the Spirit Dragon (executor mirror of the rollout's trailing pass).
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                ApplyGraveyardReturnAbility(state, state.active_player_index, a.sac_source_id,
                                            a.tutor_target);
            }
        }
        else if (a.kind == Action::Kind::ActivateRevealTop)
        {
            // Call of the Wild (executor mirror): pay K x cost, K sequential reveal-deploys.
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                for (int k = 0; k < std::max(1, a.chosen_x); ++k)
                { if (!ApplyRevealTopDeploy(state, state.active_player_index)) { break; } }
            }
        }
        else if (a.kind == Action::Kind::ActivatePod)
        {
            // Birthing Pod (executor mirror): the SAME PerformPodActivate the rollout ran.
            // Phyrexian variant: life gate first (no mutation), deduct once the mana commits --
            // lockstep twin of the rollout's trailing-pass site.
            ManaPool avail = AvailableManaPool(state);
            if ((a.phyrexian_life == 0
                 || state.players[state.active_player_index].life > a.phyrexian_life)
                && TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                state.players[state.active_player_index].life -= a.phyrexian_life;
                const bool done = PerformPodActivate(state, state.active_player_index,
                                                     ResolvePodSourceId(
                                                         state, state.active_player_index,
                                                         a.sac_source_id, a.card_name),
                                                     a.sac_victim_id, a.tutor_target.str());
                if (m_logger && done)
                {
                    m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                         "pod -> " + a.tutor_target.str()
                                             + (a.phyrexian_life > 0 ? " (paid 2 life)" : ""));
                }
                // BREAKPOINT SITE 7 executor twin (lockstep pair of ApplyPlanDirect's pod-chain
                // site; header note at TurnSolver::PodBreakpointClassOn). Same gates, same
                // counting: only an ENABLED class increments the shared index (mirrors
                // bp_searched_plan's class_on counting), and the searched continuation indexes
                // the SHARED EnumerateBreakpointPlans list -- re-solving greedily for a plan
                // that scored a searched continuation would realise a turn the search never
                // scored (fd-diverge). Human play never auto-continues: the human owns the rest
                // of the phase (Pod #2 with the fetch as victim is on the next decision poll).
                if (done && a.tutor_target != kPodNoFetch && !HumanPlayActive()
                    && TurnSolver::PodBreakpointClassOn()
                    && TurnSolver::PodChainAnotherActivatablePod(state))
                {
                    TurnSolver::Plan extra;
                    bool pod_bp_searched = false;
                    const int pod_bp_idx = (plan.bp_choice >= 0) ? bp_seen_exec++ : -1;
                    if (pod_bp_idx == plan.bp_at)
                    {
                        const std::vector<TurnSolver::Plan> cands =
                            TurnSolver::EnumerateBreakpointPlans(state, is_pre_combat_main);
                        if (plan.bp_choice < static_cast<int>(cands.size()))
                        {
                            extra           = cands[plan.bp_choice];
                            pod_bp_searched = true;
                            // The continuation's land is part of the searched decision
                            // (mirrors bp_play_searched_land; no Karoo reservation can be live
                            // this deep in the trailing pass -- it was consumed after the casts).
                            if (extra.land_decided && !extra.land_to_play.empty())
                            { TryPlaySpecificLand(state, extra.land_to_play, extra.fetch_target, extra.land_face); }
                        }
                    }
                    if (!pod_bp_searched)
                    { execgreedy::Record(-1, m_in_rollout);
                      execgreedy::RecordBpCause(plan.bp_choice >= 0);
                      extra = TurnSolver::Solve(state, is_pre_combat_main); }
                    // Precasts (SacForMana / Suspend / convoke taps) exactly as
                    // resolve_draw_breakpoint's pre-pass, then the casts in the executor's clean
                    // canonical order, then the continuation's ACTIVATIONS via this same trailing
                    // dispatcher -- the chain itself. Scope note: pod continuations belong to the
                    // one pod deck (Melira), which plays no Vial / opaque-order card -- extend
                    // this applier before a pod deck that does ever exists.
                    for (const Action& ca : extra.actions)
                    {
                        if (ca.kind == Action::Kind::SacForMana)
                        { ApplySacForMana(state, state.active_player_index, ca.sac_source_id,
                                          TurnSolver::SacFloatColorFor(state, extra.actions, ca),
                                          ca.ritual_float, ca.sac_victim_id); }
                        else if (ca.kind == Action::Kind::Suspend)
                        { ApplySuspend(state, state.active_player_index, ca.card_name); }
                        else if (ca.kind == Action::Kind::CastFromHand
                                 && (ca.convoke_green > 0 || ca.convoke_other > 0))
                        { ApplyConvokeTaps(state, state.active_player_index, ca.convoke_green, ca.convoke_other); }
                    }
                    std::vector<int> pod_cont_order;
                    for (int ci = 0; ci < static_cast<int>(extra.actions.size()); ++ci)
                    {
                        const Action& ca = extra.actions[ci];
                        if (ca.kind == Action::Kind::CastFromHand && !ca.sacrifice_land)
                        { pod_cont_order.push_back(ci); }
                    }
                    if (!extra.searched_order && pod_cont_order.size() > 1)
                    {
                        std::stable_sort(pod_cont_order.begin(), pod_cont_order.end(),
                            [&](int x, int y)
                            { return CastOrderLess(state, extra.actions[x], extra.actions[y]); });
                    }
                    for (int ci : pod_cont_order)
                    {
                        const Action& ca = extra.actions[ci];
                        cast_by_name(ca.card_name, ca.tutor_target, ca.chosen_x,
                                     ca.soulfire_own_targets, ca.ponder_keep, ca.crackle_targets,
                                     ca.splice_count, ca.chosen_float_color, ca.enchant_target,
                                     ca.free_cast, ca.bestow, ca.replicate_count, ca.convoke_green,
                                     ca.convoke_other, ca.phyrexian_life, ca.evoke);
                        resolve_now();
                    }
                    for (const Action& ca : extra.actions)
                    {
                        if (ca.kind == Action::Kind::CastFromHand && ca.sacrifice_land)
                        {
                            cast_by_name(ca.card_name, ca.tutor_target, ca.chosen_x,
                                         ca.soulfire_own_targets, ca.ponder_keep, ca.crackle_targets,
                                         ca.splice_count, ca.chosen_float_color, ca.enchant_target,
                                         ca.free_cast, ca.bestow, ca.replicate_count, ca.convoke_green,
                                         ca.convoke_other, ca.phyrexian_life, ca.evoke);
                            resolve_now();
                        }
                    }
                    exec_trailing_activations(extra.actions);
                }
            }
        }
        else if (a.kind == Action::Kind::GraveyardExileGrow)
        {
            // Scavenging Ooze (executor mirror of the rollout's trailing pass).
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                ApplyGraveyardExileGrow(state, state.active_player_index, a.sac_source_id,
                                        a.tutor_target.str());
            }
        }
        else if (a.kind == Action::Kind::ActivateBlink)
        {
            // Eldrazi Displacer / Emiel (executor mirror): the SAME ApplyBlinkLoop the rollout
            // ran, so the realised line is the scored line. `available` is re-derived per payment
            // because the loop's untaps change the board between iterations.
            if (a.def != nullptr)
            {
                const int done = ApplyBlinkLoop(
                    state, state.active_player_index, a.sac_source_id, a.sac_victim_id,
                    a.def->params, std::max(1, a.chosen_x),
                    [&state, this](const ManaCost& c)
                    {
                        ManaPool avail = AvailableManaPool(state);
                        return TapForCost(state, c, avail, /*for_creature=*/false);
                    });
                if (m_logger && done > 0)
                {
                    m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                         "blink x" + std::to_string(done));
                }
            }
        }
        else if (a.kind == Action::Kind::ActivatePermAbility)
        {
            if (a.def != nullptr && PermAbilitySourceLive(state, state.active_player_index,
                                                          a.sac_source_id, a.ability_mode))
            {
                // {T} half first -- see the rollout twin and SetPermTapped.
                const bool taps = PermAbilityTaps(a.ability_mode);   // SacDraw/Drain/ExileTop have no {T}
                if (taps) { SetPermTapped(state, state.active_player_index, a.sac_source_id, true); }
                ManaPool avail = AvailableManaPool(state);
                if (!TapForCost(state, a.cost, avail, /*for_creature=*/false))
                { if (taps) { SetPermTapped(state, state.active_player_index, a.sac_source_id, false); } }
                else
                {
                    ApplyPermAbility(state, state.active_player_index, a.sac_source_id,
                                     a.ability_mode);
                    if (m_logger)
                    {
                        m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                             PermAbilityLabel(a.ability_mode));
                    }
                    // LOCKSTEP with the rollout's repeat loop (TurnSolver apply_one). A repeatable
                    // {T}-less sink's plan carries K activations with only the FIRST on the subset's
                    // books; the rest are paid here out of what the turn actually produced. If only
                    // one world ran this loop the search would predict a kill the real game never
                    // deals -- the [fd-diverge] shape.
                    if (!taps && a.chosen_x > 1)
                    {
                        const int extra =
                            SpendRepeatActivations(state, state.active_player_index,
                                                   a.sac_source_id, a.ability_mode, *a.def,
                                                   a.chosen_x - 1);
                        if (m_logger)
                        {
                            for (int i = 0; i < extra; ++i)
                            { m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                                   PermAbilityLabel(a.ability_mode)); }
                        }
                    }
                }
            }
        }
        else if (a.kind == Action::Kind::ActivatePump)
        {
            // Burning-Fist discard-pump / Sethron team-pump-with-haste (executor mirror of the
            // rollout's trailing pass -- same shared ApplyActivatePump, so the two worlds agree).
            // The pay scope marks this as a creature-source ability payment (ActivatePump sources
            // are always creatures -- the params only exist on them), so Secluded Courtyard's
            // coloured mana is legal here (D12).
            CreatureAbilityPayScope pump_pay_scope;
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                const int n = ApplyActivatePump(state, state.active_player_index, a.sac_source_id,
                                                a.gy_exile_mode, a.chosen_x);
                if (m_logger && n > 0)
                { m_logger->LogAbility(a.sac_source_id, bf_name(a.sac_source_id),
                                       a.gy_exile_mode == 1 ? "pump (discard)" : "team pump + haste"); }
            }
        }
        else if (a.kind == Action::Kind::UntapCreature)
        {
            // Wirewood Lodge (executor mirror): precondition-check first, then pay + untap.
            const CardDefinition* ud = CardDatabase::Instance().Lookup(a.card_name);
            if (ud != nullptr
                && CanApplyUntapCreature(state, state.active_player_index, a.sac_source_id,
                                         ud->params.untap_creature_subtype))
            {
                ManaPool avail = AvailableManaPool(state);
                if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
                {
                    ApplyUntapCreature(state, state.active_player_index, a.sac_source_id,
                                       ud->params.untap_creature_subtype);
                }
            }
        }
        else if (a.kind == Action::Kind::AttachAllEquipment)
        {
            // Balan attach-all (executor mirror -- same shared ApplyAttachAllEquipment).
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                ApplyAttachAllEquipment(state, state.active_player_index, a.sac_source_id);
                if (m_logger)
                { m_logger->LogAbility(a.sac_source_id, bf_name(a.sac_source_id),
                                       "attach all equipment"); }
            }
        }
        else if (a.kind == Action::Kind::PutFromHandAbility)
        {
            // Stoneforge put (executor mirror -- same shared ApplyPutFromHand).
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                ApplyPutFromHand(state, state.active_player_index, a.sac_source_id, a.card_name.str());
                if (m_logger)
                { m_logger->LogAbility(a.sac_source_id, bf_name(a.sac_source_id),
                                       "put " + a.card_name.str() + " onto the battlefield"); }
            }
        }
        else if (a.kind == Action::Kind::JitteModeAbility)
        {
            // Jitte -1/-1 / lifegain (executor mirror -- the cost is the counter).
            // Log BEFORE apply: a -1/-1 that kills its target removes it from the battlefield,
            // and the ability string should still name it.
            if (m_logger)
            {
                const int reps = std::max(1, a.chosen_x);
                int host = 0;   // mode 3 pumps the EQUIPPED creature, which the action does not name
                for (const Permanent& jp : state.battlefield)
                { if (jp.card.m_number == a.sac_source_id) { host = jp.equipped_to; break; } }
                m_logger->LogAbility(a.sac_source_id, bf_name(a.sac_source_id),
                                     a.gy_exile_mode == 1
                                         ? "-1/-1 -> " + bf_name(a.sac_victim_id)
                                     : a.gy_exile_mode == 3
                                         ? "+2/+2 -> " + bf_name(host)
                                           + (reps > 1 ? " x" + std::to_string(reps) : "")
                                         : "gain 2 life");
            }
            // chosen_x = the repeat count (mode 3 only; 0 elsewhere -> one application, so modes 1
            // and 2 stay byte-identical). Lockstep twin of ApplyPlanDirect's JitteModeAbility branch.
            for (int k = 0; k < std::max(1, a.chosen_x); ++k)
            {
                ApplyJitteMode(state, state.active_player_index, a.sac_source_id,
                               a.gy_exile_mode, a.sac_victim_id);
            }
        }
        else if (a.kind == Action::Kind::Equip)
        {
            // Lightning Greaves (executor mirror). Skip one the mana-unlock hoist already fired
            // mid-casts -- it is attached to this exact host already, so re-firing would pay the
            // equip cost a second time (mirrors ApplyPlanDirect). Cost recomputed at payment
            // (metalcraft can flip mid-plan -- see EquipActionCostNow). A hoisted equip is still
            // LOGGED here (the hoist runs through the shared TurnSolver helper, which has no
            // logger) -- the viewer sees it at the action's plan position, same phase.
            if (m_logger
                && EquipmentAttachedTo(state, state.active_player_index, a.sac_source_id, a.sac_victim_id))
            { m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                   "equip -> " + bf_name(a.sac_victim_id)); }
            // STRANDED EQUIP (audit-only, see GameLogger.h): the host this equip was co-selected
            // against was dropped as unpayable earlier in this same plan, so TapForCost below is
            // about to pay for an attach ApplyEquip will refuse. Counted, NOT prevented -- the fix
            // moves the play digest and is a separate, measured decision.
            if (AffordAuditOn() && WasCastDroppedThisPlan(a.sac_victim_id))
            { NoteStrandedEquip(a.card_name.str(), bf_name(a.sac_victim_id)); }
            ManaPool avail = AvailableManaPool(state);
            // Do not pay for an attach ApplyEquip will refuse (MTG_EQUIP_PAY_GUARD; lockstep twin
            // in ApplyPlanDirect). Skipping the branch also removes the phantom log line, since the
            // log only fires on the path that actually attaches.
            const bool equip_payable =
                !EquipPayGuardEnabled()
                || CanAttachEquip(state, state.active_player_index, a.sac_source_id, a.sac_victim_id);
            if (equip_payable
                && !EquipmentAttachedTo(state, state.active_player_index, a.sac_source_id, a.sac_victim_id)
                && TapForCost(state,
                              EquipActionCostNow(state, state.active_player_index,
                                                 a.sac_source_id, a.cost),
                              avail, /*for_creature=*/false))
            {
                // Under MTG_EQUIP_LOG_TRUTH, decide BEFORE the apply whether it will attach, so the
                // log describes what happened rather than what was paid for (see EngineFlags.h).
                const bool will_attach =
                    !EquipLogTruthEnabled()
                    || CanAttachEquip(state, state.active_player_index, a.sac_source_id, a.sac_victim_id);
                ApplyEquip(state, state.active_player_index, a.sac_source_id, a.sac_victim_id);
                if (m_logger && will_attach)
                { m_logger->LogAbility(a.sac_source_id, a.card_name.str(),
                                       "equip -> " + bf_name(a.sac_victim_id)); }
            }
        }
        else if (a.kind == Action::Kind::ActivateLoyalty)
        {
            // Planeswalker loyalty (executor mirror; no mana cost).
            ApplyLoyaltyAbility(state, state.active_player_index, a.sac_source_id, a.loyalty_ability,
                                a.enchant_target);   // Oko +1: the searched Elk target (lockstep)
        }
        else if (a.kind == Action::Kind::GarthActivate)
        {
            // Garth One-Eye (executor mirror). Under MTG_GARTH_ORDERED it already applied
            // inside the ordered cast sequence at the copy's rank -- skip the trailing slot.
            if (!GarthOrderedEnabled())
            {
                ManaPool avail = AvailableManaPool(state);
                if (TapForCost(state, a.cost, avail, /*for_creature=*/a.tutor_target == "Shivan Dragon"))
                {
                    ApplyGarthActivate(state, state.active_player_index, a.sac_source_id, a.tutor_target, a.chosen_x);
                    // Acquisition second pass (d0; depth>0 replays the plan's recorded breakpoint
                    // script): Braingeyser's draws / Regrowth's return are same-turn castable.
                    if (a.tutor_target == "Braingeyser" || a.tutor_target == "Regrowth")
                    { cast_draw_engine = true; }
                    // Viewer visibility (USER 2026-08-19: "We should definitely have garth's
                    // ability in the viewer") -- this was the only battlefield activation with
                    // no LogAbility line, so Garth taps were invisible in every game log.
                    if (m_logger)
                    { m_logger->LogAbility(a.sac_source_id, bf_name(a.sac_source_id),
                                           "conjure + cast " + a.tutor_target.str()
                                           + (a.chosen_x > 0 ? " (X=" + std::to_string(a.chosen_x) + ")" : "")); }
                }
            }
        }
        else if (a.kind == Action::Kind::Channel)
        {
            ManaPool avail = AvailableManaPool(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            { ApplyChannel(state, state.active_player_index, a.hand_index, a.card_name, a.direct_damage); }
        }
    }
    };
    exec_trailing_activations(plan.actions);

    // Play the deferred Karoo bounce land now (mirror of ApplyPlanDirect): the main casts have
    // tapped the lands we needed, so BounceKarooLand returns a spent land at no tempo cost. Sits
    // after the cast loop (incl. any inline draw-engine breakpoint replay) and before the
    // catch-all breakpoint/dig replay below -- the same logical point as the rollout (which
    // plays it right after apply_plan_actions, before its deferred-cantrip re-solve). Taking the
    // drop here (lands_played==1) keeps a later breakpoint from playing a revealed land as it.
    if (karoo_deferred)
    {
        karoo_deferred = false;
        TryPlaySpecificLand(state, karoo_land_name, karoo_fetch);
    }

    // Commit-the-line: replay any recorded dig (Kind::DigDraw) the draw-engine breakpoint
    // above did not already replay. A flooded turn whose only action is digging for
    // Treasure Hunt casts no draw engine from plan.actions, so nothing triggered
    // replay_recorded -- replay the recorded script here so the realised turn performs the
    // exact cycles/sacrifices and dug-Treasure-Hunt line the search committed.
    if (!staged_break && fd_plan_committed && !bp_replayed && !plan.breakpoint_actions.empty())
    {
        replay_recorded(plan.breakpoint_actions);
        bp_replayed = true;
    }

    // Grove of the Burnwillows drip: once a Remedy is live, tap any still-untapped Grove for its
    // free 1-damage ping even with nothing to cast. Unconditional (NOT under fd_plan_committed) and
    // once per turn, matching ApplyPlanDirect's call so the realised game stays in lockstep with the
    // searched/committed line. After the cast loop so a spell that needed Grove's mana tapped it
    // first. Inert without a Remedy active + an untapped tap_opponent_lifegain land (every deck but
    // Anti-Lifegain).
    if (is_pre_combat_main) { TapDripLandsIfUseful(state, state.active_player_index); }

    // Animate lands and activate tap-token abilities with mana remaining after spells.
    // Only in pre-combat main so any resulting creatures can attack this turn.
    if (is_pre_combat_main)
    {
        // Reactive dig only on the non-committed paths (depth 0, or the develop-when-stuck
        // fallback that carries no recorded script); committed turns already replayed their
        // recorded digs above, so running it again would dig a second, off-line time.
        if (!fd_plan_committed) { UseSurplusLandAbilities(state); }
        ManaPool remaining = AvailableManaPool(state);
        AnimateLandsShared(state, &remaining);
        ActivateTapTokensShared(state, &remaining);
    }

    // Restore any unplayed staged cards (still flagged m_is_staged in hand) back to
    // staged_cards, removing them from hand. Cards that were cast were already removed
    // from hand; expired ones were dropped at the merge above. The expiry travels on
    // the card (m_staged_expiry, set at the merge), so no snapshot walk is needed.
    // IMPORTANT: the card must be REMOVED from hand here, not merely flag-cleared and
    // kept -- doing the latter leaves a permanent (non-staged, never-expiring) hand
    // duplicate of a card also pushed to staged_cards. That was latent until a staging
    // spell (Light Up the Stage) got a second TakeTurn pass with its staged cards still
    // unplayed, which ran this restore on a non-empty merge and duplicated them.
    Player& ap_after = state.ActivePlayer();
    std::vector<Card> regular_hand;
    for (Card& c : ap_after.hand)
    {
        if (c.m_is_staged)
        {
            c.m_is_staged = false;
            StagedCard sc;
            sc.card        = c;
            sc.expiry_turn = c.m_staged_expiry;
            ap_after.staged_cards.push_back(sc);
        }
        else
        {
            regular_hand.push_back(c);
        }
    }
    ap_after.hand = std::move(regular_hand);

    // Commit-the-line (full-depth) handles the draw breakpoint INLINE (replay_recorded
    // for a committed plan, resolve_draw_breakpoint for the fallback), so it must NOT
    // request the legacy second TakeTurn pass -- that pass would wrongly consume the
    // NEXT committed phase during this same turn (the desync that left TH's predicted
    // Treasure Hunt + Land's Edge win unrealised). Only the legacy path uses it.
    // Only the full-depth SEARCH (depth>0) handles the draw breakpoint inline; at
    // depth 0 there is no search, so we must still request the legacy second pass or
    // the draw engine never gets cast (TH d0 collapses). Gate the suppression on a
    // live search.
    return (s_full_depth && m_lookahead_depth > 0) ? false : cast_draw_engine;
}

// ---- Land drop ----

// Play a specific named land from hand. Mirrors TryPlayLand's per-card logic;
// used by the land search in TakeTurn to apply the chosen candidate.
bool AIEngine::TryPlaySpecificLand(GameState& state, const std::string& name,
                                   const std::string& fetch_target, const std::string& land_face,
                                   int rad_mode)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }
    // Prefer an exiled/STAGED copy of `name` (Light Up the Stage -- it expires; a permanent hand copy
    // keeps), mirroring the rollout's PlayLandByName so the committed line replays EXACTLY. Without
    // this the executor played the first hand match (a drawn copy) and let the staged copy lapse,
    // desyncing from the search's committed line -- the burn fd-diverge (the T4 double-Shard-Volley
    // needs the staged Mountain spent on T3 so the drawn Mountain is free for T4). Byte-identical when
    // no staged copy of `name` is in hand.
    auto pick = ap.hand.end();
    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        if (it->m_impulse_no_land) { continue; }   // Apex-exiled land: castable only as a SPELL, never played
        auto d = CardDatabase::Instance().Lookup(it->m_name);
        if (!PlayableAsLand(d)) { continue; }      // land, or spell//land back face
        if (pick == ap.hand.end()) { pick = it; }
        if (it->m_is_staged) { pick = it; break; }
    }
    if (pick != ap.hand.end())
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(pick->m_name);
        LandPlayOptions o;
        o.fetch_target       = fetch_target;
        o.land_face          = land_face;
        o.label_look_source  = true;    // executor labels the reveal log with the land's name
        o.spawn_orchard_spirit = true;  // searched drop: the on-play Spirit fires
        o.record_touch       = true;
        o.rad_mode           = rad_mode;   // the SEARCH's rad choice, so the drop realises the plan
        // Deliberately NOT setting honor_entry_chooser: that flag governs the older shock/reveal
        // entry chooser, which the executor's real drop has never consulted (a disclosed gap on that
        // axis), and switching it on here would silently change that behaviour too. The rad chooser
        // is gated on its own pointer instead, so human play gets the rad question without dragging
        // an unrelated axis along.
        o.logger             = m_logger;
        return PlayLandFromHand(state, static_cast<std::size_t>(pick - ap.hand.begin()), *def, o);
    }
    return false;
}

// Land-priority knobs (MTG_LEGACY_STATIC_TAPPED / MTG_LAND_CLOSING_WINDOW): shared readers in
// EngineFlags.h -- TurnSolver's greedy_land_name mirrors these passes and must read the same flags.

bool AIEngine::TryPlayLand(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    auto play_land_iter = [&](std::vector<Card>::iterator it, const CardDefinition& def) -> bool
    {
        LandPlayOptions o;
        o.label_look_source = true;
        // Forbidden Orchard's on-play Spirit. The GREEDY drop used to omit it while the executor's
        // searched drop, the rollout's drop, and the turn-start spawn all fire it -- so an Orchard
        // played on this path silently skipped the opponent's Spirit for that turn, and the rollout
        // scored lines against a board the executor would not produce. Fires here too, per the
        // card's modelled approximation (the active player is assumed to tap each Orchard for mana
        // every turn it is in play; a freshly-played untapped copy is tapped THIS turn).
        o.spawn_orchard_spirit = true;
        o.record_touch      = true;
        o.logger            = m_logger;
        return PlayLandFromHand(state, static_cast<std::size_t>(it - ap.hand.begin()), def, o);
    };

    // The RANKER lives in LandPlay.cpp (GreedyLandChoiceIndex) so the search's plan-ordering
    // tiebreak predicts exactly what this plays -- the two had silently drifted apart.
    const int pick = GreedyLandChoiceIndex(state);
    if (pick < 0) { return false; }
    auto it = ap.hand.begin() + pick;
    const CardDefinition* def = CardDatabase::Instance().Lookup(it->m_name);
    if (!def) { return false; }
    return play_land_iter(it, *def);
}


// ---- Surplus land card-draw abilities (cycling, sacrifice-to-draw) ----

void AIEngine::UseSurplusLandAbilities(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (!state.stack.empty()) { return; }   // let pending spells resolve first
    if (!ResolveProvider(state).HasAnyDigSource(state)) { return; }

    // Reactive "dig when stuck" used on the depth-0 / develop-when-stuck paths (full-depth
    // committed turns replay the search's recorded digs instead). Dig THROUGH lands toward
    // the first nonland (Treasure Hunt), exactly like the search rollout's loop, so the
    // decision (which source, how far) matches; the difference is only that this path does
    // not re-solve to cast a dug Treasure Hunt the same turn (it is cast next turn). The
    // gate (ShouldConsiderDig) keeps digging with Land's Edge in hand/play -- we still need
    // Treasure Hunt to refill ammo -- and stops only when a draw engine is already in hand,
    // a retrace engine sits in the yard, or Land's Edge is already lethal from the hand.
    int guard = 0;
    while (guard++ < 16 && ResolveProvider(state).ShouldConsiderDig(state) && !ap.library.empty())
    {
        ManaPool avail = AvailableManaPool(state);
        bool is_sac = false;
        std::string src = ResolveProvider(state).SelectDigSource(state, avail, is_sac);
        if (src.empty()) { break; }
        // PerformDig returns whether the drawn card was a land; on a nonland (action found)
        // we stop digging. A false-ish "could not perform" also returns false -> stop.
        if (!PerformDig(state, src, is_sac)) { break; }
    }
}

bool AIEngine::PerformDig(GameState& state, const std::string& source, bool is_sacrifice)
{
    Player& ap = state.ActivePlayer();
    const CardDefinition* sd = CardDatabase::Instance().Lookup(source);
    if (!sd) { return false; }

    if (is_sacrifice)
    {
        if (!sd->params.sacrifice_draw_cost.has_value()) { return false; }
        int idx = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index == state.active_player_index
                && !p.tapped && p.card.m_name == source) { idx = i; break; }
        }
        if (idx < 0) { return false; }
        ManaPool avail = AvailableManaPool(state);
        state.battlefield[idx].tapped = true;  // {T}; tap before paying so it isn't its own source
        if (!TapForCost(state, sd->params.sacrifice_draw_cost.value(), avail, false))
        {
            state.battlefield[idx].tapped = false;
            return false;
        }
        if (m_logger) { m_logger->LogDiscard(state.battlefield[idx].card.m_number, source); }
        ap.graveyard.push_back(state.battlefield[idx].card);
        state.battlefield.erase(state.battlefield.begin() + idx);
    }
    else
    {
        if (!sd->params.cycling_cost.has_value()) { return false; }
        ManaPool avail = AvailableManaPool(state);
        if (!avail.CanPay(sd->params.cycling_cost.value())) { return false; }
        std::vector<Card>::iterator it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&source](const Card& c) { return c.m_name == source; });
        if (it == ap.hand.end()) { return false; }
        if (!TapForCost(state, sd->params.cycling_cost.value(), avail, false)) { return false; }
        if (m_logger) { m_logger->LogDiscard(it->m_number, it->m_name); }
        ap.graveyard.push_back(*it);
        ap.hand.erase(it);
    }

    if (ap.library.empty()) { return false; }
    Card drawn = ap.library.DrawTop();
    ap.cards_drawn_this_turn += 1;   // a cycle/sac dig is a real draw (Fists of Flame counts it)
    const CardDefinition* ddef = CardDatabase::Instance().LookupCached(drawn);
    bool drew_land = ddef ? ddef->card.IsLand() : drawn.IsLand();
    if (m_logger) { m_logger->LogDraw(drawn.m_number, drawn.m_name); }
    ap.hand.push_back(std::move(drawn));
    return drew_land;
}

// ---- Firebreathing (Scourge / Lathliss) ----
// Build the leftover-combat-mana pool (the shared AvailableManaPool, the same pool the rollout's
// SimulateCombat reads) and hand it to the shared ApplyFirebreathing so the executor pumps
// exactly as the rollout projected. The pool is only READ (real sources are not tapped -- goldfish
// combat is the last mana use for firebreathing decks), so both worlds see the same leftover mana.
void AIEngine::Firebreathe(GameState& state, const std::vector<int>& attacker_indices)
{
    g_fb_activations_this_turn = 0;   // MTG_FB_TRACE diagnostic; reset every combat
    if (attacker_indices.empty()) { return; }
    if (!ControlsFirebreathingSource(state, state.active_player_index)) { return; }
    ManaPool pool = AvailableManaPool(state);
    // Human play (#4): let the player cap the pump. Probe the greedy-max activation count on a COPY
    // (ApplyFirebreathing takes the pool by value, so the probe does not consume it), ask the chooser
    // for k in [0, max], then apply exactly k. The chooser is nulled in every search/rollout
    // (RevealLogPause) and installed only under --claude-play, so autonomous stays greedy = byte-identical.
    // MTG_FB_TAP (pay-as-you-go; see FbPayer in SpellEffects.h): each pump pays by tapping real
    // sources through the SAME shared payment casts use, so a live post-combat main sees an
    // honest pool (no double-spend) and restricted sources (Haven's dragon-spells-only mana)
    // are refused for pumps exactly as they are for non-dragon casts.
    const FbPayer payer = FirebreatheTapsEnabled()
        ? +[](GameState& s, const ManaCost& c) -> bool
          { ManaPool avail = AvailableManaPool(s); return TapForCostShared(s, c, false, &avail, false); }
        : nullptr;
    if (g_play_firebreathe_chooser)
    {
        GameState probe = state;
        int max_k = ApplyFirebreathing(probe, state.active_player_index, attacker_indices, pool,
                                       std::numeric_limits<int>::max(), payer);
        if (max_k > 0)
        {
            int k = (*g_play_firebreathe_chooser)(state, state.active_player_index, attacker_indices, max_k);
            if (k < 0 || k > max_k) { k = max_k; }   // -1 / out-of-range -> greedy default (current behaviour)
            ApplyFirebreathing(state, state.active_player_index, attacker_indices, pool, k, payer);
            return;
        }
    }
    // Provider-owned activation count (FirebreatheActivations). Index 0 is the ranked pick; a
    // negative value means the greedy maximum, which is the default -> byte-identical. The rollout
    // (TurnSolver's SimulateCombat) reads the SAME hook, so executor and rollout pump alike.
    const std::vector<int> fb = ResolveProvider(state).FirebreatheActivations(state);
    const int fb_k = fb.empty() ? -1 : fb.front();
    if (fb_k < 0) { g_fb_activations_this_turn = ApplyFirebreathing(state, state.active_player_index, attacker_indices, pool, std::numeric_limits<int>::max(), payer); }
    else          { g_fb_activations_this_turn = ApplyFirebreathing(state, state.active_player_index, attacker_indices, pool, fb_k, payer); }
}

// ---- Mana ----

// Public payment entry. Delegates to the UNIFIED TapForCostShared (ManaPayment.cpp) -- formerly a
// twin of TurnSolver's TapForCostDirect kept in lockstep by comment discipline. The executor
// threads its `available` accounting pool through and has no MTG_LEGACY_CCO_PAY hatch.
bool AIEngine::TapForCost(GameState& state, const ManaCost& cost_in, ManaPool& available,
                          bool for_creature)
{
    return TapForCostShared(state, cost_in, for_creature, &available, /*honor_legacy_cco=*/false);
}

// ---- Spell selection ----

ManaCost AIEngine::EffectiveCost(const CardDefinition& def, const GameState& state, int copies) const
{
    // Delegates to the UNIFIED EffectiveSpellCost (ManaPayment.cpp) -- formerly a byte-identical
    // twin of TurnSolver's file-static EffectiveCost kept in lockstep by comment discipline.
    // (Goblin Warchief's reduces_spell_subtype reduction lives inside EffectiveSpellCost.)
    return EffectiveSpellCost(def, state, copies);
}

int AIEngine::FindOpponentCreature(const GameState& state) const
{
    const Player& opp = state.Opponent();
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index && p.card.IsCreature())
        {
            return i;
        }
    }
    return -1;
}

void AIEngine::CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available,
                                 int alt_lifegain, const std::string& tutor_target,
                                 int chosen_x, int own_targets, int ponder_keep,
                                 int crackle_targets, int splice_count,
                                 const std::string& chosen_float_color, int enchant_target,
                                 bool free_cast, bool bestow, int replicate_count,
                                 int convoke_green, int convoke_other, int phyrexian_life,
                                 bool evoke)
{
    Player& ap = state.ActivePlayer();
    auto def = CardDatabase::Instance().LookupCached(hand_card);
    if (!def) { return; }
    // BESTOW (Gnarled Scarhide): swap in the DB's synthesized "<name> (Bestowed)" AURA face for the
    // whole cast -- its cost is paid, its params resolve, and it is the permanent that enters. The
    // hand card itself is still `hand_card` (removed below by iterator/number, never by def name),
    // so this is the exact executor twin of the rollout's apply_one def swap (lockstep by
    // construction). Inert for every card without a bestow cost.
    if (bestow && def->params.bestow_cost.has_value())
    {
        const CardDefinition* bdef =
            CardDatabase::Instance().Lookup(def->card.m_name.str() + " (Bestowed)");
        if (bdef != nullptr) { def = bdef; }
    }

    // Irencrag Feat "you can cast only one more spell this turn": mirror TurnSolver::apply_one's
    // execution-time budget in the REAL executor so a replayed line OR the post-Apex staged re-solve
    // cannot cast past the limit (an Apex-exiled Dragonstorm is still a spell cast this turn). Keeps the
    // executor in lockstep with the enforced rollout. Inert (budget == -1) for every deck without a
    // max_casts_after card. See GameState::casts_remaining_this_turn.
    if (state.casts_remaining_this_turn == 0) { return; }   // budget spent: this cast is illegal

    StackEntry entry;
    entry.type             = StackEntry::EntryType::Spell;
    entry.source           = def->card;
    entry.source.m_number  = hand_card.m_number;  // preserve per-copy ID for logging
    entry.controller_index = state.active_player_index;
    entry.tutor_target     = tutor_target;        // searched fetch target (empty -> heuristic)
    // {X} spell: carry the chosen X so the effect (ResolveDirectDamage) scales by it. CR 202.3:
    // X is 0 except on the stack, where it is the chosen value -- so it is NOT in the card's
    // mana value, only here on the stack entry.
    // Positive X only -- EXCEPT the Terastodon destroy-K sentinel (kEtbKxHeuristic = "project K
    // at resolution"), which must survive to FireOwnEtbTriggers or the executor would silently
    // resolve K as -1 (destroy nothing) while the rollout (which passes the int directly)
    // projected -- an executor/rollout divergence by construction. chosen_x defaults to 0 for
    // every other cast, so no other deck's entries move.
    if (chosen_x > 0 || chosen_x == kEtbKxHeuristic) { entry.chosen_x = chosen_x; }
    // Soulfire Eruption: carry the searched own-creature target count so EffectHandler's dig
    // exiles the same N cards and kills the same own creatures as the rollout (lockstep).
    if (own_targets > 0) { entry.soulfire_own_targets = own_targets; }
    // Crackle with Power: carry the declared extra-target count so ResolveDirectDamage's derived
    // discount + faithful 5X damage match the rollout's (lockstep). Scale_x spells only.
    if (IsCrackleCountSpell(def->params)) { entry.crackle_targets = crackle_targets; }
    // Scaled divided-damage spell (Magma Opus): carry the committed opponent-face damage on the same
    // field so ResolveDirectDamage deals exactly that to the opponent. Set only when a scaled variant
    // was chosen (crackle_targets >= 0); a normal Magma cast leaves it unset -> full damage (byte-
    // identical). Magma is not IsCrackleCountSpell, so the Crackle extra-target resolution never fires.
    else if (def->params.damage_divided && crackle_targets >= 0) { entry.crackle_targets = crackle_targets; }
    // Ponder cast_reorder: carry the searched keep-vs-shuffle call so ResolveDrawSpell's reorder
    // matches the rollout's (lockstep). -1 = not a reorder spell (legacy heuristic path).
    if (ponder_keep >= 0) { entry.ponder_keep = ponder_keep; }
    // Desperate Ritual SPLICE: carry the searched splice count k so EffectHandler floats
    // (k+1)*{R}{R}{R} on resolution, matching the (k+1)-scaled cost paid below and the rollout's
    // apply_one (lockstep). Only the BASE copy is removed from hand (below); the k spliced copies are
    // OTHER hand entries, never touched -> they stay in hand, reusable. STORM (future): the
    // spells_cast_this_turn increment fires ONCE per base cast here, NOT per (k+1); each later hard-cast
    // of a leftover copy is its own increment. Unset (== 0 splices) for every non-splice spell.
    if (splice_count > 0) { entry.splice_count = splice_count; }
    // Apex of Power: carry the searched float COLOUR + the CAST-FROM-HAND gate to resolution. The gate
    // is read off the ACTUAL hand card (hand_card.m_is_staged): a hand Apex has m_is_staged == false ->
    // cast_from_hand true (adds the 10-of-one-colour float); an Apex cast off another Apex's staged exile
    // has m_is_staged == true -> cast_from_hand false (float withheld). entry.source is a fresh copy from
    // the definition, so it does NOT carry m_is_staged -- the gate MUST read hand_card. Inert (colour
    // empty, cast_from_hand unused) for every non-impulse spell.
    entry.chosen_float_color = chosen_float_color;
    entry.cast_from_hand     = !hand_card.m_is_staged;
    // Aura (Bogles): carry the searched creature to enchant so EffectHandler's aura-enter branch sets
    // aura_attached_to to the same creature the rollout's apply_one did (lockstep). 0 = not an aura /
    // heuristic fallback (ResolveEnchantTarget). Inert for every non-aura spell.
    entry.enchant_target     = enchant_target;
    entry.bestow             = bestow;
    // Evoke (Reveillark): carried to resolution so EnterBattlefield self-sacrifices the entered
    // permanent through the shared cascade (the LTB fires as on any leave). Lockstep: apply_one.
    entry.evoke              = evoke;

    int opp_index = 1 - state.active_player_index;
    switch (def->params.targeting)
    {
        case Targeting::Any:
        case Targeting::Player:
        {
            Target t;
            t.type         = Target::Type::Player;
            t.player_index = opp_index;
            entry.targets.push_back(t);
            break;
        }
        case Targeting::Creature:
        {
            // Own-creature pump (Invigorate) targets the controller's best attacker; Swords (the
            // controller-lifegain removal) targets the opponent's LARGEST creature ONLY with an enabler
            // in play (else -1 -> not cast; see FindLifegainRemovalTarget); other creature-targeting
            // spells (burn) target an opponent creature. Lockstep with the enumeration gate + rollout.
            int idx = def->params.target_own_creature
                      ? FindBestOwnAttacker(state, state.active_player_index)
                      : def->params.controller_lifegain_equals_power
                        ? FindLifegainRemovalTarget(state, state.active_player_index)
                        // Searing Blood: prefer a creature we KILL so the "when it dies" 3-to-face
                        // rider fires (else it's an arbitrary 2-damage bruise). Lockstep with the
                        // rollout apply and the value-model reach estimate (all use FindBurnKillTarget).
                        : def->params.death_trigger_damage > 0
                          ? FindBurnKillTarget(state, state.active_player_index, def->params.damage)
                          : FindOpponentCreature(state);
            // Prowess line: a creature-burn with no opponent creature self-casts onto a surviving own
            // creature (the enumeration only offered it when such a target + a prowess attacker exist).
            // Lockstep with ApplyPlanDirect / the enumeration gate. Not for Invigorate/Swords.
            if (idx < 0 && !def->params.target_own_creature && !def->params.controller_lifegain_equals_power)
            {
                idx = FindSurvivingOwnCreature(state, state.active_player_index, CreatureBurnDamage(*def, state));
            }
            if (idx < 0)
            {
                // Invigorate-type free alt-cast with no preferred (own-attacker) target: the pump is
                // moot but the alt-cost damage still resolves and can be lethal (CanAutoFireAltPayload
                // only fires this when a legal creature target exists AND it closes the game). Fall
                // through with NO creature target so the pump applies to nothing -- exactly mirroring
                // the rollout (ti<0 skips the pump; the alt-cost below still fires). A NON-alt
                // creature-targeted spell with no legal target stays uncastable (return).
                if (alt_lifegain <= 0) { return; }
                break;
            }
            Target t;
            t.type            = Target::Type::Permanent;
            t.permanent_index = idx;
            entry.targets.push_back(t);
            break;
        }
        case Targeting::NonlandPermanent:
        {
            // Unexpectedly Absent (tuck removal): autonomous target = the LARGEST opponent
            // creature (max board removed; opponent never has noncreature permanents in this
            // sim). No target -> uncastable (the enumeration gate matches; self-tuck lines are
            // human-play-only, per the user's lean-search direction 2026-08-13). Mirrors the
            // rollout's Removal tuck branch.
            int idx = -1, best_pw = -1;
            for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
            {
                const Permanent& bp = state.battlefield[bi];
                if (bp.controller_index == state.active_player_index) { continue; }
                if (bp.card.IsLand()) { continue; }
                const int pw = bp.EffectivePower();
                if (pw > best_pw) { best_pw = pw; idx = bi; }
            }
            if (idx < 0) { return; }
            Target t;
            t.type            = Target::Type::Permanent;
            t.permanent_index = idx;
            entry.targets.push_back(t);
            break;
        }
        case Targeting::Multi:
        {
            int idx = FindOpponentCreature(state);
            // Prowess line (Searing Blaze): no opponent creature -> self-cast onto a surviving own
            // creature; the "target player" then becomes that creature's controller (yourself).
            if (idx < 0) { idx = FindSurvivingOwnCreature(state, state.active_player_index, CreatureBurnDamage(*def, state)); }
            if (idx < 0) { return; }
            Target player_t;
            player_t.type         = Target::Type::Player;
            player_t.player_index = state.battlefield[idx].controller_index;   // opp_index for the normal line
            entry.targets.push_back(player_t);
            Target perm_t;
            perm_t.type            = Target::Type::Permanent;
            perm_t.permanent_index = idx;
            entry.targets.push_back(perm_t);
            break;
        }
        case Targeting::None:
        default:
            break;
    }

    // Desperate Ritual SPLICE: pay (splice_count+1)*printed cost (single Medallion floor inside
    // EffectiveCost). Matches the enum's a.cost and the rollout's apply_one -> lockstep. copies=1 for
    // every non-spliced cast -> byte-identical.
    ManaCost effective = EffectiveCost(*def, state, splice_count + 1);
    // EVOKE (Reveillark): the alternate cost replaces the printed one wholesale (CR 702.75;
    // lockstep with the enumeration's a.cost and the rollout's apply_one).
    if (evoke && def->params.evoke_cost.has_value()) { effective = *def->params.evoke_cost; }
    // Soulfire Eruption: extra Hinata discount from the searched own-creature targets (mirrors the
    // enumeration cost and the rollout's apply_one -> lockstep).
    effective.generic = std::max(0, effective.generic
                          - SoulfireOwnTargetDiscount(*def, state, state.active_player_index, own_targets));
    // Twinflame Strive: each searched EXTRA target (own_targets, reused) costs +strive_cost.
    // Mirrors the enumeration's a.cost surcharge and the rollout's apply_one -> lockstep. Inert without strive_cost.
    if (def->params.strive_cost.has_value() && own_targets > 0)
    {
        const ManaCost& sc = *def->params.strive_cost;
        effective.generic += own_targets * sc.generic;
        effective.white   += own_targets * sc.white;
        effective.blue    += own_targets * sc.blue;
        effective.black   += own_targets * sc.black;
        effective.red     += own_targets * sc.red;
        effective.green   += own_targets * sc.green;
    }
    // {X} is paid as generic mana, once per {X} pip (Crackle {X}{X}{X} -> 3X). Gated on the card
    // actually HAVING {X}: chosen_x is also the searched MODE COUNT for a modal split (Unite the
    // Coalition, modal_choose_n) whose cost is fixed -- charging it as X made the executor's
    // effective cost {2+S}WUBRG while the search's apply (correctly) priced S free, so the
    // committed cast failed at payment and the whole main silently dropped (the FiveColour
    // fd-diverge class: realized_win 6-8 vs predicted 5). Byte-identical for every real {X}
    // spell (they all have has_x).
    if (chosen_x > 0 && def->card.m_mana_cost.has_x)
    {
        int pips = def->card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
        effective.generic += chosen_x * pips;
        // Crackle: derive the discount from the declared count (lockstep with enumeration + rollout).
        effective.generic = std::max(0, effective.generic
                              - HinataGenericDiscount(*def, state, chosen_x,
                                    IsCrackleCountSpell(def->params) ? crackle_targets : -1));
        // X is now resolved into the generic cost: drop the {X} symbol so ToString()
        // prints the actual mana paid (e.g. "{4}{R}{R}") and not a stray "{X}" on top.
        effective.has_x  = false;
        effective.x_pips = 0;
    }
    // Convoke (Chord of Calling): the committed taps reduce this cast's cost -- the SAME shared
    // reduction the enumeration emitted (5d dropped-cast bug: pricing the FULL cost here dropped
    // a mana-legal convoke cast after its bodies were already tapped).
    if (def->params.convoke && (convoke_green > 0 || convoke_other > 0))
    { ApplyConvokeReduction(effective, convoke_green, convoke_other); }
    // Phyrexian ({G/P} -- Birthing Pod's cast): the variant's life-paid pips come OFF the
    // recomputed cost, the SAME strip the enumeration and rollout applied (lockstep); the life
    // itself is deducted after TapForCost succeeds, below.
    if (phyrexian_life > 0) { effective.StripPhyrexianForLife(phyrexian_life / 2); }
    // Scaled divided-damage spell (Magma Opus): recompute the committed-face cost from the archetype's
    // model on the CURRENT board, matching the enumeration/rollout (which price the same committed face
    // the same way) -> lockstep. Only a scaled Magma variant sets crackle_targets >= 0 on a
    // damage_divided spell; every other cast keeps the EffectiveCost above (byte-identical).
    if (def->params.damage_divided && crackle_targets >= 0)
    {
        for (const ScaledCastVariant& v : ResolveProvider(state).ScaledCastVariants(state, *def))
        { if (v.face == crackle_targets) { effective = v.cost; break; } }
    }
    // Maelstrom Archangel: spend a banked free cast (mirror of the rollout's prep_free +
    // cascade_free arm). Consumed here so exactly this cast skips payment; a stranded marker
    // (bank empty) falls through to the normal paid attempt below, matching apply_one.
    bool spend_free = false;
    if (free_cast && state.free_casts_available > 0)
    {
        spend_free = true;
        --state.free_casts_available;
    }
    if (alt_lifegain > 0)
    {
        // Alternative cost: pay no mana; instead the payload's players gain alt_lifegain life
        // (-> damage with a Tainted Remedy / Plague Drone in play; "each other player" cards
        // hit both 2HG heads + gift the partner -- see ApplyAltLifegainPayload). Paid at cast.
        ApplyAltLifegainPayload(state, state.active_player_index, def, alt_lifegain);
    }
    else if (!spend_free)
    {
        if (BpTraceEnabled() && !m_in_rollout)
        { BpTraceCast("exec", state, def->card.m_name.str(), effective, def->card.IsCreature()); }
        // Audit-only: the untapped-board total BEFORE the attempt, to split a colour shortfall from a
        // real total-mana shortfall at the drop below. Not computed in a normal run.
        const int audit_have = AffordAuditOn()
                             ? AvailableManaPool(state).Total() + state.floating_mana.Total() : 0;
        if (AffordAuditOn()) { g_afford_real_attempts.fetch_add(1, std::memory_order_relaxed); }
        // MTG_SPASM_UNTAP_LITERAL phase 3: tap ahead into the float before paying an untap
        // ritual (lockstep twin of the rollout apply's call -- see RitualTapAheadIntoFloat).
        // `available` EXCLUDES floating, so the pre-computed snapshot would double-count the
        // just-tapped sources for the remainder of this one payment; re-derive it.
        if (def->params.untap_x_mana_sources && SpasmUntapLiteralOn())
        {
            RitualTapAheadIntoFloat(state, chosen_x);
            available = AvailableManaPool(state);
        }
        // Same manoeuvre for an ETB-untap creature (Peregrine Drake / Cloud of Faeries): its
        // resolution untaps up to N lands, so tapping N ahead into the float makes the refund real
        // and lets the rest of the turn spend it. Lockstep twins in ApplyPlanDirect's cast branch
        // and SubsetPayableSequential -- all four worlds tap ahead or none do, or the executor
        // realises a different board than the plan priced ([fd-diverge]).
        if (def->params.etb_untap_lands > 0)
        {
            EtbUntapTapAheadIntoFloat(state, state.active_player_index, def->params.etb_untap_lands);
            available = AvailableManaPool(state);
        }
        // Sac-fodder-first (MTG_SAC_FODDER_PAYS): lockstep twin of the rollout's apply-cast
        // publish -- the additional-cost victim (own_targets) pays before any other source.
        PaySacVictimScope _psv(
            !def->params.sac_additional_creature_color.empty() ? own_targets : 0);
        // Phyrexian life gate (before the tap, which mutates): the payment must leave us alive.
        // Mirrors the rollout apply's gate exactly -- an unpayable life half drops the cast.
        if (phyrexian_life > 0
            && state.players[state.active_player_index].life <= phyrexian_life)
        {
            return;
        }
        // RESERVE THE AURA'S OWN HOST across its payment -- the executor twin of the rollout
        // apply's reservation (see apply_one) and of both payability walks. Which land pays for a
        // land Aura and which land carries it are the same scarce resource: spending the declared
        // host on the Aura's own cost strands the follow-up cast the Aura was played to enable
        // (USER, EDF seed 2 T2 -- Wild Growth on Conservatory, then Eladamri's Call off the host's
        // {W}+bonus {G}). Retried UNRESERVED on failure, so this can only ever ADD realised casts.
        // `available` is re-derived around the reservation exactly like the tap-ahead blocks above
        // (a failed TapForCost restores the board atomically).
        int reserved_host = -1;
        if (def->params.is_land_aura && def->params.land_aura_extra_mana > 0
            && enchant_target > 0)
        {
            for (Permanent& lp : state.battlefield)
            {
                if (lp.controller_index != state.active_player_index || lp.tapped) { continue; }
                if (!lp.card.IsLand() || lp.card.m_number != enchant_target) { continue; }
                lp.tapped = true; reserved_host = enchant_target; break;
            }
            if (reserved_host > 0) { available = AvailableManaPool(state); }
        }
        bool paid_ok = TapForCost(state, effective, available, def->card.IsCreature());
        if (reserved_host > 0)
        {
            SetPermTapped(state, state.active_player_index, reserved_host, false);
            available = AvailableManaPool(state);
            if (!paid_ok) { paid_ok = TapForCost(state, effective, available, def->card.IsCreature()); }
        }
        if (!paid_ok)
        {
            if (BpTraceEnabled() && !m_in_rollout) { std::fprintf(stderr, "[bp-pay]    -> FAILED\n"); }
            // SERVER-TRUTH RESOLUTION: a declared cast that cannot be paid is dropped (left in hand).
            // Mirrors ApplyPlanDirect::apply_one's drop in the rollout. Audit-only bookkeeping; see the
            // stranded-accelerant detector in GameLogger.h for why the ACCELERANT drops are the ones
            // that matter (a plain drop is benign optimism; a dropped ritual/rock strands the payoff).
            if (AffordAuditOn())
            {
                g_afford_real_fails.fetch_add(1, std::memory_order_relaxed);
                const bool colour_short = audit_have >= effective.ManaValue();
                NoteDroppedCast(def->card.m_name.str(),
                                IsManaRitual(*def)
                                    || (def->params.mana_rock && !def->card.IsCreature()),
                                colour_short);
                // ...and its NUMBER, so a later Equip in this same plan can tell that the host it
                // was co-selected against never arrived (see the stranded-equip note in
                // GameLogger.h). Audit-only, like every other line in this block.
                NoteDroppedCastNumber(hand_card.m_number);
                if (AffordAuditLevel() >= 2)
                {
                    const ManaPool rem = AvailableManaPool(state);
                    std::string untapped, tapped;
                    for (const Permanent& sp : state.battlefield)
                    {
                        if (sp.controller_index != state.active_player_index) { continue; }
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(sp.card);
                        if (!sd) { continue; }
                        const bool src = (sd->tmpl == CardTemplate::BasicLand)
                                      || (sd->tmpl == CardTemplate::ManaDork) || sd->params.mana_rock;
                        if (!src) { continue; }
                        (sp.tapped ? tapped : untapped) += sp.card.m_name.str() + ",";
                    }
                    std::fprintf(stderr,
                                 "[afford-drop] t%d %-24s cost=%-12s %s  pool[W%d U%d B%d R%d G%d C%d wild%d]"
                                 "  UNTAPPED{%s}  TAPPED{%s}\n",
                                 state.turn_number, def->card.m_name.str().c_str(),
                                 effective.ToString().c_str(),
                                 colour_short ? "COLOUR-short" : "total-short ",
                                 rem.white, rem.blue, rem.black, rem.red, rem.green,
                                 rem.colorless, rem.wild, untapped.c_str(), tapped.c_str());
                }
            }
            return;
        }
        // ...and the life half of a phyrexian payment, now that the mana half is committed
        // (CR 601.2h; lockstep twin of the rollout apply's deduction).
        if (phyrexian_life > 0)
        { state.players[state.active_player_index].life -= phyrexian_life; }
        // Paid, so the line no longer owes it -- the executor half of ApplyPlanDirect's identical
        // decrement, keeping the mid-line replicate gate below in lockstep with the rollout's.
        SubManaCost(g_line_unpaid_cost, effective);
        // A PINNED replicate count is part of THIS cast's bill (Action::cost = effective + k x
        // printed) while the payment above spends only the effective half -- so the k x printed must
        // come off the hold here or the sink gate below can never clear it. Lockstep twin of
        // ApplyPlanDirect's identical decrement; inert when nothing pinned a count.
        if (replicate_count > 0)
        {
            ManaCost rep_share;
            for (int c = 0; c < replicate_count; ++c)
            { AddManaCost(rep_share, def->card.m_mana_cost); }
            SubManaCost(g_line_unpaid_cost, rep_share);
        }
    }

    if (m_logger)
    {
        // chosen_x is the resolved X for {X} spells; pass -1 when the spell has no {X}
        // so the viewer only annotates "X=N" where it is meaningful.
        //
        // X=0 IS a meaningful value and is logged as 0, not elided. The old test also required
        // `chosen_x > 0`, which made "cast at X=0" indistinguishable in a game log from "this
        // card has no X" -- and X=0 is a real, common line (Luxurious Libation for {G}: no pump,
        // one Citizen per copy). That elision cost real diagnosis time: establishing that the
        // Libation was cast at X=0 in 172 of 172 casts needed an MTG_UNPRUNED run to prove the
        // field flowed at all, because absence was ambiguous. GameLogger's digest deliberately
        // keeps folding the OLD expression, so this changes what the log SAYS, never what the
        // engine DID -- every existing fingerprint is preserved byte for byte.
        int logged_x = def->card.m_mana_cost.has_x ? chosen_x : -1;
        // Resolve the spell's targets to stable descriptors (card identity + controller),
        // so the viewer can show e.g. Crackle -> opponent, removal -> a specific creature.
        std::vector<GameLogger::TargetDesc> tgts;
        for (const Target& t : entry.targets)
        {
            GameLogger::TargetDesc d;
            if (t.type == Target::Type::Player)
            {
                d.kind = "player";
                d.who  = (t.player_index == state.active_player_index) ? "you" : "opponent";
            }
            else if (t.type == Target::Type::Permanent
                     && t.permanent_index >= 0
                     && t.permanent_index < static_cast<int>(state.battlefield.size()))
            {
                const Permanent& tp = state.battlefield[t.permanent_index];
                d.kind      = "permanent";
                d.who       = (tp.controller_index == state.active_player_index) ? "you" : "opponent";
                d.card_num  = tp.card.m_number;
                d.card_name = tp.card.m_name;
            }
            else { continue; }
            tgts.push_back(std::move(d));
        }
        // Soulfire Eruption: the searched own creatures are extra targets (deeper dig) but are NOT
        // in entry.targets (the face-damage loop), so add them to the log here for visibility.
        if (own_targets > 0 && def->params.damage_equals_top_mv)
        {
            std::vector<int> own = SoulfireOwnCreatureOrder(state, state.active_player_index);
            for (int oi = 0; oi < own_targets && oi < static_cast<int>(own.size()); ++oi)
            {
                const Permanent& cp = state.battlefield[own[oi]];
                GameLogger::TargetDesc d;
                d.kind      = "permanent";
                d.who       = "you";
                d.card_num  = cp.card.m_number;
                d.card_name = cp.card.m_name;
                tgts.push_back(std::move(d));
            }
        }
        m_logger->LogCastSpell(hand_card.m_number, hand_card.m_name,
                               alt_lifegain > 0 ? ("(alt: opp +" + std::to_string(alt_lifegain) + ")")
                                                : effective.ToString(),
                               logged_x, tgts);
    }

    if (def->params.sacrifice_land)
    {
        // DELEGATED to the shared PerformSacrificeLandCost (SpellEffects.h) -- which since the
        // 2026-08-26 mana overhaul consumes the searched sac pin (Plan::sac_pins) itself, applies
        // the provider ranking (tapped-first; MTG_SAC_SPAWN_LAND_LAST puts a token-spawn land last)
        // and clamps a duplicate ordinal exactly as the open-coded copy here did. So this is the
        // SAME decision, not a competing one -- it just stops being a second implementation of it.
        //
        // That matters for the reason the overhaul's own note gives ("two open-coded copies of one
        // sacrifice rule is exactly the lockstep hole cg30 exposed"): a private copy is a private
        // DECISION. This site never consulted `g_play_sacrifice_chooser`, so a human at the viewer
        // could not pick the land, and it skipped FireSacrificeWatchers. Both are inert for
        // autonomous play -- the chooser is null there, and no deck pairs Slaughter-Priest with a
        // sac-land card -- which is why folding the sites is byte-identical rather than a play
        // change (verified against the post-overhaul ground truth, not assumed).
        //
        // The one shape that differs is unreachable: with lands present but an EMPTY ranking the
        // old copy sacrificed lands.front() while the helper sacrifices nothing. Every provider
        // returns a permutation of its input, so an empty ranking cannot arise from a non-empty one.
        //
        // Position is load-bearing and unchanged: an additional cost is paid at CAST time, before
        // resolution (CR 601.2h), so a Crop Rotation's fetched land is never a legal victim.
        PerformSacrificeLandCost(state, hand_card.m_name.str());
    }

    // Natural Order: "sacrifice a green creature" additional cost, paid at cast (CR 601.2h) --
    // the searched victim rides own_targets (the soulfire int, reused as a card m_number here);
    // its dies-triggers (Worldspine tokens + shuffle-back, Vaultborn copy) resolve before the
    // spell does. Lockstep with the rollout's apply_one branch.
    if (!def->params.sac_additional_creature_color.empty())
    {
        PerformSacrificeCreatureCost(state, hand_card.m_name.str(),
                                     def->params.sac_additional_creature_color, own_targets);
    }

    ap.hand.erase(std::find_if(ap.hand.begin(), ap.hand.end(),
        [&hand_card](const Card& c) { return &c == &hand_card; }));

    // Replicate: if the card (or a lord on the battlefield) grants replicate to this
    // creature type, pay the mana cost additional times before resolving to create token
    // copies. These copies enter the battlefield when the spell resolves (simplified here
    // as immediate ETB after the first copy enters via EffectHandler).
    // Note: replicate copies are queued now but actually enter after stack resolution;
    // for the goldfishing sim we pre-emptively record them so they count toward combat.
    std::vector<Card> replicate_tokens;
    if (def->card.IsCreature()
        && CanReplicate(*def, state.battlefield, state.active_player_index))
    {
        // Replicate cost = printed mana cost (CR 702.56a), not the effective cast cost.
        ManaCost rep_cost = def->card.m_mana_cost;
        ManaPool remaining = AvailableManaPool(state);
        // MTG_REPLICATE_TRACE: DIAGNOSTIC (no play change). Replicate is spent GREEDILY, and unlike
        // firebreathing -- whose pool is a by-value copy, which is what made greedy-max provably
        // dominant there -- this TAPS REAL SOURCES. So every extra copy competes with the rest of
        // the turn. Greedy-max is only dominant if nothing else wanted that mana. This counts hand
        // cards that WERE payable before the loop and are NOT after it: trades the search never
        // got offered. Zero => the firebreathing argument transfers and there is nothing to search.
        static const bool s_rep_trace = EnvOn("MTG_REPLICATE_TRACE");
        const ManaPool rep_pool_before = remaining;
        // The trace's own question, answered: 34 of 50 events squeezed a hand card (slivers, 200
        // games), so greedy-max was NOT dominant. Each copy must now clear its cost PLUS what the
        // committed line still owes -- the lockstep twin of ApplyPlanDirect's replicate gate.
        const ManaCost rep_gate = SinkCostWithLineHold(rep_cost);
        // PINNED BY THE PLAN (replicate_count >= 0): the count was chosen at queue time and its mana
        // is already in this cast's bill, so make exactly k -- never the greedy max. Lockstep twin of
        // ApplyPlanDirect's apply_one gate, so a plan the rollout scored with k copies executes with
        // k copies. -1 (autonomous play, every deck without a replicate variant) keeps the greedy.
        const int rep_cap = replicate_count;
        while ((rep_cap < 0 || static_cast<int>(replicate_tokens.size()) < rep_cap)
               && remaining.CanPay(rep_gate))
        {
            if (!TapForCost(state, rep_cost, available, true)) { break; }
            remaining = AvailableManaPool(state);
            replicate_tokens.push_back(def->card);
        }
        // Path attribution (diagnostic only): two replicate implementations exist -- this one and
        // ApplyPlanDirect's apply_one -- and which one a given mode actually runs is not obvious from
        // the call graph. Print unconditionally when replicate is AVAILABLE (not only when copies
        // were made) so "no line" means "this path did not run", never "this path made zero copies".
        if (s_rep_trace)
        {
            std::cerr << "[rep-trace] path=aiengine turn=" << state.turn_number << " "
                      << def->card.m_name.str() << " pinned=" << rep_cap
                      << " copies=" << replicate_tokens.size() << "\n";
        }
        if (s_rep_trace && !replicate_tokens.empty())
        {
            int squeezed = 0;
            for (const Card& h : state.ActivePlayer().hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
                if (!hd) { continue; }
                const ManaCost& hc = hd->card.m_mana_cost;
                if (rep_pool_before.CanPay(hc) && !remaining.CanPay(hc)) { ++squeezed; }
            }
            std::cerr << "[rep-trace] turn=" << state.turn_number << " " << def->card.m_name.str()
                      << " copies=" << replicate_tokens.size()
                      << " squeezed_hand_cards=" << squeezed << "\n";
        }
        if (m_logger && !replicate_tokens.empty())
        {
            m_logger->LogAbility(def->card.m_number, def->card.m_name,
                                 "replicate \xc3\x97" + std::to_string(replicate_tokens.size()));
        }
    }

    // STORM counter (Dragonstorm): the spell is now cast (about to go on the stack). Count it ONCE
    // per CastSpellFromHand invocation -- a spliced Desperate Ritual is ONE base cast (the k spliced
    // copies stay in hand, not cast). Mirrors TurnSolver::apply_one's increment (lockstep) so the
    // executor's storm count matches the searched line. Read only by Dragonstorm's EffectHandler
    // resolution + folded into no state key -> byte-identical for every non-storm deck.
    ++state.spells_cast_this_turn;
    state.mv_cast_this_turn += def->card.m_mana_cost.ManaValue();   // CFT damage accumulator (lockstep pair)

    // Maintain the "one more spell" budget in lockstep with TurnSolver::apply_one (same per-cast site):
    // a non-restrictor spends one (when a budget is active); the restrictor (Irencrag) installs its own
    // (decrement FIRST -- its cast is governed by any prior budget checked above -- then min). Inert
    // (budget stays -1) for every deck without a max_casts_after card.
    if (state.casts_remaining_this_turn > 0) { --state.casts_remaining_this_turn; }
    if (def->params.max_casts_after >= 0)
    {
        state.casts_remaining_this_turn =
            (state.casts_remaining_this_turn < 0)
                ? def->params.max_casts_after
                : std::min(state.casts_remaining_this_turn, def->params.max_casts_after);
    }

    state.stack.push_back(std::move(entry));

    // On-cast triggers fire when the spell is cast (CR 603.3), before it resolves.
    FireOnCastTriggers(state, *def);
    FireProwess(state, *def);
    // Real-stack cast triggers (cascade instances, demonstrate): pushed ABOVE the spell entry
    // so they -- and the free casts they make -- resolve BEFORE the spell itself (CR 601.2i /
    // 702.85a). This is what makes cascade fire on CREATURE casts. No-op for every card
    // without a cast-trigger param -> byte-identical elsewhere.
    EffectHandler::PushCastTriggers(state, *def, state.active_player_index);

    // Push replicate token copies onto the stack so they enter the battlefield when
    // the stack resolves (EffectHandler will call EnterBattlefield for each).
    for (const Card& tok : replicate_tokens)
    {
        StackEntry tok_entry;
        tok_entry.type             = StackEntry::EntryType::Spell;
        tok_entry.source           = tok;
        tok_entry.controller_index = state.active_player_index;
        state.stack.push_back(std::move(tok_entry));
    }
}

// ============================================================
// Combat / Discard
// ============================================================

std::vector<Permanent*> AIEngine::DeclareAttackers(GameState& state)
{
    // Selection is shared with the rollout (DeclareAttackerIndices, Combat.cpp); this wrapper only
    // maps the indices back to the pointer list GameEngine's signature expects.
    // Searched dork attack/hold lockstep (MTG_DORK_ATK_SEARCH, m_atk_release_pin): consume the
    // committed line's combat-variant choice for exactly this declaration. pin==1 forces the
    // collapsed-main mana hold open (the scored line attacked the held dorks); pin==0/none is
    // the natural heuristic. Never inside a rollout (its combats decide naturally).
    int pin = -1;
    if (!m_in_rollout && m_atk_release_pin >= 0) { pin = m_atk_release_pin; m_atk_release_pin = -1; }
    // pin 1 = forced RELEASE (the scored line swung the held dorks); pin 2 = forced HOLD (the
    // scored line kept an attacking dork's mana for the deferred main); pin 0/none = the natural
    // heuristic, which is what the default branch simulated.
    if (pin == 1) { g_dork_atk_override = 1; }
    else if (pin == 2) { g_dork_atk_override = 0; }
    std::vector<Permanent*> attackers;
    for (int idx : DeclareAttackerIndices(state))
    { attackers.push_back(&state.battlefield[idx]); }
    if (pin == 1 || pin == 2) { g_dork_atk_override = -1; }
    return attackers;
}

Card* AIEngine::ChooseDiscard(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.hand.empty())
    {
        throw std::runtime_error("ChooseDiscard called with empty hand");
    }
    // MTG_SHED_STATS; off by default. `m_in_rollout`, NOT a hard-coded false: this function is the
    // real cleanup's chooser, but GameEngine::PlayOut reaches the very same CleanupStep, so every
    // ROLLOUT playout (RolloutWinTurn, the bottoming/mulligan rollouts) lands here too. Counting
    // those as `real` inflated the number the analyzer's _ShedCensus reads to decide whether a deck
    // sheds in play at all -- dragons measured real=86 per 200 games while the g_real_resolution
    // trace, which every rollout scope clears, saw 7. Diagnostic-only; no play path reads these.
    ShedStats::Count(state, /*is_rollout=*/m_in_rollout);

    // Heuristic victim (land-outlet ammo, required-piece protection, highest-MV, staged-last): the
    // SHARED SelectCleanupDiscardIndex, so the search rollout's cleanup (TurnSolver) sheds the same
    // card and the greedy/d0 path is byte-identical. It is also the FALLBACK + the tie-break for the
    // searched pass below. required_pieces comes from this engine's profile (the rollout reads the
    // identical set via GameState::m_required_pieces, stamped in HandleMulligan).
    const std::vector<int> cand =
        ResolveProvider(state).CleanupDiscardCandidates(state, &m_profile.required_pieces);
    const int heur = cand.empty() ? -1 : cand.front();
    if (heur < 0) { return &ap.hand[0]; }
    const int hand_size = static_cast<int>(ap.hand.size());

    // LOCKSTEP (stage 1, docs/design/searched-discard-as-search-node.md): the executing plan's
    // searched shed (Plan::discard_choice via m_discard_choice_pin) decides the FIRST shed of the
    // real cleanup -- the search already chose among the provider's candidates under the same
    // assumptions the committed line encodes, so re-deciding here (probe or heuristic) would
    // deviate from the scored line. Consume-and-clear on first shed, clamped -- byte-for-byte the
    // rollout's semantics (TurnSolver::SimulateEndAndStartNextTurn). Never inside a rollout: a
    // shared-engine playout saves/restores the pin and sheds heuristically, as it always has.
    if (!m_in_rollout && m_discard_choice_pin >= 0)
    {
        const std::size_t pick = std::min(static_cast<std::size_t>(m_discard_choice_pin),
                                          cand.size() - 1);
        m_discard_choice_pin = -1;
        return &ap.hand[cand[pick]];
    }

    // SEARCHED cleanup discard (mirrors the lookahead bottomer, BottomCards): roll out a full
    // clairvoyant game for discarding each candidate (the card truly goes to the GRAVEYARD, unlike
    // bottoming which puts it on the library bottom) and keep only the discards that preserve the
    // EARLIEST win; the heuristic then breaks ties among them. This stops the myopic highest-MV rule
    // from pitching the deck's only combo payoff (e.g. Dragonstorm/Apex of Power) -- a discard that
    // strands the win rolls out to a later/no win and is excluded, while a redundant/stray card
    // (a spare Dragon, excess mana, a second payoff) preserves it.
    //   * Only with lookahead (m_lookahead_depth > 0); at d0 this is inert => byte-identical greedy.
    //   * NEVER inside a rollout (m_in_rollout): RolloutWinTurn -> GameEngine::PlayOut reaches this
    //     same cleanup, so a nested searched pass would blow up exponentially. Rollout cleanups use
    //     the heuristic (as before), so rollout labels are unchanged and this only refines the REAL
    //     top-level discard decision.
    // THE RETURNED LIST IS THE TRIAL SET (user design 2026-08-06: the heuristic decides the
    // candidates and returns a list of some size; ALL of those options are searched, no more).
    // A single-entry return (TreasureHunt's keep-set rule -- commissioned for exactly its
    // 15-25-card cleanups and, until now, never consulted here) decides the shed outright with
    // zero trial games: comparing one option to nothing is a no-op, so the rollout is skipped.
    // The base ranking returns the full hand, so generic decks keep the historical fan.
    if (!s_discard_node && s_searched_discard && LookaheadBottoming() && !m_in_rollout
        && hand_size > 1 && cand.size() > 1)
    {
        // Un-trialed entries keep INT_MAX so they can never read as win-optimal below. (best_win
        // is always <= max_turns + 1 once any trial ran, and the heuristic pick always runs.)
        std::vector<int> win_turn(hand_size, std::numeric_limits<int>::max());
        int best_win = std::numeric_limits<int>::max();
        for (int j : cand)
        {
            if (j < 0 || j >= hand_size) { continue; }
            if (win_turn[j] != std::numeric_limits<int>::max()) { continue; }   // duplicate entry
            GameState trial = state;
            Player& tap = trial.ActivePlayer();
            tap.graveyard.push_back(tap.hand[j]);
            tap.hand.erase(tap.hand.begin() + j);
            // Resume IN the cleanup step: the remaining sheds of this same cleanup run first (on the
            // heuristic, since m_in_rollout gates this searched pass off inside a rollout), so the
            // searched candidate is only the FIRST shed and the label is a state the real game can
            // actually reach. A plain PlayOut here would jump straight to the next turn holding the
            // whole untrimmed hand -- on Treasure Hunt that is ~20 points of phantom Land's Edge
            // ammunition, which scored every candidate a phantom turn-3 kill and cost a real T4 win
            // (gi61: T4 -> T8). See docs/design/searched-cleanup-discard.md.
            win_turn[j] = RolloutWinTurnFrom(std::move(trial), m_max_turns,
                                             GameEngine::ResumeAt::Cleanup);
            if (win_turn[j] < best_win) { best_win = win_turn[j]; }
        }
        static const bool s_discard_trace = EnvOn("MTG_DISCARD_TRACE");
        if (TurnSolver::GetTraceSolve() || s_discard_trace)
        {
            // Machine-parsed by the analyzer's discard-analysis stage (scripts/analyze_deck.py):
            // per-candidate mv / hand-copy count / land / protected let the parser evaluate
            // candidate RULES (spare-copy band, name orders) against the searched labels without
            // re-deriving card data. Keep the fields in sync with the parser.
            std::cerr << "[discard_trace turn=" << state.turn_number
                      << " depth=" << m_lookahead_depth << " seed=" << state.game_seed
                      << " handsize=" << hand_size << " heur=" << ap.hand[heur].m_name << "]\n";
            for (int j = 0; j < hand_size; ++j)
            {
                if (win_turn[j] == std::numeric_limits<int>::max()) { continue; }   // not offered by the heuristic
                int copies = 0;
                for (int k = 0; k < hand_size; ++k)
                { if (!ap.hand[k].m_is_staged && ap.hand[k].m_name == ap.hand[j].m_name) { ++copies; } }
                std::cerr << "  discard " << ap.hand[j].m_name
                          << " mv=" << CleanupDiscardManaValue(ap.hand[j])
                          << " copies=" << copies
                          << " land=" << (CleanupDiscardIsLand(ap.hand[j]) ? 1 : 0)
                          << " prot=" << (CleanupDiscardProtected(state, ap.hand[j],
                                                                  &m_profile.required_pieces) ? 1 : 0)
                          << " -> win=" << win_turn[j]
                          << (win_turn[j] == best_win ? " *" : "") << "\n";
            }
        }
        // Tie-break among the win-optimal discards: prefer the heuristic pick if it is win-optimal,
        // else the first win-optimal card (stable, index order).
        if (win_turn[heur] == best_win) { return &ap.hand[heur]; }
        for (int j = 0; j < hand_size; ++j)
        {
            if (win_turn[j] == best_win)
            {
                // DEVIATION from the heuristic. Every plan in the committed line was searched under
                // SimulateEndAndStartNextTurn, whose cleanup sheds the HEURISTIC card -- so the line
                // waiting to be replayed assumes a hand this discard is about to falsify, and the
                // rollout that justified this pick assumed no such line (RolloutWinTurnFrom starts
                // the trial on an empty one). Replaying it anyway is a train/serve split: the label
                // came from a fresh search, the realised game from a stale plan. Drop the line so
                // next turn re-searches the state we actually created.
                if (s_discard_reline) { m_committed_line.clear(); }
                return &ap.hand[j];
            }
        }
    }
    return &ap.hand[heur];
}

// ============================================================
// Land's Edge activation
// ============================================================

void AIEngine::ActivateLandsEdge(GameState& state, bool is_pre_combat)
{
    // Human play (tools/play): Land's Edge is the human's to fire -- they choose the
    // DiscardToLandsEdge amount as a plan action, executed in the segment loop via
    // ApplyPlanDirect. Suppress the autonomous end-of-main auto-fire here (mirrors the
    // matching !s_human_play guard in TurnSolver::ApplyPlanDirect) so the engine never
    // burns the lands the human just drew -- which would silently win the turn for them.
    // MTG_HUMAN_PLAY is set only by --claude-play, so every search/goldfish run is unchanged.
    // HumanPlayActive() is false inside the engine's clairvoyant rollouts, so bottoming/keep
    // rollouts DO auto-fire Land's Edge (matching the autonomous game they must reproduce).
    const bool s_human_play = HumanPlayActive();
    if (s_human_play) { return; }

    // Find the highest discard_land_damage rate among Land's Edge permanents we control.
    int rate = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.discard_land_damage > 0)
            rate = std::max(rate, def->params.discard_land_damage);
    }
    if (rate == 0) { return; }

    Player& ap = state.ActivePlayer();

    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        auto def = CardDatabase::Instance().LookupCached(c);
        if ((def ? def->card.IsLand() : c.IsLand())) { ++lands_in_hand; }
    }
    if (lands_in_hand == 0) { return; }

    // Base firing count (shared with the search's ApplyPlanDirect so both model the
    // same Land's Edge damage): fire all for lethal; else only the excess over the max
    // hand size; else hold. See LandsEdgeHeuristicFireCount.
    int fire_count = ResolveProvider(state).LandsEdgeFireCount(state, rate);

    // For depth > 0 outside a rollout: compare heuristic amount vs. firing all lands.
    // The heuristic handles "fire for lethal" and "fire excess to prevent waste";
    // the search handles the ambiguous "hold" case where early activation might win faster.
    if (m_lookahead_depth > 0 && !m_in_rollout && fire_count < lands_in_hand)
    {
        // WHERE the trial rollouts resume. Land's Edge fires at the END of a main phase, so a trial
        // captured here is mid-turn: the rest of THIS turn (combat, main 2, end step, and the
        // CLEANUP DISCARD that sheds a flooded hand) still has to happen before the next turn
        // begins. Resuming at NewTurn -- the default, and what this call passed until 2026-08-24 --
        // skips all of it, which is exactly what RolloutWinTurnFrom's own comment forbids a
        // mid-turn caller from doing: "the rest of that turn is skipped and the label describes a
        // state the real game cannot reach". The two arms are not even skipping the same thing --
        // firing empties the hand, so it is the HOLD arm that keeps, unshed, lands the real
        // cleanup would have discarded.
        //
        // HONEST SCOPE: this is a latent contract fix, not the fix for the bug below. Swept over
        // every Treasure Hunt cell at d0/d3/d5 on seeds 1001/2002/3003/4004/5005/6006/7007 --
        // 10,725 games, the only deck in the repo with a Land's Edge -- it is BYTE-IDENTICAL to the
        // old skip: 0 of 18 cells moved. It is inert only because TH's post-main-1 remainder
        // happens not to change these particular projections. MTG_LE_TRIAL_NEWTURN=1 restores the
        // old skip (A/B). See docs/design/th-colourless-first-s3003-gi301.md §7.3-7.4.
        static const bool s_newturn = EnvOn("MTG_LE_TRIAL_NEWTURN");
        const GameEngine::ResumeAt from =
            s_newturn       ? GameEngine::ResumeAt::NewTurn
            : is_pre_combat ? GameEngine::ResumeAt::Combat    // main 1 -> combat, main 2, end, cleanup
                            : GameEngine::ResumeAt::End;      // main 2 -> end, cleanup

        const int heur_count = fire_count;                    // pre-override, for the trace below

        GameState trial_heuristic = state;
        DoActivateLandsEdge(trial_heuristic, fire_count, rate, /*log=*/false);
        int w_heuristic = RolloutWinTurnFrom(std::move(trial_heuristic), m_max_turns, from);

        GameState trial_all = state;
        DoActivateLandsEdge(trial_all, lands_in_hand, rate, /*log=*/false);
        int w_all = RolloutWinTurnFrom(std::move(trial_all), m_max_turns, from);

        if (w_all < w_heuristic)
        {
            // DEVIATION from the heuristic: burning every land leaves a board the committed line
            // was never searched for. That line came out of a search whose inline executor
            // (TurnSolver::ApplyPlanDirect) auto-fires LandsEdgeHeuristicFireCount and NEVER this
            // fire-all override -- so every plan still queued in it was chosen on the assumption
            // that the lands we are about to throw away are still in hand. It holds at minimum the
            // rest of THIS turn (the second main), and when the search verified a win inside its
            // horizon it holds the whole multi-turn line to that win (see the !verified_win
            // truncation in TakeTurn) -- which is exactly the case that bites, because a verified
            // win is precisely when the plan is most specific about the lands. Meanwhile the trial
            // that justified firing rolled out on an EMPTY line (RolloutWinTurnFrom clears it) and
            // so re-searched the board it actually created. Replaying the stale line is the
            // train/serve split s_discard_reline and the searched vial already fix at their own
            // deviation sites; Land's Edge is the third such site and was missed.
            //
            // Untreated it is not a small error. TH s3304 gi301: the trial ranks fire-all a turn-4
            // win over hold's turn-5, and it is RIGHT about its own line -- re-searched, firing
            // wins on 4. The realised game replayed the pre-fire line instead, which spends a land
            // drop it no longer has and then cannot pay for the Treasure Hunt it planned
            // ("[bp-pay] -> FAILED" at T5), and won on 6. No depth (to 8) or budget (to 60000)
            // recovered it, because more search only builds a longer stale line.
            // See docs/design/th-colourless-first-s3003-gi301.md.
            static const bool s_le_reline = EnvOn("MTG_LE_RELINE", true);
            if (s_le_reline) { m_committed_line.clear(); }
            fire_count = lands_in_hand;
        }

        // MTG_LE_TRIAL: one line per fire-count decision -- the two trial projections and what was
        // chosen. This is the instrument that settled the gi301 root cause: the pair `w_heur=5
        // w_all=4` against a realised turn 6 is what shows the trial is right about a line the game
        // will not play, which no amount of reading the call graph had made visible. Off by
        // default, and the whole block is inside the depth>0 comparison, so it costs nothing.
        static const bool s_le_trial = EnvOn("MTG_LE_TRIAL");
        if (s_le_trial)
        {
            std::fprintf(stderr,
                "[le-trial] t%d main%d opp_life=%d rate=%d lands_in_hand=%d heur=%d"
                " w_heur=%d w_all=%d -> fire=%d\n",
                state.turn_number, is_pre_combat ? 1 : 2, state.Opponent().life, rate,
                lands_in_hand, heur_count, w_heuristic, w_all, fire_count);
        }
    }

    DoActivateLandsEdge(state, fire_count, rate);
}

void AIEngine::DoActivateLandsEdge(GameState& state, int count, int rate, bool log)
{
    if (count <= 0) { return; }
    Player& ap  = state.ActivePlayer();
    Player& opp = state.Opponent();

    // WHICH lands to burn: the provider ranking (LandsEdgePitchOrder), most expendable first.
    const std::vector<int> pitch = LandsEdgePitchOrder(state, &m_profile.required_pieces, count);
    std::vector<char> burn(ap.hand.size(), 0);
    for (int i : pitch) { burn[static_cast<std::size_t>(i)] = 1; }
    std::vector<Card> keep;
    int fired = 0, idx = -1;
    for (Card& c : ap.hand)
    {
        ++idx;
        bool is_land = burn[static_cast<std::size_t>(idx)] != 0;
        if (is_land && fired < count)
        {
            ap.graveyard.push_back(c);
            opp.life -= rate;
            if (rate > 0) { state.opponent_lost_life_this_turn = true; }
            // `log` is false for the trial copies in ActivateLandsEdge (those are throwaway
            // win-turn projections, not the realised game) -- logging them leaked phantom
            // ATTACK entries into the real game log. Only the final fire below logs.
            if (log && m_logger) { m_logger->LogAttack(rate, opp.life); }
            ++fired;
        }
        else
        {
            keep.push_back(std::move(c));
        }
    }
    ap.hand = std::move(keep);
}
