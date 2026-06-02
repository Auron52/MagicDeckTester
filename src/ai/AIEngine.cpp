#include "AIEngine.h"
#include "../cards/CardDatabase.h"
#include <algorithm>
#include <stdexcept>

AIEngine::AIEngine(MulliganProfile profile) : m_profile(std::move(profile)) {}

// ============================================================
// Mulligan
// ============================================================

void AIEngine::HandleMulligan(GameState& state)
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

    if (mulligan_count > 0) { BottomCards(state, mulligan_count); }
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

    if (!m_profile.skip_curve_check)
    {
        bool has_two_drop = false;
        for (const Card& c : hand)
        {
            auto def = CardDatabase::Instance().Lookup(c.m_name);
            int  mv      = def ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
            bool is_land = def ? def->card.IsLand() : c.IsLand();
            if (!is_land && mv <= 2 && land_count >= 2) { has_two_drop = true; }
        }
        if (!has_two_drop && mulligan_count < 2) { return false; }
    }

    return true;
}

void AIEngine::BottomCards(GameState& state, int count)
{
    Player& ap = state.ActivePlayer();
    for (int i = 0; i < count && !ap.hand.empty(); ++i)
    {
        std::vector<Card>::iterator worst = std::max_element(ap.hand.begin(), ap.hand.end(),
            [](const Card& a, const Card& b)
            {
                auto da = CardDatabase::Instance().Lookup(a.m_name);
                auto db = CardDatabase::Instance().Lookup(b.m_name);
                int mv_a = da ? da->card.m_mana_cost.ManaValue() : a.m_mana_cost.ManaValue();
                int mv_b = db ? db->card.m_mana_cost.ManaValue() : b.m_mana_cost.ManaValue();
                return mv_a < mv_b;
            });
        ap.library.push_back(*worst);
        ap.hand.erase(worst);
    }
}

// ============================================================
// TakeTurn
// ============================================================

void AIEngine::TakeTurn(GameState& state, bool is_pre_combat_main)
{
    TryPlayLand(state);

    // Cast spells until nothing remains castable.
    // Rebuilds available mana each iteration so ETB untap effects are reflected.
    bool cast_something = true;
    while (cast_something)
    {
        cast_something = false;
        ManaPool available = BuildAvailableMana(state);
        Card* to_cast = PickBestCastable(state, available, is_pre_combat_main);
        if (to_cast != nullptr)
        {
            CastSpellFromHand(state, *to_cast, available);
            cast_something = true;
        }
    }
}

// ---- Land drop ----

bool AIEngine::TryPlayLand(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.lands_played_this_turn >= ap.LandDropsAvailable()) { return false; }

    for (auto it = ap.hand.begin(); it != ap.hand.end(); ++it)
    {
        auto def = CardDatabase::Instance().Lookup(it->m_name);
        if (!def || !def->card.IsLand()) { continue; }

        Permanent perm;
        perm.card              = def->card;
        perm.controller        = &ap;
        perm.owner             = &ap;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);

        ap.hand.erase(it);
        ++ap.lands_played_this_turn;
        return true;
    }
    return false;
}

// ---- Mana ----

ManaPool AIEngine::BuildAvailableMana(const GameState& state) const
{
    ManaPool pool;
    const Player& ap = state.ActivePlayer();

    for (const Permanent& p : state.battlefield)
    {
        if (p.controller != &ap || p.tapped) { continue; }

        auto def = CardDatabase::Instance().Lookup(p.card.m_name);
        if (!def) { continue; }

        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && p.CanTap());
        if (!is_land && !is_dork) { continue; }

        for (Color c : def->params.produces)
        {
            pool.Add(c);
        }
    }
    return pool;
}

