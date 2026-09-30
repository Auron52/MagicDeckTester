// USER-doctrine enumeration prunes that lived in the engine are PROVIDER hooks now (USER HARD RULE
// 2026-09-30): the autonomous search never casts a tuck removal (OffersTuckRemovalCast) and never
// uses a Jitte's non-combat modes (OffersJitteNonCombatModes) -- unless a provider says so. Two arms
// each on the SAME board that MUST differ: the default provider's plan list lacks the action, an
// overriding provider's list carries it.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{
void EnsureCardsLoadedPP()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Permanent Perm(const char* name, int num, int who)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card; p.card.m_number = num; p.controller_index = who; p.owner_index = who;
    return p;
}

struct OpenBoth : public GenericProvider
{
    bool OffersTuckRemovalCast(const GameState&, const CardDefinition&, bool) const override { return true; }
    bool OffersJitteNonCombatModes(const GameState&) const override { return true; }
};

bool AnyPlanHas(const GameState& s, Action::Kind kind, const std::string& card)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != kind) { continue; }
            if (card.empty() || static_cast<const std::string&>(a.card_name) == card) { return true; }
        }
    }
    return false;
}
}   // namespace

TEST_CASE("OffersTuckRemovalCast: the default provider prunes Unexpectedly Absent, an override offers it")
{
    EnsureCardsLoadedPP();
    auto board = [](const DecisionProvider* prov)
    {
        GameState s;
        s.active_player_index = 0;
        s.m_provider = prov;
        for (int i = 0; i < 4; ++i) { s.battlefield.push_back(Perm("Plains", 10 + i, 0)); }
        s.battlefield.push_back(Perm("Llanowar Elves", 20, 1));   // an opponent creature to tuck
        const CardDefinition* ua = CardDatabase::Instance().Lookup("Unexpectedly Absent");
        REQUIRE(ua != nullptr);
        Card c = ua->card; c.m_number = 30;
        s.players[0].hand.push_back(c);
        return s;
    };
    GameState a = board(&DefaultProvider());
    CHECK_FALSE(AnyPlanHas(a, Action::Kind::CastFromHand, "Unexpectedly Absent"));
    OpenBoth open;
    GameState b = board(&open);
    CHECK(AnyPlanHas(b, Action::Kind::CastFromHand, "Unexpectedly Absent"));
}

TEST_CASE("OffersJitteNonCombatModes: the default provider prunes the modes, an override offers them")
{
    EnsureCardsLoadedPP();
    auto board = [](const DecisionProvider* prov)
    {
        GameState s;
        s.active_player_index = 0;
        s.m_provider = prov;
        Permanent j = Perm("Umezawa's Jitte", 40, 0);
        j.charge_counters = 2;
        s.battlefield.push_back(j);
        s.battlefield.push_back(Perm("Llanowar Elves", 41, 1));   // an opponent -1/-1 target
        return s;
    };
    GameState a = board(&DefaultProvider());
    CHECK_FALSE(AnyPlanHas(a, Action::Kind::JitteModeAbility, std::string()));
    OpenBoth open;
    GameState b = board(&open);
    CHECK(AnyPlanHas(b, Action::Kind::JitteModeAbility, std::string()));
}
