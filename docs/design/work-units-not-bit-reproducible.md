# Work units were not bit-reproducible across batch contexts — ROOT-CAUSED AND FIXED

**Status:** CLOSED 2026-08-20. Found while validating a KittyEquipment lever battery, root-caused to
the SEARCHED-SECOND-MAIN memo, fixed by clearing the thread_local plan memos at game start
(`TurnSolver::ClearPerGameCaches`, called from `AIEngine::HandleMulligan` beside the existing
`m_leaf_cache` clear).
**Play was never affected** — every digest was identical across thread counts throughout. The
casualty was the work METER's determinism.
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

## Root cause: the m2 memo's EVICTION SCHEDULE, not its hits

Bisected by disabling each memo and comparing the two deterministic contexts (game 54, d3):

| config | isolated | `--threads 1` |
|---|---|---|
| both memos on | 2,127,508 | 2,127,270 — differ |
| m2 memo off (`MTG_NO_M2_SEARCH_MEMO=1`), solve memo on | 2,144,680 | 2,144,680 — identical |
| both off | 2,144,680 | 2,144,680 — identical |

So the SEARCHED-SECOND-MAIN memo (`solvememo::t_m2cache`) is the sole channel, and the greedy solve
memo moves unit counts not at all (it memoises a path that consumes no budget units).

The mechanism is NOT stale hits — that really is impossible, exactly as reasoned above:
`g_decision_epoch` is thread_local, starts at 0 and only increments, so a previous game's entries
carry strictly lower epochs and the `epoch ==` test rejects them. **They are dead weight, and that
is the problem.** They still occupy the shared `Cap()`, so how much residue a worker carries decides
WHEN `cache.clear()` fires, which decides which of the CURRENT game's live entries get evicted —
a different hit pattern, a different node count. More residue happened to produce a *more* favourable
eviction schedule here, which is why the earlier "residue should cost more work" prediction had the
sign backwards.

## The fix

`TurnSolver::ClearPerGameCaches()` drops all three plan memos and is called from
`AIEngine::HandleMulligan`, beside the `m_leaf_cache` clear that already existed for precisely this
reason ("so a reused batch worker's AIEngine does not accumulate/cross-hit across games").

Verified: game 54 now reads **2,127,508 in all three contexts** (isolated, `--threads 1`,
`--threads 24`), the full 60-game units file is bit-identical between `--threads 1` and
`--threads 24`, and play is untouched (`base.train 123c0bdaaf6ffe5d`, `park.train
6a558d4d77e2241f`, both unchanged; smoke 36/36 and regression 60/60 ALL PASS with no play changes
and no slowdowns).

Note the direction: the batch numbers moved UP to meet the isolated one. The residue had been
buying a small accidental saving, so the fix costs a little work per game in exchange for the
determinism `GameWorkMeter.h` promises.

## Practical guidance

* **Units remain the right cost instrument** — they are load-INVARIANT, which wall time emphatically
  is not (the same smoke suite measured an 81 s and a 204 s makespan on this box hours apart, a
  uniform ~2.5x across every deck including ones the change could not touch). The defect here is
  ~0.005%; the wall-clock alternative is off by 150%.
* **Use the play DIGEST for byte-identity, not units.** Still true, and cheaper.
* **Artifacts generated before this fix carry the effect.** Any units file, abandon calibration or
  skip list produced before 2026-08-20 has up to ~0.01% of per-game thread-schedule noise baked in.
  That is far below any sane ceiling multiple, so nothing needs regenerating — but it is why an old
  file will not reproduce bit-exactly against a new run.