bool AIEngine::TapForCost(GameState& state, const ManaCost& cost, ManaPool& available)
{
    Player& ap = state.ActivePlayer();

    auto tap_color = [&](Color needed) -> bool
    {
        for (Permanent& p : state.battlefield)
        {
            if (p.controller != &ap || p.tapped) { continue; }
            auto def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }

            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }

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
            if (p.controller != &ap || p.tapped) { continue; }
            auto def = CardDatabase::Instance().Lookup(p.card.m_name);
            if (!def) { continue; }

            bool is_source = (def->tmpl == CardTemplate::BasicLand)
                          || (def->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_source) { continue; }

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

bool AIEngine::WinsThisTurn(const GameState& state, int extra_damage) const
{
    int pending = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller == &state.ActivePlayer() && p.CanAttack())
        {
            pending += p.EffectivePower();
        }
    }
    return (pending + extra_damage) >= state.Opponent().life;
}

Card* AIEngine::PickBestCastable(GameState& state, const ManaPool& available,
                                  bool is_pre_combat_main) const
{
    (void)is_pre_combat_main;
    Player& ap = state.ActivePlayer();
    Card* best     = nullptr;
    int best_score = -1;

    for (Card& card : ap.hand)
    {
        auto def = CardDatabase::Instance().Lookup(card.m_name);
        if (!def)                            { continue; }
        if (def->card.IsLand())              { continue; }
        if (!available.CanPay(def->card.m_mana_cost)) { continue; }

        bool timing_ok = def->card.IsInstant()
                      || def->card.HasKeyword(Keyword::Flash)
                      || state.stack.empty();
        if (!timing_ok) { continue; }

        // Win-now line: always take it immediately.
        if (def->tmpl == CardTemplate::DirectDamage)
        {
            if (WinsThisTurn(state, def->params.damage)) { return &card; }
        }

        // Score: prefer high-power creatures, then direct damage, then others.
        int score = 0;
        if (def->card.IsCreature())
        {
            score = 100 + def->card.m_power.value_or(0) * 10
                        - def->card.m_mana_cost.ManaValue();
        }
        else
        {
            score = (def->tmpl == CardTemplate::DirectDamage ? 50 : 10)
                  - def->card.m_mana_cost.ManaValue();
        }

        if (score > best_score)
        {
            best       = &card;
            best_score = score;
        }
    }

    return best;
}

void AIEngine::CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available)
{
    Player& ap = state.ActivePlayer();
    auto def = CardDatabase::Instance().Lookup(hand_card.m_name);
    if (!def) { return; }

    StackEntry entry;
    entry.type             = StackEntry::EntryType::Spell;
    entry.source           = def->card;
    entry.controller_index = state.active_player_index;

    int opp_index = 1 - state.active_player_index;
    if (def->tmpl == CardTemplate::DirectDamage)
    {
        Target t;
        t.type         = Target::Type::Player;
        t.player_index = opp_index;
        entry.targets.push_back(t);
    }

    if (!TapForCost(state, def->card.m_mana_cost, available)) { return; }

    ap.hand.erase(std::find_if(ap.hand.begin(), ap.hand.end(),
        [&hand_card](const Card& c) { return &c == &hand_card; }));

    state.stack.push_back(std::move(entry));
}

// ============================================================
// Combat / Discard
// ============================================================

std::vector<Permanent*> AIEngine::DeclareAttackers(GameState& state)
{
    std::vector<Permanent*> attackers;
    Player& ap = state.ActivePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller == &ap && p.CanAttack())
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
    return &(*std::max_element(ap.hand.begin(), ap.hand.end(),
        [](const Card& a, const Card& b)
        {
            auto da = CardDatabase::Instance().Lookup(a.m_name);
            auto db = CardDatabase::Instance().Lookup(b.m_name);
            int mv_a = da ? da->card.m_mana_cost.ManaValue() : a.m_mana_cost.ManaValue();
            int mv_b = db ? db->card.m_mana_cost.ManaValue() : b.m_mana_cost.ManaValue();
            return mv_a < mv_b;
        }));
}
