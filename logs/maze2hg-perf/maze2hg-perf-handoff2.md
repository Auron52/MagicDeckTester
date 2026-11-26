# maze2hg-perf handoff 2

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze2hg-perf`, branch `wip/maze2hg-perf` (NOT pushed by this
agent; the shared branch is never pushed from here). Scratch `$S = /workspaces/MagicDeckTester/build/scratch/maze2hg-perf`.

## Done (commits)
- **3dffb8a9c** `MTG_DEAD_CROSS_UNITS` (heurarm `DEAD_CROSS_UNITS`, default ON), `MTG_DEAD_CROSS_UNIT_W` (default 2).
  `DeadCrossMeter` in src/ai/TurnSolver.cpp charges one budget unit per W dead crossings of ONE boundary fan (unit
  site `dead_cross`, census column `u_dead_cross`). It is armed only on a finite budget
  (`budget != nullptr && !Unlimited()`), so it is never read at budget 0. In this commit the fan stops on `Overrun()`
  only. Unit test: test_mazes_end_i3.cpp "dead fan crossings charge a finite unit budget, never an unlimited one".
- **c8a895804** (WIP, builds, 144/144 relevant unit tests pass):
  - The meter's stop is now `Exhausted() || Overrun()`. With Overrun only, a decision ran on to the pass overrun
    ceiling of 25 x budget: g1380 T5 charged 450,052 units, about 900k dead crossings, 62 s.
  - THE DEAD-FAN FOLD (`MTG_DEAD_FAN_FOLD`, heurarm `DEAD_FAN_FOLD`, default ON; `MTG_DEAD_FAN_FOLD_VERIFY=1` keeps
    walking and counts violations). See the design below. Unit test: test_gods_dredge.cpp "Dead-fan fold: a
    post-advance-only boundary fan stops at its first refused crossing".
- Step 1 of the brief, verified: t_before (`$S/t_before`) and t_p0 (`$S/t_p0`) both give digest 833668b2aae5ae1f
  and 172,253 units.
  - t_before: 1937 s wall, 1706 s CPU (contended).
  - t_p0: 1554 s wall, 1368 s CPU.

## Measurements

### g1380 / g1647, one game each in the lane, contended box

| arm | g1380 digest | units | wall | CPU | g1647 digest | units | wall | CPU |
|---|---|---|---|---|---|---|---|---|
| e430 (before) | 833668b2 | 172,253 | 990-1937 s | 962-1706 s | 6b119926 | 63,114 | 27.5 s | 23.8 s |
| c5c8877d9 price only | 833668b2 | 171,522 | 410 s | 356 s | 6b119926 | 63,114 | 25.0 s | 21.7 s |
| 3dffb8a9c units, Overrun stop | 833668b2 | 1,973,590 | 252 s | 221 s | 6b119926 | 311,379 | 26.9 s | 23.0 s |
| units, Exhausted stop (bin_units2) | 128e2cac | 211,477 | 15.6 s | 13.2 s | 3f33b934 | 74,808 | 4.3 s | 3.3 s |
| + fold (bin_fold, c8a895804) | 128e2cac | 211,477 | 15.3 s | 13.4 s | 3f33b934 | 74,808 | 4.2 s | 3.5 s |

- Win turns are unchanged: g1380 wins on T6, g1647 on T7.
- The fold VERIFY run on these two games: folds=0, violations=0. The crossing-land axis is live in their fans, so the
  fold does not apply to them; this run is no evidence about the fold's soundness.
- Outputs: `$S/u1380`, `$S/u1647` (Overrun stop), `$S/v1380`, `$S/v1647` (Exhausted stop), `$S/fq` and `$S/fqv`
  (fold, and fold VERIFY). Each directory has out, err and census.tsv.

### Census (`$S/census`)
- 108 smoke play shapes, 6 games each, on/off pooled.
- Units move on 12 shapes in 8 decks:
  - mazesend and mazesend2hg
  - extraturns2hg
  - gods and gods2hg
  - shadowborn
  - antilife2hg
  - creature 2HG b30
  - elementals and elementals2hg
  - martyr and martyr2hg

### Held-out A/B of the unit price (`$S/ab`, binary bin_units2 = 3dffb8a9c plus the Exhausted stop, no fold)
- 600 paired games on each of the 12 shapes, plus 30 games on every other shape. Seeds 62000000 + 1000*idx.
- Net = on - off in turns; a loss counts as 9. Analyse with `python3 $S/abcmp.py $S/ab on off`.
- No other shape moved, apart from tiny CPU noise.

| shape | net | faster / slower | units | CPU off -> on |
|---|---|---|---|---|
| mazesend | +1 | 0 / 1 (gi205 6->7) | 1.005 | 2627 -> 2616 s |
| mazesend2hg | 0 | 0 / 0 | 1.013 | 3038 -> 3027 s |
| **gods** | **+12** | 13 / 23 | 0.847 | 6162 -> 4136 s (0.671) |
| **gods2hg** | **+47** | 9 / 45 | 0.976 | 7153 -> 5483 s (0.767) |
| extraturns2hg, shadowborn, antilife2hg, creature2hg, elementals (both), martyr (both) | 0 | 0 / 0 | 1.000-1.002 | 1.00-1.03 |

- **Verdict: NOT adoptable as-is.** Gods loses 0.020 turns per game (standard) and 0.078 (2HG). The slower games are
  all listed by the script: 23 in Gods standard, 45 in Gods 2HG, plus Maze gi205.
- d8 b0 and NC recovery checks were NOT run. They are moot until the Gods net is fixed.
- Diagnosis: Gods' Klothys / Keranos boundary fans hold many dead crossings, about a third of the deck's CPU. Billing
  them exhausts the budget, so passes get truncated or rejected.

## The dead-fan fold (design; c8a895804)
- A crossing either ends the game BEFORE the turn advances, or advances the turn number. If it ends the game, the
  win is at this turn, because SimulateEndAndStartNextTurn leaves the turn number unchanged.
- PRE-advance axes, read in our end step or the opponent's turn:
  - cl_: the crossing lands (Manabond, Burgeoning, the off-main Maze's End)
  - fu_: the morph flip
  - grace_: the end-step grace reanimation
- Every other axis is read AFTER the advance: echo, DotH, tutor, stut, land, emeria, ulk, horn, miracle, dredge,
  keranos, klothys.
- With no pre-advance axis live, every crossing has the same pre-advance history. So once one crossing comes back
  REFUSED, every later one reaches the same turn and is refused too, or fails. The cut cannot move: only a crossing
  that is not refused can improve the incumbent.
- The fold breaks after the first refused crossing has been processed exactly as before, so best_tail and its pins
  are unchanged.
- It is off while EOT dominance is live (`DomActive()`).
- It runs at all 4 FSLineTail sites (`DeadFanFold dff(fan)`; `dff.After(dead_x)` at the end of the loop).
- Known non-result side effects of the skipped crossings. Neither changes a child's answer or its units:
  - fewer priced work counts (`kDeadCross`, `kPay`)
  - fewer `eot_seen` inserts at sites 2 and 4
- Expected effect: Gods' dead fans collapse to one crossing each, soundly, so the unit price no longer bills Gods.
  Maze's cl_ fans are not covered by the fold and stay on the price.

## Remaining, in order
1. Run the prepared three-arm held-out batch (it needs the box lock):
   - Command: `nohup $S/ab2/run.sh &`. Manifest `$S/ab2/ab2.json`: 324 jobs, 30,240 games, seeds 63000000+, binary
     bin_fold.
   - Arms: `_f0` (fold off, price off), `_f1` (fold on, price off), `_on` (fold on, price on). It will take about
     45 min at 32 threads.
   - Analysis:
     - `python3 $S/abcmp.py $S/ab2 f1 f0` must show every digest and unit count identical (the fold is exact), with
       CPU lower where it fires.
     - `python3 $S/abcmp.py $S/ab2 on f1` is the price A/B on top of the fold.
   - Also run a VERIFY lane job on the Gods shapes (for example 60 games each, `MTG_DEAD_FAN_FOLD_VERIFY=1`): it must
     show violations=0.
2. If the fold is identical and the price on top of it is net <= 0 on every deck: run d8 b0 (and the NC check) on
   every slower game. Otherwise, root-cause the deck that is worse, as was needed for Gods. Options: reduce the
   price (W=4), or price only fans whose pre-advance axis is live.
3. GT: smoke and regression with the adopted defaults; Gods and Maze rows will move.
4. Update the report.

## USER rulings carried in the brief (verbatim)
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- "adopt if decks gain on average at play settings; ignore seed noise and clairvoyant losses; d0 barely counts".
- CLAUDE.md: "A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A LINE UNFINDABLE"; "A sound skip is always preferred over a
  price" (brief).

## Running jobs
None. Everything this agent started has finished.
