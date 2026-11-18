// Maze's End deck, integration phase I3 (2026-11-17; docs/design/analysis-Maze's End.md): the land-drop engines.
// Exploration (static extra drops; every extra drop SEARCHED through the site-10 extra-drop route), Burgeoning
// (a controller-aware put on the passive opponent's land play; the searched crossing-land pin), Manabond (the
// end-step dump: yes/no searched, a SIMULTANEOUS put judged against the pre-event board, then the discard).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadI3Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false, int controller = 0)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

GameState Base()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

const Permanent* Find(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool InGraveyard(const GameState& s, int num)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == num) { return true; } }
    return false;
}

const char* kGates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                         "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                         "Selesnya Guildgate", "Simic Guildgate" };

// A Maze's End board needing `drops` land drops THIS main phase to win: (10 - drops) distinct Gates already out
// (untapped: the activation's {3}), an Amulet of Vigor (every entering Gate / Maze's End untaps), `explorations`
// copies of Exploration; in hand the (drops - 1) missing Gate names but one, plus Maze's End; the last missing
// name waits in the library for Maze's End's fetch. Only every drop in one main phase wins on turn 5.
GameState MultiDropBoard(int drops, int explorations)
{
    GameState s = Base();
    const int out = 10 - drops;
    for (int i = 0; i < out; ++i) { Put(s, kGates[i], 800 + i); }
    Put(s, "Amulet of Vigor", 850);
    for (int e = 0; e < explorations; ++e) { Put(s, "Exploration", 860 + e); }
    for (int i = out; i < 9; ++i) { s.players[0].hand.push_back(C(kGates[i], 870 + i)); }
    s.players[0].hand.push_back(C("Maze's End", 880));
    s.players[0].library.push_back(C(kGates[9], 890));
    for (int k = 0; k < 12; ++k) { s.players[0].library.push_back(C("Forest", 900 + k)); }
    s.players[0].lands_played_this_turn   = 0;
    s.players[0].bonus_land_drops_this_turn = StaticExtraLandDrops(s, 0);
    return s;
}

int PlayOut(GameState& s)
{
    static const MazesEndProvider kMazeProv;
    s.m_provider = &kMazeProv;
    AIEngine eng(MulliganProfile::DefaultProfile(), /*depth=*/3, /*budget_ms=*/0);
    GameEngine ge(eng);
    return ge.PlayOutFrom(s, /*max_turns=*/7, GameEngine::ResumeAt::Main1);
}
}   // namespace

TEST_CASE("Maze's End I3: Exploration's static drops stack per copy and arrive with a copy entering")
{
    LoadI3Cards();
    GameState s = Base();
    CHECK(StaticExtraLandDrops(s, 0) == 0);
    Put(s, "Exploration", 700);
    Put(s, "Exploration", 701);
    Put(s, "Exploration", 702, false, /*controller=*/1);   // the opponent's copy is not ours
    CHECK(StaticExtraLandDrops(s, 0) == 2);
    CHECK(StaticExtraLandDrops(s, 1) == 1);
    // A copy entering mid-turn adds its drop that turn (the shared enter cascade).
    s.players[0].bonus_land_drops_this_turn = 2;
    Put(s, "Exploration", 703);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(s.players[0].bonus_land_drops_this_turn == 3);
    CHECK(s.players[0].LandDropsAvailable() == 4);
    // Unstamped for a deck without the card: no walk, nothing added.
    GameState t = Base();
    t.deck_has_static_land_drops = false;
    Put(t, "Exploration", 710);
    CHECK(StaticExtraLandDrops(t, 0) == 0);
}

