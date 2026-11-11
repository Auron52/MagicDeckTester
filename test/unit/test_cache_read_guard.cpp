// The plan caches' READ GUARD, by the predicate that was read (MTG_CACHE_READ_GUARD_KIND; cachecharge in
// TurnSolver.cpp). Hydras P4 2026-11-11: a breakpoint-continuation derivation's only budget read is the PRICED
// ceiling, but the guard refused its stored list whenever the decision budget was merely EXHAUSTED, so the rest of
// the pass re-derived every list it had already derived (gi 1979: 24.9M of 29.5M odometer visits). Pinned here:
//   * a priced-only reader is served with the decision budget exhausted, and refused once the ceiling trips;
//   * a computation that also read a SearchBudget keeps the broad guard (refused while the budget is exhausted);
//   * the real caches (CacheHitCharge) and the shadow (pledger::WouldServe) always give the SAME verdict;
//   * the per-job arm `read_guard_kind` = 0 restores the broad guard.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"

TEST_CASE("read guard: a priced-only reader is served while only the decision budget is exhausted")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/false, /*kind=*/1);
    CHECK(r.stored);
    CHECK(r.budget_read);
    CHECK_FALSE(r.budget_read_sb);
    CHECK(r.real_exhausted);
    CHECK(r.shadow_exhausted);
    CHECK_FALSE(r.real_priced);      // the ceiling it read has tripped: a recompute would be interrupted
    CHECK_FALSE(r.shadow_priced);
}

TEST_CASE("read guard: a SearchBudget reader keeps the broad guard")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/true, /*kind=*/1);
    CHECK(r.stored);
    CHECK(r.budget_read);
    CHECK(r.budget_read_sb);
    CHECK_FALSE(r.real_exhausted);
    CHECK_FALSE(r.shadow_exhausted);
    CHECK_FALSE(r.real_priced);
    CHECK_FALSE(r.shadow_priced);
}

TEST_CASE("read guard: kind=0 restores the broad guard for a priced-only reader")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/false, /*kind=*/0);
    CHECK(r.stored);
    CHECK_FALSE(r.budget_read_sb);
    CHECK_FALSE(r.real_exhausted);
    CHECK_FALSE(r.shadow_exhausted);
    CHECK_FALSE(r.real_priced);
    CHECK_FALSE(r.shadow_priced);
}

TEST_CASE("read guard: the real caches and the shadow agree in every case")
{
    for (int sb = 0; sb < 2; ++sb)
    {
        for (int kind = -1; kind <= 1; ++kind)
        {
            const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(sb != 0, kind);
            CHECK(r.real_exhausted == r.shadow_exhausted);
            CHECK(r.real_priced == r.shadow_priced);
        }
    }
}
