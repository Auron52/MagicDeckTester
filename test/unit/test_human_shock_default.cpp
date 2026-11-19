// HUMAN PLAY's SHOCK-LAND DEFAULT (USER 2026-11-19, Spirits seed 9).
//
// A. TurnSolver::HumanLandManaMayBeUsed -- the viewer's "let AI decide" land_entry default pays a shock
//    land's life unless NOTHING this turn could use the land's mana: "If there is nothing to play with it,
//    tapped is fine. Otherwise it should be untapped in goldfish." Pure (no HumanPlayActive read), so it is
//    driven directly; the end-to-end apply is test/scenarios/spirits_human_shock_*.json (MTG_HUMAN_PLAY).
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

void EnsureCardsHsd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHsd(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshHsd(int turn)
{
    EnsureCardsHsd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 0;
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefHsd("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int PutHsd(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefHsd(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandHsd(GameState& s, const std::string& name, int number)
{
    Card c = DefHsd(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

void GraveHsd(GameState& s, const std::string& name, int number)
{
    Card c = DefHsd(name).card;
    c.m_number = number;
    s.players[0].graveyard.push_back(c);
}

TurnSolver::Plan LandPlan(const std::string& land)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = land;
    return p;
}

}  // namespace

TEST_CASE("human shock default: seed 9 T2 -- nothing payable, the boast's {W} cannot come off Unclaimed Territory")
{
    // Board Unclaimed Territory + Usher of the Fallen; hand Watery Grave and three-drops. Two lands make no
    // three-drop, and Usher's boast {1}{W} (later, after combat) needs a {W} that neither the Grave nor the
    // Territory can give a creature's ABILITY (the Territory's colours pay creature SPELLS only).
    GameState s = FreshHsd(2);
    PutHsd(s, "Unclaimed Territory", 54);
    PutHsd(s, "Usher of the Fallen", 55);
    HandHsd(s, "Drogskol Captain", 7);
    HandHsd(s, "Lingering Souls", 23);   // its flashback {1}{B} is a GRAVEYARD cost: no use from the hand
    HandHsd(s, "Patrician Geist", 33);
    HandHsd(s, "Patrician Geist", 35);
    HandHsd(s, "Watery Grave", 59);
    CHECK_FALSE(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));
    // ...so the human-play rule enters it tapped when the segment spends nothing, and still pays when the
    // segment does, or when the human taps the land by hand.
    CHECK_FALSE(TurnSolver::HumanLandShockPay(s, LandPlan("Watery Grave"), false, false));
    CHECK(TurnSolver::HumanLandShockPay(s, LandPlan("Watery Grave"), true, false));
    CHECK(TurnSolver::HumanLandShockPay(s, LandPlan("Watery Grave"), false, true));
}

TEST_CASE("human shock default: nothing castable at all enters tapped")
{
    GameState s = FreshHsd(2);
    PutHsd(s, "Plains", 36);
    HandHsd(s, "Drogskol Captain", 7);   // {1}{W}{U}: three mana off two lands
    HandHsd(s, "Watery Grave", 59);
    CHECK_FALSE(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));
}

TEST_CASE("human shock default: a boast that is legal only LATER this turn is a use")
{
    // Plains + the Grave pay Usher's boast {1}{W} after it attacks: pay the life now.
    GameState s = FreshHsd(2);
    PutHsd(s, "Plains", 36);
    PutHsd(s, "Usher of the Fallen", 55);
    HandHsd(s, "Drogskol Captain", 7);
    HandHsd(s, "Watery Grave", 59);
    CHECK(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));

    // Secluded Courtyard's colours DO pay a creature's ability (colored_creature_ability_ok) ...
    GameState c = FreshHsd(2);
    PutHsd(c, "Secluded Courtyard", 40);
    PutHsd(c, "Usher of the Fallen", 55);
    HandHsd(c, "Watery Grave", 59);
    CHECK(TurnSolver::HumanLandManaMayBeUsed(c, LandPlan("Watery Grave")));
}

TEST_CASE("human shock default: a castable spell or a graveyard flashback is a use")
{
    GameState s = FreshHsd(2);
    PutHsd(s, "Plains", 36);
    HandHsd(s, "Mausoleum Wanderer", 30);   // {U}
    HandHsd(s, "Watery Grave", 59);
    CHECK(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));

    GameState g = FreshHsd(3);
    PutHsd(g, "Plains", 36);
    GraveHsd(g, "Lingering Souls", 23);     // flashback {1}{B}: Plains + the Grave's {B}
    HandHsd(g, "Watery Grave", 59);
    CHECK(TurnSolver::HumanLandManaMayBeUsed(g, LandPlan("Watery Grave")));
}

TEST_CASE("human shock default: never pay when the life does not cover the payment")
{
    GameState s = FreshHsd(2);
    PutHsd(s, "Plains", 36);
    PutHsd(s, "Usher of the Fallen", 55);
    HandHsd(s, "Watery Grave", 59);
    s.players[0].life = 2;
    CHECK_FALSE(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));
    s.players[0].life = 3;
    CHECK(TurnSolver::HumanLandManaMayBeUsed(s, LandPlan("Watery Grave")));
}

TEST_CASE("param_mana_costs is read mechanically off the raw JSON")
{
    EnsureCardsHsd();
    bool boast = false, flashback = false;
    for (const auto& [key, c] : DefHsd("Usher of the Fallen").param_mana_costs)
    { if (key == "pay_token_cost" && c.generic == 1 && c.white == 1) { boast = true; } }
    for (const auto& [key, c] : DefHsd("Lingering Souls").param_mana_costs)
    { if (key == "flashback_cost" && c.generic == 1 && c.black == 1) { flashback = true; } }
    CHECK(boast);
    CHECK(flashback);
    CHECK(DefHsd("Watery Grave").param_mana_costs.empty());
}
