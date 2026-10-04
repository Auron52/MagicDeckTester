// Unit cover for the whole-turn prepay's treatment of a pure COLOUR CONVERTER (Arcum's Astrolabe:
// "{1}, {T}: Add one mana of any color", filter_no_free_colorless). Snow regression s3003 gi5 T2,
// 2026-10-04: board Snow-Covered Forest + Snow-Covered Island + a Boreal Druid from last turn, hand
// Arcum's Astrolabe {S}, Boreal Druid {G}, Frost Augur {U}. Jointly payable (Forest -> {G}, Island ->
// {U}, the old Druid's snow {C} -> {S}), but BatchPrepayMainCasts declined it as a PRODUCER line
// (the Astrolabe carries rock_mana), and the per-cast greedy, walking the user's Snow cast order
// (Astrolabe, Druid, Augur), paid the {S} with the Forest and the Druid's {G} through the Island and
// the fresh Astrolabe -- so the Augur's {U} had no source and the plan realised as two casts.
//
// What is pinned here: the three-cast plan is ENUMERATED, and APPLIED it realises all three casts
// with every source tapped and nothing left in hand.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsSp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSp(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutSp(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefSp(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardSp(const std::string& name, int number)
{
    Card c     = DefSp(name).card;
    c.m_number = number;
    return c;
}

int CountOnBattlefield(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

const DecisionProvider& Snow()
{
    static const SnowProvider prov;
    return prov;
}

GameState Gi5Turn2()
{
    EnsureCardsSp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.m_provider = &Snow();
    s.players[0].lands_played_this_turn = 1;   // the land drop is spent: nothing else can pay
    PutSp(s, "Snow-Covered Forest", 44);
    PutSp(s, "Snow-Covered Island", 50);
    PutSp(s, "Boreal Druid", 9);                // cast last turn: not summoning-sick
    s.players[0].hand.push_back(CardSp("Arcum's Astrolabe", 5));
    s.players[0].hand.push_back(CardSp("Boreal Druid", 10));
    s.players[0].hand.push_back(CardSp("Frost Augur", 17));
    // The Astrolabe's ETB draw needs a library; snow lands keep it inert for this test.
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardSp("Snow-Covered Mountain", 900 + k)); }
    return s;
}

}   // namespace

TEST_CASE("Snow: {Astrolabe, Boreal Druid, Frost Augur} off Forest + Island + an old Druid casts all three")
{
    const GameState s = Gi5Turn2();
    REQUIRE(DefSp("Arcum's Astrolabe").params.filter_no_free_colorless);

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* all3 = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        int n = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
        if (n == 3) { all3 = &p; break; }
    }
    REQUIRE_MESSAGE(all3 != nullptr, "the jointly payable three-cast plan was not enumerated");

    GameState after = s;
    TurnSolver::ApplyPlan(after, *all3, /*is_pre_combat=*/true);
    CHECK(CountOnBattlefield(after, "Arcum's Astrolabe") == 1);
    CHECK(CountOnBattlefield(after, "Boreal Druid") == 2);
    CHECK(CountOnBattlefield(after, "Frost Augur") == 1);
    for (const Card& c : after.players[0].hand)
    {
        CHECK_MESSAGE(c.m_name.str() != "Frost Augur", "the Augur was stranded in hand");
        CHECK_MESSAGE(c.m_name.str() != "Boreal Druid", "the Druid was stranded in hand");
    }
}
