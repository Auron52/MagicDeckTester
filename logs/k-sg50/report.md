# K fleet-wide + SG50 integration (agent report, 2026-11-27)

Written by the parent from the agent's final message.

USER rulings (2026-11-27, verbatim):
- K: *"If it is a fully safe optimization, which it seems like it is, let's just turn it on and see how it does."*
- SG50: *"Overall it looks quite good, though, so I would probably turn it on even with a small cost."*
- CG beam drop: *"Okay, in that case I could see the argument for the change, though it sounds much less serious with the new SG50 option on."* -> not decided; no beam_width changed.

## Commits (on origin f63e48b0f)
1. 2b22d665b SG50: the net src diff e430cc585 -> 0c2465c8a (a359f1d76 rests on three lever commits that are not on the shared branch and deletes everything they added). Applied cleanly; HeuristicArm.h / ValueArm.h / BatchRunner.cpp zero net change; leftover MTG_ESC_BEAM* only in scripts/attic/.
2. d58584906 logs/beam-staged + logs/beam-adopt (logs only; the d3 code comment cites report.md).
3. e9f53bfb7 K default ON for every eligible deck: `LeafKillOnlyNoM2On()` = `EnvOn("MTG_LEAF_KILL_ONLY_NOM2", true)` (=0 still off). Deleted DecisionProvider::LeafKillOnlyNoM2OptIn + Cyclone's override. Comments (HeuristicArm.h, TurnSolver.cpp KO-boundary note), cyclone-completion-review.md (USER adoption verbatim), analysis-Unpredictable Cyclone.md QC2 answered. test_leaf_kill_only.cpp: the generic NOM2 case runs unpinned and checks the default arms a non-Cyclone eligible deck.
4. 6468c599d logs/k-fleet (logs only).
5. 06e64eb76 GT accept smoke + regression.

## Validation
- Release build clean; units 4330/4330; test/play_setting_beam_selftest.py 8/8.
- Tiers (one pooled --both, makespan 24m16s): 40 configs moved (15 smoke, 25 regression). Searched: 5 slower, 16 faster.

| deck | net | slower / faster |
|---|---|---|
| Maze's End | -8 | 0 / 8 |
| Maze's End 2HG | -1 | 0 / 1 |
| Creature Giving | -6 | 0 / 6 |
| Gods | 0 | 1 / 1 |
| Anti-Lifegain | +1 | 1 / 0 |
| Dragonstorm | +1 | 1 / 0 |
| Elementals | +1 | 1 / 0 |
| Gods 2HG | +1 | 1 / 0 |

- d0: 0 slower, 0 faster, 0 play changes. 116 same-score play changes (Maze's End 69, CG 13, AL 7, others 1-3): budget re-spend.

## Slower-game verdicts
- Recover at 4x/16x: antilife_smoke_d5_s1001 gi166 (6->7->6), dragonstorm_regression_d5_s2002 gi171 (5->7->5), elementals_regression_d3_s2002 gi38 (4->5->4).
- gods2hg_smoke_d5_s1001 gi17 (T8 -> unwon): at 4x and 16x it wins T6, better than the old T8.
- gods_regression_d3_s2002 gi16 (6->7) persists but is not real: from K (=0 reproduces the GT digest exactly); d8 b0 plays T7 with K on and off (same digest); MTG_NC_SEARCH=1 at d3 b10 plays T7 on both arms -> old T6 was budget luck / clairvoyant.

## CPU (rough; no clean baseline run)
Vs the nearest earlier tier run (land-1129 10ff7c0de, 12 later files differ), searched configs excl. Elves: units x0.969, CPU x0.974. Maze's End x0.77, Maze's End 2HG x0.63. Anti-Lifegain (K does not arm) is the only deck clearly up: units x1.012, CPU x1.025.

Checks: check_gt_logs 1457 consistent / 0 stale; check_references_safe passes. Scratch: build/scratch/k-sg50/ (tiers.log, cls_*.log, Gods replays under sl/).
