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
#include "core/SpellEffects.h"

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
    Arm(bool bucket, bool outlook, bool user = false)
    {
        heurarm::Clear();
        heurarm::t_arm[heurarm::SNOW_SCRY]         = bucket ? 1 : 0;
        heurarm::t_arm[heurarm::SNOW_SCRY_OUTLOOK] = outlook ? 1 : 0;
        heurarm::t_arm[heurarm::SNOW_SCRY_USER]    = user ? 1 : 0;
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

int Verdict(const GameState& s, const std::string& top) { return SnowSs().ScryVerdict(s, CardSs(top, 99)); }

TEST_CASE("Snow scry USER rule: firm calls (prune) and lean calls")
{
    Arm a(false, false, true);
    // "ditch lands when we have enough (including one to play next turn)"
    CHECK(Verdict(Turn3(1), "Snow-Covered Mountain") == 0);
    CHECK(Verdict(Turn3(0), "Snow-Covered Mountain") == -1);
    CHECK(Keep(Turn3(0), "Snow-Covered Mountain"));
    // "always ditch Skred"; "Extra Marit-Lage's slumber should be pitched"
    CHECK(Verdict(Turn3(0), "Skred") == 0);
    CHECK(Verdict(Turn3(0), "Marit Lage's Slumber") == 0);
    // "always keep Abominable Treefolk unless we have no means to play it" (Forest + Islands: G and U)
    CHECK(Verdict(Turn3(0), "Abominable Treefolk") == 1);
    // "Dragon and Owl are too slow unless we can play them this turn or maybe next turn"
    CHECK(Verdict(Turn3(0), "Rimefeather Owl") == -1);
    CHECK_FALSE(Keep(Turn3(0), "Rimefeather Owl"));
    // "Cards that draw are usually good to keep, except for Frost Augur when we already have multiple
    // draw sources" -- Turn3's hand holds one Frost Augur; add a Scrying Sheets on board for two.
    CHECK(Keep(Turn3(0), "Frost Augur"));
    CHECK(Keep(Turn3(0), "Ice-Fang Coatl"));
    GameState two = Turn3(0);
    Permanent sh; sh.card = CardSs("Scrying Sheets", 30); sh.controller_index = 0; sh.owner_index = 0;
    two.battlefield.push_back(sh);
    CHECK_FALSE(Keep(two, "Frost Augur"));
    // "Accelerators are good unless we are lacking threats" -- no threat in Turn3's hand.
    CHECK_FALSE(Keep(Turn3(0), "Coldsteel Heart"));
    GameState th = Turn3(0);
    th.players[0].hand.push_back(CardSs("Abominable Treefolk", 31));
    CHECK(Keep(th, "Coldsteel Heart"));
}

TEST_CASE("Snow scry USER rule: a firm verdict prunes the searched candidates to the heuristic's")
{
    Arm a(false, false, true);
    const GameState s = Turn3(0);
    CHECK(TopDispositionCandidates(s, { CardSs("Skred", 99) }, LookKind::Scry).size() == 1);
    CHECK(TopDispositionCandidates(s, { CardSs("Abominable Treefolk", 99) }, LookKind::Scry).size() == 1);
    CHECK(TopDispositionCandidates(s, { CardSs("Frost Augur", 99) }, LookKind::Scry).size() == 2);
}

TEST_CASE("Snow scry: no verdicts (and no pruning) unless the USER rule is on")
{
    Arm a(false, true);
    CHECK(Verdict(Turn3(0), "Skred") == -1);
    CHECK(TopDispositionCandidates(Turn3(0), { CardSs("Skred", 99) }, LookKind::Scry).size() == 2);
}
