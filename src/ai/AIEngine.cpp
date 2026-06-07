#include "AIEngine.h"
#include "TurnSolver.h"
#include "SearchBudget.h"
#include "../cards/CardDatabase.h"
#include "../core/GameEngine.h"
#include "../core/SpellEffects.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

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

    int mulligan_count = 0;
    while (!KeepHand(ap.hand, mulligan_count))
    {
        for (Card& c : ap.hand) { ap.library.push_back(c); }
        ap.hand.clear();
        ap.library.Shuffle(state.game_seed + static_cast<uint64_t>(mulligan_count));
        ++mulligan_count;
        ap.library.DrawN(7, ap.hand);
        if (static_cast<int>(ap.hand.size()) <= m_profile.stop_at) { break; }
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

        int best_total_deficit  = std::numeric_limits<int>::max();
        int best_uncastable     = std::numeric_limits<int>::max();
        int best_usefulness     = std::numeric_limits<int>::max();

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
            bool prefer = (chosen == -1)
                       || first_better
                       || second_better
                       || (uncastable_cnt == best_uncastable
                           && total_deficit == best_total_deficit
                           && usefulness   <  best_usefulness);

            if (prefer)
            {
                best_total_deficit = total_deficit;
                best_uncastable    = uncastable_cnt;
                best_usefulness    = usefulness;
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
        //   Secondary: MV descending — among equal deficits, bottom the more expensive card.
        int best_deficit = -1;
        int best_mv      = -1;

        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (!allowed[j]) { continue; }
            auto def     = CardDatabase::Instance().Lookup(hand[j].m_name);
            bool is_land = def ? def->card.IsLand() : hand[j].IsLand();
            if (is_land) { continue; }

            int deficit = def ? one_deficit(def->card.m_mana_cost, pool, land_count) : 0;
            int mv      = def ? def->card.m_mana_cost.ManaValue()
                              : hand[j].m_mana_cost.ManaValue();

            bool prefer = (chosen == -1)
                       || (deficit > best_deficit)
                       || (deficit == best_deficit && mv > best_mv);

            if (prefer) { chosen = j; best_deficit = deficit; best_mv = mv; }
        }
    }

    // Fallback: the branch found no eligible card of its preferred type (e.g. the
    // allowed set is all lands while we're in the spell branch). Bottom the first
    // allowed card — under lookahead these are all win-equal anyway.
    if (chosen == -1)
    {
        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (allowed[j]) { chosen = j; break; }
        }
    }
    return chosen;
}

int AIEngine::RolloutWinTurn(GameState trial, int max_turns)
{
    GameLogger* saved = m_logger;
    m_logger = nullptr;                  // suppress per-decision logging during the rollout
    GameEngine engine(*this);
    int win_turn = engine.PlayOut(trial, max_turns);
    m_logger = saved;
    return win_turn > 0 ? win_turn : max_turns + 1;
}

void AIEngine::BottomCards(GameState& state, int count, int max_turns)
{
    Player& ap = state.ActivePlayer();

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
        }

        int pick = HeuristicBottomPick(ap.hand, allowed);
        if (pick < 0) { pick = 0; }

        ap.library.push_back(ap.hand[pick]);
        ap.hand.erase(ap.hand.begin() + pick);
    }
}

// ============================================================
// TakeTurn
// ============================================================

