#include "AIEngine.h"
#include "TurnSolver.h"
#include "TranspositionTable.h"
#include "SearchBudget.h"
#include "Profiler.h"
#include "../cards/CardDatabase.h"
#include "../core/GameEngine.h"
#include "../core/SpellEffects.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <mutex>
#include <stdexcept>

// Non-convergence detector gate, read once. When set (MTG_FLAG_NONCONV in the
// environment), TakeTurn checks each committed decision and prints a [nonconv]
// record whenever a later turn's verified win turn exceeds one proved earlier.
static const bool s_flag_nonconv = std::getenv("MTG_FLAG_NONCONV") != nullptr;

// Route pre-combat (and second-main) decisions through the full-depth
// commit-the-line search instead of SolveWithLookahead, so "depth N" means fully
// searching N complete turns. This is now the DEFAULT engine (validated by the
// overnight A/B: better-or-equal on every case, 0 regressions). Set MTG_LEGACY_SEARCH
// to opt back into the old SolveWithLookahead baseline -- the held-out reference kept
// reproducible for future A/Bs. (The old MTG_FULL_DEPTH opt-in is gone; setting it is
// harmless as full depth is the default now.)
static const bool s_full_depth = std::getenv("MTG_LEGACY_SEARCH") == nullptr;

// Commit-the-line fidelity oracle (MTG_FD_ORACLE): when a recomputed line's searched
// win exceeds an earlier line's, the committed line we just replayed did NOT realise
// its predicted win — a rollout/real-execution divergence. Flag the seed + turn so it
// can be traced. Only meaningful with s_full_depth.
static const bool s_fd_oracle = std::getenv("MTG_FD_ORACLE") != nullptr;

// Enumerate-all-earliest-wins rule-miner (MTG_DUMP_EWINS): at each REAL pre-combat main,
// emit one JSON line ({"ewins":...}) scoring every candidate top-level play by the earliest
// full-game win it leads to (TurnSolver::EnumerateEarliestWins). Feeds the analyzer's
// heuristic-grounding pattern analysis (scripts/analyze_earliest_wins.py). EXPENSIVE -- run
// single-threaded on a few games. MTG_DUMP_EWINS_TURN limits it to one decision turn (default
// 1 = opening only, to bound cost; 0 = every turn). Set MTG_SEARCH_ORDER=1 to also expand
// cast orderings. Inert (zero overhead) unless MTG_DUMP_EWINS is set.
static const bool s_dump_ewins = std::getenv("MTG_DUMP_EWINS") != nullptr;
static const int  s_dump_ewins_turn = []{
    const char* e = std::getenv("MTG_DUMP_EWINS_TURN"); return e ? std::atoi(e) : 1;
}();

// Provider cast-order rank for a hand cast by name (lower = cast earlier). MUST stay
// byte-for-byte identical to TurnSolver::CastRankOf so the executor's canonical cast
// order matches the rollout's (lockstep). Unknown card falls to the noncreature rank.
static int CastRankAI(const GameState& state, const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    return d ? ResolveProvider(state).CastOrderRank(state, *d) : 20;
}

// Mirror of TurnSolver::OrderingOpaque: a cast with a mid-turn re-solve breakpoint
// (draw / staging / cascade / retrace). The CastOrderRank reordering is skipped for any
// set containing one (its ordering is search-owned); such a set keeps its canonical
// plan/breakpoint order. MUST match TurnSolver::OrderingOpaque (lockstep).
static bool OrderingOpaqueAI(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawUntilNonland
        || d->params.stages_cards
        || d->params.cascade_max_mv > 0
        || d->params.retrace
        || d->params.draw > 0;
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

    ap.library.DrawN(7, ap.hand);

    // New game: reset the per-game non-convergence baseline.
    m_nonconv_best_win  = max_turns + 1;
    m_nonconv_best_turn = 0;

    // New game: drop any committed full-depth line from a previous game.
    m_committed_line.clear();
    m_fd_best_win  = max_turns + 1;
    m_fd_best_turn = 0;

    int mulligan_count = 0;
    while (true)
    {
        bool keep = static_cast<int>(ap.hand.size()) <= m_profile.stop_at
                 || KeepHand(ap.hand, mulligan_count);

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
        ap.library.Shuffle(state.game_seed + static_cast<uint64_t>(mulligan_count));
        ++mulligan_count;
        ap.library.DrawN(7, ap.hand);
    }

    if (mulligan_count > 0) { BottomCards(state, mulligan_count, max_turns); }

    m_kept_opening_hand.clear();
    for (const Card& c : ap.hand)
    {
        m_kept_opening_hand.push_back(c.m_name);
    }
}

