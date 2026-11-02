// MTG_DEV_SEQ_TAIL_SKIP -- MTG_BP_DEV_SEQ's second pass skips a dirty node's tail when re-walking it cannot change
// anything within the second pass's window (docs/design/bp-empty-arm-deferred-site.md 6.1, "Cost fold"). The first
// pass records a complete tail's cutoff, answer and earliest latent turn; the second pass, under cutoff `cut`, skips
// it only when ALL of: its cutoff is no looser than the first pass's, the first pass's answer is later than `cut`,
// and no latent occurrence under it is at or before `cut` (a compound line there could win by `cut`).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "core/GameState.h"

#include <climits>
#include <cstdint>

namespace
{
struct TailSkipScope
{
    explicit TailSkipScope(int on) { heurarm::t_arm[heurarm::BP_DEV_SEQ_TAIL_SKIP] = static_cast<std::int8_t>(on); }
    ~TailSkipScope() { heurarm::t_arm[heurarm::BP_DEV_SEQ_TAIL_SKIP] = -1; }
};
}

TEST_CASE("dev-seq tail skip: the second pass skips only a tail that cannot win within its cutoff")
{
    GameState s;
    s.turn_number = 3;
    TailSkipScope on(1);
    // First pass: cutoff 6, best line turn 6, no latent occurrence under the tail.
    CHECK(TurnSolver::DevSeqTailSkipForTest(s, 6, 6, INT_MAX, 5));         // tighter cutoff, answer past it
    CHECK(TurnSolver::DevSeqTailSkipForTest(s, 6, 6, INT_MAX, 4));
    CHECK_FALSE(TurnSolver::DevSeqTailSkipForTest(s, 6, 6, INT_MAX, 6));   // the answer is within the cutoff
    CHECK_FALSE(TurnSolver::DevSeqTailSkipForTest(s, 4, 9, INT_MAX, 5));   // looser than the first pass searched
    // A no-win answer (past max turns) under cutoff 5 answers every cutoff <= 5.
    CHECK(TurnSolver::DevSeqTailSkipForTest(s, 5, 99, INT_MAX, 5));
    // A latent occurrence under the tail: skippable only when it lies past the cutoff.
    CHECK(TurnSolver::DevSeqTailSkipForTest(s, 6, 99, 6, 5));              // compound lines start on turn 6 > 5
    CHECK_FALSE(TurnSolver::DevSeqTailSkipForTest(s, 6, 99, 5, 5));        // ...on turn 5: walk it
    CHECK_FALSE(TurnSolver::DevSeqTailSkipForTest(s, 6, 99, 3, 5));
}

TEST_CASE("dev-seq tail skip: =0 never skips")
{
    GameState s;
    s.turn_number = 3;
    TailSkipScope off(0);
    CHECK_FALSE(TurnSolver::DevSeqTailSkipForTest(s, 6, 99, INT_MAX, 5));
}
