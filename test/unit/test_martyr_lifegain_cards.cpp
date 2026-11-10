// Martyr Lifegain card integration (2026-11-10): Thraben Inspector, Squadron Hawk, Curious Farm
// Animals, Kami of False Hope, Enduring Innocence. Each test drives the SHARED resolution helpers
// (FireOwnEtbTriggers, ApplySacCreatureOutlet, OnCreatureDies, ResolveCombatDamage) that both the
// executor and the rollout call, so a pass here is a pass in both worlds.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& MlDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card MlZone(const char* name, int number)
{
    MlDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

int MlAdd(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = MlDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int MlCount(const GameState& s, const char* name, int controller = 0)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && p.card.m_name.str() == name) { ++n; } }
    return n;
}

int MlHandCount(const GameState& s, const char* name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

void MlSetLib(GameState& s, const std::vector<Card>& cards)
{ s.players[0].library.assign(cards.begin(), cards.end()); }

GameState MlState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    return s;
}
}   // namespace

// ---- Thraben Inspector ---------------------------------------------------------------------------

TEST_CASE("Thraben Inspector: its ETB investigates -- one crackable Clue artifact token")
{
    GameState s = MlState();
    const int idx = MlAdd(s, "Thraben Inspector", 11, 0, /*sick=*/true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    REQUIRE(MlCount(s, "Clue Token") == 1);
    const Permanent& clue = s.battlefield.back();
    CHECK(clue.is_token);
    CHECK(clue.card.HasType(CardType::Artifact));
    CHECK(!clue.card.IsCreature());
    CHECK(clue.card.m_number >= GameState::kFirstTokenNumber);
    const CardDefinition* cd = CardDatabase::Instance().LookupCached(clue.card);
    REQUIRE(cd != nullptr);
    CHECK(cd->params.sac_draw_cost.has_value());   // the {2}-sac-draw lives on the Clue Token def
}

TEST_CASE("Thraben Inspector: a graveyard RETURN (Abiding Grace's route) investigates too")
{
    GameState s = MlState();
    const Card back = MlZone("Thraben Inspector", 12);
    PutCardOntoBattlefield(s, 0, back, "Abiding Grace", 0);
    CHECK(MlCount(s, "Thraben Inspector") == 1);
    CHECK(MlCount(s, "Clue Token") == 1);
}

TEST_CASE("Thraben Inspector: a creature without the param mints nothing (byte-identity guard)")
{
    GameState s = MlState();
    const int idx = MlAdd(s, "Kami of False Hope", 13, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlCount(s, "Clue Token") == 0);
}

// ---- Squadron Hawk -------------------------------------------------------------------------------

TEST_CASE("Squadron Hawk: fetches up to three Hawks to hand, never another creature, one shuffle")
{
    GameState s = MlState();
    MlSetLib(s, { MlZone("Martyr of Sands", 30), MlZone("Squadron Hawk", 31),
                             MlZone("Squadron Hawk", 32), MlZone("Plains", 33),
                             MlZone("Squadron Hawk", 34), MlZone("Squadron Hawk", 35),
                             MlZone("Serra Ascendant", 36) });
    const uint64_t searches = s.search_count;
    const int idx = MlAdd(s, "Squadron Hawk", 20, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlHandCount(s, "Squadron Hawk") == 3);
    CHECK(s.players[0].hand.size() == 3);
    CHECK(s.players[0].library.size() == 4);   // the 4th Hawk + Martyr + Plains + Serra stay
    CHECK(s.search_count == searches + 1);     // exactly ONE shuffle for the whole multi-fetch
}

TEST_CASE("Squadron Hawk: one Hawk left -> one fetched; none left -> nothing, still shuffles")
{
    GameState s = MlState();
    MlSetLib(s, { MlZone("Martyr of Sands", 30), MlZone("Squadron Hawk", 31),
                             MlZone("Plains", 33) });
    const int idx = MlAdd(s, "Squadron Hawk", 20, 0, true);
    FireOwnEtbTriggers(s, 0, idx, std::string(), -1);
    CHECK(MlHandCount(s, "Squadron Hawk") == 1);
    CHECK(s.players[0].hand.size() == 1);

    GameState t = MlState();
    MlSetLib(t, { MlZone("Martyr of Sands", 30), MlZone("Plains", 33) });
    const uint64_t searches = t.search_count;
    const int tidx = MlAdd(t, "Squadron Hawk", 21, 0, true);
    FireOwnEtbTriggers(t, 0, tidx, std::string(), -1);
    CHECK(t.players[0].hand.empty());
    CHECK(t.players[0].library.size() == 2);
    CHECK(t.search_count == searches + 1);
}
