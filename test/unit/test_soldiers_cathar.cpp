// Brutal Cathar // Moonrage Brute (Soldiers, 2026-10-04). The goldfish opponent DOES hold creatures
// (PopulateOpponentSpawns, and USER 2026-10-04: "There are actually critters for Brutal Cathar
// sometimes. They just don't do anything otherwise."), so the "exile target creature an opponent
// controls until this creature leaves the battlefield" trigger is live and its linked return is a
// real rules path:
//  1. the ETB exiles the opponent's creature and links it to the Cathar;
//  2. the linked card returns the moment the Cathar LEAVES (CR 610.3) -- not at the next day/night
//     turn boundary, which is only the backstop sweep;
//  3. transforming (day -> night) is not a zone change: the link survives, and a Moonrage Brute that
//     dies still returns the card AND lands in the graveyard as a Brutal Cathar (CR 711.8 / 712.8a).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

void EnsureCardsCathar()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

int g_nc = 7000;
int PutC(GameState& s, const std::string& name, int controller)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card             = d->card;
    p.card.m_number    = g_nc++;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

GameState BoardC()
{
    EnsureCardsCathar();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.day_night           = 1;
    return s;
}

const Permanent* FindC(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

// The death sites' shape: erase, put the card in its owner's graveyard, then OnCreatureDies.
void KillC(GameState& s, int num)
{
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    {
        if (s.battlefield[i].card.m_number != num) { continue; }
        const Permanent dead = s.battlefield[i];
        s.battlefield.erase(s.battlefield.begin() + static_cast<long>(i));
        s.players[dead.controller_index].graveyard.push_back(dead.card);
        OnCreatureDies(s, dead.controller_index, dead.card, false, 0);
        return;
    }
    FAIL("no such permanent");
}

}   // namespace

TEST_CASE("Brutal Cathar: ETB exiles the opponent's creature; it returns when the Cathar leaves")
{
    GameState s   = BoardC();
    const int cat = PutC(s, "Brutal Cathar", 0);
    const int opp = PutC(s, "Skyshroud Cutter", 1);
    ExileOppCreatureUntilLeaves(s, 0, cat, "Brutal Cathar");
    CHECK(FindC(s, opp) == nullptr);
    REQUIRE(s.exile.size() == 1);
    CHECK(s.exile[0].m_number == opp);
    CHECK(FindC(s, cat)->linked_exile_number == opp);

    KillC(s, cat);
    // Returned at the leave, under its owner -- no turn boundary has run.
    const Permanent* back = FindC(s, opp);
    REQUIRE(back != nullptr);
    CHECK(back->controller_index == 1);
    CHECK(s.exile.empty());
}

TEST_CASE("Moonrage Brute keeps the link across the transform; dying returns it and leaves a Brutal Cathar card")
{
    GameState s   = BoardC();
    const int cat = PutC(s, "Brutal Cathar", 0);
    const int opp = PutC(s, "Skyshroud Cutter", 1);
    ExileOppCreatureUntilLeaves(s, 0, cat, "Brutal Cathar");
    SetDayNight(s, 2);   // night: in-place swap to the back face
    REQUIRE(FindC(s, cat) != nullptr);
    CHECK(FindC(s, cat)->card.m_name.str() == "Moonrage Brute");
    CHECK(FindC(s, cat)->linked_exile_number == opp);
    CHECK(FindC(s, opp) == nullptr);   // a transform is not a leave

    KillC(s, cat);
    REQUIRE(FindC(s, opp) != nullptr);
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard[0].m_name.str() == "Brutal Cathar");
    CHECK(s.players[0].graveyard[0].m_number == cat);
}

TEST_CASE("Brutal Cathar with no opponent creature: the trigger is removed, nothing is linked")
{
    GameState s   = BoardC();
    const int cat = PutC(s, "Brutal Cathar", 0);
    ExileOppCreatureUntilLeaves(s, 0, cat, "Brutal Cathar");
    CHECK(s.exile.empty());
    CHECK(FindC(s, cat)->linked_exile_number == 0);
}
