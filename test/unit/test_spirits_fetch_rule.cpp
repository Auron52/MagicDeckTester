// Spirits: the USER's Flooded Strand target rule (2026-11-13), SpiritsProvider's FetchCandidates /
// FetchSearchCandidates (crack on play) and LandSearchPickOrder / FetchLandSearchPickWidth (the held crack,
// the d0 runner and rollouts -- LandSearchPicks). The USER's words: priority 1 White -> 1 Blue -> 2 White ->
// 1 Black (that can cast Lingering Souls) -> 3 White -> 2 Blue; shocklands first, filling the top two unmet
// slots; then Plains, Island, last Plains; and the search may take another land when the line needs a colour
// the pick does not make. Each case pins the pick (entry 0) and the searched set (the leading entries).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& FrDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

using Names = std::vector<std::string>;

// A state with `board` lands under our control and `library` in our library.
GameState FrState(const Names& board, const Names& library)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    int n = 100;
    for (const std::string& nm : board)
    {
        Permanent p;
        p.card             = FrDef(nm).card;
        p.card.m_number    = n++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& nm : library)
    {
        Card c     = FrDef(nm).card;
        c.m_number = n++;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Names kFullLib = { "Plains", "Plains", "Island", "Hallowed Fountain", "Godless Shrine", "Godless Shrine",
                         "Watery Grave", "Supreme Phantom" };

// The searched set on BOTH paths: the crack-on-play fan and the held crack's pick list. They must agree.
Names Searched(GameState& s)
{
    static const SpiritsProvider prov;
    s.m_provider = &prov;
    const CardDefinition& strand = FrDef("Flooded Strand");
    const Names ranked = prov.FetchCandidates(s, 0, strand.params);
    const Names fan    = prov.FetchSearchCandidates(s, 0, strand.params, ranked);
    Names held;
    for (const auto& pick : LandSearchPicks(s, 0, LandSearchSpecOf(strand.params)))
    {
        REQUIRE(pick.size() == 1);
        held.push_back(pick.front());
    }
    CHECK(fan == held);
    return fan;
}
}   // namespace

TEST_CASE("Spirits fetch rule: nothing out -> Hallowed Fountain (W1, U1), Godless Shrine searched for black")
{
    GameState s = FrState({}, kFullLib);
    CHECK(Searched(s) == Names{ "Hallowed Fountain", "Godless Shrine" });
}

TEST_CASE("Spirits fetch rule: missing blue -> Hallowed Fountain")
{
    // The black alternative is Watery Grave, not Godless Shrine: taken INSTEAD of the Fountain it still fills
    // U1 (Godless would leave blue missing).
    GameState s = FrState({ "Plains" }, kFullLib);
    CHECK(Searched(s) == Names{ "Hallowed Fountain", "Watery Grave" });
}

TEST_CASE("Spirits fetch rule: blue had, black missing -> Godless Shrine; Fountain searched for blue")
{
    GameState s = FrState({ "Hallowed Fountain" }, { "Plains", "Island", "Godless Shrine", "Watery Grave" });
    CHECK(Searched(s) == Names{ "Godless Shrine", "Watery Grave" });
}

TEST_CASE("Spirits fetch rule: creature-only lands count for W/U but not for Lingering Souls' black")
{
    // Unclaimed Territory makes W and U for Spirits (W1, U1 met); its black is creature-only, so B1 is unmet:
    // the top two unmet slots are W2, B1 -> Godless Shrine.
    GameState s = FrState({ "Unclaimed Territory" }, kFullLib);
    CHECK(Searched(s).front() == "Godless Shrine");
    // A real black source fills B1 (Watery Grave): the unmet slots are W2, W3, which Godless Shrine and Hallowed
    // Fountain fill alike, so the all-slot tie-break decides -> Hallowed Fountain.
    GameState t = FrState({ "Unclaimed Territory", "Watery Grave" }, kFullLib);
    CHECK(Searched(t).front() == "Hallowed Fountain");
}

TEST_CASE("Spirits fetch rule: two Plains out -> Watery Grave fills U1 and B1")
{
    GameState s = FrState({ "Plains", "Plains" }, kFullLib);
    CHECK(Searched(s) == Names{ "Watery Grave" , "Hallowed Fountain" });
}

TEST_CASE("Spirits fetch rule: everything needed -> Hallowed Fountain")
{
    GameState s = FrState({ "Godless Shrine", "Godless Shrine", "Watery Grave", "Azorius Chancery" }, kFullLib);
    CHECK(Searched(s).front() == "Hallowed Fountain");
}

TEST_CASE("Spirits fetch rule: shocklands gone -> Plains, then Island, then the last Plains; Island searched")
{
    GameState a = FrState({ "Hallowed Fountain" }, { "Plains", "Plains", "Island" });
    CHECK(Searched(a) == Names{ "Plains", "Island" });
    GameState b = FrState({ "Hallowed Fountain" }, { "Plains", "Island" });
    CHECK(Searched(b) == Names{ "Island", "Plains" });
    GameState c = FrState({ "Hallowed Fountain" }, { "Plains" });
    CHECK(Searched(c) == Names{ "Plains" });
}

TEST_CASE("Spirits fetch rule: the full legal list stays ORDERED behind the searched set")
{
    GameState s = FrState({}, kFullLib);
    static const SpiritsProvider prov;
    const Names ranked = prov.FetchCandidates(s, 0, FrDef("Flooded Strand").params);
    CHECK(ranked == Names{ "Hallowed Fountain", "Godless Shrine", "Watery Grave", "Plains", "Island" });
}