void AIEngine::TakeTurn(GameState& state, bool is_pre_combat_main)
{
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
        sc.card.m_is_staged = true;
        ap_ref.hand.push_back(sc.card);
    }

    TryPlayLand(state);

    TurnSolver::Plan plan;
    if (m_lookahead_depth > 0)
    {
        // Deterministic work budget for this decision (m_budget_ms is "virtual ms";
        // <= 0 means unlimited). Replaces the old steady_clock deadline so the
        // search does identical work — and reaches an identical result — on every
        // machine and every run for a given seed. See SearchBudget.
        SearchBudget budget = SearchBudget::FromVirtualMs(m_budget_ms);

        // Search at full depth every turn. The per-turn depth decrement that used
        // to live here was a workaround for candidate starvation under the timeout;
        // iterative deepening in SolveWithLookahead now guarantees every candidate
        // is evaluated, so later turns no longer need to be shallower.
        plan = TurnSolver::SolveWithLookahead(state, is_pre_combat_main,
                                              m_lookahead_depth, 20, &budget, true);
    }
    else
    {
        plan = TurnSolver::Solve(state, is_pre_combat_main);
    }

    // Deploy creatures via Aether Vial first (lords boost subsequent spell evals).
    for (const std::string& name : plan.vial_activations)
    {
        std::optional<CardDefinition> copt = CardDatabase::Instance().Lookup(name);
        if (!copt || !copt->card.IsCreature()) { continue; }
        int mv = copt->card.m_mana_cost.ManaValue();
        Player& ap_v = state.ActivePlayer();
        auto hand_it = std::find_if(ap_v.hand.begin(), ap_v.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (hand_it == ap_v.hand.end()) { continue; }
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
    }

    // Execute: regular spells first so their lands are tapped before
    // sacrifice-land spells fire, minimising the cost of the sacrifice.
    auto cast_by_name = [&](const std::string& name)
    {
        Player& ap = state.ActivePlayer();
        auto it = std::find_if(ap.hand.begin(), ap.hand.end(),
            [&name](const Card& c) { return c.m_name == name; });
        if (it == ap.hand.end()) { return; }
        ManaPool available = BuildAvailableMana(state);
        CastSpellFromHand(state, *it, available);
    };

    for (const std::string& name : plan.spells)    { cast_by_name(name); }
    for (const std::string& name : plan.sacrifice) { cast_by_name(name); }

    // Animate lands and activate tap-token abilities with mana remaining after spells.
    // Only in pre-combat main so any resulting creatures can attack this turn.
    if (is_pre_combat_main)
    {
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
}

// ---- Land drop ----

bool AIEngine::TryPlayLand(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    // Prefer multi-color lands (produce wild mana) over colorless-only lands
    // like Mutavault, so that colored spells remain castable this turn.
    // Two-pass: first look for a land producing 2+ colors; fall back to any land.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
        {
            auto def = CardDatabase::Instance().Lookup(it->m_name);
            if (!def || !def->card.IsLand()) { continue; }

            bool is_multi = def->params.produces.size() > 1;
            if (pass == 0 && !is_multi) { continue; }  // first pass: multi-color only

            if (m_logger) { m_logger->LogPlayLand(it->m_number, it->m_name); }

            Permanent perm;
            perm.card              = def->card;
            perm.card.m_number     = it->m_number;
            perm.controller_index  = state.active_player_index;
            perm.owner_index       = state.active_player_index;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);

            ap.hand.erase(it);
            ++ap.lands_played_this_turn;
            return true;
        }
    }
    return false;
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

        // Multi-color lands contribute one wild mana (any single color per tap),
        // not one of each color — matching how TapForCost actually works.
        if (def->params.produces.size() == 1)
        {
            pool.Add(def->params.produces[0]);
        }
        else if (!def->params.produces.empty())
        {
            ++pool.wild;
        }
    }
    return pool;
}

bool AIEngine::TapForCost(GameState& state, const ManaCost& cost, ManaPool& available,
                          bool for_creature)
{
    Player& ap = state.ActivePlayer();

    auto tap_color = [&](Color needed) -> bool
    {
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index || p.tapped) { continue; }
            auto def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }

            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }
            if (def->params.creature_mana_only && !for_creature) { continue; }

            for (Color c : def->params.produces)
            {
                if (c == needed)
                {
                    p.tapped = true;
                    available.Add(c, -1);
                    return true;
                }
            }
        }
        return false;
    };

    // Pay colored requirements first (most restrictive).
    for (int i = 0; i < cost.white;     ++i) { if (!tap_color(Color::White))     { return false; } }
    for (int i = 0; i < cost.blue;      ++i) { if (!tap_color(Color::Blue))      { return false; } }
    for (int i = 0; i < cost.black;     ++i) { if (!tap_color(Color::Black))     { return false; } }
    for (int i = 0; i < cost.red;       ++i) { if (!tap_color(Color::Red))       { return false; } }
    for (int i = 0; i < cost.green;     ++i) { if (!tap_color(Color::Green))     { return false; } }
    for (int i = 0; i < cost.colorless; ++i) { if (!tap_color(Color::Colorless)) { return false; } }

    // Pay generic with any untapped source.
    for (int i = 0; i < cost.generic; ++i)
    {
        bool found = false;
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index || p.tapped) { continue; }
            auto def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }

            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }
            if (def->params.creature_mana_only && !for_creature) { continue; }

            p.tapped = true;
            if (!def->params.produces.empty())
            {
                available.Add(def->params.produces[0], -1);
            }
            found = true;
            break;
        }
        if (!found) { return false; }
    }

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
    // Prefer discarding non-staged cards (staged cards are in exile and should not
    // be subject to the hand-size discard rule).
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
