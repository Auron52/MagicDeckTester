// Unit test for MTG_LEAF_GAME_OVER (TurnSolver.cpp, the depth-0 frame of FSLineWin): a frame whose game is ALREADY
// WON is not a horizon leaf. SimulateEndAndStartNextTurn leaves the turn counter on the turn that ended when the game
// ended at the boundary (Maze's End's off-main win at our end step / their turn, a deck-out at their draw), so a depth-1
// node at turn T hands its depth-0 child a state already won AT T. The no-leaf constant answered "no win" there, so a
// leafless node at T with depth 1 missed T's end-step win while the same node at depth 2 found it -- the answer
// depended on the remaining depth, and the depth-free in-window proof cache (MTG_INWIN_CACHE) stored the depth-1
// "no win by T" and served it to the depth-2 node. Found by IDG-2, root-caused by DM-2 (logs/maze/DM2-handoff.md):
// Maze's End s3076 gi73 wins T5 at d5 b0 and T6 at d6/d8 b0; MTG_INWIN_CACHE_VERIFY=1 named the store as a LEAFLESS
// T5 node at depth 1 (cutoff 5) read by the leafless T5 node at depth 2.
//
// (This file held DM-1's MTG_INWIN_EXIT_GUARD game test on Martyr s2151 gi149; that game no longer reproduces on
// 8fbbc5023 and the in-process runner played it to T7, so it is replaced by this seam test of the real mechanism.)
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

namespace
{
void EnsureCardsLgo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

constexpr int kMaxTurns = 8;

// Turn 5, the state SimulateEndAndStartNextTurn hands on after Maze's End's off-main win at our end step: the game is
// over (won_by_effect) and the counter still reads 5.
GameState WonAtBoundary()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.won_by_effect       = true;
    return s;
}
}   // namespace

TEST_CASE("Leaf game over: a depth-0 leafless frame whose game is already won answers that win, at every depth")
{
    EnsureCardsLgo();
    const GameState s = WonAtBoundary();
    REQUIRE(OpponentHasLost(s));
    // The fix: the horizon frame reports the realised win at the turn it happened.
    CHECK(TurnSolver::LeaflessFrameWinForTest(s, 0, kMaxTurns, 1) == 5);
    // The defect, pinned so the A/B hatch keeps meaning what it says: the constant leaf scored it "no win".
    CHECK(TurnSolver::LeaflessFrameWinForTest(s, 0, kMaxTurns, 0) == kMaxTurns + 1);
}
