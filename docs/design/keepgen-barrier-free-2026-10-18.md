# Keep generator: run games without barriers until the very end (2026-10-18)

**Status: IMPLEMENTED on branch `worktree-agent-afca0e879418d3107` (not pushed), default ON, byte-identical
(identity evidence in §6). The live EDF run is NOT using it (it runs `build/PGO` from d2deae044).**

> *"We should continue running games without barriers until the very end of the generation."* — USER, 2026-10-18

Context: the live EDF `fast` run (R=30, d1 b1, flip_eps 0.1, K=19, 773,890 size-7 cell-sides) finished
its sub-table FLOOR except ONE kind-1 batch that ran on one thread for 2+ hours (EDF's uncharged
slow-rollout tail, `edf-slow-rollout-tail-2026-10-17.md`). The first adaptive bottoming wave -- and its
`flip_eps price` line -- waited on that one batch, because the producer computes a wave only when
`sub_wave_pending == 0 && sub_remaining == 0`. Size-7 floor speculation and the precompute memo kept
cores busy meanwhile, but nothing reported how much of that work the table ever USED.

Every mechanism below rests on the property the precompute memo already relied on, and that
`RolloutKeepWinTurn`'s per-game cache clear (996cc3485) made true: **a rollout's value is a pure
function of `(seed_base, r, w, pd)`**. Work can therefore be done early, on any thread, as long as
(a) what is FOLDED into a cell is exactly the set of rollouts the barrier schedule folds and (b) it is
folded in the same order. Nothing here changes either.

## 1. Commits (worktree branch `worktree-agent-afca0e879418d3107`, base d2deae044 = the live run's tree)

| commit | deliverable | flag (default ON; `=0` restores the old path) |
|---|---|---|
| `0814cdb7b` | D3 usefulness + worker-idle accounting | none (diagnostic, stderr only) |
| `4306adcc1` | D2 split sub batches into per-rollout tasks | `MTG_KEEP_BATCH_SPLIT` |
| `2ea1e30c6` | D1 speculative next sub-wave into a memo | `MTG_KEEP_SUB_SPEC` (+ `MTG_KEEP_SUB_SPEC_VERIFY=1`) |
| `c9a4e9bb2` | D4 remaining barriers: async true wave, Dopt outside fold_mtx, refine-tail precompute | `MTG_KEEP_SUB_WAVE_ASYNC`, `MTG_KEEP_REFS_DOPT_UNLOCKED`, `MTG_KEEP_PRECOMPUTE_REFINE` |
| `907d584b6` | D1b chain the speculation through memo-held waves | `MTG_KEEP_SUB_SPEC_DEPTH` (levels, default 16) |

Diagnostics (default off, never set on a shipping run): `MTG_KEEP_JOURNAL_KEEP=1` keeps the superseded
journal as `<journal>.done` so two runs' records can be diffed; `MTG_KEEP_DIAG_SLOW_ROLLOUT=w:pd:r:ms[;...]`
sleeps after rolling the named rollout(s) (`-1` = wildcard) -- a straggler on demand, values untouched.

## 2. D2 -- split sub batches (the straggler batch)

A kind-1 (sub floor / cap) or kind-2 (adaptive wave) task was a RANGE `[r0,r1)` run serially by one
worker (`run_batch`). Now `push_sub_batch` enqueues one kind-3 task per rollout sharing a heap
`SplitBatch {vals[], us[], left}`; each task writes its slot, and the task whose `left.fetch_sub`
returns 1 (acq_rel, so it sees every slot) calls `commit_sub` -- the ONE commit function the serial
path also uses now: fold the slots in `r` order into a batch-local `sum/sumsq/kept` (NaN = abandoned =
left out, exactly the old loop), add under `acc_mtx`, journal the cell. Same arithmetic, same order,
same record; only which thread rolled each rollout changes. Counters (`in_flight`, `sub_remaining`,
`sub_wave_pending`, `fed_total`) still count BATCHES.

This is `keepgen-straggler-task-splitting.md` §4-5 for the sub-table half, with two simplifications
the current code permits: (1) split at FEED time instead of a backfill-only co-consumable cursor --
the cursor's reason was per-worker `AIEngine` locality, which no longer exists since every rollout
clears its per-game caches; (2) no cancellation is needed -- a sub batch is a fixed range that is always
folded whole (the wave, not a freeze, decides its length). It cannot make ONE slow rollout finish
sooner: on `fast` a sub FLOOR batch is r0=2 rollouts, so a 2-rollout straggler gains at most 2x; a wave
batch (r_batch=16) or a `complete` cap batch (40) gains up to 16x / 40x.

