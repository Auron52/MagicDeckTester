#include "../core/EnvFlags.h"
#include "AIEngine.h"
#include "ManaPayment.h"
#include "LandPlay.h"
#include "EngineFlags.h"
#include "TurnSolver.h"
#include "TranspositionTable.h"
#include "SearchBudget.h"
#include "Profiler.h"
#include "../cards/CardDatabase.h"
#include "../core/GameEngine.h"
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
static const bool  s_searched_discard = EnvOn("MTG_SEARCHED_DISCARD");
// MTG_DIVERGENCE_LOG=<file>: DIAGNOSTIC (diagnosis only, no play change). On the search-driven path,
// at each real pre-combat main decision, ALSO compute the greedy d0 plan (TurnSolver::Solve) for the
// SAME untouched state and append one JSONL record {seed,turn,diverge,search_land,search,greedy,feat[]}.
// A state where the search and greedy plans differ is one the greedy rollout policy would misplay --
// the raw material for classifying the d0/rollout gap as rule-shaped (state-determined) vs lookahead-
// bound (draw-dependent). The game CONTINUES on the search plan (state is untouched here). Features are
// the non-clairvoyant ExtractMidGameFeatures for clustering. Run SINGLE-THREADED for readable output.
// Inert unless set. See docs/design/dragonstorm-d0-divergence-digest.md.
static const char* s_divergence_log = std::getenv("MTG_DIVERGENCE_LOG");

