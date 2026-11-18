// Maze's End deck, integration phase I1 (2026-11-17; docs/design/analysis-Maze's End.md).
// Gate infrastructure (Planar Nexus's every-nonbasic-type bit, distinct-named Gate counting), the
// won_by_effect alternate win, Maze's End's activation (return-to-hand cost, Gate fetch, win check),
// Guild Summit's Gate-enter draw and ETB tap-draw, and Seahunter's unchanged TapTutorPut filter.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadMazeCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const char* kGuildgates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                              "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                              "Selesnya Guildgate", "Simic Guildgate" };

const CardDefinition& MDef(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card MCard(const char* name, int number)
{
    Card c = MDef(name).card;
    c.m_number = number;
    return c;
}

int MPut(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = MCard(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return number;
}

GameState MBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    return s;
}

const Permanent* MFind(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool MInHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}

// Activate Maze's End `num` fetching `target` (the searched pin) -- the shared resolver both worlds use.
void ActivateMaze(GameState& s, int num, const std::string& target)
{
    const SacNamedPinScope pin(0, &target);
    REQUIRE(PermAbilitySourceLive(s, 0, num, PermAbilityMode::TapTutorPut, nullptr));
    ApplyPermAbility(s, 0, num, PermAbilityMode::TapTutorPut);
}
}   // namespace

TEST_CASE("Maze's End I1: Planar Nexus is every nonbasic land type and never a basic one")
{
    LoadMazeCards();
    const Card& nexus = MDef("Planar Nexus").card;
    CHECK(nexus.m_subtypes.AllNonbasicLandTypes());
    CHECK(nexus.m_subtypes.empty());   // iterating still yields only the printed subtypes
    CHECK(CardHasSubtype(nexus, "Gate"));
    CHECK(CardHasSubtype(nexus, "Desert"));
    CHECK(CardHasSubtype(nexus, "Urza's"));
    CHECK_FALSE(CardHasSubtype(nexus, "Forest"));
    CHECK_FALSE(CardHasSubtype(nexus, "Island"));
    CHECK_FALSE(CardHasSubtype(nexus, "Merfolk"));
    CHECK(nexus.m_subtypes.HasId(SubtypeRegistry::Instance().Id("Gate")));
    CHECK_FALSE(nexus.m_subtypes.HasId(SubtypeRegistry::Instance().Id("Forest")));
    CHECK(IsGate(nexus));
    CHECK(IsGate(MDef("Azorius Guildgate").card));
    CHECK_FALSE(IsGate(MDef("Maze's End").card));
    CHECK_FALSE(IsGate(MDef("Forest").card));
    // Maze's End's library filter: "a Gate card" -- Nexus yes, a Guildgate yes, Maze's End / Forest no.
    const CardParams& mp = MDef("Maze's End").params;
    CHECK(TapTutorPutLibraryCandidate(mp, nexus));
    CHECK(TapTutorPutLibraryCandidate(mp, MDef("Simic Guildgate").card));
    CHECK_FALSE(TapTutorPutLibraryCandidate(mp, MDef("Maze's End").card));
    CHECK_FALSE(TapTutorPutLibraryCandidate(mp, MDef("Forest").card));
    // Gates keep their name in behaviour_identity; two Guildgates never share an identity.
    CHECK(MDef("Azorius Guildgate").behaviour_identity != MDef("Simic Guildgate").behaviour_identity);
}

TEST_CASE("Maze's End I1: distinct-named Gate count -- duplicates once, Planar Nexus counts, opponent's do not")
{
    LoadMazeCards();
    GameState s = MBase();
    CHECK(CountDistinctNamedGates(s, 0) == 0);
    MPut(s, "Azorius Guildgate", 100);
    MPut(s, "Azorius Guildgate", 101);
    MPut(s, "Boros Guildgate", 102);
    MPut(s, "Maze's End", 103);
    MPut(s, "Forest", 104);
    CHECK(CountDistinctNamedGates(s, 0) == 2);
    MPut(s, "Planar Nexus", 105);
    CHECK(CountDistinctNamedGates(s, 0) == 3);
    MPut(s, "Planar Nexus", 106);
    CHECK(CountDistinctNamedGates(s, 0) == 3);
    MPut(s, "Dimir Guildgate", 107);
    s.battlefield.back().controller_index = 1;
    CHECK(CountDistinctNamedGates(s, 0) == 3);
    CHECK(CountDistinctNamedGates(s, 0, /*stop_at=*/2) == 2);
    // Every Gate, duplicates included, for Gates Ablaze's X.
    CHECK(CountControlledLandsWithSubtype(s, 0, "Gate") == 5);
}