bool AIEngine::KeepHand(const std::vector<Card>& hand, int mulligan_count) const
{
    int effective_size = static_cast<int>(hand.size()) - mulligan_count;
    if (effective_size <= 1)                 { return true; }
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
                for (Color produced : def->params.produces)
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
            if (def->params.produces.size() == 1)
            {
                ++pool[def->params.produces[0]];
            }
            else if (!def->params.produces.empty())
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
        for (Color produced : def->params.produces) { ++pool[produced]; }
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
                for (Color c : def->params.produces)
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
                for (Color c : def->params.produces)
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

int AIEngine::RolloutWinTurn(GameState trial, int max_turns)
{
    GameLogger* saved = m_logger;
    m_logger          = nullptr;
    m_in_rollout      = true;
    // The rollout PlayOut shares this AIEngine by reference, so isolate its committed
    // full-depth line: stash the real game's line, run the rollout on a fresh empty
    // line, then restore. Otherwise the rollout would consume/overwrite the line the
    // real game is mid-way through replaying.
    std::deque<TurnSolver::PhasePlan> saved_line = std::move(m_committed_line);
    m_committed_line.clear();
    GameEngine engine(*this);
    int win_turn = engine.PlayOut(trial, max_turns);
    m_committed_line = std::move(saved_line);
    m_in_rollout = false;
    m_logger     = saved;
    return win_turn > 0 ? win_turn : max_turns + 1;
}

void AIEngine::BottomCards(GameState& state, int count, int max_turns)
{
    Player& ap = state.ActivePlayer();

    // One transposition table shared across every candidate rollout of this whole
    // bottoming pass: each RolloutWinTurn plays a full lookahead game over the same
    // fixed library, so later turns/candidates reuse memoised exact win turns rather
    // than rebuilding a fresh table ~(count*hand_size) times. Scoped to this loop only
    // (m_shared_tt restored to its prior value on return), so the real game and any
    // enclosing rollout are unaffected and byte-identical. See m_shared_tt.
    TranspositionTable  shared_tt;
    TranspositionTable* saved_tt = m_shared_tt;
    m_shared_tt = LookaheadBottoming() ? &shared_tt : saved_tt;

    for (int i = 0; i < count && !ap.hand.empty(); ++i)
    {
        int hand_size = static_cast<int>(ap.hand.size());
        std::vector<char> allowed(hand_size, 1);

        if (LookaheadBottoming())
        {
            // Clairvoyant greedy: roll out a full game for removing each candidate
            // card (it goes to the bottom of the library, so the draws the rollout
            // sees are the real future draws). The best removal is the one that
            // preserves the earliest win. Restrict the heuristic tiebreak below to
            // those win-optimal removals, so lookahead only ever overrides the
            // heuristic to secure a strictly earlier win.
            std::vector<int> win_turn(hand_size, 0);
            int best_win = std::numeric_limits<int>::max();
            for (int j = 0; j < hand_size; ++j)
            {
                GameState trial = state;
                Player& trial_ap = trial.ActivePlayer();
                trial_ap.library.push_back(trial_ap.hand[j]);
                trial_ap.hand.erase(trial_ap.hand.begin() + j);
                win_turn[j] = RolloutWinTurn(std::move(trial), max_turns);
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
    // An external controller (claude-play / human-play) may decide differently.
    if (m_external_vial_chooser) { return m_external_vial_chooser(state, vial, heuristic); }
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

    // The post-combat (second) main phase does NOTHING unless post-combat search
    // is explicitly enabled (see SetSearchPostCombat). In a clairvoyant goldfish
    // combat creates no new resources, so everything castable was already cast in
    // the first main (casting is timing-neutral there) and the lookahead rollout
    // likewise plays no second main. An unsearched greedy second main would be an
    // off-model action the search never evaluated — worse, it could execute a cast
    // the rollout merely assumed, so model and reality would diverge. Decks whose
    // combat enables genuine second-main plays (Bear Umbra / Hidden Strings
    // untapping lands; spectacle costs unlocked by combat damage) turn it on.
    const bool play_this_phase = is_pre_combat_main || m_search_post_combat;

    // External-controller intercept (Claude-play / human-play prototype, opt-in via
    // SetExternalChooser; inert otherwise so the normal autonomous AI path is
    // unchanged). Offer the SAME candidate plans the solver would search and execute
    // the chosen one, bypassing the search. Self-contained: ApplyPlan is the rollout-
    // style direct application -- it applies the same life/board/draw effects and fires
    // the same on-cast triggers that decide win/loss, so win-turn outcomes are faithful
    // (log fidelity is lower than the stack-based real path, fine for a flag-generating
    // sweep). Combat and cleanup discard stay on the engine heuristics.
    if (m_external_chooser && play_this_phase)
    {
        std::vector<TurnSolver::Plan> plans =
            TurnSolver::EnumerateMainPlans(state, is_pre_combat_main);
        if (!plans.empty())
        {
            int idx = m_external_chooser(state, plans, is_pre_combat_main);
            if (idx >= 0 && idx < static_cast<int>(plans.size()))
            {
                TurnSolver::ApplyPlan(state, plans[idx], is_pre_combat_main);
            }
            // (idx < 0 or out of range => pass / cast nothing this phase)
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
                    ManaPool avail = BuildAvailableMana(state);
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
                    std::getenv("MTG_LEGACY_2ND_MAIN_LAND") != nullptr;
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
            if (s_full_depth)
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
                    int searched_depth = m_lookahead_depth;
                    TurnSolver::SearchLine line = TurnSolver::FullSearchLine(
                        state, m_lookahead_depth, m_max_turns, m_search_post_combat,
                        m_shared_tt, &budget, &searched_depth);

                    // Oracle: track the EARLIEST win any line predicted this game. The
                    // realised win is compared against it at game end (OnGameEnd) — NOT
                    // per recompute, because a pre-combat recompute happens before that
                    // turn's combat and so can't see a win that arrives via combat, which
                    // made the old per-recompute flag fire on games that won on time.
                    if (s_fd_oracle && !m_in_rollout && line.win_turn < m_fd_best_win)
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
                        std::getenv("MTG_FD_ALWAYS_RESEARCH") != nullptr;
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
                TryPlaySpecificLand(state, plan.land_to_play, plan.fetch_target);
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
                            int chosen_x = 0)
    {
        Player& ap = state.ActivePlayer();
        auto it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == ap.hand.end()) { return; }
        ManaPool available = BuildAvailableMana(state);
        CastSpellFromHand(state, *it, available, 0, tutor_target, chosen_x);
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
        ManaPool available = BuildAvailableMana(state);
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
        ManaPool available = BuildAvailableMana(state);
        ManaCost effective = EffectiveCost(*def, state);
        if (!available.CanPay(effective)) { return; }
        if (!TapForCost(state, effective, available, def->card.IsCreature())) { return; }

        // Additional cost: discard `discard_lands` land cards from hand to the graveyard.
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
                  || d->params.stages_cards))
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
        return d && d->params.stages_cards;
    };

    // A spell whose resolution reveals new cards to play (Light Up the Stage staging,
    // Treasure Hunt's DrawUntilNonland, cascade) -- the same set ApplyPlanDirect
    // re-solves after.
    auto is_draw_engine = [&](const std::string& name) -> bool
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        return d && (d->tmpl == CardTemplate::DrawUntilNonland || d->params.cascade_max_mv > 0
                     || d->params.stages_cards);
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
        if (std::getenv("MTG_FD_TRACE") != nullptr)
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
            { if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); } else { cast_by_name(a.card_name, a.tutor_target, a.chosen_x); } resolve_now(); }
            else if (a.kind == Action::Kind::CastFromGraveyard)
            { cast_from_graveyard(a.card_name, a.discard_lands); resolve_now(); }
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
    std::function<void()> resolve_draw_breakpoint = [&]()
    {
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
        TurnSolver::Plan extra = TurnSolver::Solve(state, is_pre_combat_main);
        for (const Action& a : extra.actions)
        { if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); } }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
            {
                cast_by_name(a.card_name, a.tutor_target, a.chosen_x); resolve_now();
                if (is_draw_engine(a.card_name)) { resolve_draw_breakpoint(); }
            }
        }
        for (const Action& a : extra.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
            { cast_by_name(a.card_name, a.tutor_target, a.chosen_x); resolve_now(); }
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
        // non-flood draw turns keep their normal land timing.
        if (is_pre_combat_main
            && static_cast<int>(state.ActivePlayer().hand.size()) > 7) { TryPlayLand(state); }
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
    static const bool s_order_trace = std::getenv("MTG_ORDER_TRACE") != nullptr;
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
            if (OrderingOpaqueAI(a.card_name)) { opaque = true; }
        }
        if (!opaque && !plan.searched_order)
        {
            std::stable_sort(ns.begin(), ns.end(), [&](int x, int y)
            { return CastRankAI(state, plan.actions[x].card_name) < CastRankAI(state, plan.actions[y].card_name); });
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
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x); note_draw_engine(a.card_name); resolve_now();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else { resolve_draw_breakpoint(); }
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
            && OrderingOpaqueAI(a.card_name)) { opaque = true; break; }
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
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x); note_draw_engine(a.card_name); resolve_now();
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
            cast_by_name(a.card_name, a.tutor_target, a.chosen_x); note_draw_engine(a.card_name); resolve_now();
            if (s_full_depth && is_draw_engine(a.card_name))
            {
                if (fd_plan_committed)
                { if (!bp_replayed) { replay_recorded(plan.breakpoint_actions); bp_replayed = true; } }
                else { resolve_draw_breakpoint(); }
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
    // branch (CastRankAI == TurnSolver::CastRankOf) so the executor realises the same line
    // the rollout scored. No draw engine here, so no breakpoint handling is needed.
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(plan.actions.size()); ++i)
    {
        const Action& a = plan.actions[i];
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land) { order.push_back(i); }
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastRankAI(state, plan.actions[x].card_name) < CastRankAI(state, plan.actions[y].card_name); });
    for (int oi : order)
    {
        const Action& a = plan.actions[oi];
        if (a.alt_cost) { cast_alt(a.card_name, a.alt_lifegain); resolve_now(); continue; }
        cast_by_name(a.card_name, a.tutor_target, a.chosen_x); note_draw_engine(a.card_name); resolve_now();
    }
    }
    }
    for (const Action& a : plan.actions)
    {
        if (staged_break) { break; }
        if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
        { cast_by_name(a.card_name, a.tutor_target, a.chosen_x); note_draw_engine(a.card_name); resolve_now(); }
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

    // Animate lands and activate tap-token abilities with mana remaining after spells.
    // Only in pre-combat main so any resulting creatures can attack this turn.
    if (is_pre_combat_main)
    {
        // Reactive dig only on the non-committed paths (depth 0, or the develop-when-stuck
        // fallback that carries no recorded script); committed turns already replayed their
        // recorded digs above, so running it again would dig a second, off-line time.
        if (!fd_plan_committed) { UseSurplusLandAbilities(state); }
        ManaPool remaining = BuildAvailableMana(state);
        AnimateLands(state, remaining);
        ActivateTapTokens(state, remaining);
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
                                   const std::string& fetch_target)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }
    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        auto def = CardDatabase::Instance().Lookup(it->m_name);
        if (!def || !def->card.IsLand()) { continue; }

        if (m_logger) { m_logger->LogPlayLand(it->m_number, it->m_name); }
        // Fetchland: sacrifice it to search out a real land (same heuristic as the rollout
        // so the committed line replays identically).
        if (!def->params.fetch_land_types.empty())
        {
            Card fetchland = *it;
            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            ap.graveyard.push_back(fetchland);
            PerformFetch(state, state.active_player_index, def->params, fetch_target);
            return true;
        }
        bool tapped = LandEntersTapped(state, *def);
        Permanent perm;
        perm.card              = def->card;
        perm.card.m_number     = it->m_number;
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
        return true;
    }
    return false;
}

