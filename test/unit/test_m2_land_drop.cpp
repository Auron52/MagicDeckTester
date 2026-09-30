// The post-combat land drop offers only lands that ARRIVED after main 1 began (USER 2026-09-30:
// "Only newly arrived lands in second main would be okay"). A land already in hand at main 1 was
// enumerated there; its main-2 copy only re-derives a main-1 line. Two arms on the SAME board that
// MUST differ: with the turn's main-1 hand stamped, the held Forest is barred and the newly arrived
// Mountain offered; with no stamp this turn, every card reads as new and the Forest is offered too.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{
void EnsureCardsLoadedM2L()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card HandCard(const char* name, int num)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card; c.m_number = num;
    return c;
}

bool Main2OffersLand(const GameState& s, const std::string& land)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, false))
    {
        if (p.land_to_play == land) { return true; }
    }
    return false;
}

GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.players[0].hand.push_back(HandCard("Forest", 11));
    return s;
}
}   // namespace

TEST_CASE("main-2 land drop: a land held since main 1 is barred, a newly arrived land is offered")
{
    EnsureCardsLoadedM2L();
    GameState s = Board();
    s.StampMain1Hand();                                     // main 1 began holding the Forest
    s.players[0].hand.push_back(HandCard("Mountain", 12));  // arrived after (drawn / revealed)
    CHECK(s.HeldSinceMain1(s.players[0].hand[0]));
    CHECK_FALSE(s.HeldSinceMain1(s.players[0].hand[1]));
    CHECK_FALSE(Main2OffersLand(s, "Forest"));
    CHECK(Main2OffersLand(s, "Mountain"));
}

TEST_CASE("main-2 land drop: without this turn's main-1 stamp every land reads as new")
{
    EnsureCardsLoadedM2L();
    GameState s = Board();
    s.StampMain1Hand();
    s.turn_number += 1;                                     // stale stamp from an earlier turn
    CHECK_FALSE(s.HeldSinceMain1(s.players[0].hand[0]));
    CHECK(Main2OffersLand(s, "Forest"));
}
