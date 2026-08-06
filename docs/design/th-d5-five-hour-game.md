# A single treasure_hunt game costs ~5 hours at d5 unbounded — ROOT-CAUSED

**Status: ROOT CAUSE FOUND (2026-08-06). Fix proposed, NOT implemented.** The user's read was
right: there is no reason a bounded 8-turn goldfish game should cost hours, and it is a defect.

## Root cause: the leaf transposition table only stores WINS

`SimulateToEnd` (`TurnSolver.cpp`, the leaf rollout) ends with:

```cpp
int result = SimulateToEndImpl(state, depth, max_turns, budget, cutoff_turn, second_main, tt);
if (tt != nullptr && result <= max_turns) { tt->Store(key, result); }
```

A rollout that finds no win returns `max_turns + 1` and is **never cached**. Measured on the
pathological game (`--seed 9010 --game-index 1 --depth 4`):

```
TT lookups : 138,346   hits: 0   (0% hit)
TT stores  : 0         nowin NOT stored: 138,346   (100%)
```

**Every one of the 138,346 leaf rollouts returned a no-win, so the table was written zero times.**
There is no leaf memoisation at all in this position: every leaf re-rolls from scratch, and the
search grows exponentially with depth unchecked. Measured cost of one extra ply on this game:

| depth | wall | search nodes | EnumeratePlans |
|---|---|---|---|
| 3 | 7.7 s | 123,076 | 31,973 |
| 4 | 88.0 s | 1,349,112 | 624,163 |
| 5, 6, 7 | did not finish | — | — |

Both depths return the same answer (`avg = 3.0000`, a turn-3 win), so the extra work buys nothing.

The overhead is worse than "a cache that misses": with search-shuffle enabled (default),
`BuildSimKey` folds **the entire ordered library of both players** into the key — ~120 hashes — and
that key is computed 138,346 times to consult a table that is always empty.

**This is the same bug class as the `FSLineCache` no-win asymmetry fixed one layer up on
2026-08-05** (`bound-qualified-nowin-memo.md`) — "a no-win may be a branch-and-bound abort, so
discard it" — except the leaf table was never revisited, and unlike `FSLineCache` the store site
carries no comment justifying the restriction.

## Proposed fix (not yet implemented)

Apply the same bound qualification that worked for `FSLineCache`:

* store a no-win with the `cutoff_turn` it was proved under, and reuse it only for a query asking
  `cutoff <= stored_bound`. `cutoff_turn` is NOT part of `BuildSimKey`, which is precisely why an
  unqualified no-win would be unsound to store — the same reasoning that made the interior memo work;
* suppress the store when the rollout was cut short by the budget, or the "no win" is not a proof but
  "I ran out" — the `g_fs_trunc_events` watermark pattern already exists for this;
* gate behind a flag (`EnvOn`, default off until measured), verify byte-identity at an unbounded
  budget first (with no budget to reallocate, a sound memo must return the same line), then A/B.

Expected payoff is large exactly where the engine currently hurts most: treasure_hunt's search cost is
the reason it produced zero label rows in 34 h, thrashed 3.2 M enumerations in the labeller, and
holds the matrix's only intractable cells.

## Ruled out along the way

* **`MTG_LADDER_VALUE_LEAF`** — initially ranked first suspect because it shipped 2026-08-05 and its
  identity sweep never covered TH at d5. Wrong, and the evidence was already available: seeds
  8008/10010 ran 400 games with the ladder equally ON, and an in-horizon win is found by simulation
  rather than by the leaf estimate, so the leaf cannot change which pass commits. The pathological
  game reproduces in the V arm, where the ladder is not even enabled.
* **Search-shuffle key folding.** Re-running with `MTG_NO_SEARCH_SHUFFLE=1`: 138,346 lookups / 0
  hits / 1,345,156 nodes / 85.3 s versus 138,346 / 0 / 1,349,112 / 88.0 s. Identical. The full-library
  fold makes each key expensive but is not why the table never hits.
* **CPU starvation.** Contention does not turn a 2.5 s batch into a 5 h one.

## The observation

Phase C of the regeneration measured `TH_staged` H5 across four seeds. Two of them are ordinary; two
are dominated entirely by their FIRST 25-game batch:

