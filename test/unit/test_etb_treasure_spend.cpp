// Unit cover for MTG_ETB_TREASURE_SPEND (USER 2026-09-26, Pirates deferral D3: "definitely wrong and
// should be fixed"). The §2a fresh-hold banks a Treasure that entered THIS turn -- a doctrine written
// for Gold Rush, a mana-negative Treasure-minting SPELL. Corsair Captain's enter-trigger Treasure (and
// a Kitesail Larcenist conversion) is free, and real Magic lets it be cracked at once:
// T3, three lands -> Corsair Captain, crack the Treasure -> a one-drop.
//
// What is pinned here:
//   1. the PAYMENT half: an ETB Treasure is spendable the turn it enters, a Gold Rush Treasure stays
//      banked without a magnet, a Larcenist-converted fresh permanent is spendable;
//   2. the ENUMERATION half: {Corsair Captain, Goblin Tomb Raider} on exactly three untapped lands is
//      offered, is realised Corsair-FIRST (the funding order both apply worlds share), and resolves
//      both casts with the Treasure spent;
//   3. the lever OFF (heurarm slot == MTG_ETB_TREASURE_SPEND=0) is the old behaviour on each point.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsEts()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefEts(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutEts(GameState& s, const std::string& name, int number, bool sick)
{
    Permanent p;
    p.card              = DefEts(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card HandEts(const std::string& name, int number)
{
    Card c     = DefEts(name).card;
    c.m_number = number;
    return c;
}

GameState BaseState()
{
    EnsureCardsEts();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    for (int k = 0; k < 10; ++k)
    {
        Card c;
        c.m_name = "Mountain";
        c.RehashName();
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* NewestTreasure(const GameState& s)
{
    const Permanent* t = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Treasure Token") { t = &p; } }
    return t;
}

int CountNamedEts(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// Force the lever for this thread (the heurarm slot IS the flag's reader), restored on scope exit.
struct EtbSpendArm
{
    explicit EtbSpendArm(bool on) { heurarm::t_arm[heurarm::ETB_TREASURE_SPEND] = on ? 1 : 0; }
    ~EtbSpendArm() { heurarm::t_arm[heurarm::ETB_TREASURE_SPEND] = -1; }
};

// T3: Mountain, Mountain, Island untapped; hand Corsair Captain {2}{U} + Goblin Tomb Raider {R}.
// Four mana of cost on three lands -- payable ONLY with the Corsair's Treasure.
GameState CorsairTurn()
{
    GameState s = BaseState();
    PutEts(s, "Mountain", 300, false);
    PutEts(s, "Mountain", 301, false);
    PutEts(s, "Island",   302, false);
    s.players[0].hand.push_back(HandEts("Goblin Tomb Raider", 11));   // hand order puts the 1-drop FIRST
    s.players[0].hand.push_back(HandEts("Corsair Captain", 10));
    return s;
}

bool CastsBoth(const TurnSolver::Plan& p)
{
    bool corsair = false, raider = false;
    for (const Action& a : p.actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Corsair Captain")    { corsair = true; }
        if (a.card_name.str() == "Goblin Tomb Raider") { raider  = true; }
    }
    return corsair && raider;
}

const TurnSolver::Plan* FindBoth(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans) { if (CastsBoth(p)) { return &p; } }
    return nullptr;
}

}   // namespace

TEST_CASE("ETB Treasure spend: Corsair's Treasure pays the turn it enters; Gold Rush's stays banked")
{
    GameState s = BaseState();
    {
        EtbSpendArm on(true);
        const int idx = PutEts(s, "Corsair Captain", 60, /*sick=*/true);
        FireOwnEtbTriggers(s, 0, idx);
        const Permanent* t = NewestTreasure(s);
        REQUIRE(t != nullptr);
        CHECK(t->entered_this_turn);
        CHECK(t->fresh_hold_exempt);
        CHECK(PaySacSpendableNow(s, *t, DefEts("Treasure Token")));
        CHECK(AvailableManaPool(s).Total() == 1);   // the pool scan promises what the payer delivers

        // The Gold Rush helper call (CreateTreasureTokens' default) -- a spell mint, no magnet live.
        CreateTreasureTokens(s, 0, 1);
        const Permanent* g = NewestTreasure(s);
        REQUIRE(g != nullptr);
        CHECK_FALSE(g->fresh_hold_exempt);
        CHECK_FALSE(PaySacSpendableNow(s, *g, DefEts("Treasure Token")));
        CHECK(AvailableManaPool(s).Total() == 1);   // still just the Corsair's
    }
}

TEST_CASE("ETB Treasure spend: a Larcenist-converted permanent that entered THIS turn is spendable")
{
    GameState s = BaseState();
    EtbSpendArm on(true);
    const int bi = PutEts(s, "Goblin Tomb Raider", 70, /*sick=*/true);
    TreasurifyPermanent(s, bi);
    CHECK(s.battlefield[bi].card.m_name.str() == "Treasure Token");
    CHECK(s.battlefield[bi].entered_this_turn);
    CHECK(PaySacSpendableNow(s, s.battlefield[bi], DefEts("Treasure Token")));
}

TEST_CASE("ETB Treasure spend OFF: both Treasures are held exactly as before")
{
    GameState s = BaseState();
    EtbSpendArm off(false);
    const int idx = PutEts(s, "Corsair Captain", 60, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, idx);
    const Permanent* t = NewestTreasure(s);
    REQUIRE(t != nullptr);
    CHECK_FALSE(t->fresh_hold_exempt);
    CHECK_FALSE(PaySacSpendableNow(s, *t, DefEts("Treasure Token")));
    const int bi = PutEts(s, "Goblin Tomb Raider", 70, /*sick=*/true);
    TreasurifyPermanent(s, bi);
    CHECK_FALSE(s.battlefield[bi].fresh_hold_exempt);
    CHECK_FALSE(PaySacSpendableNow(s, s.battlefield[bi], DefEts("Treasure Token")));
}

TEST_CASE("ETB Treasure spend: the untap step clears the exemption with entered_this_turn")
{
    GameState s = BaseState();
    EtbSpendArm on(true);
    const int idx = PutEts(s, "Corsair Captain", 60, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, idx);
    for (Permanent& p : s.battlefield) { p.entered_this_turn = false; p.fresh_hold_exempt = false; }
    const Permanent* t = NewestTreasure(s);
    REQUIRE(t != nullptr);
    CHECK(PaySacSpendableNow(s, *t, DefEts("Treasure Token")));   // an old Treasure pays regardless
}

TEST_CASE("ETB Treasure spend: {Corsair Captain, 1-drop} on exactly three lands is offered and resolves Corsair-first")
{
    EtbSpendArm on(true);
    const GameState s = CorsairTurn();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both = FindBoth(plans);
    REQUIRE_MESSAGE(both != nullptr, "the Corsair + Tomb Raider line on 3 lands was not enumerated");

    // The Corsair cast carries the Treasure credit; the one-drop does not.
    for (const Action& a : both->actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        CHECK(a.rock_mana.Total() == (a.card_name.str() == "Corsair Captain" ? 1 : 0));
    }

    // The funding order both apply worlds run after their rank sort: whatever order the plan holds,
    // the Corsair ends up FIRST because the 1-drop-first order cannot pay.
    std::vector<int> order;
    int corsair_i = -1, raider_i = -1;
    for (int i = 0; i < static_cast<int>(both->actions.size()); ++i)
    {
        const Action& a = both->actions[i];
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Corsair Captain")    { corsair_i = i; }
        if (a.card_name.str() == "Goblin Tomb Raider") { raider_i = i; }
    }
    REQUIRE(corsair_i >= 0);
    REQUIRE(raider_i >= 0);
    order = { raider_i, corsair_i };
    ApplyEtbTreasureFundingOrder(s, both->actions, order);
    CHECK(order == std::vector<int>{ corsair_i, raider_i });
    // ...and an order that already pays is left alone.
    order = { corsair_i, raider_i };
    ApplyEtbTreasureFundingOrder(s, both->actions, order);
    CHECK(order == std::vector<int>{ corsair_i, raider_i });

    // The rollout apply realises the line: both bodies down, the Treasure cracked for the {R}.
    GameState after = s;
    TurnSolver::ApplyPlan(after, *both, /*is_pre_combat=*/true);
    CHECK(CountNamedEts(after, "Corsair Captain") == 1);
    CHECK(CountNamedEts(after, "Goblin Tomb Raider") == 1);
    CHECK(CountNamedEts(after, "Treasure Token") == 0);
    CHECK(after.players[0].hand.empty());
}

TEST_CASE("ETB Treasure spend OFF: the three-land {Corsair Captain, 1-drop} line is not offered")
{
    EtbSpendArm off(false);
    const GameState s = CorsairTurn();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(FindBoth(plans) == nullptr);
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { CHECK(a.rock_mana.Total() == 0); } }
}
