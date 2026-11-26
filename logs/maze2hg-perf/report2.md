# Maze's End 2HG slow games, part 2: dead crossings on the unit budget

Written by the parent session from the agent's final message (its harness blocks report files).


Commits on wip/maze2hg-perf: 3dffb8a9c, c8a895804 (WIP), c71525197 (handoff2). Handoff: `logs/maze2hg-perf/maze2hg-perf-handoff2.md`.

### Leftover jobs from the previous agent
Both finished with digest 833668b2aae5ae1f and 172,253 units. t_before took 1937 s wall / 1706 s CPU; t_p0 took 1554 s / 1368 s. The box was contended for both.

### What changed
1. **The unit price, `MTG_DEAD_CROSS_UNITS`** (default ON, per-job lever).
   - Every 2 dead crossings in one boundary fan cost 1 unit (`MTG_DEAD_CROSS_UNIT_W`, unit site `dead_cross`).
   - It is only armed on a finite budget, so it is never read at budget 0.
   - The fan stops on `Exhausted() || Overrun()`. Stopping on `Overrun()` alone was not enough: the pass overrun ceiling is 25x the budget, so g1380's T5 decision still charged 450k units and took 62 s.
2. **The dead-fan fold, `MTG_DEAD_FAN_FOLD`** (default ON, per-job lever). This is the sound skip, but it covers a different class of fans than g1380's.
   - Only three axes are decided before the turn advances: crossing lands, the morph flip and the end-step grace reanimation.
   - A fan with none of them live gives every crossing the same history up to the advance.
   - So after the first refused crossing, every later one reaches the same turn and is refused too. The fan stops there with the same result, pins and units.
   - It does not cover Maze's End's own fans, because the crossing-land axis is live in them; Maze still relies on the price.
   - `MTG_DEAD_FAN_FOLD_VERIFY=1` keeps walking the fan and counts violations.
3. Unit tests for both are in `test_mazes_end_i3.cpp` and `test_gods_dredge.cpp`. All 144 relevant cases pass.

### g1380 and g1647 (single games on a contended box)

| arm | g1380 | g1647 |
|---|---|---|
| e430 (before) | 990 s wall / 962 s CPU, T6, 172,253 units | 27.5 s / 23.8 s, T7 |
| c5c8877d9 (price only) | 410 s / 356 s | 25.0 s / 21.7 s |
| units, Overrun stop | 252 s / 221 s, same digest | 26.9 s / 23.0 s, same digest |
| **units, Exhausted stop** | **15.6 s / 13.2 s**, T6 (digest moved), 211,477 units | **4.3 s / 3.3 s**, T7 (digest moved), 74,808 units |
| + fold | 15.3 s / 13.4 s, identical to the row above | 4.2 s / 3.5 s, identical |

The fold never fires in these two games (VERIFY: folds=0, violations=0), so that run says nothing yet about whether it is sound.

### Census
Across 108 smoke play shapes, the price changes units on 12 shapes: Maze's End (std and 2HG), ExtraTurns 2HG, Gods (std and 2HG), Shadowborn, AntiLife 2HG, Creature 2HG, Elementals (std and 2HG), and Martyr (std and 2HG).

### Held-out A/B of the price
This ran without the fold: 600 paired games per shape, with a loss counted as turn 9.

| shape | net turns (on - off) | faster / slower | units | CPU off -> on |
|---|---|---|---|---|
| Gods std | **+12** | 13 / 23 | 0.847 | 6162 -> 4136 s (0.67x) |
| Gods 2HG | **+47** | 9 / 45 | 0.976 | 7153 -> 5483 s (0.77x) |
| Maze std | +1 | 0 / 1 (gi205, T6 -> T7) | 1.005 | 0.996x |
| Maze 2HG | 0 | 0 / 0 | 1.013 | 0.996x |
| the other 8 shapes | 0 | 0 / 0 | about 1.00 | 1.00-1.03x |

- **Verdict: not adoptable as it stands.** Gods loses 0.020 turns per game in standard and 0.078 in 2HG.
- Cause: about a third of Gods' CPU goes into dead crossings in its Klothys and Keranos fans. Charging them uses up the budget, so passes get cut short. The fold is meant to collapse exactly those fans.
- The d8 b0 and NC recovery checks have **not been run**. They are only worth running once the Gods net is fixed.
- The list of slower games is in `$S/ab`; print it with `python3 $S/abcmp.py $S/ab on off`.

### Next, in order
1. Run the prepared three-arm batch with `$S/ab2/run.sh`. It needs the box lock: 30,240 games, about 45 minutes.
   - Fold on vs off (`abcmp.py $S/ab2 f1 f0`) must be identical in every digest and unit count.
   - The price on top of the fold is `abcmp.py $S/ab2 on f1`.
   - Also run a VERIFY lane job on the Gods shapes; it must show 0 violations.
2. If the fold is identical and every deck nets 0 or better with the price on top, run d8 b0 and NC on every slower game. If a deck still comes out worse, find the cause; the options are W=4, or charging only fans whose crossing-land axis is live.
3. Accept smoke and regression GT; the Gods and Maze rows will move.

`$S` = `/workspaces/MagicDeckTester/build/scratch/maze2hg-perf`
