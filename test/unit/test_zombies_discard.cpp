#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void LoadZombiesCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

GameState ZombiesDiscardState(const std::vector<std::string>& hand,
                              const std::vector<std::string>& board)
{
    LoadZombiesCards();
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    int id = 1;
    for (const std::string& name : hand)
    {
        REQUIRE(CardDatabase::Instance().Lookup(name) != nullptr);
        Card c;
        c.m_name = name;
        c.RehashName();
        c.m_number = id++;
        s.players[0].hand.push_back(c); // real hands are name-only placeholders
    }
    for (const std::string& name : board)
    {
        Permanent p;
        p.card = CardDatabase::Instance().Lookup(name)->card;
        p.card.m_number = id++;
        p.controller_index = 0;
        p.owner_index = 0;
        s.battlefield.push_back(p);
    }
    return s;
}
}

TEST_CASE("Zombies discard: spare lands shed before the two-threat floor")
{
    const GameState s = ZombiesDiscardState(
        {"Swamp", "Swamp", "Swamp", "Swamp", "Swamp", "Swamp",
         "Gravecrawler", "Undead Warchief"}, {});
    CHECK(CleanupDiscardIsLand(s.players[0].hand[0]));
    CHECK(CardDatabase::Instance().LookupCached(s.players[0].hand[6]) != nullptr);
    CHECK(CardDatabase::Instance().LookupCached(s.players[0].hand[6])->card.IsCreature());
    CHECK(CardDatabase::Instance().LookupCached(s.players[0].hand[7])->card.IsCreature());
    CHECK_FALSE(ZombiesProvider().CleanupDiscardShedStable());
    CHECK(CleanupDiscardRankingWithOrder(s, nullptr, {4, 5, 7}).front() == 4);
    const auto pick = ZombiesProvider().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(pick.size() == 1);
    CHECK(pick.front() == 4);
}

TEST_CASE("Zombies discard: an idle Fleshbag sheds before a useful ritual")
{
    const GameState s = ZombiesDiscardState(
        {"Swamp", "Swamp", "Gravecrawler", "Dread Wanderer",
         "Dark Ritual", "Undead Warchief", "Gray Merchant of Asphodel",
         "Fleshbag Marauder"}, {"Swamp", "Swamp"});
    const auto pick = ZombiesProvider().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(pick.size() == 1);
    CHECK(pick.front() == 7);
}

TEST_CASE("Zombies discard: board lands reduce the hand land quota")
{
    const GameState s = ZombiesDiscardState(
        {"Swamp", "Swamp", "Swamp", "Swamp", "Swamp", "Culling the Weak",
         "Gray Merchant of Asphodel", "Undead Warchief"},
        {"Swamp", "Swamp", "Gravecrawler", "Dread Wanderer"});
    const auto pick = ZombiesProvider().CleanupDiscardCandidates(s, nullptr);
    REQUIRE(pick.size() == 1);
    CHECK(pick.front() == 3);
}
