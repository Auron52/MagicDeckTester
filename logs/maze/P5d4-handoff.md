# Maze's End P5d -- P5d-4 checkpoint/handoff (2026-11-19). Predecessor: logs/maze/P5d3-handoff.md (USER rulings VERBATIM there)

Worktree `.claude/worktrees/maze-p5d`, branch `wip/maze-p5d`. Push ONLY wip/maze-p5d. Scratch `build/scratch/maze/p5d4/` (= `$P4`),
P5d-3 scratch `build/scratch/maze/p5d/` (= `$S`).

## Working tree (UNCOMMITTED until build10 verifies): the CLEAN candidate
- `TapTutorPutEval` no-drop tier: full also while Amulet of Vigor is ours (`MazeAmuletOurs`) -- Maze's End's next-turn replay
  re-enters untapped, so the "activation cycle lost" premise of the 0 is false.
- Prune (a) `MazeIdleFetchAdmit`, lever `MTG_MAZE_IDLE_FETCH` (heurarm `MAZE_IDLE_FETCH`), DEFAULT ON (P5 provider-restriction
  convention), plain form (no Manabond amendment).
- Binaries: `$P4/mtg.p4` = build9 TEMP tree (`logs/maze/P5d4-build9-temp.patch`: Amulet default + TEMP slots MTG_P5D_IDLE /
  _IDLE_MB / _EV_*); `$P4/mtg.p5` = build10 clean candidate.

## Results
- abb AMU (d8 b0, mtg.p3): Amulet tier (AMU1 and AMUF) recovers d5 g499 6->5 and 2HG g287 7->6; g635 stays 6 (= EV_A digest).
- cmb: g635 recovers only under EV_A+B or EV_A+C2 (= shipped-eval digest 0af18ffeb). b2000: d1b3 g4 u2 5 vs ship 6; d0 g270 5 = 5.
- ac blind g635 (d8 b0, salts 1-4, shipev / fix / fixc2): 6/6/6, 6/6/6, 6/6/6, 7/6/7 -> fix never worse blind: CLAIRVOYANCE.
- abb prune (a) vs u2 (clairvoyant): idle d5 +12 2HG +4 d3 +1 d1b3 +4 d1b1 -2; mb +10 +4 +1 +4 -2.
  BLIND (vs u2B): idleB d5 0 (34/34) 2HG 0 (15/15) d3 +2 d1b3 -3 d1b1 -6 (std -7, 2HG 0); mbB +2 +1 +3 -4 -6 (std -5, 2HG +1).
- ac vs ab1 ship (clairvoyant, net turns): fix d5 -8 2HG -1 d3 -4 d1b3 -8 d1b1 +4 d0 -16; fixidle +4 +3 -3 -4 +2;
  fixmb +2 +3 -3 -4 +2; fixc2 -11 0 -6 -6 0 -16.

## Running
- rc (`$P4/run_rc.sh`, marker `$P4/rc/done`): d8 b0 of the 132 candidate-slower ac games (cand mtg.p5; ship mtg.ab where not
  already in `$S/d8b`, `$S/d8`) + identity check `ID_*` of mtg.p5 vs ac fixidle (d0: vs fix).

## Remaining
1. rc -> recovery table; blind-attribute any not recovered. 2. commit clean candidate, squash P5d commits, smoke + regression,
accept `--deck=mazesend,mazesend2hg`, check_gt_logs, push wip/maze-p5d. 3. ledger P5d section + TapTutorPutEval comment numbers.
4. Report: C2 (lands-short tier 100 instead of the USER's 1000 "win out") measures -6 better overall but is the USER's call.