TEST_CASE("Maze's End I1: the activation returns it to hand, fetches the Gate tapped, and wins only at ten names")
{
    LoadMazeCards();
    GameState s = MBase();
    // Eight distinct Guildgates + a duplicate, Maze's End and three Forests for the {3}.
    for (int i = 0; i < 8; ++i) { MPut(s, kGuildgates[i], 200 + i); }
    MPut(s, "Azorius Guildgate", 208);
    const int maze = MPut(s, "Maze's End", 210);
    s.players[0].library.push_back(MCard("Selesnya Guildgate", 300));   // a NEW name: brings us to 9
    s.players[0].library.push_back(MCard("Simic Guildgate", 301));      // the 10th name
    s.players[0].library.push_back(MCard("Boros Guildgate", 302));      // a duplicate
    s.players[0].library.push_back(MCard("Forest", 303));
    REQUIRE(CountDistinctNamedGates(s, 0) == 8);

    ActivateMaze(s, maze, "Boros Guildgate");             // a duplicate: still eight names
    CHECK(MInHand(s, maze));
    CHECK(MFind(s, maze) == nullptr);
    REQUIRE(MFind(s, 302) != nullptr);
    CHECK(MFind(s, 302)->tapped);                         // a Guildgate enters tapped
    CHECK(CountDistinctNamedGates(s, 0) == 8);
    CHECK_FALSE(s.won_by_effect);
    CHECK(s.players[0].library.size() == 3);

    // Replay it (a land drop) and fetch the ninth name: no win at nine.
    s.players[0].hand.clear();
    MPut(s, "Maze's End", 211);
    ActivateMaze(s, 211, "Selesnya Guildgate");
    CHECK(CountDistinctNamedGates(s, 0) == 9);
    CHECK_FALSE(s.won_by_effect);
    CHECK_FALSE(OpponentHasLost(s));

    // Fail to find with nine: the check runs, and still no win.
    s.players[0].hand.clear();
    MPut(s, "Maze's End", 212);
    ActivateMaze(s, 212, kTutorDeclineTarget);
    CHECK_FALSE(s.won_by_effect);
    CHECK(MInHand(s, 212));

    // The tenth name wins, at resolution, through the one win predicate.
    s.players[0].hand.clear();
    MPut(s, "Maze's End", 213);
    ActivateMaze(s, 213, "Simic Guildgate");
    CHECK(CountDistinctNamedGates(s, 0) == 10);
    CHECK(s.won_by_effect);
    CHECK(OpponentHasLost(s));
    CHECK(dmgev::OpponentLostRaw(s));
}

TEST_CASE("Maze's End I1: a fail-to-find with ten names already wins; Planar Nexus is the tenth name")
{
    LoadMazeCards();
    GameState s = MBase();
    for (int i = 0; i < 9; ++i) { MPut(s, kGuildgates[i], 400 + i); }
    MPut(s, "Planar Nexus", 409);
    const int maze = MPut(s, "Maze's End", 410);
    REQUIRE(CountDistinctNamedGates(s, 0) == 10);
    ActivateMaze(s, maze, kTutorDeclineTarget);
    CHECK(s.won_by_effect);
}

TEST_CASE("Maze's End I1: the deck provider folds the fetch to one target only when the activation wins")
{
    // P1d: this test checks the UNPRUNED engine menu -- pin the USER-directed provider prunes off.
    struct P1dOff { P1dOff() { heurarm::t_arm[heurarm::MAZE_SUMMIT_COUNT] = 0; heurarm::t_arm[heurarm::MAZE_LAND_TARGETS] = 0; } ~P1dOff() { heurarm::t_arm[heurarm::MAZE_SUMMIT_COUNT] = -1; heurarm::t_arm[heurarm::MAZE_LAND_TARGETS] = -1; } } p1d_off;
    LoadMazeCards();
    const MazesEndProvider prov;
    const CardDefinition& maze = MDef("Maze's End");
    GameState s = MBase();
    for (int i = 0; i < 8; ++i) { MPut(s, kGuildgates[i], 500 + i); }
    // Eight names: ORDER only (new names first, duplicates last), nothing dropped, decline kept.
    std::vector<std::string> names = { "Azorius Guildgate", "Simic Guildgate", "Planar Nexus" };
    CHECK_FALSE(prov.TapTutorPutCandidates(s, maze, names));
    CHECK(names == std::vector<std::string>{ "Simic Guildgate", "Planar Nexus", "Azorius Guildgate" });
    // Nine names: a new name wins -> collapse to it (the sound win fold).
    MPut(s, kGuildgates[8], 508);
    names = { "Azorius Guildgate", "Simic Guildgate", "Planar Nexus" };
    CHECK(prov.TapTutorPutCandidates(s, maze, names));
    CHECK(names == std::vector<std::string>{ "Simic Guildgate" });
    // Nine names, only duplicates findable: no fold, every option kept.
    names = { "Azorius Guildgate", "Boros Guildgate" };
    CHECK_FALSE(prov.TapTutorPutCandidates(s, maze, names));
    CHECK(names.size() == 2);
    // Ten names: everything wins -> one target.
    MPut(s, "Simic Guildgate", 509);
    names = { "Azorius Guildgate", "Boros Guildgate" };
    CHECK(prov.TapTutorPutCandidates(s, maze, names));
    CHECK(names.size() == 1);
}

