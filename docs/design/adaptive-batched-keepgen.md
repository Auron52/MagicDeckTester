# Adaptive batched keep-gen (continuous single queue)

## TARGET DESIGN — the ONE way to run the gen (user spec, 2026-07-24)

The goal is that there is exactly **one** way to run keep/bottom generation, so it cannot be run
incorrectly (agents have repeatedly mis-invoked it: uniform R=1 instead of continuous, missing
`MTG_EQUIV_*` cache-fingerprint params, etc.). One invocation — `analyze <deck>` — and nothing else to
get wrong. The gen is **always**:

1. **Continuous** — a single barrier-free pool over ALL cells (all hand sizes, floor + refine). Cores go
   idle only at the very END of all processing — never at a group/wave/phase boundary. No 32-group floor
   barrier, no per-wave refine barrier, no floor→refine barrier, and discovery must not leave cores idle.
2. **Incremental + restartable** — resumable at any point; poolable across runs/machines.
3. **Adaptive** — freeze confident keep/mull cells instead of over-sampling (size-7 keep decision).
4. **Prune low-probability cells early** — on by default (the `CUTOFF_P`/`CUTOFF_R` mechanism, no longer
   an opt-in flag).
5. **Full bottoming ALWAYS** — sub-tables are always fully sampled (to the cap); bottoming is never
   skipped. (So `adaptive_bottom` / any bottoming-off shortcut is NOT the direction — deleted.)
6. **Depth & budget from the PLAY PROFILE** — the only tunable, encoded once in the deck's play profile
   (not `MTG_EQUIV_DEPTH`/`MTG_EQUIV_BUDGET` on the command line). Set once, read from there.

Every other `MTG_KEEP_*` / `MTG_EQUIV_*` knob and the uniform/round/wave code paths are to be **removed**.

**Persistence = per-cell incremental journal, NOT periodic full-snapshot checkpoints — SHIPPED 2026-07-24.**
Because a cell completes atomically (floor reached, or terminal freeze/cap), durability is a per-cell append
log: persist each cell-side the moment it finishes. Records key by `(H, cell-index, pd)` — valid because a
matching fingerprint ⇒ identical discovery ⇒ identical comp ordering ⇒ stable indices (validated on resume;
out-of-range indices ignored) — and carry `{s:sum, q:sumsq, n:cnt, f:frozen}` (doubles at 17 sig figs, so
they round-trip exactly). A record is appended: at floor completion (`f=0`), at a terminal freeze/cap
(`f=1`), for each sub-table cell's final sample (via `run_batch`, `f=0`), plus a one-time **REFS record**
(`{refs:1, dopt0, dopt1, vg}`) when `Dopt`/`vg` are fixed from the complete floor. Completion IS persistence:
there is no "checkpoint" event. This is strictly better than the interim periodic `write_raw_atomic` snapshot
(b7f33b0): no periodic ~40–100 MB full-raw write held under `fold_mtx` (eliminates the snapshot-write stall —
the last residual idle source), and a crash loses only the few in-flight cells.

Resume replay is **order-independent** (apply the highest-`cnt` record per cell-side, so a later lower-`cnt`
record can't clobber a completed one) and **byte-identical + zero-waste**: terminal (`f=1`) size-7 cells set
`frozen7` and are skipped; floored cells re-refine from `r0` with the SAME fixed refs (restored from the REFS
record, NOT recomputed from the partially-refined state — which would differ). The authoritative poolable raw
is still written once at the end from the complete accumulators; the journal is then **deleted** (superseded).
Default ON for the continuous path; `MTG_KEEP_JOURNAL=0` reverts to the periodic snapshot (kept as A/B +
fallback). Implemented in `src/analyzer/ExhaustiveKeep.cpp` (journal_append / journal replay / journal_refs).

**Validated (Slivers, R=8):** `jA==jA2` raw byte-identical (journaling deterministic); `jA==snB` raw
byte-identical (journal changes PERSISTENCE only, never results); wall-clock 452s (journal) vs 495s (snapshot)
— journaling is **not more expensive** even where the snapshot is cheap (~1 MB raw); interrupt→resume killed
mid-refine (refs fixed, 158 terminal cells) reloaded 27690 cell-sides + fixed refs and produced a
**byte-identical** final raw. See `logs/cont_validate/journal_ab.sh` + `journal_resume.sh`.

**Orthogonal problem (separate from the pool):** even a perfect single pool leaves the final end-tail =
the slowest single game. Dragonstorm has degenerate *games* — individual rollouts of certain combo hands
(Dragonstorm + rituals + payoff) take MINUTES (pathological deterministic search; the `SearchBudget` is a
virtual work-quota with no wall bound — see `mulligan-gen-cost-value-model-lever` memory). This is a
degenerate SEARCH, not a degenerate SCHEDULE. Root-cause it from CAPTURED games before any wall-clock cap
(instrument: `MTG_KEEP_SLOW_MS` logs hand+side+seed+elapsed for any rollout over the threshold). Capturing
first, hypothesizing second.

**Current state vs target:** DONE — barrier-free floor (b7f33b0) and per-cell journal persistence (the
snapshot-write stall is gone). REMAINING idle sources / gaps to the one-way method: the floor→refine barrier
(the producer waits `in_flight==0` before fixing refs), the sub-refine waves (`run_refine_waves(sub_only)` for
`sub_floor` cases), single-threaded discovery, and depth/budget still being env flags rather than read from
the play profile. Low-prob prune is present but opt-in (`CUTOFF_P`/`CUTOFF_R`), not default-on.

---

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

The continuous path resumes from the **per-cell journal** (see the SHIPPED persistence note above): replay
applies the highest-`cnt` record per cell-side, restores `frozen7` from terminal (`f=1`) size-7 records, and
loads the fixed `Dopt_ref`/`vg_ref` from the REFS record. This is **byte-identical + zero-waste** — killed
mid-refine and resumed, the final raw matches an uninterrupted run exactly (validated Slivers R=8). Stopping
at any point yields a valid low-R profile via the authoritative raw. The old comp-based `out_raw` reload
(cumulative raw, tmp+rename every `MTG_KEEP_SWEEP_SEC`) remains as the **fallback**: it is used when
journaling is off (`MTG_KEEP_JOURNAL=0`) or no journal is present (e.g. a completed prior run / merged pool),
and is policy-safe but not byte-identical (it re-refines frozen-below-cap cells toward the cap, since the
snapshot stores `cnt` not the frozen flag).

## Flags

- `MTG_KEEP_CONTINUOUS=1` — select the continuous pool (requires `adaptive`, i.e. `r0 < r_max`). Unset →
  wave/barrier path (byte-identical; smoke/regression and every other deck unaffected).
- `MTG_KEEP_CONTINUOUS_LOOKAHEAD` (default 4) — max rollouts a cell may be fed past its committed prefix.
- `MTG_KEEP_JOURNAL` (default 1 for the continuous path) — per-cell journal persistence. `=0` reverts to the
  periodic full-raw snapshot (A/B + fallback). Temporary knob: the target design (top) is journal-only.
- `MTG_KEEP_SWEEP_SEC` (default 20) — snapshot interval, used only by the `MTG_KEEP_JOURNAL=0` fallback.
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
