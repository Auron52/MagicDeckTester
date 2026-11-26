# Maze's End 2HG slow games, part 3: scoping the dead-crossing price

(Written by the parent from agent a7c4c9150dfb9a112's final message.)

Commits on wip/maze2hg-perf (pushed): `7fe531593` (src reset to c5c8877d9), `1619f7be4` (the fold), `a5e940460` (the scoped price). `$S` = `/workspaces/MagicDeckTester/build/scratch/maze2hg-perf`.

## Why the unscoped price hurt Gods
The fold stops a fan right after its first dead crossing, and with W=2 the meter never charges on that crossing alone. So in ab2, all of the price's effect on Gods came from crossings the fold does not see: **failed crossings** (`fan.Cross` returns false) feeding the meter inside fold-eligible fans. One failed crossing plus one dead one makes a charge, and at an exhausted budget the fan then stops and is marked truncated. That is the Gods quality loss (+19 / +24). This mechanism is inferred from the code; it was not traced with a counter.

## The change
- **`FanPreAdvanceLive(fan)`** = `CrossingLandLive() || FaceUpLive() || GraceLive()`, shared by the fold and the price.
  - The fold applies when it is false (and EOT dominance is not live).
  - The meter is armed only when it is true, on a finite budget, with `MTG_DEAD_CROSS_UNITS` on.
- Otherwise the price is c8a895804's: W=2, counts dead and failed crossings, stops on `Exhausted() || Overrun()`, inert at budget 0.
- The scope depends only on the fan's structure, never on the fold flag, so the two levers are independent: a fan the fold covers is unbilled even with `MTG_DEAD_FAN_FOLD=0`.
- Unit tests:
  - `test_mazes_end_i3.cpp`: a Manabond (crossing-land) fan is billed on a finite budget and never on an unlimited one.
  - `test_gods_dredge.cpp`, new: "Dead-cross unit price: a fan the dead-fan fold covers is never billed" (fold off, 2 dead crossings walked on a finite budget, units equal with the price on and off).
  - `test_gods_dredge.cpp`, existing: the fold test.

## g1380 / g1647 (single games, lane, 2 slots, bin_scoped = a5e940460)

| game | digest | units | win turn | wall / CPU |
|---|---|---|---|---|
| g1380 (s31301380 gi1380) | 128e2cac5c272c7a | 211,477 | T6 | 4.9 s / 4.6 s |
| g1647 (s31301647 gi1647) | 3f33b93479fdd0b6 | 74,808 | T7 | 1.3 s / 1.2 s |

Both are identical to the unscoped price. Before any of this work, g1380 took 990-1937 s.

## Held-out A/B (`$S/ab3`)
- ab2's 108 shapes on fresh seeds, 64000000 + 1000·idx. 600 games each on the 12 shapes the price moved, 30 on the rest; 20,160 games.
- Arms: `f1` (fold on, price off) and `on` (fold on, scoped price on).
- One pooled batch under the box lock at 32/32 workers, 36 min wall, 66,799 s CPU.
- Analysis: `python3 $S/abcmp.py $S/ab3 on f1`.

| shape | net turns | faster / slower | units | CPU f1 -> on | digest |
|---|---|---|---|---|---|
| Maze std | 0 | 0 / 0 | 1.004 | 2499 -> 2503 s (1.002) | DIFF |
| **Maze 2HG** | **-1** | 1 / 0 | 1.008 | 3176 -> 3040 s (**0.957**) | DIFF |
| Gods std | 0 | 0 / 0 | **1.000** | 7033 -> 7459 s (1.061, timing noise) | **SAME** |
| Gods 2HG | 0 | 0 / 0 | **1.000** | 9558 -> 9558 s | **SAME** |
| ExtraTurns 2HG | 0 | 0 / 0 | 1.000 | 1962 -> 1961 s | SAME |
| Shadowborn | 0 | 0 / 0 | 1.000 | 806 -> 803 s | SAME |
| AntiLife 2HG | 0 | 0 / 0 | 1.000 | 99.5 -> 96.7 s | SAME |
| Creature 2HG b30 | 0 | 0 / 0 | 1.000 | 584 -> 575 s | SAME |
| Landfall Elementals std | 0 | 0 / 0 | 1.001 | 936 -> 944 s | SAME |
| Landfall Elementals 2HG | 0 | 0 / 0 | 1.003 | 2830 -> 2792 s | SAME |
| Martyr std | 0 | 0 / 0 | 1.001 | 573 -> 560 s | SAME |
| Martyr 2HG | 0 | 0 / 0 | 1.000 | 737 -> 734 s | SAME |
| Morph 2HG (30 games) | 0 | 0 / 0 | 1.000 | 26.1 -> 26.0 s | SAME |
| every other shape (30 games each) | 0 | 0 / 0 | 1.000 | about 1.00 | SAME |

- Every non-Maze deck plays identically; Gods and Gods 2HG match on units too.
- Elementals, Martyr and Morph 2HG move +0.1% to +0.3% in units with identical digests: their fans have a pre-advance axis live (Abiding Grace end-step grace, morph flip, Elementals' end-of-turn tap-put), so the price bills a few units, but no game changed.
- No slower games in any shape, so no d8 b0 / NC replays were needed (`$S/mkslow.py <abdir> <manifest> on f1 d8b0|nc <out.json>` is ready).
- The fold's own CPU gain (ab2, f1 vs f0, byte-identical): Gods 2HG x0.968, Elementals 2HG x0.941, AntiLife 2HG x0.924. Gods VERIFY: 718,047 folds, 0 violations.

## Verdict
- **Fold (`1619f7be4`)**: sound, byte-identical, default ON.
- **Scoped price (`a5e940460`)**: passes. Non-Maze decks identical; Maze std net 0, Maze 2HG net -1 at CPU x1.00 / x0.96; no slower games; g1380 ~990 s -> ~5-15 s on the same win turn. Default ON; it moves Maze's play, so the landing needs a smoke + regression GT accept.

Outputs: `$S/ab3/{out,err,log}`, `$S/sc/{g2.out,g2.err,unit.out}`, `$S/split/unit_fold.out`.
