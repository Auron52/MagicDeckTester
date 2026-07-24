# Adaptive batched keep-gen (continuous single queue)

Status: **implemented + validated (size-7 + sub-tables), 2026-07-24**. Flag-gated
(`MTG_KEEP_CONTINUOUS=1`); default path (wave/barrier) byte-unchanged. Supersedes both the
section/window idea (rejected: no pruning inside a section, no resumable output until a boundary) and
the interim `latest_full`-sweep variant (rejected: a single slow combo cell pins the sweep frontier and,
with bounded look-ahead, idles the whole pool; and its freeze was wall-clock-timing-dependent →
non-deterministic).

## Problem

The exhaustive keep/mull generation underutilizes CPU. Two structural causes:

1. **Floor pass** is split into `MTG_KEEP_FLOOR_GROUPS` (default 32) contiguous waves with a thread
   join (barrier) at each, so ~`nthreads` cores idle at every group's slow-cell tail.
2. **Refine loop** runs discrete **waves with a barrier between each** (`process_tasks` joins), and
   within a wave a task is a whole cell (`r_batch` rollouts on one thread). An expensive Dragonstorm
   combo cell is one indivisible multi-minute rollout, so at the end of every wave the handful of
   expensive cells strand on 2–3 cores while the other ~21 idle. Measured live at R=20: the wave path
   spent 730 s+ still on **refine-wave-1** with many waves to go, each with its own tail.

Raising R per pass does **not** help with per-cell tasks — it just makes fatter tail-tasks.

## Design: one continuous pool, floor → fix refs → per-cell independent freeze

A single persistent worker pool over a **dynamic** task source, where a task is **one rollout**
`{cell, pd, r}` (this is the "40 × R=1 passes, but all in one batched queue" shape). The producer feeds
the next rollout for every *live* (not-frozen, not-at-cap) cell; thousands of live cells ≫ cores, so the
queue never starves and cores stay full to the last live cell.

The determinism-vs-CPU tension (a wall-clock sweep makes freezing timing-dependent) is resolved by
**removing all cross-cell timing coupling**:

1. **Floor phase.** Feed every size-7 cell to the floor `r0`. No freezing yet (`refs_ready=false`).
2. **Fix the refs, once, from the complete floor.** When every cell has folded to `≥ r0` and nothing is
   in flight, compute `Dopt_ref` (the keep/mull thresholds) and `vg_ref` (per-table pooled variance)
   from that complete snapshot and **freeze them for the rest of the run**. `Dopt` reads only the
   sub-tables (see below) — never size-7 V — so it is exact at this point.