static void EmitEvalRows(const GameState& state, int max_turns, bool second_main)
{
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
        const TurnSolver::EarliestWinReport rep =
            TurnSolver::EnumerateEarliestWins(s, max_turns, second_main, s_eval_rows_rollout,
                                              s_eval_rollout_depth, s_eval_rows_honest);
        const int e = (rep.earliest > 0 && rep.earliest <= max_turns) ? rep.earliest : (max_turns + 1);
        earliest_sum += e; ++earliest_n;
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
    : m_profile(std::move(profile)), m_lookahead_depth(lookahead_depth), m_budget_ms(budget_ms) {}

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
                  << " proven_at_turn=" << m_fd_best_turn << "\n";
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

    // Attach the deck's learned mid-game play evaluator (nothing to do with the mulligan itself --
    // this is just the first per-game hook that has BOTH the live state and the profile). Non-owning
    // (m_profile outlives the game); propagates through every deep copy. Presence + MTG_EVAL_MODEL
    // gate its actual use in TurnSolver::Solve. See GameState::m_evaluator.
    state.m_evaluator = m_profile.eval_model.empty() ? nullptr : &m_profile.eval_model;
    // ... and the deck's learned leaf value model (replaces the search's horizon rollout when
    // MTG_VALUE_MODEL is set). Same non-owning / deep-copy propagation. See GameState::m_value_model.
    state.m_value_model = m_profile.value_model.empty() ? nullptr : &m_profile.value_model;

    ap.library.DrawN(7, ap.hand);

    // New game: reset the per-game non-convergence baseline.
    m_nonconv_best_win  = max_turns + 1;
    m_nonconv_best_turn = 0;

    // New game: drop any committed full-depth line from a previous game.
    m_committed_line.clear();
    m_fd_best_win  = max_turns + 1;
    m_fd_best_turn = 0;

    // New game: reset the game-persistent leaf cache (MTG_LEAF_CACHE) so a reused batch
    // worker's AIEngine does not accumulate/cross-hit across games. See m_leaf_cache.
    if (m_leaf_cache_enabled) { m_leaf_cache.Clear(); }

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

void AIEngine::FlagNonConvergence(const GameState& state, const TurnSolver::Plan& plan,
                                  int committed_win, int committed_sub_depth)
{
    const int turn = state.turn_number;

    // A win is exhaustively VERIFIED only when the committing pass ran at full
    // depth, the win is within the horizon, and the win sits inside that pass's
    // branched lookahead (so "no earlier win" was actually proved, not assumed).
    const bool verified = committed_sub_depth == m_lookahead_depth - 1
                       && committed_win <= m_max_turns
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
    ShuffleEvalGuard  _seg(true);   // decoupling instrument: rollout shuffles use shuffle_salt_search
    // The rollout PlayOut shares this AIEngine by reference, so isolate its committed
    // full-depth line: stash the real game's line, run the rollout on a fresh empty
    // line, then restore. Otherwise the rollout would consume/overwrite the line the
    // real game is mid-way through replaying.
    std::deque<TurnSolver::PhasePlan> saved_line = std::move(m_committed_line);
    m_committed_line.clear();
    GameEngine engine(*this);
    int win_turn = engine.PlayOut(trial, max_turns);
    if (lands_out)
    {
        int n = 0;
        for (const Permanent& p : trial.battlefield)
        { if (p.controller_index == 0 && p.card.IsLand()) { ++n; } }
        *lands_out = n;
    }
    m_committed_line = std::move(saved_line);
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
    // Whether to use it is driven by the PROFILE's bottoming_enabled flag (baked in at generation:
    // off for low-R/noise-limited profiles, on for validated high-R ones). MTG_EXHAUSTIVE_BOTTOM is a
    // 3-state A/B override: unset => follow the profile flag; "0" => force off; else => force on. Keep
    // is presence-gated and independent of this, so a profile with bottoming off still uses its keep.
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

    // When forced, bottom EXACTLY the listed cards (its size, not `count`): a full list (size ==
    // count) is a faithful replay; an empty/short list is a deliberate probe that exposes the
    // pre-bottom depth-N hand (used to derive the bottomed set when patching old references).
    const int stop = m_forced_mull_active ? static_cast<int>(m_forced_bottom_numbers.size()) : count;
    for (int i = 0; i < stop && !ap.hand.empty(); ++i)
    {
        int hand_size = static_cast<int>(ap.hand.size());
        std::vector<char> allowed(hand_size, 1);

        if (LookaheadBottoming() && !m_forced_mull_active)
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
                if (s_blind_bottom)
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

bool AIEngine::DecideVialCharge(const GameState& state, const Permanent& vial) const
{
    // Hand-aware charge policy (shared with the rollout): hold while a creature of the
    // current MV is in hand to deploy, otherwise climb toward a bigger creature in hand
    // (up to Haytham's MV 4), else pre-charge toward the deck's dominant MV. See
    // WantVialCharge.
    bool heuristic = ResolveProvider(state).WantVialCharge(state, vial);
    // An external controller (claude-play / human-play) may decide differently -- but NOT inside the
    // engine's clairvoyant rollouts (bottoming / keep eval), which must play autonomously so the kept
    // hand reproduces the real search's game. Without the m_in_rollout guard the external chooser
    // would fire (exit 70 / consume a --choices token) from within a bottoming rollout for a Vial deck.
    if (m_external_vial_chooser && !m_in_rollout) { return m_external_vial_chooser(state, vial, heuristic); }
    return heuristic;
}

bool AIEngine::TakeTurn(GameState& state, bool is_pre_combat_main,
                        const std::function<void(GameState&)>& resolve_stack)
{
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

    // Echo upkeep resolution (Mogg War Marshal {1}{R}, Stingscourger {3}{R}) -- executor world.
    // "At the beginning of your upkeep, if this came under your control since your last upkeep,
    // sacrifice it unless you pay its echo cost" (CR 702.29). This lives at the top of the pre-combat
    // main rather than GameEngine::UpkeepStep because paying the echo taps lands through AIEngine's
    // (private) mana API -- BuildAvailableMana + TapForCost, the byte-identical mirror of the rollout's
    // TapForCostDirect -- so both worlds spend identical mana. Resolving here (before any land drop or
    // cast, and before the search/ewins enumeration below) makes the tapped mana unavailable for this
    // turn's plays and removes any declined body before combat -- functionally the upkeep timing for a
    // goldfish. Guarded to pre_combat so a permanent that entered THIS turn is not charged until its
    // next upkeep, and the per-permanent echo_resolved flag makes it idempotent (a no-op in-rollout,
    // where SimulateEndAndStartNextTurn already resolved it). Heuristic (deterministic, no branch): a
    // creature whose own death makes a replacement token (Mogg -- dies_watch_includes_self + a death
    // token) DECLINES (net same body, saves mana); any other echo creature (Stingscourger) PAYS if
    // affordable, else is sacrificed. Gated on echo_cost -> byte-identical for every non-echo deck.
    if (is_pre_combat_main)
    {
        for (std::size_t i = 0; i < state.battlefield.size(); )
        {
            Permanent& p = state.battlefield[i];
            if (p.controller_index != state.active_player_index || p.echo_resolved)
            { ++i; continue; }
            const CardDefinition* edef = CardDatabase::Instance().LookupCached(p.card);
            if (!edef || !edef->params.echo_cost) { ++i; continue; }
            p.echo_resolved = true;   // obligation resolved this upkeep, whatever the outcome
            const CardParams& ep = edef->params;
            const bool self_token = ep.dies_watch_includes_self && ep.dies_trigger_creates_tokens > 0;
            // Affordability decided up front so the human-play chooser is offered only a REAL choice
            // (paying is possible); if unaffordable the creature is simply sacrificed (no decision).
            // BuildAvailableMana is a read-only snapshot (taps nothing), so computing it unconditionally
            // is behaviourally identical to the old non-self_token-only path.
            ManaPool avail = BuildAvailableMana(state);
            const bool affordable = avail.CanPay(*ep.echo_cost);
            bool pay = affordable && !self_token;   // heuristic: self-replacing body declines, else pays
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
            state.players[ctrl].graveyard.push_back(dead);
            state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
            OnCreatureDies(state, ctrl, dead);   // may append a token at the end -> safe (no echo_cost)
            // Do not advance i: the erased slot now holds the next permanent.
        }
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
        // #12 "Commit Line": the phase re-prompts not only after a DRAW (the classic breakpoint below)
        // but also when the committed line's plays put a NEW permanent with a player-activated ability
        // into play -- e.g. playing Horizon Canopy (a land) this turn enables its same-turn
        // "{1},{T},Sacrifice: draw" dig, which the old draw-only breakpoint skipped (playing a land
        // draws nothing), forcing the sac to the next turn. Human-play only (inside use_external) ->
        // GT-neutral; the autonomous search never enters this branch. "Commit turn" naturally skips this
        // breakpoint: its auto-pass (index.html advanceTo) fires on every same-turn main_phase decision.
        // Two signals decide the #12 breakpoint precisely -- their INTERSECTION on a source that
        // BECAME both this segment:
        //   inplay_sac(state) -- sac-to-draw sources IN PLAY and UNTAPPED (Horizon Canopy, Fiery Islet).
        //     A source appears here the segment you PLAY it untapped; a STANDING one is present at
        //     phase start (so it never counts as "new"), which is what stops per-turn re-prompt spam.
        //   offer_sac(plans)  -- sac-to-draw digs actually OFFERED among the enumerated plans
        //     (AppendHumanPlayDigPlans additionally gates on the {1}-style sac cost being affordable
        //     THIS phase). This is the affordability signal.
        // Re-prompt iff a source is NEWLY in inplay_sac (just played untapped) AND currently in
        // offer_sac (its sac is affordable). Requiring BOTH excludes: (a) a standing source whose sac
        // merely became affordable when you played a second land -- spam, not "a just-played ability";
        // and (b) a source you played untapped but cannot yet pay to sac -- a pointless re-prompt.
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
            if (seg == 0 && is_pre_combat_main && g_play_storage_hold_chooser)
            {
                for (Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index) { continue; }
                    if (p.tapped || p.storage_counters <= 0) { continue; }
                    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                    if (!d || !d->params.storage_land
                        || d->params.storage_charge_mode == "upkeep_if_tapped") { continue; }
                    if ((*g_play_storage_hold_chooser)(state, p, p.storage_counters))
                    { p.storage_hold_this_turn = true; }
                }
            }
            std::vector<TurnSolver::Plan> plans =
                TurnSolver::EnumerateMainPlans(state, is_pre_combat_main);
            if (plans.empty()) { break; }
            std::set<std::string> cur_inplay = inplay_sac(state);
            std::set<std::string> cur_offer  = offer_sac(plans);
            // Phase complete once a committed plan neither DREW (draw breakpoint) nor put a NEW,
            // AFFORDABLE sac source into play (#12 breakpoint). seg==0 is the initial prompt, shown.
            if (seg > 0 && !drew_last)
            {
                bool new_activation = false;
                for (const std::string& name : cur_inplay)
                { if (!prev_inplay.count(name) && cur_offer.count(name)) { new_activation = true; break; } }
                if (!new_activation) { break; }
            }
            // Post-combat (second) main: on the FIRST entry, only prompt when there is a real play
            // available (a Spectacle spell unlocked by combat, a castable spell, or a land drop) --
            // with nothing to do the second main is a no-op, so skip it silently rather than asking
            // the human to "pass" every single turn. A seg>0 re-prompt (draw or #12 new-activation)
            // is handled above and always shows -- matching the first main, which never suppresses.
            if (!is_pre_combat_main && seg == 0)
            {
                bool any_play = false;
                for (const TurnSolver::Plan& p : plans)
                { if (!p.actions.empty() || !p.land_to_play.empty()) { any_play = true; break; } }
                if (!any_play) { break; }
            }
            size_t lib_before = state.ActivePlayer().library.size();
            const int this_main_ordinal = m_ext_main_ordinal++;   // #10 side-channel key
            int idx = m_external_chooser(state, plans, is_pre_combat_main);
            if (idx < 0 || idx >= static_cast<int>(plans.size())) { break; }  // pass / done
            // #10: honour a human-pinned cast order for this main-phase decision (empty / unset =>
            // no-op => canonical). Copy the chosen plan so the enumerated menu stays untouched.
            TurnSolver::Plan chosen = plans[idx];
            if (g_play_cast_order_chooser)
            {
                std::vector<std::string> ord = (*g_play_cast_order_chooser)(this_main_ordinal);
                ReorderPlanCasts(chosen, ord);
            }
            TurnSolver::ApplyPlan(state, chosen, is_pre_combat_main);
            drew_last = state.ActivePlayer().library.size() < lib_before;
            prev_inplay = std::move(cur_inplay);
        }

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
        const bool fold_land = (m_lookahead_depth > 0 && is_pre_combat_main);

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
                if (is_pre_combat_main && m_committed_line.empty())
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
                    static const int s_vmd_override = []{ const char* e = std::getenv("MTG_VALUE_MIN_DEPTH");
                                                          return (e && *e) ? std::atoi(e) : -1; }();
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
                    if (!verified_win && !line.phases.empty())
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
                    SearchBudget fallback_budget = SearchBudget::FromVirtualMs(m_budget_ms);
                    plan = TurnSolver::SolveWithLookahead(state, is_pre_combat_main,
                                                          m_lookahead_depth, m_max_turns,
                                                          &fallback_budget, true,
                                                          m_search_post_combat, m_shared_tt,
                                                          &committed_win, &committed_sub_depth);
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
                    for (int li = 0; li < 3 && li < static_cast<int>(tp.library.size()); ++li)
                    { os << tp.library[li].m_name << "; "; }
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
                    TryPlaySpecificLand(state, plan.land_to_play, plan.fetch_target, plan.land_face);
                }
            }
        }
        else
        {
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
            if (copt->card.HasSupertype(Supertype::Legendary))
            {
                EnforceLegendRule(state, state.active_player_index);
            }
            break;
        }
    };

    // Cast a spell from hand by name.
    auto cast_by_name = [&](const std::string& name, const std::string& tutor_target = "",
                            int chosen_x = 0, int own_targets = 0, int ponder_keep = -1,
                            int crackle_targets = -1,   // -1 = legacy auto-max discount (see Action::crackle_targets)
                            int splice_count = 0,       // Desperate Ritual splice count k (0 = plain cast)
                            const std::string& chosen_float_color = "",  // Apex of Power: searched float colour
                            int enchant_target = 0)      // Aura: searched creature to enchant (0 = none)
    {
        Player& ap = state.ActivePlayer();
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
                          crackle_targets, splice_count, chosen_float_color, enchant_target);
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
            && !ResolveProvider(state).ShouldEmitRiskyAltPayload(state, state.active_player_index, *adef))
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
        // (Autonomous ExecutePlan path -- claude-play executes retrace via TurnSolver::ApplyPlan,
        // where the human's land-discard chooser lives; here the heuristic first-land pick stands.)
        int discarded = 0;
        for (auto hit = ap.hand.begin(); hit != ap.hand.end() && discarded < discard_lands; )
        {
            const CardDefinition* hdef = CardDatabase::Instance().Lookup(hit->m_name);
            bool is_land = hdef ? hdef->card.IsLand() : hit->IsLand();
            if (is_land)
            {
                if (m_logger) { m_logger->LogDiscard(hit->m_number, hit->m_name); }
                ap.graveyard.push_back(*hit);
                hit = ap.hand.erase(hit);
                ++discarded;
            }
            else { ++hit; }
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
                  || d->params.stages_cards || d->params.impulse_exile > 0))
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
                     || d->params.stages_cards || d->params.expressive_iteration
                     || d->params.impulse_exile > 0);
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

        for (const Action& a : recs)
        {
            if (a.kind == Action::Kind::PlayLand)
            { TryPlaySpecificLand(state, a.card_name); }
            else if (a.kind == Action::Kind::ActivateVial)
            { deploy_via_vial(a.card_name); resolve_now(); }
            else if (a.kind == Action::Kind::CastFromHand)
            { if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); } else { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); } resolve_now(); }
            else if (a.kind == Action::Kind::CastFromGraveyard)
            { cast_from_graveyard(a.card_name, a.discard_lands); resolve_now(); }
            else if (a.kind == Action::Kind::SacForMana)
            { ApplySacForMana(state, state.active_player_index, a.sac_source_id, a.chosen_float_color, a.ritual_float, a.sac_victim_id); }
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
    std::function<void(int)> resolve_draw_breakpoint = [&](int bp_depth)
    {
        if (bp_depth >= kMaxDrawBreakpointDepth || ++rdb_calls > kMaxDrawBreakpointCalls) { return; }
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
        // divergence, exactly the class of bug the Hook 21/22 mirrors below exist to prevent).
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
                if (extra.land_decided && !extra.land_to_play.empty())
                { TryPlaySpecificLand(state, extra.land_to_play, extra.fetch_target, extra.land_face); }
            }
        }
        if (!bp_searched_here) { extra = TurnSolver::Solve(state, is_pre_combat_main); }
        // Lockstep trace (MTG_BP_TRACE): the EXECUTOR's breakpoint sequence, to be diffed against
        // ApplyPlanDirect's [bp-apply] lines for the same committed line. Diagnosis only.
        if (BpTraceEnabled())
        {
            std::fprintf(stderr, "[bp-exec]  turn=%d idx=%d bp_at=%d bp_choice=%d searched=%d\n",
                         state.turn_number, bp_idx, plan.bp_at, plan.bp_choice,
                         bp_searched_here ? 1 : 0);
        }
        // Lotus Bloom: apply any SacForMana (float the chosen colour) / Suspend this re-solve chose BEFORE
        // the casts, mirroring TakeTurn's top-of-turn pre-pass. Without this a mid-turn Lotus sac was
        // silently dropped in the fallback breakpoint re-solve, so the floated mana never materialised and
        // the staged Dragonstorm/rituals it was meant to pay for no-op'd. Empty (no SacForMana) -> unchanged.
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::SacForMana)
            { ApplySacForMana(state, state.active_player_index, a.sac_source_id, a.chosen_float_color, a.ritual_float, a.sac_victim_id); }
            else if (a.kind == Action::Kind::Suspend)
            { ApplySuspend(state, state.active_player_index, a.card_name); }
        }
        for (const Action& a : extra.actions)
        { if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); } }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
            {
                cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); resolve_now();
                if (is_draw_engine(a.card_name)) { resolve_draw_breakpoint(bp_depth + 1); }
            }
        }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); resolve_now(); }
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
        // marginal Land's Edge ammo for a lethal this turn (Hook 21, mirrors the search's
        // play_drawn_flood_keep_land) so the deferred drop isn't spent out of the ammo pool.
        // Hook 22 is mirrored here for the SAME reason as Hook 21: the rollout applies it in
        // play_drawn_flood_keep_land, so an executor that developed the drop anyway would play a land
        // the proved line does not -- a rollout/execution divergence, not a heuristic disagreement.
        // The keep-land case (Hook 13) still goes through TryPlayLand's own Reliquary pre-pass.
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
        { ApplySacForMana(state, state.active_player_index, a.sac_source_id, a.chosen_float_color, a.ritual_float, a.sac_victim_id); }
        else if (a.kind == Action::Kind::Suspend)
        { ApplySuspend(state, state.active_player_index, a.card_name); }
    }
    // Whole-turn batch pre-payment -- mirror of ApplyPlanDirect (lockstep): tap for the combined
    // cost of the main hand casts and pre-load floating so the casts below drain the pool instead of
    // the stranding per-cast greedy. Same (state, plan.actions) inputs as the rollout at the same
    // logical point (after the land drop + Vial deploys) -> identical prepay. Declined -> greedy.
    TurnSolver::BatchPrepayMainCasts(state, plan.actions);
    // Indices of sac-land casts hoisted ahead of the Spectacle spell (mirrors ApplyPlanDirect);
    // the trailing sac loop skips them so they are not double-cast. Empty unless a Spectacle
    // enabler is hoisted below.
    std::set<size_t> spec_hoisted_sac;
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
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else { resolve_draw_breakpoint(0); }
            }
            else if (stage_draw_break(a.card_name)) { staged_break = true; break; }
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
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land && !a.alt_cost
            && ResolveProvider(state).CastEnablerFirst(state, a.card_name))
        {
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now();
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
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now();
            spec_hoisted_sac.insert(ai);
        }
    }
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.alt_cost)
        {
            cast_alt(a.card_name, a.alt_lifegain); resolve_now();
        }
        else if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land
                 && !ResolveProvider(state).CastEnablerFirst(state, a.card_name))
        {
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else { resolve_draw_breakpoint(0); }
            }
            else if (stage_draw_break(a.card_name)) { staged_break = true; break; }
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
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { order.push_back(i); }
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(state, plan.actions[x], plan.actions[y]); });
    for (int oi : order)
    {
        const Action& a = plan.actions[oi];
        if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); resolve_now(); continue; }
        cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now();
    }
    }
    }
    for (size_t ai = 0; ai < plan.actions.size(); ++ai)
    {
        if (staged_break) { break; }
        if (spec_hoisted_sac.count(ai)) { continue; }   // already cast by the Spectacle hoist
        const Action& a = plan.actions[ai];
        if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
        { cast_by_name(a.card_name, a.tutor_target, a.chosen_x, a.soulfire_own_targets, a.ponder_keep, a.crackle_targets, a.splice_count, a.chosen_float_color, a.enchant_target); note_draw_engine(a.card_name); resolve_now(); }
    }
    for (const Action& a : plan.actions)
    {
        if (staged_break) { break; }
        if (a.kind == Action::Kind::CastFromGraveyard)
        { cast_from_graveyard(a.card_name, a.discard_lands); note_draw_engine(a.card_name); resolve_now(); }
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
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::SacCreatureOutlet)
        {
            ManaPool avail = BuildAvailableMana(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            {
                if (a.sac_count > 1)
                { ApplySacCreatureOutletBurst(state, state.active_player_index, a.sac_source_id, a.sac_count); }
                else
                { ApplySacCreatureOutlet(state, state.active_player_index, a.sac_source_id, a.sac_victim_id); }
            }
        }
        else if (a.kind == Action::Kind::Channel)
        {
            ManaPool avail = BuildAvailableMana(state);
            if (TapForCost(state, a.cost, avail, /*for_creature=*/false))
            { ApplyChannel(state, state.active_player_index, a.hand_index, a.card_name, a.direct_damage); }
        }
    }

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
                                   const std::string& fetch_target, const std::string& land_face)
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
        if (!d || !d->card.IsLand()) { continue; }
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
    if (attacker_indices.empty()) { return; }
    if (!ControlsFirebreathingSource(state, state.active_player_index)) { return; }
    ManaPool pool = AvailableManaPool(state);
    // Human play (#4): let the player cap the pump. Probe the greedy-max activation count on a COPY
    // (ApplyFirebreathing takes the pool by value, so the probe does not consume it), ask the chooser
    // for k in [0, max], then apply exactly k. The chooser is nulled in every search/rollout
    // (RevealLogPause) and installed only under --claude-play, so autonomous stays greedy = byte-identical.
    if (g_play_firebreathe_chooser)
    {
        GameState probe = state;
        int max_k = ApplyFirebreathing(probe, state.active_player_index, attacker_indices, pool);
        if (max_k > 0)
        {
            int k = (*g_play_firebreathe_chooser)(state, state.active_player_index, attacker_indices, max_k);
            if (k < 0 || k > max_k) { k = max_k; }   // -1 / out-of-range -> greedy default (current behaviour)
            ApplyFirebreathing(state, state.active_player_index, attacker_indices, pool, k);
            return;
        }
    }
    ApplyFirebreathing(state, state.active_player_index, attacker_indices, pool);
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
                                 const std::string& chosen_float_color, int enchant_target)
{
    Player& ap = state.ActivePlayer();
    auto def = CardDatabase::Instance().LookupCached(hand_card);
    if (!def) { return; }

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
    if (chosen_x > 0) { entry.chosen_x = chosen_x; }
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
    // Soulfire Eruption: extra Hinata discount from the searched own-creature targets (mirrors the
    // enumeration cost and the rollout's apply_one -> lockstep).
    effective.generic = std::max(0, effective.generic
                          - SoulfireOwnTargetDiscount(*def, state, state.active_player_index, own_targets));
    // {X} is paid as generic mana, once per {X} pip (Crackle {X}{X}{X} -> 3X).
    if (chosen_x > 0)
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
    // Scaled divided-damage spell (Magma Opus): recompute the committed-face cost from the archetype's
    // model on the CURRENT board, matching the enumeration/rollout (which price the same committed face
    // the same way) -> lockstep. Only a scaled Magma variant sets crackle_targets >= 0 on a
    // damage_divided spell; every other cast keeps the EffectiveCost above (byte-identical).
    if (def->params.damage_divided && crackle_targets >= 0)
    {
        for (const ScaledCastVariant& v : ResolveProvider(state).ScaledCastVariants(state, *def))
        { if (v.face == crackle_targets) { effective = v.cost; break; } }
    }
    if (alt_lifegain > 0)
    {
        // Alternative cost: pay no mana; instead make the opponent gain alt_lifegain life
        // (-> that much damage with a Tainted Remedy / Plague Drone in play). Paid at cast.
        OpponentGainsLife(state, state.active_player_index, alt_lifegain);
    }
    else
    {
        if (BpTraceEnabled() && !m_in_rollout)
        { BpTraceCast("exec", state, def->card.m_name.str(), effective, def->card.IsCreature()); }
        // Audit-only: the untapped-board total BEFORE the attempt, to split a colour shortfall from a
        // real total-mana shortfall at the drop below. Not computed in a normal run.
        const int audit_have = AffordAuditOn()
                             ? AvailableManaPool(state).Total() + state.floating_mana.Total() : 0;
        if (AffordAuditOn()) { g_afford_real_attempts.fetch_add(1, std::memory_order_relaxed); }
        if (!TapForCost(state, effective, available, def->card.IsCreature()))
        {
            if (BpTraceEnabled() && !m_in_rollout) { std::fprintf(stderr, "[bp-pay]    -> FAILED\n"); }
            // SERVER-TRUTH RESOLUTION: a declared cast that cannot be paid is dropped (left in hand).
            // Mirrors ApplyPlanDirect::apply_one's drop in the rollout. Audit-only bookkeeping; see the
            // stranded-accelerant detector in GameLogger.h for why the ACCELERANT drops are the ones
            // that matter (a plain drop is benign optimism; a dropped ritual/rock strands the payoff).
            if (AffordAuditOn())
            {
                g_afford_real_fails.fetch_add(1, std::memory_order_relaxed);
                NoteDroppedCast(def->card.m_name.str(),
                                IsManaRitual(*def)
                                    || (def->params.mana_rock && !def->card.IsCreature()),
                                audit_have >= effective.ManaValue());
            }
            return;
        }
    }

    if (m_logger)
    {
        // chosen_x is the resolved X for {X} spells; pass -1 when the spell has no {X}
        // so the viewer only annotates "X=N" where it is meaningful.
        int logged_x = (def->card.m_mana_cost.has_x && chosen_x > 0) ? chosen_x : -1;
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
        // Prefer a tapped land (already spent this turn) to preserve untapped mana sources.
        int idx = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
            if (idx < 0)   { idx = i; }       // first land found
            if (p.tapped)  { idx = i; break; } // tapped land preferred
        }
        if (idx >= 0)
        {
            ap.graveyard.push_back(state.battlefield[idx].card);
            state.battlefield.erase(state.battlefield.begin() + idx);
        }
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
        while (remaining.CanPay(rep_cost))
        {
            if (!TapForCost(state, rep_cost, available, true)) { break; }
            remaining = AvailableManaPool(state);
            replicate_tokens.push_back(def->card);
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
    std::vector<Permanent*> attackers;
    const DecisionProvider& provider = ResolveProvider(state);
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index
            && CanAttackFull(p, state.battlefield, state.active_player_index)
            && provider.ShouldAttackWith(state, p))
        {
            attackers.push_back(&p);
        }
    }
    return attackers;
}

