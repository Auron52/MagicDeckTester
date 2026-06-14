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
#include <stdexcept>

// Non-convergence detector gate, read once. When set (MTG_FLAG_NONCONV in the
// environment), TakeTurn checks each committed decision and prints a [nonconv]
// record whenever a later turn's verified win turn exceeds one proved earlier.
static const bool s_flag_nonconv = std::getenv("MTG_FLAG_NONCONV") != nullptr;

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

// ============================================================
// Mulligan
// ============================================================

void AIEngine::HandleMulligan(GameState& state, int max_turns)
{
    Player& ap = state.ActivePlayer();
    ap.library.DrawN(7, ap.hand);

    // New game: reset the per-game non-convergence baseline.
    m_nonconv_best_win  = max_turns + 1;
    m_nonconv_best_turn = 0;

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
        auto def = CardDatabase::Instance().Lookup(c.m_name);
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
                auto def = CardDatabase::Instance().Lookup(c.m_name);
                if (!def) { continue; }
                bool is_mana_source = (def->tmpl == CardTemplate::BasicLand)
                                   || (def->tmpl == CardTemplate::ManaDork);
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
            auto def = CardDatabase::Instance().Lookup(c.m_name);
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
            auto def = CardDatabase::Instance().Lookup(c.m_name);
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
            auto def     = CardDatabase::Instance().Lookup(c.m_name);
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
        auto def = CardDatabase::Instance().Lookup(c.m_name);
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
            auto sdef = CardDatabase::Instance().Lookup(hc.m_name);
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
            auto def     = CardDatabase::Instance().Lookup(hand[j].m_name);
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
                auto sdef = CardDatabase::Instance().Lookup(hc.m_name);
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
            auto def     = CardDatabase::Instance().Lookup(hand[j].m_name);
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
    GameEngine engine(*this);
    int win_turn = engine.PlayOut(trial, max_turns);
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
    m_shared_tt = m_lookahead_bottoming ? &shared_tt : saved_tt;

    for (int i = 0; i < count && !ap.hand.empty(); ++i)
    {
        int hand_size = static_cast<int>(ap.hand.size());
        std::vector<char> allowed(hand_size, 1);

        if (m_lookahead_bottoming)
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

    TurnSolver::Plan plan;  // empty plan == do nothing this phase

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
                    auto def = CardDatabase::Instance().Lookup(p.card.m_name);
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
                        auto def = CardDatabase::Instance().Lookup(c.m_name);
                        if (!def || def->tmpl != CardTemplate::DrawUntilNonland) { continue; }
                        if (avail.CanPay(th_cost)) { defer_land = true; }
                        break;
                    }
                }
                if (!defer_land) { TryPlayLand(state); }
            }
            else
            {
                // Second main: play a land if a drop still remains (matches the
                // pre-fold unconditional land play).
                TryPlayLand(state);
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
            plan = TurnSolver::SolveWithLookahead(state, is_pre_combat_main,
                                                  m_lookahead_depth, m_max_turns,
                                                  &budget, true, m_search_post_combat,
                                                  m_shared_tt,
                                                  &committed_win, &committed_sub_depth);
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
                for (const Permanent& p : state.battlefield)
                {
                    if (p.controller_index != state.active_player_index && p.card.IsCreature())
                    { ++opp_creatures; }
                }
                std::ostringstream os;
                os << "[traj] seed=" << state.game_seed
                   << " turn=" << state.turn_number
                   << " committed_win=" << committed_win
                   << " sub_depth=" << committed_sub_depth
                   << " opp_life=" << state.Opponent().life
                   << " opp_creatures=" << opp_creatures
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
                TryPlaySpecificLand(state, plan.land_to_play);
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
        std::optional<CardDefinition> copt = CardDatabase::Instance().Lookup(name);
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
            std::optional<CardDefinition> vdef =
                CardDatabase::Instance().Lookup(vp.card.m_name);
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
            break;
        }
    };

    // Cast a spell from hand by name.
    auto cast_by_name = [&](const std::string& name)
    {
        Player& ap = state.ActivePlayer();
        auto it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == ap.hand.end()) { return; }
        ManaPool available = BuildAvailableMana(state);
        CastSpellFromHand(state, *it, available);
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
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(name);
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
            std::optional<CardDefinition> hdef = CardDatabase::Instance().Lookup(hit->m_name);
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

    // Note whether an action casts a draw-engine spell (DrawUntilNonland / cascade);
    // the caller gives a second pass so the AI can play the newly drawn cards.
    auto note_draw_engine = [&](const std::string& name)
    {
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(name);
        if (d && (d->tmpl == CardTemplate::DrawUntilNonland || d->params.cascade_max_mv > 0))
        { cast_draw_engine = true; }
    };

    // Canonical execution order: Vial deployments first (lords live before spell casts),
    // then regular spells (their lands tap first), then sacrifice-land spells, then
    // graveyard (Retrace) casts last. Each cast is resolved before the next (when a
    // resolver was supplied) so same-phase interactions (prowess, lords, spectacle,
    // on-cast triggers) see the up-to-date board/life, matching the lookahead rollout.
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::ActivateVial) { deploy_via_vial(a.card_name); resolve_now(); }
    }
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && !a.sacrifice_land)
        { cast_by_name(a.card_name); note_draw_engine(a.card_name); resolve_now(); }
    }
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.sacrifice_land)
        { cast_by_name(a.card_name); note_draw_engine(a.card_name); resolve_now(); }
    }
    for (const Action& a : plan.actions)
    {
        if (a.kind == Action::Kind::CastFromGraveyard)
        { cast_from_graveyard(a.card_name, a.discard_lands); note_draw_engine(a.card_name); resolve_now(); }
    }

    // Animate lands and activate tap-token abilities with mana remaining after spells.
    // Only in pre-combat main so any resulting creatures can attack this turn.
    if (is_pre_combat_main)
    {
        UseSurplusLandAbilities(state);
        ManaPool remaining = BuildAvailableMana(state);
        AnimateLands(state, remaining);
        ActivateTapTokens(state, remaining);
    }

    // Restore any unplayed staged cards from hand back to staged_cards.
    // Walk the snapshot to match cards that are still in hand (by m_number when
    // non-zero, by name as fallback). Those no longer in hand were cast.
    Player& ap_after = state.ActivePlayer();
    for (const StagedCard& orig : staged_snapshot)
    {
        bool still_in_hand = false;
        for (Card& c : ap_after.hand)
        {
            if (!c.m_is_staged) { continue; }
            bool match = (orig.card.m_number != 0)
                         ? (c.m_number == orig.card.m_number)
                         : (c.m_name    == orig.card.m_name);
            if (match)
            {
                c.m_is_staged  = false; // clear flag; card moves back to staged
                still_in_hand  = true;
                StagedCard sc;
                sc.card        = c;
                sc.expiry_turn = orig.expiry_turn;
                ap_after.staged_cards.push_back(sc);
                break;
            }
        }
        (void)still_in_hand;
    }
    // Remove staged cards from the hand (they've been moved back to staged_cards).
    std::vector<Card> regular_hand;
    for (const Card& c : ap_after.hand)
    {
        if (!c.m_is_staged) { regular_hand.push_back(c); }
    }
    ap_after.hand = std::move(regular_hand);

    return cast_draw_engine;
}