bool AIEngine::TryPlayLand(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    auto play_land_iter = [&](std::vector<Card>::iterator it, const CardDefinition& def) -> bool
    {
        if (m_logger) { m_logger->LogPlayLand(it->m_number, it->m_name); }
        // Fetchland: sacrifice it to search out a real land (PerformFetch heuristic).
        if (!def.params.fetch_land_types.empty())
        {
            Card fetchland = *it;
            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            ap.graveyard.push_back(fetchland);
            PerformFetch(state, state.active_player_index, def.params);
            return true;
        }
        // Resolve "as this land enters" choices (shock life payment, reveal-untap)
        // while the card is still in hand; this also tells us if it enters tapped.
        bool tapped = LandEntersTapped(state, def);
        Permanent perm;
        perm.card              = def.card;
        perm.card.m_number     = it->m_number;
        perm.controller_index  = state.active_player_index;
        perm.owner_index       = state.active_player_index;
        perm.entered_this_turn = true;
        perm.tapped            = tapped;
        if (def.params.enters_tapped_with_depletion > 0)
        {
            Counter dep;
            dep.type  = Counter::Type::Depletion;
            dep.count = def.params.enters_tapped_with_depletion;
            perm.counters.push_back(dep);
        }
        state.battlefield.push_back(perm);
        ap.hand.erase(it);
        ++ap.lands_played_this_turn;
        // ETB scry/surveil (e.g. Temple of Epiphany scry; Thundering Falls surveil),
        // after the land is on the battlefield.
        if (def.params.etb_scry > 0)    { ScryTop(state, def.params.etb_scry); }
        if (def.params.etb_surveil > 0) { SurveilTop(state, def.params.etb_surveil); }
        if (def.params.etb_bounce_land) { BounceKarooLand(state, state.active_player_index, static_cast<int>(state.battlefield.size()) - 1); }
        return true;
    };

    // Pre-pass: prioritize a no_max_hand_size land (Reliquary Tower) when either a
    // DrawUntilNonland spell (Treasure Hunt) is in hand (play it BEFORE the draw) OR the hand
    // is already flooding past max size (play it AFTER a draw to KEEP the cards). The latter
    // is the gi=65/gi=881 case: Treasure Hunt resolved and drew a Reliquary, but with TH no
    // longer in hand the old TH-in-hand-only check missed it, so the drawn Reliquary (and the
    // whole flood) was discarded at cleanup instead of kept as Land's Edge ammo.
    bool has_draw_until_nonland = false;
    for (const Card& c : ap.hand)
    {
        auto cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland)
        {
            has_draw_until_nonland = true;
            break;
        }
    }
    bool hand_flooding = static_cast<int>(ap.hand.size()) > 7;
    if (has_draw_until_nonland || hand_flooding)
    {
        for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
        {
            auto def = CardDatabase::Instance().Lookup(it->m_name);
            if (!def || !def->card.IsLand() || !def->params.no_max_hand_size) { continue; }
            return play_land_iter(it, *def);
        }
    }

    // Four-pass priority: prefer untapped-entering over tapped-entering, and
    // multi-color (wild mana) over single-color within each group.
    //   Pass 0: untapped + multi-color
    //   Pass 1: untapped + any
    //   Pass 2: tapped   + multi-color
    //   Pass 3: tapped   + any
    for (int pass = 0; pass < 4; ++pass)
    {
        bool want_untapped = (pass < 2);
        bool want_multi    = (pass == 0 || pass == 2);
        for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
        {
            auto def = CardDatabase::Instance().Lookup(it->m_name);
            if (!def || !def->card.IsLand()) { continue; }
            bool is_tapped = def->params.enters_tapped;
            bool is_multi  = def->params.produces.size() > 1;
            if (want_untapped && is_tapped)  { continue; }
            if (!want_untapped && !is_tapped) { continue; }
            if (want_multi && !is_multi)     { continue; }
            return play_land_iter(it, *def);
        }
    }
    return false;
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
        ManaPool avail = BuildAvailableMana(state);
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
        ManaPool avail = BuildAvailableMana(state);
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
        ManaPool avail = BuildAvailableMana(state);
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

