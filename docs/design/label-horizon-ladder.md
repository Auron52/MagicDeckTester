# The labeller's horizon ladder — why every value/eval label was pessimistic

**Status: FIXED (`MTG_LABEL_LADDER`, default ON; `=0` restores the old behaviour).** Offline only —
`TurnSolver::EnumerateEarliestWins` is reached from the `MTG_DUMP_VALUE_ROWS` / `MTG_DUMP_EVAL_ROWS`
dumps and the `MTG_DUMP_EWINS` rule-miner, and from nothing on the play path. **No ground-truth
rebaseline.** What it *does* invalidate is training data: see "Blast radius".

## The bug

`FSLineWin` stops at the first in-horizon win it finds and returns it as the node's optimum. Its own
comment states the premise that makes that sound:

> Under the iterative-deepening caller (FullSearchLine) a pass runs only after every shallower pass
> found no win, and every node in a pass shares the same horizon edge, so any in-horizon win is at
> that edge = the global minimum. […] NOTE: this couples FSLineWin's correctness to that calling
> convention — it is not a standalone earliest-win finder.

The labeller called it as a standalone earliest-win finder. `EnumerateEarliestWins` computed one
depth covering the whole game (`max_turns - turn + 1`) and made a **single** `FSLineTail` call at it.
With a horizon that far away, wins at many different turns are all "in horizon", they are not tied,
and the first one the move ordering happens to reach is *a* win — not *the earliest*. The label came
out **later than the truth**, i.e. systematically pessimistic, with no signal that anything was wrong.

## The fix

Ladder the horizon the way `FullSearchLine` does. Pass `dd = 1, 2, …` searches `dd` turns with
cutoff `turn + dd`; a candidate's win turn is recorded at the **first** pass that finds one. Every
node in a pass then shares one horizon edge, so the shortcut's premise holds and the first win found
is the minimum. Shallower passes are exponentially cheaper than the deepest, so the ladder's overhead
is the usual iterative-deepening constant, and the per-pass cutoff is a free branch-and-bound the
labeller never had (`FSLineWin` prunes at `turn > cutoff`).

Candidates are re-applied per pass rather than caching one `GameState` each: the apply is cheap next
to the search, and plan counts reach the thousands.

## Measurement

8 games/deck, K=3, seed 555000, value rows. **Direction is the gate**: a sound fix to a pessimistic
search can only ever move a label *earlier*, so one label moving later would refute it.

| deck | rows | earlier | **later** | mean shift | sec off → on |
|---|---|---|---|---|---|
| burn | 37 | 2 | **0** | −0.018 | 37.6 → 44.3 |
| Goblins | 30 | 12 | **0** | −0.333 | 0.2 → 1.4 |
| Anti-Lifegain | 32 | 11 | **0** | −0.406 | 2.8 → 3.3 |
| Dragonstorm | 39 | 16 | **0** | −0.402 | 1.1 → 1.1 |
| slivers_vial | 33 | 9 | **0** | −0.111 | 0.2 → 2.7 |
| treasure_hunt | 35 | 10 | **0** | −0.152 | 1.1 → 1.4 |
| Knights | 34 | 9 | **0** | −0.098 | 0.3 → 0.6 |

**69 of 240 rows (29%) were wrong; none moved later.** Individual errors are much larger than the
means: antilife seed 555002 turn 1 labelled **7.00, truth 4.00**; dragonstorm seed 555001 turns 3–4
both labelled **8.00, truth 6.33**.

Harness: `test/label_ladder_ab.sh <tag> [games] [seed]`, which reports the earlier/later split joined
on `(seed, turn)` so a thread-ordering difference cannot fake a movement.

## This also closes the MTG_VALUE_LABEL_BNB anomaly

`MTG_VALUE_LABEL_BNB` (cross-candidate branch-and-bound on the label path) was rejected because it
produced **earlier** labels than the unpruned search — impossible for a sound search, and therefore
evidence of a latent bug rather than of B&B being wrong. It was. B&B's tight cutoff narrows the
horizon, which accidentally restores the shortcut's premise; the "pruned" arm was simply the less
broken one. With the ladder, B&B is **lossless** — measured byte-identical labels on Anti-Lifegain
and Dragonstorm — so `earliest_only` can be turned on for value-row runs as a pure speedup.

The standing suspicion that this was cache order-dependence is **refuted**: `MTG_LABEL_COLD_CACHE=1`
gives every candidate a fresh `tt`/`lc` and leaves the discrepancy exactly intact. (The earlier
cold-cache probe measured 0/256 on **burn**, which is the one deck where B&B labels never differed —
the probe was run where the effect wasn't.)

## Blast radius: the training data, not the engine

No play path calls `EnumerateEarliestWins`, so play, digests and ground truth are untouched. But
every value-leaf and eval model in the repo was trained on labels produced by the broken path, and
~29% of rows carried a too-late label. That includes the pooled Hinata rows
(`logs/eval/hinata_rows_B.all.rows`, 8,231 rows).

The bias is not uniform noise — it is one-sided and largest exactly where the search has to work
hardest to find the win, so a model trained on it learns to under-rate positions whose win is
findable but not first in the move ordering. Rows generated before this fix should be regenerated
rather than topped up.

## Related

- `docs/design/learned-d0-policy.md` — what the labels feed.
- `docs/design/rollout-executor-lockstep.md` — the same "a helper's correctness is coupled to its
  caller's convention" failure mode, one layer down.
