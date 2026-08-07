# The depth matrix should use `mtg --batch` pooling, not a per-batch process pool

**Status: PROPOSED (2026-08-06), not implemented.** Raised by the user while phase C of the
value-leaf regeneration sat 13 hours on six stuck batches with the box half idle. Recording it rather
than refactoring mid-run.

## What it does today

`scripts/attic/valueleaf_depth_matrix.py --incremental` runs a Python work-stealing pool. Each cell
`(deck, arm, depth, seed)` is filled 25 games at a time, and **every batch is a separate
`build/Release/mtg … --threads 1` process**. The 2026-08-05 run issued 3,120+ such processes for
161,000 games.

The design is deliberate and its stated rationale is sound as far as it goes: at `--threads 1` the
scheduling unit is a 25-game batch, so a cell stuck on a long-tail game ties up one core while other
cells keep streaming — rather than one process per cell at `--threads 24`, where every cell's tail
would idle the whole box.

## Why it is the wrong shape anyway

`BatchRunner` already solves this better, one level down (`src/runner/BatchRunner.cpp:230-263`):

* it **flattens every game of every job into one work list** — the scheduling unit is already a
  single GAME, finer than a 25-game batch;
* it does **LPT scheduling** (most expensive first, cheap games backfill the tail), with an explicit
  per-job `weight` for costs the depth/budget proxy misjudges;
* it groups by profile path so the ProfileCache holds ~1 profile at a time;
* per-job `depth`, `budget_ms`, `profile` and `ignore_play_profile` are all manifest fields, so one
  manifest can carry every depth and seed;
* it prints `<job>: played=N avg=… digest=…` per job — **which is exactly the cell value the matrix
  computes by hand.**

CLAUDE.md already requires this shape, and the matrix predates it:

> Pool ALL work (every game of every job) into ONE `mtg --batch` so the runner keeps cores saturated
> to a single tail… bake per-variant profiles into the manifest's `profile` field rather than
> re-launching per variant.

## The real cost of the current shape: condemnation is an artifact of batching

Idle cores are the visible symptom; the corrosive one is that **the tractability guard reasons about
25-game batch averages**, so one pathological game poisons a cell.

Measured 2026-08-05, `TH_staged`:

| cell | games | s/game | first batch |
|---|---|---|---|
| H5 s8008 | 400 | 14.8 | 4.4 s |
| H5 s9009 | 50 | 358.2 | **17,875 s** |
| H5 s11011 | 50 | 213.4 | **10,668 s** |
| V6 s9009 | 50 | — | **43,466 s**, then batch 2 took **6 s** |

The cell is condemned to `--reference-target` games, and the resulting value is **biased optimistic** —
the 50-game references read 3.9200 / 4.0200 against a 400-game truth of 4.0400 / 4.0875. For
treasure_hunt that changed the conclusion: `H5` measured 4.0244 (looks like the heuristic is still
improving past d4) versus a true 4.0713 (**identical to H4** — converged). `H_conv` feeds the
trust-depth decision, so the artifact would have produced the wrong trust depth.

Worse, a cell whose first batch never returns can never be condemned at all: the guard only runs when
a batch commits, and there is no wall-clock abort. On 2026-08-06 six V7/V8 batches had run 13 hours
with `games=0`, and killing a worker does not help — `run_batch` returns `p=0`, the commit block is
skipped, and the pool **resubmits the same batch forever**.

## What the batched shape would look like

Two `mtg --batch` invocations, not thousands of processes:

* **Only `MTG_VALUE_MODEL` (0/1) and `MTG_LADDER_VALUE_LEAF` are genuinely process-global.** They are
  cached in function-local statics (`TurnSolver.cpp` `s_ladder_value_leaf`, `DecisionProviders.cpp:151`)
  and so cannot vary within a process → one invocation for the H arm, one for V.
* **`MTG_VALUE_PROFILE` does not need to be env at all.** The queue already builds scratch variant
  deck folders (`make_variant_deck`) holding the staged `value.json` with siblings symlinked, and the
  engine resolves sibling models directory-relative off the job's `profile` path. Put the variant
  path in the manifest's `profile` field and the per-deck model selects itself.
* Jobs = deck × depth × seed (8 × 8 × 4 = 256 per arm), each `games=400`. Read each cell straight off
  the per-job `avg`.

