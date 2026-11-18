# P5b handoff (Maze's End identity folds)

Branch `wip/maze-p5b` (worktree `.claude/worktrees/maze-p5b`, from 7093daceb). Results: `logs/maze/P5b-results.md`.

Done:
- 28ddf156a: fold 2 (MTG_FOLD_LAND_ORDER) + LandEntryOrderClass hook (Maze override) + heurarm slot; fold 3 unit test
  (test_mazes_end_p5b.cpp); census comment (copy price is an upper bound).
- Next commit (this handoff): fold 2 search-only (g_rollout_nest == 0, context bit in FoldPhysicalCards), DEFAULT OFF.
- Fold 1: no code (the merge exists; residual = Maze's End battlefield copies, order-carrying).
- Fold 3: no fold (unpinned default != pins.front(); unit test).

DONE 17:00 UTC: stage 3 ALL PASS (see P5b-results.md). Nothing remains but the USER decisions.
MTG_BUILD_JOBS=12 build, all units, then `bash test/regression.sh --smoke --regression --deck=mazesend,mazesend2hg`.
Progress/DONE marker: build/scratch/maze/p5b/heavy3.progress; logs build3.log, units3.log, tiers3.log there.
Expected: every mazesend key PASSES (fold off == GT). If a key FAILs, the default-OFF claim is wrong: diff the key
against GT before anything else. No GT accept is owed if it passes. Nothing is pushed to the shared branch.
Measurements so far: logs/maze/P5b-results.md (A/B 1-3, d8 b0, root cause).

USER rulings (VERBATIM): "I thought we already did the duplicate merge, but if not then yes, let's do it."
"7. True, the order doesn't matter much here unless the second land is played after other things or we are paying
for a cost like Gateway Plaza." "the top priority are optimizations and fixes."

Incident: my cleanup of my own killed tier run (pattern kill on MTG_JOBSERVER / test/logs/*/mtg.run) also killed
the maze-p6 agent's regression run that took the lock at 16:33:12 (its regression_result_regression.txt holds only
the header). It needs relaunching by its owner.
