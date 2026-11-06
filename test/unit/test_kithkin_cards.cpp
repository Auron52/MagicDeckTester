#include <doctest/doctest.h>
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace {
const CardDefinition& KithkinDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
}

TEST_CASE("Borderguard enters with a counter per other Kithkin")
{
    GameState s;
    Permanent ally;
    ally.card = KithkinDef("Wizened Cenn").card;
    ally.card.m_number = 101;
    ally.controller_index = ally.owner_index = 0;
    s.battlefield.push_back(ally);
    Permanent kindred;
    kindred.card = KithkinDef("Militia's Pride").card;
    kindred.card.m_number = 104;
    kindred.controller_index = kindred.owner_index = 0;
    s.battlefield.push_back(kindred);
    Permanent other_player = ally;
    other_player.card.m_number = 102;
    other_player.controller_index = other_player.owner_index = 1;
    s.battlefield.push_back(other_player);
    Permanent borderguard;
    borderguard.card = KithkinDef("Kinsbaile Borderguard").card;
    borderguard.card.m_number = 103;
    borderguard.controller_index = borderguard.owner_index = 0;
    std::vector<int> done;
    ApplyEntersWithOnPut(borderguard, KithkinDef("Kinsbaile Borderguard"), 0,
                         done, s, s.battlefield.size());
    CHECK(PlusCountersOn(borderguard) == 2); // Kindred enchantments are Kithkin permanents
}

TEST_CASE("Borderguard dies into one token per counter of any kind")
{
    GameState s;
    Permanent dead;
    dead.card = KithkinDef("Kinsbaile Borderguard").card;
    dead.counters.Add(Counter::Type::PlusOnePlusOne, 2);
    dead.ice_counters = 1;
    OnCreatureDies(s, 0, dead.card, false, 0, nullptr, TotalCountersOn(dead));
    CHECK(s.battlefield.size() == 3);
    for (const Permanent& token : s.battlefield)
    {
        CHECK(token.is_token);
        CHECK(CardHasSubtype(token.card, "Kithkin"));
        CHECK(CardHasSubtype(token.card, "Soldier"));
    }
}

TEST_CASE("Goldmeadow Stalwart can reveal a Kindred Kithkin enchantment")
{
    GameState s;
    s.active_player_index = 0;
    s.players[0].hand.push_back(KithkinDef("Goldmeadow Stalwart").card);
    s.players[0].hand.push_back(KithkinDef("Militia's Pride").card);
    CHECK(CanRevealForAdditionalCost(s, KithkinDef("Goldmeadow Stalwart")));
}

TEST_CASE("Kithkin provider chooses Kithkin for Mimic and Automaton even with another dominant tribe")
{
    KithkinProvider provider;
    GameState s;
    s.m_provider = &provider;
    for (int i = 0; i < 5; ++i)
    { s.players[0].library.push_back(KithkinDef("Corsair Captain").card); }
    CHECK(DominantCreatureSubtypeId(s, 0) != SubtypeRegistry::Instance().Id("Kithkin"));
    for (const char* name : {"Metallic Mimic", "Adaptive Automaton"})
    {
        Permanent p;
        p.card = KithkinDef(name).card;
        p.controller_index = p.owner_index = 0;
        s.battlefield.push_back(p);
        FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
        CHECK(s.battlefield.back().chosen_subtype_id == SubtypeRegistry::Instance().Id("Kithkin"));
        CHECK(CardHasSubtype(s.battlefield.back().card, "Kithkin"));
    }
}

TEST_CASE("animated Mutavault has its full toughness under a Kithkin lord")
{
    GameState s;
    Permanent land;
    land.card = KithkinDef("Mutavault").card;
    land.card.m_number = 201;
    land.controller_index = land.owner_index = 0;
    land.is_animated = true;
    s.battlefield.push_back(land);
    Permanent cenn;
    cenn.card = KithkinDef("Wizened Cenn").card;
    cenn.card.m_number = 202;
    cenn.controller_index = cenn.owner_index = 0;
    s.battlefield.push_back(cenn);
    CHECK(CombatPowerOf(s.battlefield[0], s) == 3);
    CHECK(LethalToughness(s.battlefield[0], s) == 3);
}

TEST_CASE("Militia's Pride tokens join Heir's attack count but do not retrigger Pride")
{
    GameState s;
    s.active_player_index = 0;
    auto add = [&](const char* name, int number) {
        Permanent p;
        p.card = KithkinDef(name).card;
        p.card.m_number = number;
        p.controller_index = p.owner_index = 0;
        s.battlefield.push_back(p);
    };
    add("Plains", 101);
    add("Plains", 102);
    add("Cenn's Heir", 103);
    add("Goldmeadow Stalwart", 104);
    add("Militia's Pride", 105);
    s.scripted_militia_pride_pay = 2;
    std::vector<int> attacking{2, 3};
    FireMilitiaPrideTokens(s, 0, attacking);
    CHECK(attacking.size() == 4); // exactly 2 triggers: the new tokens were never declared
    CHECK(s.battlefield.size() == 7);
    for (int i = 5; i < 7; ++i)
    {
        CHECK(s.battlefield[i].is_token);
        CHECK(s.battlefield[i].tapped);
        CHECK(s.battlefield[i].card.HasColor(Color::White));
        CHECK(CardHasSubtype(s.battlefield[i].card, "Kithkin"));
        CHECK(CardHasSubtype(s.battlefield[i].card, "Soldier"));
    }
    ApplyAttackSelfPumps(s, 0, attacking);
    CHECK(s.battlefield[2].temp_power_bonus == 3); // Stalwart + both Pride tokens
    CHECK(s.battlefield[2].temp_tough_bonus == 3);
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
}