TEST_CASE("Maze's End I3: the extra-drop route arms only on a NEW open drop with a land in hand")
{
    LoadI3Cards();
    GameState s = MultiDropBoard(2, 1);
    const int snap = TurnSolver::LandDropSnapshot(s);
    CHECK_FALSE(TurnSolver::LandDropNewlyOpen(snap, s, false));   // nothing played yet: the plan already offered it
    s.players[0].lands_played_this_turn = 1;                       // the section played a land
    CHECK(TurnSolver::LandDropNewlyOpen(snap, s, false));
    CHECK_FALSE(TurnSolver::LandDropNewlyOpen(snap, s, /*drop_reserved=*/true));
    s.players[0].lands_played_this_turn = 2;                       // every drop used
    CHECK_FALSE(TurnSolver::LandDropNewlyOpen(snap, s, false));
    GameState t = s;
    t.deck_has_static_land_drops = false;                          // every other deck: dead code
    t.players[0].lands_played_this_turn = 1;
    CHECK_FALSE(TurnSolver::LandDropNewlyOpen(TurnSolver::LandDropSnapshot(t), t, false));
}

TEST_CASE("Maze's End I3: the search finds TWO land drops in one main phase (Exploration, d3, unlimited budget)")
{
    LoadI3Cards();
    GameState s = MultiDropBoard(2, 1);
    CHECK(PlayOut(s) == 5);
    CHECK(s.won_by_effect);
    // Control: without Exploration the same board cannot win this turn.
    GameState c = MultiDropBoard(2, 0);
    CHECK(PlayOut(c) > 5);
}

// P5c (2026-11-18): the win is {Maze's End; at the site-9 occurrence play a Gate; at the site-10 occurrence the front
// {Gate, activate Maze's End}}. With the new-name fetch valued in every state (MTG_MAZE_FETCH_DEFER deleted) the Gate
// play sits at rank 3 of the first list and the front of the second is reached only BY PLAN -- the untargeted canon
// withholds its activation -- so it is two deviations: the engine's canon-drop compound (BpWaveWalker::DevSeqCanonDrop)
// is what finds it.
TEST_CASE("Maze's End I3: the search finds THREE land drops in one main phase (two Explorations)")
{
    LoadI3Cards();
    GameState s = MultiDropBoard(3, 2);
    CHECK(PlayOut(s) == 5);
    CHECK(s.won_by_effect);
    GameState c = MultiDropBoard(3, 1);   // one copy short: no turn-5 win
    CHECK(PlayOut(c) > 5);
}

TEST_CASE("Maze's End I3: Burgeoning puts our land on the opponent's land play, judged for US")
{
    LoadI3Cards();
    GameState s = Base();
    Put(s, "Burgeoning", 700);
    s.players[0].hand.push_back(C("Simic Guildgate", 710));
    s.players[0].hand.push_back(C("Forest", 711));
    // Unpinned: rank 0 = the first land in hand; the menu is {Simic, Forest, none}.
    {
        GameState t = s;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 1);
        CHECK(pr.size[0] == 3);
        REQUIRE(Find(t, 710) != nullptr);
        CHECK(Find(t, 710)->controller_index == 0);
        CHECK(Find(t, 710)->tapped);                       // a Guildgate enters tapped
        CHECK(t.players[0].hand.size() == 1);
    }
    // The pin picks Forest (rank 1) or none (rank 2).
    {
        GameState t = s;
        t.scripted_crossing_lands = 1;
        ResolveBurgeoningTriggers(t);
        CHECK(Find(t, 711) != nullptr);
        CHECK(Find(t, 710) == nullptr);
        GameState u = s;
        u.scripted_crossing_lands = 2;
        ResolveBurgeoningTriggers(u);
        CHECK(u.players[0].hand.size() == 2);
    }
    // Our Amulet untaps it; the OPPONENT's Amulet / Gond Gate do not (controller-aware).
    {
        GameState t = s;
        Put(t, "Amulet of Vigor", 720, false, /*controller=*/1);
        Put(t, "Gond Gate", 721, false, /*controller=*/1);
        ResolveBurgeoningTriggers(t);
        CHECK(Find(t, 710)->tapped);
        GameState u = s;
        Put(u, "Amulet of Vigor", 722);
        ResolveBurgeoningTriggers(u);
        CHECK_FALSE(Find(u, 710)->tapped);
        CHECK(u.floating_mana.Total() == 0);   // no float off our main phase
        GameState v = s;
        Put(v, "Gond Gate", 723);
        ResolveBurgeoningTriggers(v);
        CHECK_FALSE(Find(v, 710)->tapped);
    }
    // Two copies: two triggers, two ordinals.
    {
        GameState t = s;
        Put(t, "Burgeoning", 701);
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        ResolveBurgeoningTriggers(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 2);
        CHECK(Find(t, 710) != nullptr);
        CHECK(Find(t, 711) != nullptr);
    }
    // Through the opponent's land play itself.
    {
        GameState t = s;
        t.opponent_plays_lands = true;
        t.players[1].hand.push_back(C("Forest", 730));
        ResolvePassiveOpponentLandTurn(t);
        CHECK(Find(t, 730) != nullptr);
        CHECK(Find(t, 710) != nullptr);
    }
}