// ---- Land drop ----

// Play a specific named land from hand. Mirrors TryPlayLand's per-card logic;
// used by the land search in TakeTurn to apply the chosen candidate.
bool AIEngine::TryPlaySpecificLand(GameState& state, const std::string& name)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }
    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        auto def = CardDatabase::Instance().Lookup(it->m_name);
        if (!def || !def->card.IsLand()) { continue; }

        if (m_logger) { m_logger->LogPlayLand(it->m_number, it->m_name); }
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
        if (def->params.etb_scry > 0) { ScryTop(state, def->params.etb_scry); }
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
        // ETB scry (e.g. Temple of Epiphany), after the land is on the battlefield.
        if (def.params.etb_scry > 0) { ScryTop(state, def.params.etb_scry); }
        return true;
    };

    // Pre-pass: when a DrawUntilNonland spell (Treasure Hunt) is in hand, prioritize
    // no_max_hand_size lands (Reliquary Tower) so they're on the battlefield before
    // TH resolves — this prevents the drawn cards from being discarded at end of turn.
    bool has_draw_until_nonland = false;
    for (const Card& c : ap.hand)
    {
        auto cdef = CardDatabase::Instance().Lookup(c.m_name);
        if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland)
        {
            has_draw_until_nonland = true;
            break;
        }
    }
    if (has_draw_until_nonland)
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
    if (ap.library.empty())   { return; }

    // Gate: only dig when a surplus land is worth more as a card than as mana/ammo.
    // Skip if any Land's Edge outlet exists (lands are ammo) or any draw/cascade/retrace
    // engine is available (the deck already has a better card source this turn).
    int active = state.active_player_index;
    for (const Card& c : ap.hand)
    {
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(c.m_name);
        if (!d) { continue; }
        if (d->params.discard_land_damage > 0) { return; }            // Land's Edge in hand
        if (d->tmpl == CardTemplate::DrawUntilNonland) { return; }    // Treasure Hunt in hand
        if (d->params.cascade_max_mv > 0)       { return; }           // Throes of Chaos in hand
    }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(p.card.m_name);
        if (d && d->params.discard_land_damage > 0) { return; }       // Land's Edge in play
    }
    for (const Card& c : ap.graveyard)
    {
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(c.m_name);
        if (d && d->params.retrace) { return; }                       // retrace engine available
    }

    int lands_controlled = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == active && p.card.IsLand()) { ++lands_controlled; }
    }
    if (lands_controlled < 2) { return; }   // don't strand ourselves on mana

    // Cycling: discard a cycling land from hand to draw a card, if its cost is payable.
    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(it->m_name);
        if (!d || !d->params.cycling_cost.has_value()) { continue; }
        ManaPool avail = BuildAvailableMana(state);
        if (!avail.CanPay(d->params.cycling_cost.value())) { continue; }
        if (!TapForCost(state, d->params.cycling_cost.value(), avail, false)) { continue; }
        if (m_logger) { m_logger->LogDiscard(it->m_number, it->m_name); }
        ap.graveyard.push_back(*it);
        ap.hand.erase(it);
        ap.hand.push_back(ap.library.DrawTop());
        return;   // one dig action per main phase is plenty for this heuristic
    }

    // Sacrifice-to-draw (e.g. Fiery Islet): pay the cost, tap + sacrifice the land, draw.
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != active || p.tapped) { continue; }
        std::optional<CardDefinition> d = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!d || !d->params.sacrifice_draw_cost.has_value()) { continue; }
        ManaPool avail = BuildAvailableMana(state);
        if (!avail.CanPay(d->params.sacrifice_draw_cost.value())) { continue; }
        p.tapped = true;  // {T} cost; tap before paying the mana so it isn't its own source
        if (!TapForCost(state, d->params.sacrifice_draw_cost.value(), avail, false))
        {
            p.tapped = false;
            continue;
        }
        ap.graveyard.push_back(p.card);
        state.battlefield.erase(state.battlefield.begin() + i);
        ap.hand.push_back(ap.library.DrawTop());
        return;
    }
}

