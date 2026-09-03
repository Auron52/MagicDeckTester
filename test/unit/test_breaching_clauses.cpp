// Unit tests for the two BreachingDragonstorm clauses implemented 2026-09-03 on user sign-off
// (both were PROVISIONAL deferrals during the deck's onboarding):
//
//  1. Call Forth the Tempest's damage clause ("deals damage to each creature your opponents
//     control equal to the total mana value of other spells you've cast this turn") -- the
//     shared PerformMvCastDamageOppCreatures sweep. Easy to get silently wrong in two ways no
//     aggregate fingerprint would catch: forgetting to subtract the card's OWN mana value
//     ("other spells"), and killing an indestructible / high-toughness survivor.
//
//  2. Breaching Dragonstorm's self-bounce ("When a Dragon you control enters, return this
//     enchantment to its owner's hand") -- the FireEtbWatchers record + DrainPendingSelfBounces
//     drain. The trigger can NEVER fire in the current 60 (zero Dragons), so games exercise
//     none of it; these tests are its only coverage until a Dragon-carrying list exists.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Battlefield permanent with a distinct per-copy id (the bounce drain matches on m_number, which
// in a real game is unique per physical card).
void Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
}

// Route an enter through the SAME universal cascade both worlds use, then drain the bounce
// queue the way every enter site does.
void Enter(GameState& s, const std::string& name, int controller, int number)
{
    Put(s, name, controller, number);
    FireEtbWatchers(s, controller, static_cast<int>(s.battlefield.size()) - 1);
    DrainPendingSelfBounces(s);
}

}   // namespace

TEST_CASE("CFT damage clause: X = total MV cast minus its own, sweep kills only lethal")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;

    Put(s, "Hornet Queen", 1, 101);       // 2/2 -- dies to 2
    Put(s, "Annoyed Altisaur", 1, 102);   // 6/5 -- survives 2 with damage marked
    Put(s, "Boarding Party", 0, 103);     // OURS -- must be untouched

    // CFT (mv 8) resolving with 10 total MV cast this turn -> "other spells" = 2 damage each.
    s.mv_cast_this_turn = 10;
    PerformMvCastDamageOppCreatures(s, 0, Def("Call Forth the Tempest"));

    REQUIRE(s.battlefield.size() == 2);                       // the Queen died
    CHECK(s.players[1].graveyard.size() == 1);
    CHECK(s.players[1].graveyard[0].m_name.str() == "Hornet Queen");
    for (const Permanent& q : s.battlefield)
    {
        if (q.card.m_number == 102) { CHECK(q.damage == 2); }   // survivor keeps marked damage
        if (q.card.m_number == 103) { CHECK(q.damage == 0); }   // our creature untouched
    }
}

TEST_CASE("CFT damage clause: no other spells cast -> no damage at all")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    Put(s, "Hornet Queen", 1, 101);

    s.mv_cast_this_turn = 8;   // ONLY the CFT itself was cast -> X = 0
    PerformMvCastDamageOppCreatures(s, 0, Def("Call Forth the Tempest"));

    CHECK(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].damage == 0);
    CHECK(s.players[1].graveyard.empty());
}

TEST_CASE("BD self-bounce: a Dragon entering under our control returns BD to hand")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Breaching Dragonstorm", 0, 42);
    Enter(s, "Shivan Dragon", 0, 7);

    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand[0].m_name.str() == "Breaching Dragonstorm");
    CHECK(s.players[0].hand[0].m_number == 42);
    // The Dragon stays; BD is gone from the battlefield.
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_name.str() == "Shivan Dragon");
}

TEST_CASE("BD self-bounce: an OPPONENT's Dragon, or a non-Dragon of ours, does not fire it")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Breaching Dragonstorm", 0, 42);
    Enter(s, "Shivan Dragon", 1, 8);        // theirs -- "a Dragon YOU control" it is not
    Enter(s, "Annoyed Altisaur", 0, 9);     // ours, but a Dinosaur

    CHECK(s.players[0].hand.empty());
    CHECK(s.battlefield.size() == 3);       // BD still out
}

TEST_CASE("BD self-bounce: two BDs out -> a single Dragon bounces both")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;

    Put(s, "Breaching Dragonstorm", 0, 42);
    Put(s, "Breaching Dragonstorm", 0, 43);
    Enter(s, "Shivan Dragon", 0, 7);

    CHECK(s.players[0].hand.size() == 2);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_name.str() == "Shivan Dragon");
}