## 3. D1 -- speculative next sub-wave

**When:** only in the TAIL of a sub stage -- every real batch of the sub floor / current wave already
enqueued (`sub_cursor` exhausted; waves are fed synchronously) and some still in flight. That is
exactly when the next wave is held back by stragglers and no critical-path work is left to feed.

**What:** a planner thread (one, jobs sequential) takes a SNAPSHOT under `acc_mtx` -- copies of the sub
tables' `sum/sumsq/cnt/V` and `cell_us` -- derives `V = sum/cnt` (recompute_sub's formula), applies the
change-detection prior override to the copy, marks a cell whose first batch has not committed (cnt 0,
projected > 0) ABSENT from every argmin, and runs the SAME marking code as the real wave:

* `sub_mark_core(A, Dopt, eps, out, count_refined, cost, cancel)` is the old
  `compute_sub_wave_tasks_at` body made generic over an accessor `A` (V / cnt / sumsq / start /
  present). The real wave passes `RealAcc{tables}` (start == cnt, all present) -- term for term the old
  arithmetic, identical set iteration order; the planner passes `SpecAcc` over its snapshot, with
  `start = max(cnt, projected)` so in-flight cells continue PAST the batch they already own.
* `KeepVal` / `ComputeDopt` likewise became `KeepValWith` / `ComputeDoptWith` over a value accessor
  (the plain functions call them with the tables' own V).

The planner never holds a reference to committed state it could write: speculation is structurally
unable to move a value. Its tasks are fed (kind -2) into a sharded `(w,pd,r)` memo, ahead of the size-7
floor speculation and the precompute memo (the sub chain is the critical path to refs; size-7 work is
work refine does anyway). A real sub rollout (kind 1/2/3) checks the memo first:

| memo state | real rollout does |
|---|---|
| 2 (value held) | takes it, erases it (`sspec_hit`), adds the rolled cost to `cell_us` so wave ordering is unchanged |
| 3 (a kind -2 task is rolling it) | waits for it (re-rolling would take at least as long), then takes it |
| 1 (queued, not started) | erases it and rolls itself; the queued task then sees it gone and skips |
| absent | rolls itself |

**Chaining (D1b).** One level of speculation fills a straggler window with only ONE wave of work, because
the next wave depends on that wave's results. But a speculated wave whose rollouts are all parked in the
memo HAS known results -- the values the real batches will fold -- so `sub_plan` folds them into its
snapshot (batch-local sum in r order, NaN left out, as `commit_sub` does), plans the wave after, and
repeats until a level still needs rollouts (cap `MTG_KEEP_SUB_SPEC_DEPTH`, 16). Only cells whose first
batch is still in flight -- the straggler itself -- stay out. A plan whose rollouts have all finished is a
replan trigger (no sub batch commits while a straggler holds the stage).

A plan is superseded the moment the real wave is fed (its memo entries stay: a later wave may use
them) and the memo is cleared when the sub-refine converges. A replan happens only when the previous
plan's list is exhausted AND a sub batch committed since (`sub_commit_seq`). `MTG_KEEP_SUB_SPEC_VERIFY=1`
re-rolls every hit and counts mismatches (`sspec_bad`, must be 0). Off under a per-rollout wall (wall-
clock abandonment) and under `MTG_KEEP_TRACE` (the memo carries no touched-set).

## 4. D3 -- usefulness accounting

Monitor line (every period) and an end-of-pool summary on stderr (never the report):

* `idle=X%` -- worker microseconds waiting on an EMPTY queue while the producer runs, as a share of
  capacity since the last wake; end of pool: `worker idle: S core-s of T (P%)`. The barrier, measured.
* `spec7=` fed; end: **size7-spec used/fed** -- rollouts inside each cell's FINAL count (the rest was
  truncated by reconcile or past a freeze).
* `pre=` enqueued `rolled=` `hit=` -- precompute memo; end: **hit/rolled**.
* `subspec=` requested `rolled=` `hit=` `plans=` -- D1; end: **hit/rolled**.
* `split=batches/rollouts` -- D2.

## 5. Every barrier in the schedule, start to finish

