// Maze's End deck, P7d (2026-11-19): the engine's LAST-PLY NO-WIN FOLD (TurnSolver.cpp lastply, MTG_LASTPLY_NOWIN_FOLD)
// and the Maze's End WINLESS CERTIFICATE (MazesEndProvider::ProvenWinlessThisTurn / MazeWinlessBound,
// MTG_MAZE_WINLESS_CERT, both DEFAULT ON). A full-line node with
// one ply left and a constant next leaf answers no-win when the provider proves this turn cannot reach ten Gate
// names. The proof must be SOUND: (1) the bound's arithmetic on hand-built positions, at and either side of the
// threshold; (2) the leafless search answers the same win turn with the fold on and off, and the fold fires.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
void LoadP7dCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card C(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

const char* const kGuildgates[10] = { "Azorius Guildgate", "Rakdos Guildgate", "Selesnya Guildgate", "Simic Guildgate",
                                      "Gruul Guildgate",   "Golgari Guildgate", "Dimir Guildgate",   "Orzhov Guildgate",
                                      "Boros Guildgate",   "Izzet Guildgate" };

// Turn 4, main 1, no land played: the first `g` Guildgates (untapped) and, if `maze`, an untapped Maze's End in play.
// `deep`: the other Guildgates are in the library, so the NAMES bound (N) reads exactly ten names and never proves --
// the cases below then exercise (P), (C) and (S). The identity searches pass false and build their own library.
GameState Board(const DecisionProvider& prov, int g, bool maze, bool deep = true)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.players[0].lands_played_this_turn = 0;
    for (int i = 0; i < g; ++i) { Put(s, kGuildgates[i], 1 + i); }
    if (maze) { Put(s, "Maze's End", 20); }
    if (deep) { for (int i = g; i < 10; ++i) { s.players[0].library.push_back(C(kGuildgates[i], 100 + i)); } }
    return s;
}
void Hand(GameState& s, const char* name, int number) { s.players[0].hand.push_back(C(name, number)); }
void Lib(GameState& s, const char* name, int number) { s.players[0].library.push_back(C(name, number)); }

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};
}   // namespace

