# Pull a blocked cell's remaining rollouts in and run them in parallel

**Status: PROPOSED (user-directed 2026-10-04). Not implemented.**

> *"In the ideal design we would eventually pull in the blocked games and run them in parallel, so
> that is another change I would like to make going forward."* — USER
>
> *"I'm sure, sooner or later, there are going to be 1 or 2 degenerate cells that need this
> change."* — USER

## 1. The defect: a cell-side's rollouts are indivisible

The keep generator's unit of dispatch is a **range**, not a rollout:

```cpp
// ExhaustiveKeep.cpp
struct Task { int w; int pd; long long r0; long long r1; };
run_batch(ai, t.w, t.pd, t.r0, t.r1);     // :2642   one worker takes the whole range
  for (long long r = r0; r < r1; ++r)     // :2431   ...and runs it SERIALLY
```

`r_batch` is **16**, so one task can be sixteen rollouts of one cell-side, executed back to back on
a single thread. No other worker can help, however idle it is.

On 2026-10-04 that cost 14.8 hours of a 24-core box: a single rollout
(`size6 draw r=20`, 22.97 h) held the floor-completion barrier, and `frozen` sat at 0/343,538
throughout. **With five such rollouts in one batch it would have been ~100 h, serial, on one core.**

## 2. Why splitting is sound — by construction, not by measurement

A rollout is a **pure function of `(seed_base, r, w, pd)`**. The engine already relies on this
everywhere: resume accepts an arbitrary per-cell `n` because cells are prefixes of one sequence,
and cross-machine pooling sums unequal counts from disjoint seed bases.

So the `r0..r1` range has **no internal data dependency**. It is batched purely to amortise queue
overhead. Any partition of the range across any number of workers computes the same set of
rollout values.

That is the whole safety argument, and it is structural. Contrast the wave barrier
(`keepgen-producer-barrier-and-durability.md` Defect 3), which is a *race guard* that has to be
reasoned about; this one needs no guard at all.

## 3. What it fixes, and what it does NOT

Three changes are needed and none subsumes the others. Keeping them distinct matters, because the
2026-10-04 incident was wrongly attributable to any one of them in isolation:

| change | fixes | leaves |
|---|---|---|
| the board-scan fixes (`ae97edbf`, 283x) | makes degenerate rollouts cheap | a rollout that is *still* slow |
| Defect 3, per-cell continuous refinement | one cell blocking **the whole table** | that cell still runs serially |
| **this document** | a cell's rollouts running **serially**: 5x20 h becomes 20 h, not 100 h | a **single** 20 h rollout |

**Be explicit about the last cell of that table.** Splitting is a makespan fix. It divides a
congregated batch by the number of helpers; it cannot make one rollout finish sooner. A lone
monster still takes its full wall time, and only the engine fix or Defect 3 keeps that from
mattering.

## 4. Proposed mechanism: a shared cursor, consumed backfill-only

Rather than stealing whole tasks, make an in-flight range **co-consumable**:

* A dispatched range publishes `std::atomic<long long> next_r` initialised to `r0`.
* A worker claims rollouts with `next_r.fetch_add(1)` and stops at `r1`. The owning worker does
  this too, so the common case is unchanged in behaviour and one atomic per rollout in cost.
* An **idle** worker may attach to any open range and start claiming from it.

**Attach only as backfill.** A worker should prefer new queue work and attach to an open range only
when the queue is (near) empty — otherwise 24 workers pile onto one cell-side, lose their per-worker
`AIEngine` locality, and starve the rest of the table. *This interlock already exists in the
codebase*: the precompute filler (kind `-1`) declines to push unless the queue is under a quarter
full. Reuse that rule rather than inventing a second one.

## 5. Determinism: fold in `r` order, and it composes with durable precompute

`run_batch` accumulates `sum` / `sumsq` / `cnt` for the cell-side and folds once. Summing doubles
in a different order changes the last bits, so a naive split makes the raw non-reproducible.

`keepgen-reproducibility-is-not-the-requirement.md` argues that is acceptable for ordinary profile
generation — but it is **avoidable here for free**, and should be avoided:

* Have each claimed rollout write its value into a per-`r` slot for the range.
* Fold the slots **in `r` order** when the range completes.

Then the result is bit-identical no matter how the range was partitioned, and the split costs no
reproducibility at all. This is the same per-rollout storage
`keepgen-durable-precompute.md` proposes to persist, so the two changes share one structure —
build them together, not separately.

**The one thing that must stay pinned regardless:** `refs` / `vg_roll`. The freeze threshold is
re-derived as the frontier advances, and a different trajectory *moves freeze verdicts* — measured
on burn, pinning it removed 98% of the cells that resumed under-sampled (539 → 9). Splitting must
not be allowed to perturb that; see §3 of the reproducibility doc.

## 6. Why this will recur — the sampler is cost-blind

> *"I'm sure, sooner or later, there are going to be 1 or 2 degenerate cells that need this
> change."*

This is not bad luck, it is a feedback loop. Adaptive sub-refinement selects the next wave's cells
by **uncertainty (`se`), never by cost**. A degenerate cell plays long, scattered games, so it
tends to have high `se` — which is exactly the signal that earns a cell *more* rollouts. The
sampler can therefore preferentially pour budget into the most expensive cell in the table.

