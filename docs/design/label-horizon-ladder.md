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

| deck | rows | earlier | **later** | mean shift |
|---|---|---|---|---|
| burn | 37 | 2 | **0** | −0.018 |
| Goblins | 30 | 12 | **0** | −0.333 |
| Anti-Lifegain | 32 | 11 | **0** | −0.406 |
| Dragonstorm | 39 | 16 | **0** | −0.402 |
| slivers_vial | 33 | 9 | **0** | −0.111 |
| treasure_hunt | 35 | 10 | **0** | −0.152 |
| Knights | 34 | 9 | **0** | −0.098 |

**69 of 240 rows (29%) were wrong; none moved later.** Individual errors are much larger than the
means: antilife seed 555002 turn 1 labelled **7.00, truth 4.00**; dragonstorm seed 555001 turns 3–4
both labelled **8.00, truth 6.33**.

Harness: `test/label_ladder_ab.sh <tag> [games] [seed]`, which reports the earlier/later split joined
on `(seed, turn)` so a thread-ordering difference cannot fake a movement.

## What it costs — and a correction

The 8-game run above also produced wall-clock numbers, and they said the ladder was roughly free
(burn 37.6 s → 44.3 s, Dragonstorm 1.1 s → 1.1 s). **That was published and it was wrong.** Eight
games of a heavy-tailed deck is dominated by one or two outlier positions that are expensive in
*both* arms, which masks the ratio completely — the same undersampling that once produced a
"+0.14 % instructions" claim from a 60-game callgrind (see `shard-volley-hold.md`). Wall-clock needs
its own sample size; a correctness gate does not make a timing column trustworthy.

Re-measured at **40 games/deck, 20 threads, seed 888000, idle box** (`test/label_throughput.sh`),
against the pre-fix path (`MTG_LABEL_LADDER=0 MTG_VALUE_LABEL_BNB=0 MTG_LABEL_NOWIN_CACHE=0`). All
three arms produce identical row counts, and `ladder`/`ladder+memo` produce identical labels:

| deck | old (s) | ladder (s) | ladder+memo (s) | net |
|---|---|---|---|---|
| Hinata2 | **hangs** (44 of 51 rows in 68 min, then stalled) | — | **17** | was impossible |
| treasure_hunt | 38.9 | 25.6 | 22.8 | **1.7x faster** |
| Dragonstorm | 120.3 | 194.9 | 152.2 | 1.3x slower |
| Anti-Lifegain | 4.1 | 25.9 | 10.1 | 2.4x slower |
| Knights | 1.2 | 3.6 | 3.0 | 2.5x slower |
| slivers_vial | 0.3 | 1.0 | 0.8 | 2.7x slower |
| burn | 0.3 | 3.2 | 1.0 | 3.3x slower |
| Goblins | 0.9 | 67.0 | 9.9 | 11x slower |

**Do not summarise this table with one multiplier.** Per-deck cost spans five orders of magnitude, so
any average is dominated by whichever expensive deck is in the list -- an earlier draft of this file
reported "1.9x slower overall" on exactly that mistake. Read it per deck: the decks that got slower
are the ones where 40 games cost under a second to begin with (11x worse on Goblins is 0.9 s ->
9.9 s), and the decks that got faster, or became possible at all, are the expensive ones. Size a
regeneration per deck.

**A measurement trap this table fell into twice.** The first run of `test/label_throughput.sh` set
`MTG_FS_NOWIN_CACHE` to vary the memo. But the labeller forces the memo on for itself, and
`FSNoWinCacheOn()` is `global || forced` -- so **every arm had it on**, including "old". The tell was
the `cache` arm coming out *slower* than the `new` arm on Dragonstorm, which is impossible if they
are the same configuration. The knob is `MTG_LABEL_NOWIN_CACHE`. Generally: when a flag gains a
second way to be enabled, every harness that varied the first one is silently measuring nothing.

**The cost is real and it is intrinsic.** Finding *a* win is goal-directed and cheap; proving *no
earlier* win exists means exhausting the subtree at each horizon, and that is what the ladder buys
correctness with. This is the same effect the regeneration queue originally diagnosed for
`earliest_only` B&B and mistook for B&B being the wrong shape.

**The no-win memo is therefore not optional here** — it is what pays for the ladder, by stopping
every candidate re-proving the refutations its siblings already proved. Hence
`MTG_LABEL_NOWIN_CACHE` (default ON) forces it inside `EnumerateEarliestWins` even though the
global `MTG_FS_NOWIN_CACHE` stays off for play. With it on, correct labels cost **5 % more
wall-clock in total than wrong ones** — but that total is dominated by Dragonstorm and
treasure_hunt, and the per-deck spread is 11× worse to 2.6× better, so size a regeneration per
deck and never from the aggregate.

**treasure_hunt getting 3.6× *faster* is a lead, not a curiosity.** It is one of the three decks
(with slivers_vial and Knights) that produced zero rows in 34 hours, and the ladder is exactly the
kind of change that would unblock it: a deck whose positions have no early win pays the old path a
full deep search per candidate to discover that, where the ladder refutes cheaply and stops.

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