TEST_CASE("Maze's End P7d: the last-ply proof's bound, at and either side of ten Gate names")
{
    LoadP7dCards();
    const MazesEndProvider prov;
    {
        // 7 names + Maze's End + one drop + a new Gate: play it (8), fetch (9) -- cannot win.
        GameState s = Board(prov, 7, true);
        Hand(s, "Boros Guildgate", 30);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
        // A second new Gate in hand does not help (one drop) ...
        Hand(s, "Izzet Guildgate", 31);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
        // ... an Exploration does: drop, drop, fetch = 10.
        Hand(s, "Exploration", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
    }
    {
        // 8 names + Maze's End + a drop and a new Gate: 9 + fetch = 10 -- no proof.
        GameState s = Board(prov, 8, true);
        Hand(s, "Boros Guildgate", 30);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
        // The drop already spent: only the fetch is left (9).
        s.players[0].lands_played_this_turn = 1;
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
        // Archdruid's Charm puts a Gate without a drop.
        Hand(s, "Archdruid's Charm", 31);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
    }
    {
        // 8 names, no Maze's End anywhere in play, one drop: playing Maze's End spends the drop and it enters tapped.
        GameState s = Board(prov, 8, false);
        Hand(s, "Maze's End", 30);
        Hand(s, "Boros Guildgate", 31);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);   // (C): the Gate names in hand (1) + no fetch without an Amulet
        // With an Amulet of Vigor the replayed Maze's End untaps: drop it, fetch -- still one entry from a drop (9) ...
        Put(s, "Amulet of Vigor", 40);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
        // ... but with two drops: Boros (9) + Maze's End, fetch (10).
        Hand(s, "Exploration", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
    }
    {
        // Talon Gates of Madara's {4} put needs no drop -- in hand, or tutored by a Sylvan Scrying.
        GameState s = Board(prov, 7, true);
        Hand(s, "Boros Guildgate", 30);
        Hand(s, "Sylvan Scrying", 31);
        Lib(s, "Talon Gates of Madara", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);   // Boros (8), Scrying -> Talon, put (9), fetch (10)
        GameState t = Board(prov, 7, true);
        Hand(t, "Boros Guildgate", 30);
        Hand(t, "Sylvan Scrying", 31);
        Lib(t, "Izzet Guildgate", 33);
        CHECK(MazesEndProvider::MazeWinlessBound(t) == 1);    // Scrying finds a Gate, but it needs the drop already spent on Boros
    }
    {
        // Burgeoning: one put per copy on the opponent's turn (the off-main window).
        GameState s = Board(prov, 7, true);
        Hand(s, "Boros Guildgate", 30);
        Hand(s, "Izzet Guildgate", 31);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
        Put(s, "Burgeoning", 40);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
    }
    {
        // Manabond dumps every land in hand at our end step: only the cards bound holds.
        GameState s = Board(prov, 6, true);
        Hand(s, "Manabond", 30);
        Hand(s, "Boros Guildgate", 31);
        Hand(s, "Izzet Guildgate", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);    // 6 + 2 names in hand + fetch = 9
        Hand(s, "Orzhov Guildgate", 33);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);   // 6 + 3 + fetch = 10
        s.players[0].hand.pop_back();
        Hand(s, "Boros Guildgate", 34);           // a duplicate name adds nothing
        Hand(s, "Golgari Guildgate", 35);         // nor does a name already controlled
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 1);
    }
    {
        // Guild Summit (S): count the entry sources in hand, play and library, however the draws fall.
        GameState s = Board(prov, 5, true);
        Hand(s, "Guild Summit", 30);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 3);    // 5 + fetch + one drop = 7
        Lib(s, "Exploration", 31);
        Lib(s, "Exploration", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 3);    // + two Explorations (drawn, cast) = 9
        Lib(s, "Archdruid's Charm", 33);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -1);   // + the Charm = 10: no proof
        GameState t = Board(prov, 5, true);
        Put(t, "Guild Summit", 30);
        Put(t, "Burgeoning", 31);
        Lib(t, "Burgeoning", 32);
        CHECK(MazesEndProvider::MazeWinlessBound(t) == 3);    // 5 + fetch + drop + two Burgeoning puts = 9
        Lib(t, "Talon Gates of Madara", 33);
        CHECK(MazesEndProvider::MazeWinlessBound(t) == -1);   // + the Talon put = 10
        // A Manabond anywhere (here the library) dumps the hand with no source: give up.
        GameState u = Board(prov, 1, false);
        Put(u, "Guild Summit", 30);
        Lib(u, "Manabond", 31);
        CHECK(MazesEndProvider::MazeWinlessBound(u) == -1);
    }
    {
        // (N) NAMES: whatever Summit / Manabond do, a Gate name that is in neither hand nor library cannot enter.
        GameState s = Board(prov, 6, true, false);
        Put(s, "Guild Summit", 30);
        Put(s, "Manabond", 31);
        Lib(s, "Boros Guildgate", 32);
        Lib(s, "Izzet Guildgate", 33);
        Hand(s, "Citadel Gate", 34);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 2);    // 6 + 3 names = 9
        Lib(s, "Sea Gate", 35);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -1);   // 10 names reachable; Summit + Manabond: give up
    }
    {
        // In play a hand / library Card instance carries NO type bits (Card::IsLand() is false there): the bound
        // must read the definition. With the instance test, every name below was invisible and (N) "proved" 6 < 10.
        GameState s = Board(prov, 6, true, false);
        Put(s, "Guild Summit", 30);
        Put(s, "Manabond", 31);
        const char* const lib[4] = { "Boros Guildgate", "Izzet Guildgate", "Sea Gate", "Citadel Gate" };
        for (int k = 0; k < 4; ++k) { Card c = C(lib[k], 40 + k); c.m_type_mask = 0; s.players[0].library.push_back(c); }
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -1);   // 6 + 4 names = 10: no proof
        Card h = C("Selesnya Guildgate", 50); h.m_type_mask = 0; s.players[0].hand.push_back(h);
        s.players[0].library.pop_back();
        s.players[0].library.pop_back();
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 2);    // 6 + Boros + Izzet + Selesnya = 9: the names bound proves
    }
    {
        // Already ten names: never a proof (an activation wins).
        GameState s = Board(prov, 10, true);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == 0);
    }
    {
        // Two Maze's Ends in play are two fetches: 7 + Boros + fetch + fetch = 10.
        GameState s = Board(prov, 7, true);
        Put(s, "Maze's End", 21);
        Hand(s, "Boros Guildgate", 30);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -2);
    }
    {
        // A card outside the reviewed list: no proof.
        GameState s = Board(prov, 1, false);
        Hand(s, "Llanowar Elves", 30);
        CHECK(MazesEndProvider::MazeWinlessBound(s) == -3);
    }
    {
        // The certificate: the bound's proof, for the active player only, and the lever.
        GameState s = Board(prov, 1, false);
        CHECK(prov.ProvenWinlessThisTurn(s, 0));
        CHECK_FALSE(prov.ProvenWinlessThisTurn(s, 1));
        const Arm off(heurarm::MAZE_WINLESS_CERT, 0);
        CHECK_FALSE(prov.ProvenWinlessThisTurn(s, 0));
    }
}

TEST_CASE("Maze's End P7d: the fold is an identity fold -- the leafless search answers the same win turn")
{
    LoadP7dCards();
    const MazesEndProvider prov;
    const int max_turns = 10;
    {
        // A win next turn: T4 play Izzet (8 names), T5 play Boros (9) and fetch the tenth. Lines that spend T4
        // otherwise leave T5 provably short (e.g. 7 names + one drop + the fetch), and the fold answers those.
        GameState s = Board(prov, 7, true, false);
        Hand(s, "Izzet Guildgate", 30);
        Hand(s, "Boros Guildgate", 31);
        for (int k = 0; k < 6; ++k) { Lib(s, k % 2 ? "Citadel Gate" : "Orzhov Guildgate", 40 + k); }
        const TurnSolver::LastPlyFoldProbe r = TurnSolver::LastPlyFoldProbeForTest(s, 2, max_turns, true);
        CHECK(r.win_off == 5);
        CHECK(r.win_on == r.win_off);
        // No fold count here: once the first T4 line wins at T5, every sibling's T5 node is cut by the incumbent
        // (turn_number > cutoff) before it reaches the fold, so whether any node folds depends on move order.
        // The identity is the property; the block below checks that the fold fires and saves leaves.
    }
    {
        // No win within the pass: both arms refute, the fold saves leaves.
        GameState s = Board(prov, 3, true, false);
        Hand(s, "Izzet Guildgate", 30);
        Hand(s, "Boros Guildgate", 31);
        for (int k = 0; k < 6; ++k) { Lib(s, k % 2 ? "Citadel Gate" : "Orzhov Guildgate", 40 + k); }
        const TurnSolver::LastPlyFoldProbe r = TurnSolver::LastPlyFoldProbeForTest(s, 2, max_turns, true);
        CHECK(r.win_off == max_turns + 1);
        CHECK(r.win_on == r.win_off);
        CHECK(r.folded >= 1);
        CHECK(r.leaves_on < r.leaves_off);
    }
}