Card* AIEngine::ChooseDiscard(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.hand.empty())
    {
        throw std::runtime_error("ChooseDiscard called with empty hand");
    }

    // Heuristic victim (land-outlet ammo, required-piece protection, highest-MV, staged-last): the
    // SHARED SelectCleanupDiscardIndex, so the search rollout's cleanup (TurnSolver) sheds the same
    // card and the greedy/d0 path is byte-identical. It is also the FALLBACK + the tie-break for the
    // searched pass below. required_pieces comes from this engine's profile (the rollout reads the
    // identical set via GameState::m_required_pieces, stamped in HandleMulligan).
    const int heur = SelectCleanupDiscardIndex(state, &m_profile.required_pieces);
    const int hand_size = static_cast<int>(ap.hand.size());

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
    if (s_searched_discard && LookaheadBottoming() && !m_in_rollout && hand_size > 1)
    {
        std::vector<int> win_turn(hand_size, 0);
        int best_win = std::numeric_limits<int>::max();
        for (int j = 0; j < hand_size; ++j)
        {
            GameState trial = state;
            Player& tap = trial.ActivePlayer();
            tap.graveyard.push_back(tap.hand[j]);
            tap.hand.erase(tap.hand.begin() + j);
            win_turn[j] = RolloutWinTurn(std::move(trial), m_max_turns);
            if (win_turn[j] < best_win) { best_win = win_turn[j]; }
        }
        if (TurnSolver::GetTraceSolve())
        {
            std::cerr << "[discard_trace depth=" << m_lookahead_depth << "]\n";
            for (int j = 0; j < hand_size; ++j)
            {
                std::cerr << "  discard " << ap.hand[j].m_name << " -> win=" << win_turn[j]
                          << (win_turn[j] == best_win ? " *" : "") << "\n";
            }
        }
        // Tie-break among the win-optimal discards: prefer the heuristic pick if it is win-optimal,
        // else the first win-optimal card (stable, index order).
        if (win_turn[heur] == best_win) { return &ap.hand[heur]; }
        for (int j = 0; j < hand_size; ++j)
        {
            if (win_turn[j] == best_win) { return &ap.hand[j]; }
        }
    }
    return &ap.hand[heur];
}

// ============================================================
// Land's Edge activation
// ============================================================

void AIEngine::ActivateLandsEdge(GameState& state)
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
        GameState trial_heuristic = state;
        DoActivateLandsEdge(trial_heuristic, fire_count, rate, /*log=*/false);
        int w_heuristic = RolloutWinTurn(std::move(trial_heuristic), m_max_turns);

        GameState trial_all = state;
        DoActivateLandsEdge(trial_all, lands_in_hand, rate, /*log=*/false);
        int w_all = RolloutWinTurn(std::move(trial_all), m_max_turns);

        if (w_all < w_heuristic) { fire_count = lands_in_hand; }
    }

    DoActivateLandsEdge(state, fire_count, rate);
}

void AIEngine::DoActivateLandsEdge(GameState& state, int count, int rate, bool log)
{
    if (count <= 0) { return; }
    Player& ap  = state.ActivePlayer();
    Player& opp = state.Opponent();

    std::vector<Card> keep;
    int fired = 0;
    for (Card& c : ap.hand)
    {
        auto def     = CardDatabase::Instance().LookupCached(c);
        bool is_land = def ? def->card.IsLand() : c.IsLand();
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
