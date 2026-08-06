# A single treasure_hunt game costs ~5 hours at d5 unbounded — open investigation

**Status: OPEN, deferred to after the 2026-08-05 value-leaf regeneration finishes.** Nothing is
being changed on the strength of this yet. The user's read is the right one: *there should be no
reason for one goldfish game to cost that much*, so this is a suspected defect, not a heavy tail to
be accepted.

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

## Hypotheses

**The shape of the evidence points at ONE GAME STATE, not at a global setting.** Whatever the cause,
it has to explain why two seeds of the same deck at the same depth under the same flags finished 400
games at ~15 s/game while two others spent hours inside their first 25. Any explanation that would
apply uniformly is already contradicted by seeds 8008 and 10010.

1. **A specific board state that explodes the enumerator** — e.g. many Treasure Hunt / Land's Edge
   activations producing a combinatorial plan set at a single node. This is the hypothesis that fits
   the per-seed spread. `MTG_PROFILE=ON` counters (`EnumeratePlans`, `plans/call`, `Search nodes`) on
   the isolated game separate "huge but healthy tree" from "pathological enumeration at one node".
2. **Unbounded d5 on a deck with bad move ordering.** treasure_hunt wins through lands and card draw,
   which never looks lethal to `MoveOrderPlans`, so the search cannot prune and must exhaust — the
   documented reason TH thrashed 3.2 M enumerations in the labeller (`label-horizon-ladder.md`). A
   contributing factor rather than a cause: it applies to all four seeds, only two of which explode.
3. **A non-terminating or near-non-terminating loop.** The game is bounded by `--max-turns 8`, so the
   cost is inside ONE decision's search. Worth confirming the process is making progress at all
   rather than spinning.
4. **`MTG_LADDER_VALUE_LEAF` (a 2026-08-05 change, ON in H cells).** Kept on the list only because it
   is new and its identity sweep covered 7 decks at depths 1–4 with **treasure_hunt at d5 absent**, so
   its cost there is formally unmeasured. But it is a **weak** suspect and an earlier draft of this
   file wrongly ranked it first:
   * seeds 8008/10010 ran 400 games at ~15 s/game **with the ladder equally ON** — a global flag does
     not produce a per-seed 4,000× spread;
   * an in-horizon win is detected by real simulation, not by the leaf estimate, so the leaf cannot
     change *which* pass commits (consistent with the observed 21/21 identity). It can only weaken
     B&B pruning in the WARM-UP passes;
   * the committed pass uses the heuristic leaf in both arms, and a five-hour game is almost certainly
     dominated by that pass, which the flag does not touch.
   Still cheap to falsify — re-run the repro with `MTG_LADDER_VALUE_LEAF=0` — so do it early to close
   the question, not because it is likely.

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
