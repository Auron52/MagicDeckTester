#include <doctest/doctest.h>
#include "ai/ManaOracle.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/ManaPool.h"
#include "core/SpellEffects.h"

namespace {
const CardDefinition& FigureDef()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup("Figure of Destiny");
    REQUIRE(d != nullptr);
    return *d;
}

GameState FigureState()
{
    GameState s;
    s.active_player_index = 0;
    Permanent p;
    p.card = FigureDef().card;
    p.card.m_number = 101;
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = true; // none of the abilities has a tap symbol
    s.battlefield.push_back(p);
    return s;
}
}

TEST_CASE("Figure six hybrid pips are all payable with white")
{
    const ManaCost& c = *FigureDef().params.figure_eight_cost;
    CHECK(c.hybrid_count == 6);
    CHECK(c.ManaValue() == 6);
    ManaPool white;
    white.white = 6;
    CHECK(white.CanPay(c));
    white.white = 5;
    CHECK_FALSE(white.CanPay(c));
}

TEST_CASE("Figure six hybrid pips pay through the shared payer and scripted float")
{
    const ManaCost& c = *FigureDef().params.figure_eight_cost;
    REQUIRE(c.hybrid_count == 6); // the payer must enumerate all 64 assignments

    // A later white bill makes the float-aware assignment order score all 64 entries.
    ManaCost line;
    line.white = 1;
    line.red = 6;
    LineUnpaidCostScope hold(line);
    GameState normal = FigureState();
    normal.floating_mana.white = 6;
    CHECK(TapForCostShared(normal, c, false, nullptr, true));
    CHECK(normal.floating_mana.Total() == 0);

    // The oracle pre-taps sources and requires this exact payment to use only that float.
    GameState scripted = FigureState();
    scripted.floating_mana.white = 6;
    const bool prior = manaoracle::t_float_only;
    manaoracle::t_float_only = true;
    const bool paid = TapForCostShared(scripted, c, false, nullptr, true);
    manaoracle::t_float_only = prior;
    CHECK(paid);
    CHECK(scripted.floating_mana.Total() == 0);
}

TEST_CASE("Figure checks Spirit and Warrior on resolution and retains granted keywords")
{
    auto s = FigureState();
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureEight);
    CHECK(s.battlefield[0].card.m_power == 1); // legal activation, conditional effect did nothing
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureFour);
    CHECK(s.battlefield[0].card.m_power == 1);
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureTwo);
    CHECK(s.battlefield[0].card.m_power == 2);
    CHECK(CardHasSubtype(s.battlefield[0].card, "Spirit"));
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureFour);
    CHECK(s.battlefield[0].card.m_power == 4);
    CHECK(CardHasSubtype(s.battlefield[0].card, "Warrior"));
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureEight);
    CHECK(s.battlefield[0].card.m_power == 8);
    CHECK(s.battlefield[0].card.HasKeyword(Keyword::Flying));
    CHECK(s.battlefield[0].card.HasKeyword(Keyword::FirstStrike));
    ApplyPermAbility(s, 0, 101, PermAbilityMode::FigureTwo);
    CHECK(s.battlefield[0].card.m_power == 2);
    CHECK_FALSE(CardHasSubtype(s.battlefield[0].card, "Warrior"));
    CHECK(s.battlefield[0].card.HasKeyword(Keyword::Flying));
    CHECK(s.battlefield[0].card.HasKeyword(Keyword::FirstStrike));
}