// ---- Land animation (e.g. Mutavault) ----

void AIEngine::AnimateLands(GameState& state, ManaPool& available)
{
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index
            || p.tapped || p.is_animated) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.can_animate || !def->params.animate_cost.has_value()) { continue; }
        const ManaCost& cost = def->params.animate_cost.value();
        if (!available.CanPay(cost)) { continue; }
        if (!TapForCost(state, cost, available, false)) { continue; }
        p.is_animated = true;
    }
}

// ---- Tap-and-pay token abilities (e.g. Sliver Hive) ----

void AIEngine::ActivateTapTokens(GameState& state, ManaPool& available)
{
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != state.active_player_index
            || state.battlefield[i].tapped) { continue; }
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!def || !def->params.tap_token_cost.has_value()) { continue; }

        if (!def->params.tap_token_requires_subtypes.empty())
        {
            bool found = false;
            for (int j = 0; j < bf_size; ++j)
            {
                if (state.battlefield[j].controller_index != state.active_player_index) { continue; }
                for (const std::string& req : def->params.tap_token_requires_subtypes)
                    for (const std::string& cs : state.battlefield[j].card.m_subtypes)
                        if (cs == req) { found = true; break; }
                if (found) { break; }
            }
            if (!found) { continue; }
        }

        const ManaCost& add_cost = def->params.tap_token_cost.value();
        if (!available.CanPay(add_cost)) { continue; }

        state.battlefield[i].tapped = true;
        if (!TapForCost(state, add_cost, available, true))
        {
            state.battlefield[i].tapped = false;
            continue;
        }

        // CreateToken appends to battlefield — access `i` via index afterward, never via ref.
        CreateToken(state, state.active_player_index,
                    def->params.tap_token_power,
                    def->params.tap_token_toughness,
                    def->params.tap_token_subtypes);
    }
}

