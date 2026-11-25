# land-crit report (2026-11-25)

Branch `wip/land-crit` (worktree `.claude/worktrees/land-crit`), from `origin/phase-1-2-deck-analyzer` 05e2e0030.
Prepared for landing, NOT landed: no tiers run, no GT accepted (the coordinator runs one combined `--both` + accept).

USER ruling (2026-11-25): "If it is slightly better, more correct in your understanding and doesn't have much CPU cost
let's do it." -> MTG_LINE_KEEP_CRIT ADOPTED default ON.

## Commits
- b0fa03667  cherry-pick of 864969114 (MTG_LINE_KEEP_CRIT code + units + the three fixture pins)
- 8dc593eda  cherry-pick of f04665636 (doc + mk_ab.py --extra)
- 9691382fb  adoption: comment/slot text "ADOPTED default ON (USER 2026-11-25)"; MTG_LKR_PLAYOUT_OFF exempts crit lines; doc status
- (skipped 08e979d9e: handoff-only)

## Conflict decisions
- `src/ai/HeuristicArm.h`: origin deleted the UO_BUDGET_PRICE / UO_BUDGET_CUT slots (923a5a8d7) and added
  LKR_PLAYOUT_OFF / MAZE_SUMMIT_PAY_FIRST. Kept origin's slots; appended LINE_KEEP_CRIT after MAZE_SUMMIT_PAY_FIRST in
  both the enum and the name table.
- The flag was already `EnvOn("MTG_LINE_KEEP_CRIT", true)` on wip/bruna-crit2; only the text changed.
- **Semantic interaction (not a textual conflict):** origin's MTG_LKR_PLAYOUT_OFF (adopted 2026-11-24, not in the
  crit A/B binary's base e8a2f7d64) skips the line-keep rung inside horizon playouts for every no-reducer-credit line.
  After the merge that would also have dropped the crit rung in playouts -- a composition neither A/B measured. The
  skip now tests `g_line_keep_bound != kLineKeepCrit`, so rock lines skip in playouts (as LKR_PLAYOUT_OFF was
  measured) and crit lines keep the rung there (as the crit A/B measured). The alternative (extend the cut to crit
  lines, cheaper) is a one-condition change and would need its own measurement.

## Deleted flags
None: wip/bruna-crit2 added no flag other than MTG_LINE_KEEP_CRIT (MTG_LINE_REDUCER_KEEP_STATS pre-existed).

## Fixtures
The three historical-drop unit fixtures that pin the lever OFF (recompiled in this build of mtg-test):
`test/unit/test_angels_cast_order.cpp` (Angels Remote Farm), `test/unit/test_line_generic.cpp` (EDF Wild Growth +
Cloud of Faeries), `test/unit/test_mana_oracle.cpp` (EDF Training Grounds + Eldrazi Displacer viewer line check).

## Validation (Release, `./build.sh` under claude_build.lock, MTG_BUILD_JOBS=4; build exit 0)
- Units: `build/Release/mtg-test` (box_lane, 2 slots): **4174 / 4174 test cases, 3,670,624 assertions, 0 failed**.
- Scenarios: `test/scenarios.sh` (SCEN_JOBS=2, box_lane 2 slots): **677 passed, 0 failed, 0 error**.
- NOT run, by brief: smoke / regression tiers, GT accept (the coordinator's combined `--both` on the landing batch).
  This branch moves budgeted play (crit ON), so smoke + regression GT must be accepted with it.

Scratch: /workspaces/MagicDeckTester/build/scratch/claude_land_crit/ (build.log, unit.out, scen.out).
