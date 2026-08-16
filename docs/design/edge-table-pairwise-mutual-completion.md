# The depth matrix should compare on MUTUALLY COMPLETED GAMES, per compared pair

**Status:** deferred, designed, not implemented. Applies to
`scripts/attic/valueleaf_depth_matrix.py` (`apply_skiplist`, `emit_table`, `dead_rung`) and
`scripts/attic/valueleaf_table_to_metadata.py`. Raised by the user, 2026-08-16.

**Supersedes** `abandonment-union-gated-on-completion.md`. That document fixes the global union;
this one removes the need for a global union at all, which dissolves the problem instead of gating
it. If this lands, that document is moot.

## The idea

> I do wonder whether, for the future, we want to leave this as just the matrix or also have some
> kind of other structure that makes escalation clearer. The reason being the differing levels of
> completion between cells. In theory the best would be to take the full set of mutually completed
> games between the cells being compared. — user, 2026-08-16

Every comparison uses the maximal set of games that **both** compared cells completed. No global
filter, no single game set spanning the table.

## Why this is the natural shape

The pipeline's real decisions are ALREADY pairwise:

- `dead_rung` intersects `set(dg) & set(sg)` -- the paired rung verdict.
- The trust gate uses "the PAIRED gap `value_leaf_lp[d] - h_conv` is BOUNDED below tol".
- The crossover is V*d* against h_conv.

None of them needs one global game set. The global union in `apply_skiplist` exists so a reader can
scan `H1=... H2=... H3=...` as a column of directly comparable means. It is a PRESENTATION artifact,
and every comparison in the table pays for it in sample.

## Measured cost of the union (Mirrorwing, 2026-08-16, ~81% of a 400x4 matrix)

Sample available to each edge under global-union pairing vs pairwise mutual completion:

| edge | union-n | pairwise-n | gain |
|---|---|---|---|
| H1->H2 | 1334 | 1582 | +19% |
| H2->H3 | 1534 | 1717 | +12% |
| H3->H4 | 1534 | 1610 | +5% |
| H4->H5 | 1489 | 1499 | +1% |
| V1->V2 | 1339 | 1600 | +19% |
| V2->V3 | 1339 | 1599 | +19% |
| V3->V4 | 1339 | 1579 | +18% |
| V4->V5 | 1491 | 1679 | +13% |
| **V5->V6** | 1047 | 1223 | **+17%** |
| **V6->V7** | 1047 | 1260 | **+20%** |
| **V7->V8** | 1039 | 1254 | **+21%** |

ESTIMATE, not exact: each cell's own completed set was reconstructed from its `.abandoned` files
plus the skip list, because `apply_skiplist` filters `chunks` destructively and the unfiltered sets
are not recoverable from the state file. Directionally solid.

The gain concentrates on **V5->V6, V6->V7, V7->V8** -- precisely the edges whose verdicts condemned
those rungs. About +20% n is about 9% tighter `se`, free, on the deciding comparisons. H4->H5 gains
+1%, because the reference already abandons nearly everything any other cell does.

## Backfill becomes unnecessary -- and that is the larger saving

Today an abandoned game is removed from EVERY cell (the union) and then BACKFILLED: a new index is
queued so each cell still reaches `--target`. Because a seed has 9 live cells (H1-H4, V1-V5), one
abandoned game costs NINE extra games. Measured on this run:

| seed | abandoned | extra games forced |
|---|---|---|
| 8008 | 61 | 549 |
| 9009 | 76 | 684 |
| 10010 | 65 | 585 |
| 11011 | 59 | 531 |
| | **261** | **2,349** |

2,349 games -- about 12% of the whole matrix -- is backfill created by abandonment. It is also the
run's critical path: the phase-C tail was H1..H4 on seed 9009 sitting at offsets 400..475, i.e.
nothing but backfill.

BACKFILL EXISTS ONLY TO REPAIR DAMAGE THE UNION CAUSES -- but removing it is NOT the win, and an
earlier revision of this document got that wrong. Backfill restores each cell to the full target, so
today's arm must be priced WITH it. Correctly measured, over 4 seeds x a 400 nominal target:

| edge | A: today (union+backfill) | B: pairwise, no backfill | C: pairwise, same queue |
|---|---|---|---|
| H1->H2 | 1600 | 1587 | 1848 |
| H2->H3 | 1600 | 1522 | 1783 |
| H3->H4 | 1600 | 1415 | 1676 |
| V1->V2 | 1600 | 1600 | 1861 |
| V2->V3 | 1600 | 1599 | 1860 |
| V3->V4 | 1600 | 1579 | 1840 |
| V4->V5 | 1600 | 1527 | 1788 |
| **TOTAL** | **11200** | **10829** (-3.3%) | **12656** (+13.0%) |
| games run per cell | 476 | 400 | 476 |

