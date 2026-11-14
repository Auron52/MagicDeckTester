#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace
{
const CardDefinition& Def(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

int Put(GameState& s, const char* name, int id)
{
    Permanent p;
    p.card = Def(name).card;
    p.card.m_number = id;
    p.controller_index = p.owner_index = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int Tokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, "Merfolk")) { ++n; } }
    return n;
}
}

TEST_CASE("Deeproot Pilgrimage: simultaneous attackers cause one trigger per enchantment")
{
    GameState s;
    s.active_player_index = 0;
    Put(s, "Deeproot Pilgrimage", 1);
    Put(s, "Deeproot Pilgrimage", 2);
    const int a = Put(s, "Merfolk Mistbinder", 3);
    const int b = Put(s, "Merfolk Mistbinder", 4);
    CreateToken(s, 0, 1, 1, {"Merfolk"}, "U", {});
    const int token = static_cast<int>(s.battlefield.size()) - 1;
    ResolveCombatDamage(s, {a, b, token}, 0, false);
    CHECK(Tokens(s) == 3); // original token plus one from each Pilgrimage
    for (std::size_t i = static_cast<std::size_t>(token + 1); i < s.battlefield.size(); ++i)
    {
        const Permanent& p = s.battlefield[i];
        CHECK(p.is_token);
        CHECK(p.card.HasKeyword(Keyword::Hexproof));
        CHECK(p.card.HasColor(Color::Blue));
        CHECK(p.entered_this_turn);
    }
}

TEST_CASE("Deeproot Pilgrimage: token and vigilant Merfolk taps do not trigger")
{
    GameState s;
    s.active_player_index = 0;
    Put(s, "Deeproot Pilgrimage", 1);
    int v = Put(s, "Merfolk Mistbinder", 2);
    s.battlefield[v].card.AddKeyword(Keyword::Vigilance);
    CreateToken(s, 0, 1, 1, {"Merfolk"}, "U", {});
    ResolveCombatDamage(s, {v, static_cast<int>(s.battlefield.size()) - 1}, 0, false);
    CHECK(Tokens(s) == 1);
}

TEST_CASE("Deeproot Pilgrimage: each mana activation creates a separate token")
{
    GameState s;
    s.active_player_index = 0;
    Put(s, "Deeproot Pilgrimage", 1);
    // An animated Island that is a Merfolk is a legal mana source and a
    // nontoken Merfolk; two {T} mana activations are two distinct events.
    for (int id : {2, 3})
    {
        const int i = Put(s, "Island", id);
        s.battlefield[i].card.AddType(CardType::Creature);
        s.battlefield[i].card.m_subtypes.push_back("Merfolk");
    }
    ManaCost bill;
    bill.blue = 2;
    REQUIRE(TapForCostShared(s, bill, false, nullptr, true));
    CHECK(Tokens(s) == 2);

    GameState manual;
    manual.active_player_index = 0;
    Put(manual, "Deeproot Pilgrimage", 11);
    const int i = Put(manual, "Island", 12);
    manual.battlefield[i].card.AddType(CardType::Creature);
    manual.battlefield[i].card.m_subtypes.push_back("Merfolk");
    const TurnSolver::PreTap tap{"Island", 12, static_cast<int>(Color::Blue)};
    REQUIRE(ApplyHumanPreTap(manual, tap).empty());
    CHECK(Tokens(manual) == 1);
}
