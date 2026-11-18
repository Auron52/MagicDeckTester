// Unit tests for CR 400.3 at the death sites: "If an object would go to any library, graveyard or hand other
// than its owner's, it goes to its owner's corresponding zone." A creature we control but do not own (Bolas's -2,
// Expropriate) dies into its OWNER's graveyard, and a death trigger that reads "it" back out of the graveyard
// (persist: "return it to the battlefield under its OWNER's control") finds it there.
//
// Before the fix the executor's SBA (GameEngine::CheckStateBasedActions), CounterPingKillAt, the rollout's
// zero-toughness sweep, the echo / devour / sac-outlet sacrifices and several sacrifice-a-source costs put the
// card in the CONTROLLER's graveyard, and OnCreatureDies looked for it (persist, Worldspine, Ilharg, Enduring
// Innocence, the graveyard-entry shuffle) in the controller's graveyard -- so even a site that already used the
// owner (SacrificePermanentAt) lost the persist return. Each case below failed before the fix.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < 10; ++k)
    {
        Card c = Def("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
        c.m_number = 950 + k;
        s.players[1].library.push_back(c);
    }
    return s;
}

// A creature the OPPONENT owns and WE control (stolen).
std::size_t PutStolen(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
    return s.battlefield.size() - 1;
}

bool InGraveyard(const GameState& s, int player, const std::string& name)
{
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

const Permanent* FindName(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

void ExecutorSba(GameState& s)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    GameEngine ge(eng);
    MtgTestSeam::GeCheckSba(ge, s);
}

}   // namespace

TEST_CASE("CR 400.3 executor SBA: a stolen creature with lethal damage dies into its OWNER's graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const std::size_t i = PutStolen(s, "Llanowar Elves", 40);
    s.battlefield[i].damage = 1;
    ExecutorSba(s);
    CHECK(FindName(s, "Llanowar Elves") == nullptr);
    CHECK(InGraveyard(s, 1, "Llanowar Elves"));
    CHECK_FALSE(InGraveyard(s, 0, "Llanowar Elves"));
}

TEST_CASE("CR 400.3 CounterPingKillAt and the rollout sweep: a stolen creature goes to its OWNER's graveyard")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        const std::size_t i = PutStolen(s, "Llanowar Elves", 40);
        s.battlefield[i].damage = 1;
        CounterPingKillAt(s, i, "test");
        CHECK(InGraveyard(s, 1, "Llanowar Elves"));
        CHECK_FALSE(InGraveyard(s, 0, "Llanowar Elves"));
    }
    {
        GameState s = Fresh();
        const std::size_t i = PutStolen(s, "Llanowar Elves", 40);
        s.battlefield[i].damage = 1;
        CHECK(SbaKillDeadCreatures(s, "test") == 1);
        CHECK(InGraveyard(s, 1, "Llanowar Elves"));
        CHECK_FALSE(InGraveyard(s, 0, "Llanowar Elves"));
    }
}

TEST_CASE("CR 400.3 persist on a stolen creature: it returns under its OWNER's control (SBA death)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const std::size_t i = PutStolen(s, "Kitchen Finks", 40);
    s.battlefield[i].damage = 2;   // 3/2
    ExecutorSba(s);
    const Permanent* back = FindName(s, "Kitchen Finks");
    REQUIRE(back != nullptr);   // persist found it (in the owner's graveyard)
    CHECK(back->controller_index == 1);
    CHECK(back->owner_index == 1);
    CHECK(MinusCountersOn(*back) == 1);
    CHECK_FALSE(InGraveyard(s, 0, "Kitchen Finks"));
    CHECK_FALSE(InGraveyard(s, 1, "Kitchen Finks"));
}

TEST_CASE("CR 400.3 persist on a SACRIFICED stolen creature: OnCreatureDies reads the OWNER's graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const std::size_t i = PutStolen(s, "Kitchen Finks", 40);
    // SacrificePermanentAt already put the card in the owner's graveyard; OnCreatureDies then looked for it in
    // the controller's, so persist never returned it.
    SacrificePermanentAt(s, 0, static_cast<int>(i));
    const Permanent* back = FindName(s, "Kitchen Finks");
    REQUIRE(back != nullptr);
    CHECK(back->controller_index == 1);
    CHECK(MinusCountersOn(*back) == 1);
}

TEST_CASE("CR 400.3 is inert for an owned creature: it still dies into its controller's (= owner's) graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Permanent p;
    p.card = Def("Kitchen Finks").card;
    p.card.m_number = 41;
    p.damage = 2;
    s.battlefield.push_back(p);   // ours: controller 0, owner 0
    ExecutorSba(s);
    const Permanent* back = FindName(s, "Kitchen Finks");
    REQUIRE(back != nullptr);
    CHECK(back->controller_index == 0);
    CHECK(MinusCountersOn(*back) == 1);
}