So dropping backfill SAVES 16% of the work and COSTS 3.3% of the sample. It is a defensible trade on
cost-per-sample, but it is not free and it is not the point.

THE UNION IS THE DEFECT; QUEUE LENGTH IS AN INDEPENDENT KNOB. Today they are entangled, because
backfill exists to undo the union's deletions. Separate them:

  * **Remove the union.** Every cell keeps every game it completed; each comparison intersects the
    two cells involved. That alone is +13.0% paired sample at IDENTICAL cost (column C).
  * **Then choose a span ONCE, up front** -- no dynamic backfill loop at all. 0..475 for +13% at
    today's cost, or 0..399 for -3.3% at 16% less cost. A policy decision, not a mechanism.

**Per-cell backfill must NOT be built** (user, 2026-08-16: *"I don't like that idea, because it means
those games won't align with other cells"*). Backfilling only into the cell that abandoned produces an
index no other cell ran, so it pairs with NOTHING: it inflates that cell's game count while adding
zero to every comparison.

ALIGNMENT IS PRESERVED BY EITHER SPAN. Every cell runs the IDENTICAL index range, so all pairings are
on shared indices; what differs between cells is only which of those they finished, which is exactly
what the intersection handles. It is TODAY's dynamic backfill that makes the queue length a function
of how much was abandoned.

## Proposed artifact: an EDGE table

Rows are comparisons, not cells; each on its own mutually-completed set:

```
edge         n_paired  coverage   delta      se      upper95   verdict
H4->H5           1499     94%    +0.0002  0.0011   +0.0020   EQUIVALENT
V5->V6           1223     76%    +0.0075  0.0032   +0.0128   LIVE
V6->V7           1260     79%    +0.0013  0.0009   +0.0028   EQUIVALENT
V6 vs H5         1180     74%    +0.0019  0.0011   +0.0037   INDISTINGUISHABLE   <- crossover
```

Escalation is then PRINTED rather than inferred by subtracting two row means -- which is the
"structure that makes escalation clearer" the user is asking for. `coverage` is
`n_paired / min(target of the two cells)`.

## Costs, and how each is handled

1. **Deltas stop chaining.** Each edge sits on a different game set, so a sum of edge deltas is not
   an end-to-end delta. Any end-to-end claim must be computed as ITS OWN edge -- which is already
   how the V-vs-H crossover works. The edge table should refuse to print chained sums.

2. **Each edge measures a different population.** A deep edge's mutual set skews toward easier
   games, so "V6->V7 is flat" is a statement about V6-intersect-V7's population, not about all
   games. This is DISCLOSED by the `coverage` column rather than hidden. `--quality-coverage`
   (`the paired intersection must cover this fraction of the smaller rung's games before a quality
   verdict is allowed`) already exists as exactly this defence and must stay.

3. **The level table still has consumers.** `valueleaf_table_to_metadata.py` regexes `H(\d+)=...`,
   `V(\d+)=...` and `# games/cell:`. Those rows must survive as a LABELLED DIAGNOSTIC -- computed
   per-cell and explicitly marked not-mutually-comparable -- not as the decision basis.

4. **`UNEQUAL GAME SETS` changes meaning.** Today it is an alarm, because the union is supposed to
   make every comparable cell hold the same games. Under edge pairing, unequal sets are the normal
   state and the invariant moves to each edge's `coverage`. The guard should be reworded, not
   deleted -- what it must still catch is a cell whose per-game identities are unknown (the
   `wins is None` case), which corrupts any intersection.

## Migration

**Phase 1 -- pure addition, no consumer changes.** Emit the edge table alongside the current one,
computed from the per-game records already on disk. Read-only, so it can be run RETROACTIVELY
against a finished matrix to show exactly what the union cost on that run. This alone delivers the
"escalation is clearer" half of the request.

**Phase 2.** Move `valueleaf_table_to_metadata.py` to read edges rather than regexing row means.

**Phase 3.** Drop the global union from `apply_skiplist`. Keep per-cell means as a diagnostic.
Retain the per-cell skip list for its OTHER role -- never re-queue an abandoned game, since it would
just abandon again at full cost.

Phases 1 and 2 are safe under any run; phase 3 changes what the table measures and wants a rerun of
a known deck to confirm no verdict moves.