TEST_CASE("Maze's End I1: Seahunter's library filter is unchanged by the def-driven predicate")
{
    LoadMazeCards();
    const CardDefinition* hunter = CardDatabase::Instance().Lookup("Seahunter");
    REQUIRE(hunter != nullptr);
    int checked = 0;
    for (const std::string& nm : CardDatabase::Instance().AllNames())
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        if (d == nullptr) { continue; }
        CHECK(TapTutorPutLibraryCandidate(hunter->params, d->card) == SeahunterLibraryCandidate(d->card));
        ++checked;
    }
    CHECK(checked > 500);
}

TEST_CASE("Maze's End I1: Guild Summit draws when a Gate enters by land drop and by Maze's End's fetch")
{
    LoadMazeCards();
    GameState s = MBase();
    s.players[0].lands_played_this_turn = 0;
    MPut(s, "Guild Summit", 600);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(MCard("Forest", 900 + k)); }
    // Land drop: Azorius Guildgate from hand.
    s.players[0].hand.push_back(MCard("Azorius Guildgate", 601));
    const std::size_t hand0 = s.players[0].hand.size();
    REQUIRE(PlayLandFromHand(s, 0, MDef("Azorius Guildgate"), LandPlayOptions{}));
    CHECK(s.players[0].hand.size() == hand0);            // -1 the land, +1 the Summit draw
    CHECK(s.players[0].library.size() == 5);
    // A non-Gate land draws nothing.
    s.players[0].lands_played_this_turn = 0;
    s.players[0].hand.push_back(MCard("Forest", 602));
    const std::size_t hand1 = s.players[0].hand.size();
    REQUIRE(PlayLandFromHand(s, s.players[0].hand.size() - 1, MDef("Forest"), LandPlayOptions{}));
    CHECK(s.players[0].hand.size() == hand1 - 1);
    // Maze's End's fetch puts a Gate: one draw (the returned Maze's End adds one card to hand too).
    MPut(s, "Forest", 603);
    MPut(s, "Forest", 604);
    const int maze = MPut(s, "Maze's End", 605);
    s.players[0].library.insert(s.players[0].library.begin(), MCard("Boros Guildgate", 606));
    const std::size_t hand2 = s.players[0].hand.size();
    const std::size_t lib2 = s.players[0].library.size();
    ActivateMaze(s, maze, "Boros Guildgate");
    CHECK(MFind(s, 606) != nullptr);
    CHECK(s.players[0].hand.size() == hand2 + 2);       // Maze's End back + the Summit draw
    CHECK(s.players[0].library.size() == lib2 - 2);     // the Gate + the drawn card
}

TEST_CASE("Maze's End I1: Guild Summit's ETB taps the pinned count of Gates and draws that many")
{
    // P1d: this test checks the UNPRUNED engine menu -- pin the USER-directed provider prunes off.
    struct P1dOff { P1dOff() { heurarm::t_arm[heurarm::MAZE_SUMMIT_COUNT] = 0; heurarm::t_arm[heurarm::MAZE_LAND_TARGETS] = 0; } ~P1dOff() { heurarm::t_arm[heurarm::MAZE_SUMMIT_COUNT] = -1; heurarm::t_arm[heurarm::MAZE_LAND_TARGETS] = -1; } } p1d_off;
    LoadMazeCards();
    GameState s = MBase();
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(MCard("Forest", 900 + k)); }
    MPut(s, "Azorius Guildgate", 700);
    MPut(s, "Boros Guildgate", 701);
    MPut(s, "Dimir Guildgate", 702, /*tapped=*/true);
    MPut(s, "Forest", 703);
    MPut(s, "Guild Summit", 704);
    const int summit_idx = static_cast<int>(s.battlefield.size()) - 1;
    // Searched count: two untapped Gates exist; the ETB choice axis offers N = 0..2 as pins 1..3.
    const std::vector<int> vals = MazesEndProvider().EtbChoiceCandidates(s, 0, MDef("Guild Summit"));
    CHECK(vals == std::vector<int>{ 1, 2, 3 });
    GameState t = s;
    FireOwnEtbTriggers(t, 0, summit_idx, std::string(), /*etb_kx=*/2);   // N = 1
    CHECK(t.players[0].hand.size() == 1);
    int tapped_gates = 0;
    for (const Permanent& p : t.battlefield) { if (p.tapped && IsGate(p.card)) { ++tapped_gates; } }
    CHECK(tapped_gates == 2);   // the pre-tapped Dimir + one
    GameState u = s;
    FireOwnEtbTriggers(u, 0, summit_idx, std::string(), /*etb_kx=*/9);   // clamped to the two untapped
    CHECK(u.players[0].hand.size() == 2);
    CHECK_FALSE(MFind(u, 703)->tapped);                                   // a Forest is never tapped
    GameState v = s;
    FireOwnEtbTriggers(v, 0, summit_idx, std::string(), /*etb_kx=*/1);   // N = 0 (decline)
    CHECK(v.players[0].hand.empty());
}