| # | barrier | before | now |
|---|---|---|---|
| 1 | play-digest battery (64 games, parallel) -> discovery | tail of 64 games on N threads; discovery cannot start (it keys its cache on the digest) | **unchanged** -- minutes at most; overlapping it with discovery needs discovery to defer its cache write. Listed, not removed |
| 2 | discovery -> pool | discovery's own probe tail | **unchanged** (cached on resume) |
| 3 | Pass A (sub floor) -> size-7 floor | already fused into one pool (no join) | no barrier |
| 4 | sub FLOOR -> first wave | first wave computed only when every kind-1 batch committed; one slow batch held it, serially, on one thread | **D2** spreads the batch over threads; **D1** rolls the provisional first wave into the memo while it runs; **D4a** computes the true wave off the producer. Still a barrier for *values* (by design: the wave reads committed means) but no longer for *work* |
| 5 | wave k -> wave k+1 | same gate per wave, plus the marking pass (2x backward induction + subcomposition scan; ~minutes on EDF) run ON THE PRODUCER, so feeding stopped and the queue drained | same three fixes; the marking pass now runs on the planner thread while the producer keeps feeding |
| 6 | sub converged -> refs (floor->refine) | `compute_refs` held `fold_mtx` through two full `ComputeDopt` passes: every size-7 worker froze at its next fold | **D4b** Dopt (and the sub recompute) computed BEFORE taking `fold_mtx` -- its inputs are final and quiescent; only vg + reconcile stay under the lock |
| 7 | size-7 floor speculation bound (r0+4) | precompute memo backfills past it during the floor | **NOT barrier-free -- see §7.** The sweep was LATCHED and starved by the blocking wave enqueue, so on Bruna it ran alone after the sub-refine converged; fixed by §7's E1-E3 |
| 8 | refine tail -> final emit | each live cell advances only cont_lookahead=4 past its count, so the last few live cells are serial chains and the box idles | **D4c** refine-tail precompute: backfill (queue < QCAP/4) rolls the next `r` of every live cell into `pre_val`, round-robin; rollouts past a freeze are wasted, on cores that had nothing else |
| 9 | pool join -> raw/profile/report | serial single-thread compute (recompute, BuildPolicyFromTables, reports) | **unchanged** -- no games remain; pure compute |
| 10 | shutdown | queued speculative tasks (kind < 0) would still be rolled after `producing=false` | skipped |

Not a barrier but worth knowing: `MTG_KEEP_REFS_OFFSET` (default 2) re-derives the freeze shrink
target `vg` on a TIMING-triggered schedule, so production output is not byte-reproducible across
timing anyway (keepgen_check.sh pins `=0` for that reason). Everything here is identical under either
setting in the sense that matters -- it changes no value the barrier schedule would compute from the
same committed state -- and the identity runs pin `=0`, as keepgen_check does.

Residual (not done): an in-flight speculative rollout cannot be cancelled when it turns out unneeded
(the sub-refine converged, or its cell left the wave); it runs to completion and is dropped. The
`GameWorkMeter` abandonment primitive could release the core (`keepgen-straggler-task-splitting.md`
§7), keyed on a deterministic condition -- the dropped value is never folded, so this is a pure
wall-clock question.

## 6. Identity and measured effect

**Constraints the runs were made under.** The live EDF generation owned the box (it ran at ~3,000% CPU
throughout); every run here was `nice -n 19 taskset -c 0-3` (later 1 core for the 1-thread arms), one at
a time, and in practice got about ONE core between all its threads. Wall clock of a CPU-bound run is
therefore load noise (a 4-thread arm took 420-600 s for the same work), so the EFFECT is measured on a
schedule benchmark that a starved box cannot distort (below), and identity is checked on every arm.