Consequences: two load-imbalance tails instead of thousands; the deck/keep model loaded per worker
rather than per 25 games; a pathological game becomes one item in a ~100k-game pool, scheduled FIRST
by LPT (give those cells an explicit `weight`), occupying one core while everything else finishes
around it.

And the whole tractability apparatus can be **deleted**: `--intractable-sec-per-game`,
`--reference-target`, `first_wall`, the sticky-flag fix, and the `--never-condemn-at-or-below` depth
floor added on 2026-08-05 all exist only to manage batch-average poisoning that per-game pooling does
not create. If a cap on total work is still wanted, set per-cell `games` up front in the manifest —
an honest, declared sample size rather than one decided mid-run by a timing artifact.

## The goal is CPU utilisation to the very end, not shorter games

State the objective correctly and the design follows from it: **keep the box full right through to
the end of the run.** A long-running game is not itself the problem — idle cores beside it are. What
a pooled queue buys is that cheap work keeps backfilling behind the expensive work, so utilisation
stays high until the last item, instead of collapsing once everything but the stragglers is done.

Measured on the 2026-08-06 run, phase C:

```
phase C started        15:16
last batch committed   00:05
now                    04:32     -> 4.4 h with ZERO progress
cores in use            5.9 of 24  (25% of the box)
idle core-hours since last progress   ~80
```

Thirteen hours in, three quarters of the machine had been idle for over four hours. That is the cost
being paid, and it is a harness property, not a property of the deck.

It is tempting to answer "a 13-hour game is 13 hours either way, pooling just stops it blocking
others". That understates the difference twice over.

**1. The 25-game barrier is an artificial tail.** A batch process does not return until all 25 of its
games finish, and it runs them SEQUENTIALLY, so one pathological game blocks every game behind it.

Measured directly on 2026-08-06 — the stuck cell `TH_staged V7 s9009` (batch = games 0..24 at
`--depth 7`) re-run as 25 independent single-game processes on the idle half of the box:

```
24 of 25 games:  total 54.4 s   mean 2.27 s   max 2.60 s
game index 1  :  still running when the other 24 had finished
```

**The entire 25-game batch is ~54 seconds of work plus one game.** The batch process completed game 0
in ~2 s, hit game 1, and games 2..24 NEVER STARTED — ~52 seconds of trivial work queued behind one
game for 13 hours. Across the six stuck batches that is on the order of five minutes of real work
held hostage on six cores overnight, while the cells reported `games=0`.

Under per-game pooling those 24 games finish in seconds and are banked, the cell reports 24/25, and
one core carries the straggler while the box drains normally. The tail narrows to its irreducible
width: ONE game.

(The pathological game is therefore identified: base seed 9009, game index 1 — i.e. `--seed 9010
--game-index 1` — at d7. See `th-d5-five-hour-game.md`.)

**2. Sequential issue inside a cell delays discovery.** `needs(c)` is `(not c["running"]) and …`, so a
cell has at most one batch in flight and its batches are issued one after another. An expensive game
cannot be started early — the scheduler does not reach it until its batch's turn arrives. TH V7
s10010's pathological batch is at offset 275, the 12th of the cell, so it only began ~1 hour into
phase C and is still running 9.7 hours later. Under a flat pool, every game is visible at t=0 and LPT
sorts the expensive ones first, so their runtime overlaps the whole run rather than extending it.

Together these change the wall-clock model from roughly `discovery_delay + longest_game` to
`max(longest_game, total_work / cores)`. Only the second term is physics; the first is the harness.

## Caveats

* The irreducible part is real: one pathological game still costs what it costs, and at the very end
  it will hold one core with the box otherwise idle. Pooling minimises the tail, it cannot delete it.
  Why that game costs 13 hours is a separate question — see `th-d5-five-hour-game.md`.
* `--incremental` resume would need rework: today it resumes at batch granularity from
  `<out>.cells.json`. With one job per cell the natural unit is the job, so resume means "skip jobs
  already recorded" — simpler, but not a drop-in change.
* Memory: `--workers` currently exists partly to bound RAM (~1 GB/process for antilife's keep model).
  Under batch pooling the equivalent knob is `--threads` plus the ProfileCache cap, which the runner
  already sizes by grouping on profile path.

## Related

- `batch-runner.md` — why pooling beats per-item invocations (the rule this restores).
- `th-d5-five-hour-game.md` — the pathological game that exposed all of this.
- `value-leaf-regeneration-queue.md` — phase C is the caller to change.
