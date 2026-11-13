# Pooled A/B CPU is biased by the batch runner's profile-path sort (found 2026-11-13, deferred)

## What happens
`BatchRunner` (`src/runner/BatchRunner.cpp`, the `std::stable_sort(items...)` that builds the work list) orders games by
`(sched_weight desc, depth desc, budget_ms desc, profile_path asc, job asc)`. In an A/B whose arms share depth and
budget but carry DIFFERENT profile paths (one sidecar/profile directory per arm, the usual way to pin per-arm
settings), the manifest's "arms innermost" order is undone: each deck's arms run as contiguous blocks in alphabetical
order of their profile paths, and the alphabetically last deck/arm runs in the batch's draining tail.

In the tail most hardware threads are idle, so SMT siblings do not compete and the same game costs far less thread-CPU
(Snow game 144, byte-identical work of 1,003,088 units: 26 s alone vs 57 s at 32 busy threads). An arm that sorts last
therefore reads as CHEAPER in thread-CPU (and in wall per game) for identical work.

## Evidence (Hydras sampled-gate work, P18d)
- Pooled screen S8: `snow_heur` finished at completion lines 1512-1561 of 1562; heur 41 s vs gate arms 62-73 s on game
  144 with identical units. Re-run as 16 heur + 16 gate copies interleaved at 32 threads: 57.0 vs 57.0 s.
- With order-preserving weights (below) the Snow gate arm moved from 1.119x to 1.011x heur thread-CPU, same games.
- Full write-up: `docs/design/sampled-gate-ladder.md` Iteration 9 on branch `wip/p18-ladder`.

## Workaround (use it for any CPU/wall comparison in one pooled batch)
Give every job `"weight": N - index` (N = number of jobs, index = its manifest position) so the sort keeps manifest
order, put arms innermost, and set `MTG_BATCH_PROFILE_CACHE=30` so the interleaved profiles stay resident.

## Deferred work
1. Decide whether `scripts/deck_compare.py` and the other pooled A/B drivers (valueleaf/mullgen A/Bs, ad-hoc
   manifests) should emit order-preserving weights by default, or whether the runner should interleave arms itself
   (e.g. sort profile path only as a tie-break AFTER a per-game arm-interleave key). This is a USER call: it changes
   how every past CPU number compares with future ones.
2. Audit which past CPU/wall verdicts compared arms with different profile paths in one batch; the arm that sorted
   last was flattered. Win-turn/quality results are unaffected (play is deterministic per game).
