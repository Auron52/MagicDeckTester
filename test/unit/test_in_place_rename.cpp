// In-place renames of a battlefield permanent must drop the Card::m_def memo (2026-09-26).
//
// Card::m_def caches the CardDatabase entry resolved from m_name. A permanent that is rewritten in
// place into something else (Oko's +1 Elk) used to get a new m_name while keeping the memo, so every
// LookupCached reader kept seeing the ORIGINAL card: an Elked Birds of Paradise still tapped for mana.
// Card::Rename keeps name, hash and memo together; these tests pin it through the real Oko ability.
#include <doctest/doctest.h>

#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

Permanent& Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

int PoolTotal(const ManaPool& m)
{
    return m.white + m.blue + m.black + m.red + m.green + m.colorless + m.wild;
}

}   // namespace

TEST_CASE("Card::Rename resets the definition memo so LookupCached re-resolves the new name")
{
    EnsureCardsLoaded();
    Card c = Def("Birds of Paradise").card;
    REQUIRE(CardDatabase::Instance().LookupCached(c) == &Def("Birds of Paradise"));
    c.Rename("Treasure Token");
    CHECK(c.m_name.str() == "Treasure Token");
    CHECK(c.m_name_hash == std::hash<std::string>{}("Treasure Token"));
    CHECK(CardDatabase::Instance().LookupCached(c) == &Def("Treasure Token"));
    c.Rename("Elk");                                     // no DB entry -> a cached miss
    CHECK(CardDatabase::Instance().LookupCached(c) == nullptr);
}

TEST_CASE("Oko +1: an Elked Birds of Paradise loses its mana ability (no stale definition)")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    Permanent& oko = Put(s, "Oko, Thief of Crowns", 70);
    oko.loyalty = 4;
    Put(s, "Birds of Paradise", 71);
    // Prime the memo the way any board scan would before the ability resolves.
    REQUIRE(CardDatabase::Instance().LookupCached(s.battlefield[1].card) == &Def("Birds of Paradise"));
    CHECK(PoolTotal(AvailableManaPool(s)) == 1);

    ApplyLoyaltyAbility(s, 0, 70, /*ability_index=*/1, /*elk_target=*/71);   // +1: elk_transform

    const Permanent& elk = s.battlefield[1];
    REQUIRE(elk.card.m_number == 71);
    CHECK(elk.card.m_name.str() == "Elk");
    CHECK(elk.card.m_power == 3);
    CHECK(CardDatabase::Instance().LookupCached(elk.card) == nullptr);
    CHECK(PoolTotal(AvailableManaPool(s)) == 0);         // "loses all abilities": no {T}: Add
}
