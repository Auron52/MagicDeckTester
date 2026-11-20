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

## Update 2026-11-19 ~22:40 UTC
- Squashed: P5d = `2b72ec017` (tag `p5d4-pre-squash` = the unsquashed chain, identical tree). Clean binary `$P4/mtg.p5`
  == ac fixidle (d0: fix) on 500 identity games.
- rc (d8 b0): 120/132 candidate-slower games recover (d0 37/37, d3 11/11, d1b3 8/9, d1b1 7/9, d5 41/48, 2HG 16/18).
- bl (blind, cand vs ship, salts 1-4, d8 b0): 10 of the 12 clairvoyance (cand <= ship blind). PERSIST blind: d5 g231 (+2:
  7/7 9/7 6/7 8/7) and d5 g320 (+4: 8/7 6/6 6/6 9/6). g231: P5d-3 b3 had u = idle = 28 = ship 28 blind -> the Amulet tier.
- Running chain (`$P4/run_chain.sh`): units -> `regression.sh --both` (markers `$P4/tiers/units.done`, `both.done`); then
  bl2 (`$P4/bl2/done`: fix-only blind on g231/g320, u2/u2idle on g320) then blw (`$P4/blw/done`: sample-wide blind
  fixB/candB/shipB at blind_salt 1, compare with abb u2B/idleB).

## Update 2026-11-20 ~02:30 UTC (P5d-4 stopping at ~215K context)
- DONE: units 3655/3655; smoke+regression all decks: only 10 Maze keys moved; accepted `--deck=mazesend,mazesend2hg`
  (`d616a508a`), check_gt_logs 0 stale; ledger "## P5d" + TapTutorPutEval comment (`02b566d48`); pushed wip/maze-p5d
  (force-with-lease over the pre-squash 4cc103057).
- FINDING: d0 is +459 turns / 20k vs ship (fresh @310001, `$P4/d0/`); cause = kMazeFetchWins (1000, the USER's "win out")
  outranking Exploration / Guild Summit / Burgeoning in the d0 greedy. C2 (tier = full 100) fixes it (-190 vs ship) and
  is better on every searched row -- OWED to the USER, NOT shipped.
- PENDING (queued on heavy.lock, launched by P5d-4; no waiter survives this agent -- set your own):
  * bl2 (`$P4/bl2/run.sh`, marker `$P4/bl2/done`, outputs `p5.out` fix-only blind g231/g320 salts 1-4, `p3.out` u2/u2idle
    blind g320). Read: does the Amulet tier alone (fix) make g231/g320 worse blind?
  * blw (`$P4/blw/run.sh`, marker `$P4/blw/done`, `p5.out` fixB/candB, `ab.out` shipB; blind_salt 1, abb seeds). Compare with
    `$S/abb/run.out` u2B/idleB: `cat $S/abb/run.out $P4/blw/*.out | grep -v ^AMU > x; python3 $S/anab.py x shipB`.
  If the Amulet tier is net worse BLIND sample-wide, root-cause g231/g320 (logs: CLI can't set heurarm slots; use
  `MTG_MAZE_IDLE_FETCH` env / a TEMP build) and update the ledger P5d section's "Two persist blind" line.
- bl2 DONE: blind totals (salts 1-4) g231 ship 28 / u2 28 / u2+(a) 28 / fix 29 / shipped 30; g320 ship 25 / u2 26 /
  u2+(a) 29 / fix 26 / shipped 29. g320 s4 (seed 201321, game_index 320, blind_salt 4, d8 b0): prune (a) 6 -> 9 BLIND on
  both bases = first non-clairvoyant (a) loss. NEXT: root-cause it (which removed plan, which turn), then report.