// ---- Land animation (e.g. Mutavault) ----

void AIEngine::AnimateLands(GameState& state, ManaPool& available)
{
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index
            || p.tapped || p.is_animated) { continue; }
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
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
        std::optional<CardDefinition> def =
            CardDatabase::Instance().Lookup(state.battlefield[i].card.m_name);
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

        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def) { continue; }

        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap());
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
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap());
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
        int amt = ManaProducedPerTap(def);
        floating.Add(col, amt);
        available.Add(col, -amt);
    };

    // Ensure floating can satisfy one pip: `any` = generic, else specific colour
    // `needed`. Taps at most one producing source (a filter may also tap one feeder).
    std::function<bool(Color,bool)> produce = [&](Color needed, bool any) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def || def->params.is_filter || !usable(p, *def)) { continue; }
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
                std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
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
            std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(p.card.m_name);
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
                        std::optional<CardDefinition> sd = CardDatabase::Instance().Lookup(s.card.m_name);
                        if (!sd || sd->params.is_filter || !usable(s, *sd)) { continue; }
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
        return false;
    };

    auto pay = [&](Color needed, bool any) -> bool
    {
        if (!produce(needed, any)) { return false; }
        if (any) { Color took; return ConsumeFloatingAny(floating, took); }
        return ConsumeFloating(floating, needed);
    };

    // Pay coloured requirements first (most restrictive), then generic.
    for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) { return false; } }
    for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) { return false; } }
    for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) { return false; } }
    for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) { return false; } }
    for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) { return false; } }
    for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) { return false; } }
    for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) { return false; } }

    return true;
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

