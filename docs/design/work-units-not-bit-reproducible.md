# Work units are not bit-reproducible across batch contexts (small; channel CONFIRMED, structure open)

**Status:** open, deferred. Found 2026-08-20 while validating a KittyEquipment lever battery.
**Channel:** per-thread state surviving a game boundary — CONFIRMED 2026-08-20 (see below). Which
structure is still open.
**Impact measured:** ~0.005% of a game's units. Nothing in that battery's conclusions is affected.
**Why it is written down anyway:** it contradicts an invariant `ai/GameWorkMeter.h` documents as
load-bearing, and the contradiction is cheap to re-find and expensive to rediscover the hard way.

## The claim under test

`GameWorkMeter.h` says, as the reason units exist at all:

> In units, the set of abandoned games is a deterministic function of (deck, seed, depth, arm,
> limit) and is identical everywhere. That is what lets an abandoned game become a SKIP LIST the
> whole table can share.

`SearchBudget.h` makes the same promise one level up ("identical seed + budget therefore does an
identical amount of work ... on every machine and every run"). The depth matrix, the abandon
ceiling, and cross-machine pooling all rest on it.

## What was measured

KittyEquipment, d3, budget 0, game index 54 of seed block 300001 (`--seed 300055 --game-index 54`),
~2.13M units. Play digests are IDENTICAL in every run below; only the unit count moves.

| context | units |
|---|---|
| ISOLATED (1-game batch), run 1 / 2 / 3 | 2,127,508 / 2,127,508 / **2,127,508** |
| inside the 150-game job, memo on (battery) | 2,127,462 |
| inside the 150-game job, memo on (re-run) | 2,127,459 |
| inside the 150-game job, `MTG_SOLVE_MEMO=0` run 1 | 2,127,462 |
| inside the 150-game job, `MTG_SOLVE_MEMO=0` run 2 | 2,127,346 |

So: **perfectly stable in isolation, and variable inside a batch** — three different values across
runs of the same job, all of them *below* the isolated count. A second game in the same job (gi=86,
8.6M units) moves the same way. Everything else in the 150-game job is bit-identical run to run.

## What has been RULED OUT

* **The solve memo.** `MTG_SOLVE_MEMO=0` still diverges, and one memo-off run reproduced the
  memo-on value exactly (2,127,462). The memo appears not to move unit counts at all.
* **Wall-clock leaking into the search.** `SearchBudget` is unit-based by construction and
  `Overrun()` is an absolute used-unit ceiling; there is no `steady_clock` in the path.
* **A code change.** This reproduces at commit 1154aa0d and the same divergence pattern is present
  in artifacts written before the diagnostic work began.
* **Cross-game memo HITS specifically.** `g_decision_epoch` is `thread_local`, starts at 0, and is
  only ever incremented (never reset per game), so a previous game's entries on the same worker
  thread carry strictly lower epochs and can never be hit. This is why the memos are an unsatisfying
  explanation, and it is the reason this document still does not name a structure — though the
  monotonicity measured below shows that SOMETHING per-thread is being reused across games.

## The channel is CONFIRMED: per-thread residue (2026-08-20)

The experiment this doc proposed was run. Same 60-game job, `--threads 1`, two independent
processes: **units BIT-IDENTICAL**. And game 54 comes back a THIRD distinct value:

| context | residue on the worker before game 54 | game 54 units |
|---|---|---|
| isolated 1-game batch | none | 2,127,508 |
| `--threads 24` | ~1/24 of the prior games | 2,127,346 – 2,127,462 (varies run to run) |
| `--threads 1` | all 53 prior games | **2,127,270** (stable across runs) |

Two conclusions, both firm:

* **Fix the thread assignment and units are exactly reproducible.** The non-determinism under
  `--threads 24` is entirely which games shared a worker, nothing else. So units are a
  deterministic function of (deck, seed, depth, arm, limit) **plus the thread schedule** — and it
  is that last term `GameWorkMeter.h` claims does not exist.
* **It is monotone in residue, and in the direction of HITS**: more prior games on the thread ->
  FEWER units. This REFUTES the sign argument written below (that inherited residue would make the
  cap-clear fire earlier and therefore cost more work). Whatever the structure is, a game is
  reusing work computed by an earlier game on the same thread.

## What is still open

Which structure. All the candidates share one shape — per-thread state that survives a game boundary,
so a game's cost depends on which games preceded it on its worker:

* the three plan memos (`solvememo::t_cache`, `solvememo::t_m2cache`, `enummemo::t_cache`), which are
  cleared ONLY on exceeding their cap, never at a game boundary;
* the ~dozen other `static thread_local` scratch buffers in `TurnSolver.cpp`.

The memos remain the obvious suspects and remain hard to blame: `g_decision_epoch` is `thread_local`,
starts at 0 and only ever increments, so a previous game's entries carry strictly lower epochs and
cannot be hit. Something per-thread is surviving the game boundary and being REUSED (the monotonicity
above rules out the "residue only shifts when the cap-clear fires" story, which predicts the opposite
sign).

The bisect is now cheap and well-posed: clear one candidate at game start, then re-run the
`--threads 1` vs isolated pair from the table above. They must converge on the isolated value when
the right structure is cleared.

## Practical guidance until it is closed

* **Units remain the right cost instrument** — they are load-INVARIANT, which wall time emphatically
  is not (the same smoke suite measured an 81 s and a 204 s makespan on this box hours apart, a
  uniform ~2.5x across every deck including ones the change could not touch). The defect here is
  ~0.005%; the wall-clock alternative is off by 150%.
* **Do not treat a units diff as a byte-identity check.** Use the play DIGEST for identity. A
  handful of games differing in the 5th significant figure is this effect, not a regression.
* **Be careful with an abandon ceiling set near a cell's median.** A game within ~0.01% of the
  ceiling can be abandoned in one run and not the next, which makes the skip list run-dependent —
  exactly what the ceiling was designed to avoid. Ceilings set at a multiple of the median (the
  documented practice) are far enough from the edge for this not to bite.