| seed | games | total | s/game | first batch (25 games) |
|---|---|---|---|---|
| 8008 | 400 | 5,919 s | 14.8 | 4.4 s |
| **9009** | 50 | 17,912 s | 358.2 | **17,875 s = 4.96 h** |
| 10010 | 400 | 6,137 s | 15.3 | 1.9 s |
| **11011** | 50 | 10,671 s | 213.4 | **10,668 s = 2.96 h** |

Every *other* game in those two cells is trivially cheap: once the first batch was carried over, the
fill run took 9009 from 50 → 150 and 11011 from 50 → 400 in minutes (~0.03–0.12 s/game). So the cost
is not spread across the cell — it is one or a few individual games inside `[0, 25)`.

For scale: a 4.4 s batch on one seed versus 17,875 s on another, for the same deck, depth and game
count, is a **4,000×** spread.

## Why this matters beyond the matrix

It is what condemned the cells. The tractability guard keys on the cumulative rate, which was the fix
for an earlier per-batch version (see the comment at the condemnation site), but a cumulative average
over 50 games cannot dilute a five-hour batch: 17912/50 = 358 > 60. The cell was capped at
reference-only, discarding ~350 cheap games to avoid re-running one expensive game **already paid
for**. `--never-condemn-at-or-below 5` now prevents that for the depths the trust-depth decision
reads, but the underlying game is still there and will be paid again by any future run that does not
resume from a seeded state.

## Exact repro

```
MTG_VALUE_MODEL=0 \
MTG_VALUE_PROFILE=logs/eval/treasure_hunt.value.STAGED.json \
MTG_LADDER_VALUE_LEAF=1 \
build/Release/mtg decks/treasure_hunt/treasure_hunt.txt \
    --profile decks/treasure_hunt/treasure_hunt.profile.json \
    --seed 9009 --game-index 0 --games 25 --max-turns 8 --threads 1 \
    --ignore-play-profile --depth 5
```

(`MTG_EVAL_MODEL`, `MTG_EVAL_PROFILE`, `MTG_NC_SEARCH`, `MTG_VALUE_MIN_DEPTH`, `MTG_VALUE_REDO_MODE`,
`MTG_VALUE_STARTGATE_ALPHA` are cleared from the environment by `run_batch`.) Seed 11011 with the
same arguments is the second instance. Engine frozen at `94c917f`, src-tree `4cd06296`.

**The single game is already identified** (2026-08-06). Running indices 0..24 of the `TH V7 s9009`
batch as 25 separate one-game processes at d7 gave:

```
24 of 25 games:  total 54.4 s   mean 2.27 s   max 2.60 s
game index 1  :  did not finish while the other 24 completed
```

So the culprit is **base seed 9009, game index 1** — reproduce it alone with:

```
MTG_VALUE_MODEL=1 MTG_VALUE_PROFILE=logs/eval/treasure_hunt.value.STAGED.json \
MTG_VALUE_MIN_DEPTH=0 MTG_VALUE_STARTGATE_ALPHA=8 \
build/Release/mtg decks/treasure_hunt/treasure_hunt.txt \
    --profile decks/treasure_hunt/treasure_hunt.profile.json \
    --seed 9010 --game-index 1 --games 1 --max-turns 8 --threads 1 \
    --ignore-play-profile --depth 7
```

One game, one core, and every sibling game in its batch costs ~2.3 s — so ANY instrumentation of
this is cheap to set up and the contrast is stark. Note this is the d7 V-cell instance; the d5 H-cell
instance (`--depth 5`, `MTG_VALUE_MODEL=0` + `MTG_LADDER_VALUE_LEAF=1`) is a separate reproduction
worth checking against it, since it tells us whether the blow-up is depth-specific or arm-specific.

## Method note

Use the `-DMTG_PROFILE=ON` build (`build-instr/`) for counters rather than wall-clock — the counters
are deterministic and load-immune, which matters because this box was running two matrix jobs when
the anomaly was recorded. Note the sibling lesson from the same day: wall-clock on heavy-tailed decks
produced two separate retracted claims (`bound-qualified-nowin-memo.md`).

## Related

- `bound-qualified-nowin-memo.md` — the memo measured ~1.00× on treasure_hunt at every depth, so it
  is not the cause here.
- `label-horizon-ladder.md` — why TH is expensive to search at all, and the ladder this flag extends.
- `value-leaf-regeneration-queue.md` — the queue whose phase C surfaced this.