TEST_CASE("Maze's End I1: the search finds the Maze's End win at d3 with an unlimited budget")
{
    LoadMazeCards();
    GameState s = MBase();
    s.players[0].lands_played_this_turn = 0;
    for (int i = 0; i < 9; ++i) { MPut(s, kGuildgates[i], 800 + i); }
    MPut(s, "Maze's End", 810);
    s.players[0].library.push_back(MCard("Azorius Guildgate", 820));
    s.players[0].library.push_back(MCard("Simic Guildgate", 821));
    for (int k = 0; k < 12; ++k) { s.players[0].library.push_back(MCard("Forest", 830 + k)); }
    static const MazesEndProvider kMazeProv;
    s.m_provider = &kMazeProv;
    AIEngine eng(MulliganProfile::DefaultProfile(), /*depth=*/3, /*budget_ms=*/0);
    GameEngine ge(eng);
    const int win = ge.PlayOutFrom(s, /*max_turns=*/6, GameEngine::ResumeAt::Main1);
    CHECK(win == 5);
    CHECK(s.won_by_effect);
    CHECK(CountDistinctNamedGates(s, 0) >= 10);
}

TEST_CASE("Maze's End I1: Planar Nexus shares a land type with any Gate, never with a basic")
{
    LoadMazeCards();
    const CardDefinition* nexus = &MDef("Planar Nexus");
    CHECK(LandDefsShareLandType(nexus, &MDef("Azorius Guildgate")));
    CHECK(LandDefsShareLandType(&MDef("Azorius Guildgate"), nexus));
    CHECK(LandDefsShareLandType(nexus, nexus));
    CHECK_FALSE(LandDefsShareLandType(nexus, &MDef("Forest")));
    CHECK_FALSE(LandDefsShareLandType(&MDef("Forest"), nexus));
    CHECK_FALSE(LandDefsShareLandType(&MDef("Forest"), &MDef("Azorius Guildgate")));
}

TEST_CASE("Maze's End I1: Archdruid's Charm mode 3 exiles its pinned artifact/enchantment and searches nothing")
{
    LoadMazeCards();
    GameState s = MBase();
    MPut(s, "Guild Summit", 900);
    MPut(s, "Forest", 901);
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(MCard("Forest", 910 + k)); }
    const CardParams& pp = MDef("Archdruid's Charm").params;
    REQUIRE(pp.modal_exile_target_artifact_enchantment);
    CHECK(ModalExileArtEnchPinTarget(ModalExileArtEnchPin(900)) == 900);
    CHECK(ModalExileArtEnchPinTarget("Forest") == 0);
    // A land is not a legal target: the pin on it does nothing (and does not search).
    GameState t = s;
    PerformLandTutorToBattlefield(t, 0, pp, ModalExileArtEnchPin(901));
    CHECK(MFind(t, 901) != nullptr);
    CHECK(t.players[0].library.size() == 5);
    // The enchantment is exiled; the library is untouched (no search, no shuffle).
    GameState u = s;
    const auto lib_before = u.players[0].library;
    PerformLandTutorToBattlefield(u, 0, pp, ModalExileArtEnchPin(900));
    CHECK(MFind(u, 900) == nullptr);
    CHECK(u.players[0].library.size() == 5);
    for (std::size_t i = 0; i < lib_before.size(); ++i)
    { CHECK(u.players[0].library[i].m_number == lib_before[i].m_number); }
    // Mode 1 (a land name) still finds the land and puts it TAPPED.
    GameState v = s;
    PerformLandTutorToBattlefield(v, 0, pp, "Forest");
    CHECK(v.players[0].library.size() == 4);
    int tapped_forests = 0;
    for (const Permanent& q : v.battlefield) { if (q.card.m_name.str() == "Forest" && q.tapped) { ++tapped_forests; } }
    CHECK(tapped_forests == 1);
    // The deck provider withholds mode 3 autonomously; the base provider offers it.
    CHECK_FALSE(MazesEndProvider().OffersModalExileArtEnch(s, MDef("Archdruid's Charm")));
    CHECK(ShadowbornProvider().OffersModalExileArtEnch(s, MDef("Archdruid's Charm")));   // a DeckProvider without the override
}