Observed distribution on the candidate-b run (711 slow rollouts, ~6.2 M total): slowness clusters
only weakly — 40 of 662 cell-sides had more than one slow rollout, max 6 — and the *monsters* did
not cluster at all (exactly one over 1 h; the next is 0.40 h, a 57x gap). So today the congregated
case is rare. Nothing in the design keeps it rare.

**A cheap partial mitigation worth building alongside:** once a cell-side produces one slow
rollout, drop its `r_batch` to 1 for the remainder. That converts a would-be congregated batch into
individually schedulable rollouts without needing the cursor at all, and it uses information the
generator already has (`capture_slow` fires per rollout).

## 7. Cancellation is REQUIRED, not optional — and the primitive already exists

> *"that design would require that we allow cancelling of in-progress games that are not necessary.
> So if the cell doesn't require them (say the cell froze) the final ones should be dropped."*
> *"I believe the value-leaf already uses cancellation, so it should be possible."* — USER

**Correct, and it is the part that makes §4 safe rather than merely fast.** Parallel pull-in
*creates* the over-sampling it then has to cancel: once idle workers are claiming `r` ahead of the
owner, a cell that freezes at `r=18` may already have `r=19..31` in flight. Without cancellation,
splitting converts a serial waste into a parallel one and can push a cell past the point its own
stopping rule chose.

**The primitive exists and the user is right that the value leaf uses it:**

| piece | where |
|---|---|
| `inline thread_local bool t_abandoned` | `ai/GameWorkMeter.h:55` |
| prompt unwind — the recursion polls it | `SearchBudget::Overrun()`, `ai/SearchBudget.h:83` |
| the game yields **no result at all** | `GameEngine.cpp:110,137` return `kAbandoned` |

**And it already solved this design's hardest problem.** `GameWorkMeter.h`'s own header explains
why abandonment is keyed on **units rather than wall clock**: *"Keyed on wall time, the same game
would be abandoned [differently per machine] ... In units, the set of abandoned games is a
deterministic [set, and the] abandoned game[s] become a SKIP LIST the whole table can share."*
Copy that discipline exactly — **never cancel on a wall-clock or arrival-order condition**, or the
cell's final `n` becomes a race and the run stops being reproducible for a reason §5 took care to
avoid.

### The rule that keeps cancellation deterministic

Do not let "did this rollout finish before the cancel landed?" decide what is folded. Instead:

1. The freeze rule determines the cell's final `n` (as it does today).
2. Fold **exactly** `r = 0 .. n-1`, in `r` order (§5).
3. Discard every `r >= n` **regardless of whether it completed**, and signal cancellation to any
   still running so the core is released.

Then the result is identical whether a surplus rollout finished, was half-done, or never started —
and splitting plus cancellation together remain bit-identical to the unsplit run.

### Two hazards to carry

* **A cancelled rollout must contribute NOTHING — not a zero.** This exact bug has shipped here
  before: `keepgen-durable-precompute.md` records 905 cell-sides left with a count-inconsistent sum
  from *"summed zeros … fabricated prefix."* A partial rollout has no value; folding one as 0 is a
  silent data-quality corruption, not a rounding error.
* **Abandonment discards the work already done.** `GameWorkMeter.h:84` records the cost bluntly:
  *"abandoned and its work discarded, so the 4.26 h game produced nothing at all."* Cancelling a
  20 h rollout at hour 19 burns 19 h. That argues for evaluating the freeze test as early as the
  rule allows, and for cancelling the **highest** `r` first (least likely to be needed).

### What cancellation buys on its own

It is worth noting separately from splitting: **cancellation alone shortens a straggler's hold.**
In the refine phase, a cell that freezes while a monster is in flight can release that core
immediately instead of waiting hours for a value it will discard. It does not help in the floor
phase of the 2026-10-04 incident, because there the cell *could not* freeze — refs were not fixed,
which is Defect 3 — but that is an argument for building all three, not for ranking them.

## 8. Verification

* The existing control-vs-SIGKILLed-twin harness (`keepgen-resume-exactness.md`) should still assert
  `cmp` on the final raw: under §5's ordered fold, splitting is bit-identical, so this check stays
  as-is and becomes the regression test for the fold order.
* Add a makespan case: force a cell-side to contain N artificially slow rollouts, assert wall
  scales as `N / workers` rather than `N`.
* Assert the backfill interlock: with a full queue, no worker attaches to an open range.
* **Cancellation, the decisive test:** run a cell-side split across N workers and force a freeze at
  `r = n` while surplus rollouts are mid-flight. Assert (a) the raw is **bit-identical** to the
  unsplit, uncancelled control, (b) `cnt == n` exactly — no zero-valued contribution from a dropped
  rollout, which is the 905-cell-side failure mode, and (c) the surplus workers return to the queue
  promptly rather than running to completion.
* Assert cancellation never fires on a wall-clock or arrival-order condition — the `GameWorkMeter`
  units discipline. A test that passes only on a quiet box is not a test of this.

Related: `keepgen-producer-barrier-and-durability.md` (Defect 3 — the barrier this is distinct
from), `keepgen-durable-precompute.md` (shares the per-rollout slot structure),
`keepgen-reproducibility-is-not-the-requirement.md` (§3's pinned threshold),
`mulligan-rollout-performance-floor.md` (the 22.97 h straggler that motivated this).
