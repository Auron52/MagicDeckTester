# Live progress reporting for exhaustive keep/bottom generation

Status: **deferred / not implemented** (written 2026-07-03 during the Knights R=100 grind, which
cannot rebuild — Rule 0 commit-freeze). Implement on a commit boundary *between* deck grinds.

## Problem

`RunExhaustiveKeep()` ([src/analyzer/ExhaustiveKeep.cpp](../../src/analyzer/ExhaustiveKeep.cpp))
prints the bucket layout + a `rollouts/hand R=… depth=… horizon=…` header, then goes **completely
silent for the entire rollout phase** — which is hours for a real deck — and only emits the report
at completion. There is no way to gauge how far a chunk is or compute an ETA while it runs.

Observed on Knights: an R=5 chunk ran **>4h** with the log frozen at 16 lines the whole time, so we
could not tell whether it was 30% or 95% done, and could only *floor* the R=100 projection instead
of measuring it.

## What already exists

The parallel evaluator work-steals over `work` items via
`std::atomic<int> next{0}` (~line 218); each worker does `int w = next.fetch_add(1)` (~line 226).
So a monotonic "items claimed" counter and the total (`work.size()`) are already in hand — no new
bookkeeping needed for a good-enough progress signal.

## Proposal

Add a lightweight progress reporter around the existing worker fan-out:

1. Capture `total = work.size()` and a start timestamp before spawning workers.
2. Spawn one **monitor thread** that, until `next.load() >= total`, sleeps ~10s and prints to
   **stderr**:
   ```
   [keepprog] <done>/<total> (<pct>%) elapsed=<mm:ss> eta=<mm:ss> R=<R> deck=<stem>
   ```
   ETA = `elapsed * (total-done)/max(done,1)`. Join the monitor after the workers finish.
3. `next` counts *claimed* (slightly ahead of *completed*) items — fine for progress. If exact
   completion is wanted, add a second `std::atomic<int> done` incremented at the end of each work
   item and report that instead.
4. **Gating / output hygiene:** use a distinct `[keepprog]` prefix so it never collides with the
   `[win]`/report parsing the skills rely on. Default-on is acceptable (informational, low-rate);
   optionally gate behind `MTG_KEEP_PROGRESS` if a silent mode is desired.

## Payoffs

- Live % + ETA for any chunk → real (not floored) runtime projections early in the first chunk.
- The pool driver (`logs/knights_exhaustive/driver.sh` pattern) can scrape `[keepprog]` and write a
  live per-chunk progress line into its STATUS file.

## Constraints

- **Byte-neutral to results is the load-bearing requirement.** Only add stderr prints / a passive
  monitor thread that reads the existing `next` atomic; do NOT touch the RNG, seeds, work split, or
  per-item math. Then the V-tables are numerically identical before vs after.
- **The merge gates on the `MTG_COMMIT` string stamp, NOT the binary.** The driver sets
  `MTG_COMMIT="$(git rev-parse --short HEAD)"`, and the merge only checks that stamp is equal across
  sidecars. So a byte-neutral progress build rebuilt **without committing** (HEAD unchanged) keeps the
  same stamp → its sidecars pool fine with pre-change ones. i.e. this CAN be added mid-pool; it does
  NOT require a fresh commit. (Correcting an earlier note here that claimed it "won't pool regardless"
  — that was wrong.)
- **The residual risk of the mid-pool path:** keeping the old stamp while the binary changes means the
  stamp no longer certifies the binary, so there is NO guardrail if the change turns out not to be
  byte-neutral (it would silently corrupt the pool with a matching stamp). Mitigation: verify byte-
  neutrality first — run one tiny fit at a fixed seed with the old vs new binary and confirm the raw
  sidecar's V is identical — before trusting a mid-pool rebuild. The cleaner, lower-risk option is
  still to land it on a commit boundary between grinds; the mid-pool path is available but riskier.
- Keep the print rate low (~0.1 Hz) so it adds no measurable overhead to the rollout loop.