**Generation used.** burn (copied into scratch, nothing written under `decks/`), `MTG_KEEP_EXHAUSTIVE`,
adaptive bottoming, `MTG_EQUIV_PROBES=4`, force-merged to 4 buckets (36-110 size-7 cells, 166-408 sub
batches -- the smallest table that still runs 4-8 adaptive waves), seed 4242, `r_batch=2`,
`MTG_KEEP_REFS_OFFSET=0` (keepgen_check's pin: the rolling vg is timing-triggered at the default). Two
configs: d1/b1 R=6 (refuses to write a profile below R=10, so raw + report are compared) and d0/b1 R=12
(profile written). Harness: `logs/afca/ident.sh` + `compare.sh` on the worktree (gitignored).

**Identity -- every arm byte-identical:**

| config | arms | raw (normalized engine_fp) | profile | report | journal |
|---|---|---|---|---|---|
| d1 R=6 | OLD binary (`build/PGO` copy = d2deae044) x4 threads; new all-on x4, x1; all-off x4; straggler all-off x4, all-on x4; all-on + both VERIFY x4 | 7/7 identical (md5 a9dca68952b4) | n/a (R<10) | 7/7 | deterministic records 1408/1408 identical |
| d0 R=12 | OLD binary x4, x1; new all-on x4, x1; all-off x4, x1; schedule-benchmark all-on, all-on+VERIFY, all-off x4 | 9/9 identical (md5 04fbeeeb832c) | 9/9 identical | 9/9 | deterministic records 837/837 identical |

VERIFY arms (`MTG_KEEP_SUB_SPEC_VERIFY=1 MTG_KEEP_PRECOMPUTE_VERIFY=1`): 726 (d1) and 1,050 (d0) sub-memo
hits plus 59 precompute hits each re-rolled live -- **0 mismatches**. Unit tests (`mtg-test`, final binary): 1428/1428
cases pass (the generator is not linked into it; run for completeness).

*The journal, precisely.* Raw journals never were record-identical across runs, with or without these
changes: two runs with EVERY new flag off (`off4` vs `off1`) differ exactly where all-on vs all-off do. Two
fields are arrival order by construction: a size-7 PROGRESS record's `n` (where the fold happened to be when
the stride was crossed) and whether a terminal record carries the `fs/fq/fn` prefix (inline fold vs
compute_refs reconcile, whichever reached the cell first). Dropping progress records and those three
fields, every remaining record -- every sub-table commit, every size-7 floor and terminal record, s/q/n/f
-- is identical across all arms (sorted).

**Effect -- schedule benchmark** (d0 R=12; `MTG_KEEP_DIAG_SLOW_ROLLOUT="-1:-1:-1:100;110:0:1:120000"`:
every rollout costs 100 ms of WALL and no CPU, and one sub-FLOOR rollout is a 120 s straggler -- the EDF
shape in miniature):

| arm (4 workers) | first wave dispatched | sub-refine converged | pool wall | worker idle | sub-spec used |
|---|---|---|---|---|---|
| all flags OFF (the old schedule) | 133 s | 164 s | 164.9 s | 281.8 core-s (42.7%) | -- |
| all ON, one level of speculation (before D1b) | 132 s | 158 s | 158.8 s | 256.5 core-s (40.4%) | 634/656 (96.6%) |
| **all ON, chained (final)** | 132 s | **133 s** | **133.4 s** | **170.2 core-s (31.9%)** | **1048/1056 (99.2%)**, 41 waves chained |

With the old schedule the 8 adaptive waves ran serially AFTER the straggler (31 s); one level of
speculation served wave 1 from the memo and nothing else (25 s); chained, all eight waves were rolled
during the straggler and the whole chain re-validated in about one second -- 1048 of the 1056 speculative
rollouts landed in the table. The remaining idle is the straggler window itself once every
straggler-INDEPENDENT rollout of the run is done (the size-7 floor speculation and precompute had already
exhausted their bounds): there was nothing left that the barrier rules allow.

On the d1 R=6 runs (real rollouts, starved box) one level of speculation behind a 240 s sub-floor straggler
rolled 516 of wave 1's 534 rollouts during the straggler; the true wave then took 534/534 from the memo
(596/622 = 95.8% of all speculative rollouts used). Size-7 floor speculation was 384/880 used (43.6% --
the rest truncated by reconcile or past a freeze), the precompute memo 54/432 (12.5%) on the R=12 config:
both pre-existing, now visible.

**What to expect on EDF.** Its sub-FLOOR straggler is one 2-rollout batch on one thread: D2 lets its two
rollouts run in parallel (at most 2x on that batch; the single 2-hour rollout still takes 2 hours). D1 is
the large one: the provisional first wave (KittyEquipment v3's first wave was 50,063 tasks ~ 801,000
rollouts) starts rolling into the memo the moment the floor's last batches are in flight, instead of after the
straggler, and D4a keeps the producer feeding through each wave's minutes-long marking pass. Not
measured on EDF itself (a run that size cannot share the box with the live generation).

## 7. Follow-up 2026-10-20: the producer itself was a barrier (Bruna)