// ---- Mana ----

ManaPool AIEngine::BuildAvailableMana(const GameState& state) const
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

        // Depletion lands contribute 2; multi-color lands contribute 1 wild; filter
        // lands (Cascade Bluffs) contribute wild iff fed, else {C}. See AddSourceToPool.
        AddSourceToPool(pool, state, *def);
    }
    return pool;
}

bool AIEngine::TapForCost(GameState& state, const ManaCost& cost, ManaPool& available,
                          bool for_creature)
{
    Player&  ap     = state.ActivePlayer();
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed (held locally)

    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def.params.mana_rock;
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        return true;
    };

    // Tap one non-filter source, producing `amt` of colour `col`, applying depletion
    // decrement and pain. Mirrors the accounting in BuildAvailableMana (AddSourceToPool).
    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        p.tapped = true;
        DecrementDepletionOnTap(p);
        if (def.params.tap_self_damage > 0) { ap.life -= def.params.tap_self_damage; }
        // Grove of the Burnwillows: each coloured tap makes the opponent gain 1 (-> 1 damage
        // with Tainted Remedy out). Mirrors TurnSolver's tap_source.
        if (def.params.tap_opponent_lifegain > 0)
        { OpponentGainsLife(state, state.active_player_index, def.params.tap_opponent_lifegain); }
        int amt = ManaProducedPerTap(def);
        floating.Add(col, amt);
        available.Add(col, -amt);
    };

    // Ensure floating can satisfy one pip: `any` = generic, else specific colour
    // `needed`. Taps at most one producing source (a filter may also tap one feeder).
    // allow_ramp: may a ramp filter (Ferrous Lake) be used? false when called to FEED a
    // ramp filter's {1}, so ramp filters never feed each other (avoids recursion; the
    // unmodelled ramp->ramp chain is inert unless 2+ ramp filters are the ONLY sources).
    std::function<bool(Color,bool,bool)> produce = [&](Color needed, bool any, bool allow_ramp) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || def->params.is_filter || def->params.ramp_filter || !usable(p, *def)) { continue; }
            Color col;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                col = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
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
                if (available.colorless > 0)  { --available.colorless; }
                else if (available.wild > 0)  { --available.wild; }
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
            // Need one of the filter's colours floating; feed it from a non-filter source.
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
                        for (Color c : sd->params.produces) { if (c == ic) { m = true; break; } }
                        if (!m) { continue; }
                        tap_source(s, *sd, ic);
                        fed = true; break;
                    }
                    if (fed) { break; }
                }
                if (!fed) { continue; }  // can't feed this filter; try the next one
            }
            for (Color c : def->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            p.tapped = true;
            floating.Add(out, 2);
            if (available.wild > 0) { --available.wild; }  // filter counted as 1 wild in the pool
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
                // Pay the {1}: use floating if any, else feed one mana from a non-ramp source.
                if (floating.Total() == 0 && !produce(Color::Colorless, true, false)) { continue; }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                for (Color c : def->params.produces) { floating.Add(c, 1); }
                if (available.wild > 0) { --available.wild; }  // ramp filter counted as 1 wild
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
    // (mirrors TurnSolver::TapForCostDirect, so the rollout and the real game stay in
    // sync). Snapshot so the greedy success path is byte-identical (no GT churn) and only
    // previously-FAILING casts gain the chain solution. See TapForCostBacktrack.
    const std::vector<Permanent> bf_pre = state.battlefield;
    const int life_pre = state.players[active].life;
    auto greedy = [&]() -> bool
    {
        // Pay coloured requirements first (most restrictive), then generic.
        for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) return false; }
        for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) return false; }
        for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) return false; }
        for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) return false; }
        for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) return false; }
        for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) return false; }
        for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) return false; }
        return true;
    };
    if (greedy()) { return true; }
    const std::vector<Permanent> bf_greedy_fail = state.battlefield;
    const int life_greedy_fail = state.players[active].life;
    state.battlefield        = bf_pre;
    state.players[active].life = life_pre;
    if (TapForCostBacktrack(state, cost, for_creature, ManaPool{})) { return true; }
    state.battlefield        = bf_greedy_fail;
    state.players[active].life = life_greedy_fail;
    return false;
}

