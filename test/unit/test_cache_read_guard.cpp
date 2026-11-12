// The plan caches' READ GUARD, by the predicate that was read (MTG_CACHE_READ_GUARD_KIND; cachecharge in
// TurnSolver.cpp). Hydras P4 2026-11-11: a breakpoint-continuation derivation's only budget read is the PRICED
// ceiling, but the guard refused its stored list whenever the decision budget was merely EXHAUSTED, so the rest of
// the pass re-derived every list it had already derived (gi 1979: 24.9M of 29.5M odometer visits). Pinned here:
//   * the shadow keeps its rule (WouldServe refuses: the virtual world recomputes and counts it);
//   * a priced-only reader is served from the real cache there, and the work it counted is REPLAYED into workctr;
//   * it recomputes instead when the replay would trip the priced ceiling, or once the ceiling has tripped;
//   * a SearchBudget reader never replays; the per-job arm read_guard_kind = 0 restores the plain guard.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "core/WorkCounters.h"

TEST_CASE("read guard: a priced-only reader is served by replay while only the decision budget is exhausted")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/false, /*kind=*/1);
    CHECK(r.stored);
    CHECK(r.budget_read);
    CHECK_FALSE(r.budget_read_sb);
    CHECK(r.work_odo == 5);
    CHECK(r.real_exhausted);
    CHECK_FALSE(r.shadow_exhausted);          // the virtual world recomputes ...
    CHECK(r.shadow_replay_exhausted);         // ... and the real cache stands in for it
    CHECK(r.replayed_odo == 5);               // counting what the recompute counts
    CHECK(r.replayed_priced == 5 * workctr::kPriceNs[workctr::kOdoVisit]);
    CHECK_FALSE(r.real_headroom);             // a replay that would trip the ceiling recomputes instead
    CHECK_FALSE(r.shadow_replay_headroom);
    CHECK_FALSE(r.real_priced);               // the ceiling it read has tripped
    CHECK_FALSE(r.shadow_priced);
    CHECK_FALSE(r.shadow_replay_priced);
}

TEST_CASE("read guard: the ledger's replay counts the SHADOW's work, whether or not the real entry is resident")
{
    // The defect this pins (Hydras P4): replay used to be decided and counted from the REAL entry, so a lookup the
    // real cache had evicted recomputed and counted the recompute's (smaller, warm) work instead -- units followed
    // residency. Now the shadow decides and counts; the real entry (which counted 3 visits, not 5) only stands in.
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/false, /*kind=*/1);
    REQUIRE(r.shadow_replay_exhausted);
    CHECK(r.standin_served);                  // a resident real entry is served without its own guard or count
    CHECK(r.led_present_odo == 5);            // resident: the shadow's 5, not the real entry's 3
    CHECK(r.led_absent_odo == 5);             // absent: the muted recompute's 3 are not counted either
    CHECK(r.led_present_odo == r.led_absent_odo);
    CHECK_FALSE(r.led_hit_exhausted);         // a FREE ledger hit keeps the plain guard (the shadow refused it)
    CHECK(r.nest_odo == 5);                   // an enclosing computation inherits the shadow's work, not the real 3
}

TEST_CASE("read guard: a SearchBudget reader keeps the plain guard")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/true, /*kind=*/1);
    CHECK(r.stored);
    CHECK(r.budget_read_sb);
    CHECK_FALSE(r.real_exhausted);
    CHECK_FALSE(r.shadow_exhausted);
    CHECK_FALSE(r.shadow_replay_exhausted);
    CHECK(r.replayed_odo == 0);
    CHECK_FALSE(r.real_priced);
    CHECK_FALSE(r.shadow_replay_priced);
}

TEST_CASE("read guard: kind=0 restores the plain guard for a priced-only reader")
{
    const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(/*searchbudget_read=*/false, /*kind=*/0);
    CHECK(r.stored);
    CHECK_FALSE(r.budget_read_sb);
    CHECK_FALSE(r.real_exhausted);
    CHECK_FALSE(r.shadow_exhausted);
    CHECK_FALSE(r.shadow_replay_exhausted);
    CHECK(r.replayed_odo == 0);
}

TEST_CASE("read guard: the real caches serve exactly where the shadow serves or stands in")
{
    for (int sb = 0; sb < 2; ++sb)
    {
        for (int kind = -1; kind <= 1; ++kind)
        {
            const TurnSolver::ReadGuardProbe r = TurnSolver::ReadGuardProbeForTest(sb != 0, kind);
            CHECK(r.real_exhausted == (r.shadow_exhausted || r.shadow_replay_exhausted));
            CHECK(r.real_priced == (r.shadow_priced || r.shadow_replay_priced));
        }
    }
}

// Hydras P7 2026-11-12: a breakpoint derivation advances the CALLER's scripted pins (dredge / Keranos / sac cursors)
// through the candidates it applies, and a cache hit advances nothing -- so a muted recompute standing in for a virtual
// hit consumed pins the hit would have left (Gods regression gi 4: the next derivation lost a continuation whenever
// the byte pool had dropped the entry). The mute restores them; a counted computation still consumes them.
TEST_CASE("priced ledger: a muted recompute leaves the caller's scripted pins where it found them")
{
    const TurnSolver::MutePinProbe r = TurnSolver::MutePinRestoreProbeForTest();
    CHECK(r.dredge == 2);
    CHECK(r.keranos == 1);
    CHECK(r.sac == 3);
    CHECK(r.dredge_counted == 6);
}