TEST_CASE("Maze's End I3: Manabond dumps every land simultaneously (pre-event board), then discards the hand")
{
    LoadI3Cards();
    GameState s = Base();
    s.phase = Phase::Ending;
    Put(s, "Manabond", 700);
    s.players[0].hand.push_back(C("Simic Guildgate", 710));
    s.players[0].hand.push_back(C("Gond Gate", 711));
    s.players[0].hand.push_back(C("Forest", 712));
    s.players[0].hand.push_back(C("Exploration", 713));
    // Unpinned: yes (a land in hand).
    {
        GameState t = s;
        CrossingLandProbe pr;
        t_crossing_land_probe = &pr;
        PerformEndStepManabond(t);
        t_crossing_land_probe = nullptr;
        CHECK(pr.n == 1);
        CHECK(pr.size[0] == 2);
        REQUIRE(Find(t, 710) != nullptr);
        REQUIRE(Find(t, 711) != nullptr);
        REQUIRE(Find(t, 712) != nullptr);
        CHECK(Find(t, 710)->tapped);          // the Gond Gate in the SAME batch untaps nothing (CR 614.12)
        CHECK_FALSE(Find(t, 712)->tapped);
        CHECK(t.players[0].hand.empty());
        CHECK(InGraveyard(t, 713));            // "discard your hand"
    }
    // Rank 1 = no: nothing moves.
    {
        GameState t = s;
        t.scripted_crossing_lands = 1;
        PerformEndStepManabond(t);
        CHECK(t.players[0].hand.size() == 4);
        CHECK(Find(t, 710) == nullptr);
    }
    // A Gond Gate already out untaps the dumped Gate.
    {
        GameState t = s;
        Put(t, "Gond Gate", 720);
        PerformEndStepManabond(t);
        CHECK_FALSE(Find(t, 710)->tapped);
    }
    // No land in hand: the default is no (yes would only discard).
    {
        GameState t = Base();
        Put(t, "Manabond", 700);
        t.players[0].hand.push_back(C("Exploration", 713));
        PerformEndStepManabond(t);
        CHECK(t.players[0].hand.size() == 1);
        t.scripted_crossing_lands = 1;   // the searched other answer: yes
        PerformEndStepManabond(t);
        CHECK(t.players[0].hand.empty());
        CHECK(InGraveyard(t, 713));
    }
}

TEST_CASE("Maze's End I3: the turn crossing fans Manabond's yes/no (the crossing-land digit)")
{
    LoadI3Cards();
    GameState s = Base();
    s.phase = Phase::PostCombatMain;
    Put(s, "Manabond", 700);
    s.players[0].hand.push_back(C("Simic Guildgate", 710));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(C("Forest", 900 + k)); }
    const std::vector<long long> pins = TurnSolver::CrossingLandBoundaryPinsForTest(s);
    REQUIRE(pins.size() == 2);
    CHECK(pins[0] == 0);   // yes (the front)
    CHECK(pins[1] == 1);   // no
    // Every other deck: no digit.
    GameState t = s;
    t.deck_has_crossing_land_put = false;
    CHECK(TurnSolver::CrossingLandBoundaryPinsForTest(t).size() == 1);
}