// ---- Spell selection ----

ManaCost AIEngine::EffectiveCost(const CardDefinition& def, const GameState& state) const
{
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        return def.params.spectacle_cost.value();
    }
    ManaCost cost = def.card.m_mana_cost;
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
    // Hinata per-target reduction for fixed-cost spells (mirrors TurnSolver::EffectiveCost;
    // {X} spells apply it where the chosen X is added to generic, in CastSpellFromHand).
    if (!def.card.m_mana_cost.has_x)
    {
        cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
    }
    return cost;
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
                                 int chosen_x)
{
    Player& ap = state.ActivePlayer();
    auto def = CardDatabase::Instance().LookupCached(hand_card);
    if (!def) { return; }

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
            // Own-creature pump (Invigorate) targets the controller's best attacker; other
            // creature-targeting spells (removal/burn) target an opponent creature.
            int idx = def->params.target_own_creature
                      ? FindBestOwnAttacker(state, state.active_player_index)
                      : FindOpponentCreature(state);
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
            if (idx < 0) { return; }
            Target player_t;
            player_t.type         = Target::Type::Player;
            player_t.player_index = opp_index;
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

    ManaCost effective = EffectiveCost(*def, state);
    // {X} is paid as generic mana, once per {X} pip (Crackle {X}{X}{X} -> 3X).
    if (chosen_x > 0)
    {
        int pips = def->card.m_mana_cost.x_pips; if (pips < 1) { pips = 1; }
        effective.generic += chosen_x * pips;
        effective.generic = std::max(0, effective.generic - HinataGenericDiscount(*def, state, chosen_x));
    }
    if (alt_lifegain > 0)
    {
        // Alternative cost: pay no mana; instead make the opponent gain alt_lifegain life
        // (-> that much damage with a Tainted Remedy / Plague Drone in play). Paid at cast.
        OpponentGainsLife(state, state.active_player_index, alt_lifegain);
    }
    else if (!TapForCost(state, effective, available, def->card.IsCreature())) { return; }

    if (m_logger)
    {
        m_logger->LogCastSpell(hand_card.m_number, hand_card.m_name,
                               alt_lifegain > 0 ? ("(alt: opp +" + std::to_string(alt_lifegain) + ")")
                                                : effective.ToString());
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
        ManaPool remaining = BuildAvailableMana(state);
        while (remaining.CanPay(rep_cost))
        {
            if (!TapForCost(state, rep_cost, available, true)) { break; }
            remaining = BuildAvailableMana(state);
            replicate_tokens.push_back(def->card);
        }
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

    // The victim-selection policy (land-outlet ammo, required-piece protection, staged-last)
    // is the SHARED SelectCleanupDiscardIndex so the search rollout's cleanup sheds the same
    // card. required_pieces comes from this engine's profile (the rollout reads the identical
    // set via GameState::m_required_pieces, stamped in HandleMulligan).
    int idx = SelectCleanupDiscardIndex(state, &m_profile.required_pieces);
    return &ap.hand[idx];
}

// ============================================================
// Land's Edge activation
// ============================================================

void AIEngine::ActivateLandsEdge(GameState& state)
{
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
        DoActivateLandsEdge(trial_heuristic, fire_count, rate);
        int w_heuristic = RolloutWinTurn(std::move(trial_heuristic), m_max_turns);

        GameState trial_all = state;
        DoActivateLandsEdge(trial_all, lands_in_hand, rate);
        int w_all = RolloutWinTurn(std::move(trial_all), m_max_turns);

        if (w_all < w_heuristic) { fire_count = lands_in_hand; }
    }

    DoActivateLandsEdge(state, fire_count, rate);
}

void AIEngine::DoActivateLandsEdge(GameState& state, int count, int rate)
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
            if (m_logger) { m_logger->LogAttack(rate, opp.life); }
            ++fired;
        }
        else
        {
            keep.push_back(std::move(c));
        }
    }
    ap.hand = std::move(keep);
}