void AIEngine::CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available)
{
    Player& ap = state.ActivePlayer();
    auto def = CardDatabase::Instance().Lookup(hand_card.m_name);
    if (!def) { return; }

    StackEntry entry;
    entry.type             = StackEntry::EntryType::Spell;
    entry.source           = def->card;
    entry.source.m_number  = hand_card.m_number;  // preserve per-copy ID for logging
    entry.controller_index = state.active_player_index;

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
            int idx = FindOpponentCreature(state);
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
    if (!TapForCost(state, effective, available, def->card.IsCreature())) { return; }

    if (m_logger)
    {
        m_logger->LogCastSpell(hand_card.m_number, hand_card.m_name, effective.ToString());
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
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index
            && CanAttackFull(p, state.battlefield, state.active_player_index))
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

    // Check whether a land-discard outlet (Land's Edge) exists in hand or on battlefield.
    // When it does, lands are the ammunition and should be discarded in preference to spells.
    bool has_land_outlet = false;
    for (const Card& c : ap.hand)
    {
        auto def = CardDatabase::Instance().Lookup(c.m_name);
        if (def && def->params.discard_land_damage > 0) { has_land_outlet = true; break; }
    }
    if (!has_land_outlet)
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            auto def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (def && def->params.discard_land_damage > 0) { has_land_outlet = true; break; }
        }
    }

    if (has_land_outlet)
    {
        for (Card& c : ap.hand)
        {
            if (c.m_is_staged) { continue; }
            auto def     = CardDatabase::Instance().Lookup(c.m_name);
            bool is_land = def ? def->card.IsLand() : c.IsLand();
            if (is_land) { return &c; }
        }
    }

    // No land-discard outlet, or no land in hand: discard highest-MV spell that is not
    // a required combo piece.  Required pieces are the engine; discard them last.
    Card* best_non_req = nullptr;
    int   best_mv      = -1;
    for (Card& c : ap.hand)
    {
        if (c.m_is_staged) { continue; }
        bool is_req = false;
        for (const std::string& piece : m_profile.required_pieces)
        {
            if (c.m_name == piece) { is_req = true; break; }
        }
        if (is_req) { continue; }
        auto def = CardDatabase::Instance().Lookup(c.m_name);
        int  mv  = def ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
        if (mv > best_mv) { best_mv = mv; best_non_req = &c; }
    }
    if (best_non_req) { return best_non_req; }

    // Last resort: max-MV pick including staged cards and required pieces.
    return &(*std::max_element(ap.hand.begin(), ap.hand.end(),
        [](const Card& a, const Card& b)
        {
            if (a.m_is_staged != b.m_is_staged) { return a.m_is_staged; }
            auto da = CardDatabase::Instance().Lookup(a.m_name);
            auto db = CardDatabase::Instance().Lookup(b.m_name);
            int mv_a = da ? da->card.m_mana_cost.ManaValue() : a.m_mana_cost.ManaValue();
            int mv_b = db ? db->card.m_mana_cost.ManaValue() : b.m_mana_cost.ManaValue();
            return mv_a < mv_b;
        }));
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
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (def && def->params.discard_land_damage > 0)
            rate = std::max(rate, def->params.discard_land_damage);
    }
    if (rate == 0) { return; }

    Player& ap  = state.ActivePlayer();
    Player& opp = state.Opponent();

    int lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        auto def = CardDatabase::Instance().Lookup(c.m_name);
        if ((def ? def->card.IsLand() : c.IsLand())) { ++lands_in_hand; }
    }
    if (lands_in_hand == 0) { return; }

    // Determine effective hand size limit (Reliquary Tower grants unlimited).
    bool unlimited_hand = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }
    int max_hand = unlimited_hand ? std::numeric_limits<int>::max() : 7;

    // Lethal threshold: fewest lands needed to kill the opponent this activation.
    int lethal_lands = (opp.life + rate - 1) / rate;

    // Heuristic: fire all for lethal; fire only excess (cleanup-waste prevention); hold otherwise.
    int fire_count = 0;
    if (lands_in_hand >= lethal_lands)
    {
        fire_count = lands_in_hand;
    }
    else
    {
        int hand_size = static_cast<int>(ap.hand.size());
        int excess    = std::max(0, hand_size - max_hand);
        fire_count    = std::min(excess, lands_in_hand);
    }

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
        auto def     = CardDatabase::Instance().Lookup(c.m_name);
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