3. **Refine phase, per-cell independent freeze.** Each worker, after committing a rollout, folds its
   cell's contiguous prefix in fixed `r`-order and — at each new level `c ≥ r0` — evaluates the freeze
   test against the **fixed** refs: freeze iff `flip = ½·erfc(|mean − Dopt_ref|/(se·√2)) ≤ flip_eps`,
   **OR** low-probability (`P < cutoff_P` and `c ≥ cutoff_R`), **OR** `c ≥ r_max`. On freeze the fold
   stops (the cell's `cnt` truncates at that level) and an atomic `afroze` flag publishes to the
   producer, which stops feeding it.

Because a cell's freeze level is a **pure function of its own rollouts + the fixed refs**, it does not
depend on when any other cell finishes. Two runs are therefore **byte-identical regardless of
scheduling**, and **no cell ever waits on another** (no slowest-cell throttle) → cores stay full to the
last live cell. The per-rollout decision is one comparison inside the fold; the only whole-table work
(`compute_refs`) happens exactly once.

## Sub-tables (bottoming) and Dopt

`compute_Dopt` reads **only** the sub-tables (`keep_val` = min over size-(7-m) subcomps), never size-7
V — the dependency is one-directional `sub-tables → Dopt → size-7 freeze`. So the size-7 pool can gate
against an exact fixed `Dopt` as long as the sub-tables are final *before* step 2:

- **Bottoming-on, no `adaptive_bottom` (the default gen):** Pass A drives every sub-table to the cap
  (`sub_floor=false`), so they are already final → the size-7-only pool needs nothing extra. This is the
  first-validated path.
- **`sub_floor` cases (`MTG_KEEP_ADAPTIVE_BOTTOM`, or incremental change-detect):** Pass A only floors
  the sub-tables, so the continuous block first calls `run_refine_waves(sub_only=true)` — the shared
  refine loop with the m=0 size-7 gate skipped — to **converge the sub-tables** (and thus `Dopt`) before
  fixing the refs. Since sub-refinement never reads size-7 V, converging the sub-tables in isolation
  reaches the **same fixpoint** the full wave path would, so `Dopt_ref` equals the wave path's final
  `Dopt`. (This sub-refine phase is still barrier-bound, but sub-tables are small vs size-7; making it
  continuous too — staged smallest-table-first, since `Dopt[m]` depends on `Dopt[m+1]` — is future work.)

`run_refine_waves(bool sub_only)` is the single shared refine implementation: the non-continuous path
calls it with `sub_only=false` (byte-identical to the previous inline wave loop by construction — the
only change is a `!sub_only` guard on the m=0 branch and the wave label).

## Determinism & pooling parity

- Each rollout writes its **own slot**; a cell's `sum`/`sumsq` are **prefix-folded in fixed `r`-order**
  under one lock, so the value is byte-deterministic given the rollout count — no concurrent-`sum` ULP
  drift (float add is non-associative; an unordered concurrent `+=` would perturb the raw by an ULP and
  break the pooling-parity fingerprint).
- The freeze level is deterministic (above), so the raw is **byte-identical run-to-run** — a stronger
  guarantee than the interim design's "policy-identical only". Seeds are a pure function of
  `(seed_base, r, w, pd)`, disjoint across machines by `seed_base`; merge/commit-fingerprint pooling is
  unchanged.

## Resumability

The checkpoint is the cumulative raw (per-cell `sum/sumsq/cnt`) written atomically (tmp + rename) every
`MTG_KEEP_SWEEP_SEC`. Resume reloads it (when `out_raw` exists and its meta fingerprint — deck, buckets,
`seed_base`, cap `R` — matches) and seeds the `fed[]` cursor from the reloaded `S7.cnt`. Stopping at any R
(e.g. R=20) yields a valid profile at that point — finer than the section idea, which produced nothing
until a 5/10 boundary.

**Resume is policy-safe but not yet byte-identical / zero-waste.** The checkpoint stores `cnt`, not the
per-cell frozen flag, so on resume a cell that had *frozen* below the cap is indistinguishable from one
merely interrupted there — it gets re-fed toward the cap. Validated (Slivers R=4): resuming a completed
run reloaded all 27690 cell-sides, re-refined the frozen-below-cap cells to the cap (+7830 rollouts), and
produced a profile with **KEEP diffs=0, BOTTOM diffs=0** vs the reference. So resume never loses work and
never changes the policy; it just re-does the frozen cells' remaining rollouts. Byte-identical, zero-waste
resume is future work: persist `frozen7` + the fixed `Dopt_ref`/`vg_ref` (e.g. a `continuous.frozen`
sidecar, leaving the poolable raw format untouched) and restore them on resume.

## Flags

- `MTG_KEEP_CONTINUOUS=1` — select the continuous pool (requires `adaptive`, i.e. `r0 < r_max`). Unset →
  wave/barrier path (byte-identical; smoke/regression and every other deck unaffected).
- `MTG_KEEP_CONTINUOUS_LOOKAHEAD` (default 4) — max rollouts a cell may be fed past its committed prefix.
- `MTG_KEEP_SWEEP_SEC` (default 20) — checkpoint interval.
- Reuses `MTG_KEEP_CUTOFF_P` / `MTG_KEEP_CUTOFF_R` (low-prob freeze arm), `MTG_KEEP_R_FLOOR`,
  `MTG_KEEP_ROLLOUTS`, `MTG_KEEP_ADAPTIVE_BOTTOM`.

## Validation (done)

- **Determinism (size-7):** two continuous runs → `RAW BYTE-IDENTICAL`; policy vs wave `KEEP diffs=0
  BOTTOM diffs=0` over 7758 comps (Slivers R=6). Continuous used 67464 rollouts vs wave 74256 (adaptive
  freeze at the exact crossing level rather than the next `r_batch` multiple).
- **Equivalence + CPU (size-7, v1):** V byte-exact vs wave for same-`cnt` cells; policy identical; load
  ≈ nproc sustained; ~18 % faster (Slivers R=6).
- **Sub-table path (`adaptive_bottom`):** wave-vs-continuous policy parity + run-to-run byte-identity
  (Slivers, `subtable_ab.sh`).

## Open / next

- R=20 perf showcase (higher-R regime where wave's many refine-wave tails hurt most).
- Interrupt+resume equivalence test.
- Wire a single-continuous-process gen script (replaces N pooled R=1 chunks with one adaptive run to R).
- (Future) make the sub-refine phase continuous too, staged smallest-table-first.