**What happened.** Bruna's `fast` gen (K=21, 671,400 size-7 cells, flip_eps 0.10) finished its size-7
floor in 62 h and its sub-refine converged after ~140 more hours -- and then sat in `phase=floor` with
~99 h of size-7 floor speculation still to run before refs could fix. `roll7` had read exactly 2,685,600
for those 140 h: the sweep advanced 384 rollouts (one `spec_chunk`) per WAVE. Cause: the producer is one
thread, and every feeding stage enqueued its whole list in one loop blocking on a QCAP-deep (96) queue --
`feed_wave` for a 324,290-batch wave sat inside that loop for the 76 h the wave took to RUN. The filler
got one chunk between waves; and because the sweep is LATCHED (`spec_active` blocks `compute_refs`
until it saturates), the whole of it then ran alone. D1-D4 do not touch this: `feed_wave` and
`push_sub_batch` still block per batch.

The box was busy throughout, so the cost was not idle cores; it was ORDER: the speculation pushed every
cell-side to r0+4 before any freeze could apply, and the refine pass then fed `cont_lookahead`
rollouts per cell back to back, so a freezing cell's next 3 rollouts were already rolling.

**Fixes (all default ON):**

| id | flag | what | identity |
|---|---|---|---|
| E1 | `MTG_KEEP_FEED_BOUNDED` | every feeding stage (size-7 floor, sub floor, each wave, the refine pass) keeps a cursor; each producer iteration spends one budget of `spec_chunk` feeds across them in priority order (floor > sub floor > wave > sub-spec > size-7 speculation > precompute) and comes back round, so nothing the producer owns waits on a stage's run time | byte-identical |
| E2 | `MTG_KEEP_FEED_RR` | refine pass and floor speculation feed ONE `r` per cell per pass (bounded by `cont_lookahead` / `r0+spec_budget` as before): a cell's next rollout is queued a pass later, after its last has folded, so a freeze wastes nothing; few-cell tails still fill the box through the lookahead | byte-identical |
| E3 | `MTG_KEEP_SKIP_FROZEN` | a queued size-7 rollout whose cell froze after it was fed is not rolled (the fold discarded it anyway) | byte-identical |
| E4 | `MTG_KEEP_SPEC_UNLATCH` | the speculation sweep stops when nothing else holds the floor phase, and refs fix from wherever it got to (USER 2026-10-20: *"I don't care about reproducibility as much as quality"*) | **not** byte-reproducible across schedules (speculated depth at refs time is timing); values reconciled per cell as before |

Monitor: `skip7=` (rollouts not rolled under E3); the end-of-pool usefulness line reports it too; E4
logs `floor speculation UNLATCHED at cell-side k/N` once.

**Measured on slivers_vial (2026-10-20).** `MTG_KEEP_EXHAUSTIVE`, adaptive bottoming, K=10 (7,758
size-7 cells), R=10, d1/b3, r_batch 2, seed 4242, `MTG_KEEP_REFS_OFFSET=0`; same binary, flags select
the arm; harness `logs/bruna_mullgen/slv_ident/run.sh` (gitignored).

| arm | raw / profile / report | size-7 speculation fed | used | rolled and discarded |
|---|---|---|---|---|
| old (E1-E4 all `=0`) | baseline | 62,064 | 29,215 (47.1%) | 32,849 |
| E1-E3 on, `MTG_KEEP_SPEC_UNLATCH=0` | **byte-identical** | 62,064 | 29,215 | 32,785 (E3 skipped 64) |
| all defaults (E1-E4) | **byte-identical** | 6,965 | 6,965 (100%) | **0** |

All three arms fold the same 159,076 rollouts. The old arm's sub-refine converged at 2,501 s and its
latched sweep then ran ALONE until refs fixed at 4,465 s, after which 76.7% of the cell-sides froze at
once -- the Bruna shape in miniature. With E4 the sweep unlatched at cell-side 6,965/15,516 the moment
the sub-refine converged and refs fixed 8 s later. ~31% fewer size-7 rollouts for the identical table.
The shared-branch binary (upstream engine) gives the same old == new identity (`slv_shared`).

Resume: a run hard-killed during sub-refine wave 2 and resumed lands on a table whose 9 size-6
sub-cells carry different COUNTS than the uninterrupted run (values per rollout are pure, so only
sample sizes differ). This is the scheduling-state gap recorded in `keepgen-resume-exactness.md` --
the journal persists accumulators, not which wave a sub-cell was in.
It is pre-existing, not E1-E4: the OLD schedule (all four `=0`), killed in wave 2 and resumed, likewise
differs from its own uninterrupted run (1 size-6 sub-cell's count; no value differs) -- `slv_oldres`.
