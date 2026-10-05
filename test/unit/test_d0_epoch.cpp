// A depth-0 turn is a decision: constructing the D0Runner permit advances TurnSolver::DecisionEpoch,
// so every per-decision cache (the equip route's verdict memo + transposition set, the breakpoint
// enumeration memo) is scoped to one depth-0 turn instead of persisting across a whole depth-0 game
// and across every depth-0 game a thread plays in a row (the lookahead's bottoming trial games, the
// `--depth 0` rows, the analyzer's card-score games). Before 2026-10-05 the epoch advanced only under
// a budgeted Solve or the committed-decision driver, and a memoised equip-route LETHAL from one trial
// game was replayed on another game's board ("APPLY replay found no kill on a board the trial
// proved"), with the silent mirror image -- a stale "no" suppressing a kill -- never reported at all.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"

TEST_CASE("a D0Runner permit advances the decision epoch; a HorizonLeaf permit does not")
{
    const std::uint64_t e0 = TurnSolver::DecisionEpoch();
    {
        TurnSolver::GreedyPermit d0(TurnSolver::GreedySite::D0Runner, 0);
        CHECK(TurnSolver::DecisionEpoch() == e0 + 1);
        CHECK(TurnSolver::InD0Runner());
        {
            // Nested depth-0 turns (a trial game inside a lookahead) each open their own scope.
            TurnSolver::GreedyPermit inner(TurnSolver::GreedySite::D0Runner, 0);
            CHECK(TurnSolver::DecisionEpoch() == e0 + 2);
        }
    }
    CHECK_FALSE(TurnSolver::InD0Runner());
    const std::uint64_t e1 = TurnSolver::DecisionEpoch();
    {
        // The horizon leaf sits INSIDE a budgeted decision; it must not split that decision's caches.
        TurnSolver::GreedyPermit leaf(TurnSolver::GreedySite::HorizonLeaf, 0);
        CHECK(TurnSolver::DecisionEpoch() == e1);
    }
}
