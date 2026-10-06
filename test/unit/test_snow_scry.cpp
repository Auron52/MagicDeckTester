// Snow's own scry keep (SnowProvider::ScryKeepOnTop). USER 2026-10-06: "Scry should be optionally
// searched, but I think we should be able to design a heuristic for it as well." Every Snow scry is
// Marit Lage's Slumber's triggered scry 1; the generic rule it replaces bottoms every land once two
// lands are in play, on a deck whose lands are snow permanents. Pinned here: each lever arm's keep /
// bottom on the states that separate them. The arms are set per thread (heurarm), exactly as a batch
// job sets them.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

const CardDefinition& DefSs(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSs(const std::string& name, int number)
{
    Card c = DefSs(name).card;
    c.m_number = number;
    return c;
}

void PutSs(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card = CardSs(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}

const SnowProvider& SnowSs() { static const SnowProvider prov; return prov; }

// T3, the land drop just made (the Slumber trigger fires off it): three snow lands and a Slumber in
// play, `hand_lands` Snow-Covered Islands in hand plus a Frost Augur.
GameState Turn3(int hand_lands)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.m_provider = &SnowSs();
    s.players[0].lands_played_this_turn = 1;
    PutSs(s, "Snow-Covered Island", 1);
    PutSs(s, "Snow-Covered Forest", 2);
    PutSs(s, "Snow-Covered Island", 3);
    PutSs(s, "Marit Lage's Slumber", 4);
    for (int k = 0; k < hand_lands; ++k) { s.players[0].hand.push_back(CardSs("Snow-Covered Island", 10 + k)); }
    s.players[0].hand.push_back(CardSs("Frost Augur", 20));
    return s;
}

struct Arm
{
    Arm(bool bucket, bool outlook)
    {
        heurarm::Clear();
        heurarm::t_arm[heurarm::SNOW_SCRY]         = bucket ? 1 : 0;
        heurarm::t_arm[heurarm::SNOW_SCRY_OUTLOOK] = outlook ? 1 : 0;
    }
    ~Arm() { heurarm::Clear(); }
};

bool Keep(const GameState& s, const std::string& top) { return SnowSs().ScryKeepOnTop(s, CardSs(top, 99)); }

}   // namespace

TEST_CASE("Snow scry: the generic arm bottoms a land with three in play even with none in hand")
{
    Arm a(false, false);
    CHECK_FALSE(Keep(Turn3(0), "Snow-Covered Mountain"));
    CHECK(Keep(Turn3(0), "Skred"));
}

TEST_CASE("Snow scry BUCKET: a land is kept up to the user's quota of two spare lands; Skred is bottomed")
{
    Arm a(true, false);
    CHECK(Keep(Turn3(0), "Snow-Covered Mountain"));
    CHECK(Keep(Turn3(1), "Snow-Covered Mountain"));
    CHECK_FALSE(Keep(Turn3(2), "Snow-Covered Mountain"));
    CHECK_FALSE(Keep(Turn3(0), "Skred"));                  // NeverCast
    CHECK_FALSE(Keep(Turn3(0), "Marit Lage's Slumber"));   // legendary, one already in play
    CHECK(Keep(Turn3(0), "Abominable Treefolk"));
    CHECK(Keep(Turn3(0), "Rimefeather Owl"));              // BUCKET has no castability clause
}

TEST_CASE("Snow scry OUTLOOK: a land only when it is next turn's drop; a seven-drop far off is bottomed")
{
    Arm a(false, true);
    CHECK(Keep(Turn3(0), "Snow-Covered Mountain"));
    CHECK_FALSE(Keep(Turn3(1), "Snow-Covered Mountain"));
    CHECK_FALSE(Keep(Turn3(0), "Rimefeather Owl"));        // 7 > next turn's 3 lands + 1
    CHECK(Keep(Turn3(1), "Abominable Treefolk"));          // 4 <= 3 + spare land + 1
}

TEST_CASE("Snow scry: an OPEN land drop spends one land in hand before counting spares")
{
    Arm a(true, true);
    GameState s = Turn3(1);
    s.players[0].lands_played_this_turn = 0;               // the Island in hand is this turn's drop
    CHECK(Keep(s, "Snow-Covered Mountain"));
}
