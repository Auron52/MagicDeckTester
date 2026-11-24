# Maze's End Guild Summit pay-first, handoff 3 (2026-11-24 ~12:50 UTC; Codex-readable)

Read `handoff.md` (background + USER rulings VERBATIM) and `handoff2.md` first. This records the integration.

## State: READY TO LAND (the coordinator lands it; this agent never pushes phase-1-2-deck-analyzer)
- Branch `wip/maze-summit`, worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-summit`, REBASED onto
  `origin/phase-1-2-deck-analyzer` = `20b9994d5` (never merged). Pre-rebase tip kept as local branch
  `backup-pre-rebase3-maze-summit` (7a5417a42).
- Rebase fix: the ManaPayment.cpp conflict (both sides appended after the anonymous namespace) first landed
  `LinePayKeepSet` INSIDE `AuraHostHoldKeepsLine`; the missing `}` was folded into the first maze commit (198d703d1).
  HeuristicArm.h: origin's new slots kept, `MAZE_SUMMIT_PAY_FIRST` appended last (enum and names both); the temporary
  PF_NAMED / PF_ALL3 slots are gone.
- Commits on top of 20b9994d5: 198d703d1 (pay-first, off) .. 40058408e (DEFAULT ON + all pin) .. 1f2833d86 (twin-fold
  tests pin =0), 694777217 (GT accept), 09405d717 (ledger section), then this handoff.
- Release build of 1f2833d86's src (GT commit changes no src): frozen copy `build/scratch/claude_maze_summit3/bin/`.

## Results
- Units: 4167/4167 test cases, 3,670,341 assertions pass (`build/scratch/claude_maze_summit3/unit.log`).
- `MTG_PGO=0 MTG_RSS_CAP_GB=40 test/regression.sh --both` (`.../claude_maze_summit3/reg.log`): only Maze's End cases moved
  (3 per tier); every other deck byte-identical; VPC 0 play-drift; REQUIRE-BEAT ok.
  | case | old -> new | games | slower / faster |
  |---|---|---|---|
  | smoke d5 s1001 (PLAY SETTING) | 5.5985 -> 5.6136 (+2) | 132 | 2 (gi16 4->5, gi41 5->6) / 0 |
  | regression d5 s2002 (PLAY SETTING) | 5.4891 -> 5.4783 (-2) | 184 | 0 / 2 |
  | regression d5 s3003 (PLAY SETTING) | 5.5133 -> 5.5067 (-1) | 150 | 0 / 1 |
  | 2HG smoke d5 | unchanged | 30 | 0 / 0 |
  | smoke d3 | same score, digest moved | 10 | 0 / 0 |
  | smoke d0 | 6.7300 -> 6.7120 (-18) | 1000 | 3 / 16 |
  | regression d0 | 6.7200 -> 6.7140 (-6) | 1000 | 7 (gi594 8->unwon) / 13 |
- Play setting overall: held-out A/B -1 (n 2500) and tier -1 (n 466); 2HG A/B -1 (n 1000), tier 0.
- Slower games, paired off/on replay (`.../claude_maze_summit3/ts.out`, made by `mk_tier_slow.py`): every tier-setting
  row reproduces the tier, and ALL 12 recover at d8 b0 (on = off): smoke d5 gi16 (4/4), gi41 (5/5); smoke d0 gi444,
  663, 806; regression d0 gi33, 173, 249, 357, 555, 584, 594.
- 27 searched play-changed games at the same score: 24 cast a Guild Summit (the tap set changed); 3 (smoke d3 gi7,
  regression d5 gi106 / gi183) cast none on the winning line and diverge through the lookahead's valuation of later
  Summits (handoff2: the arms diverge before any Summit resolves).
- GT accepted scoped (`--deck=mazesend,mazesend2hg`, note in regression_gt.txt), so batch-1123's notes are kept.
  `test/check_gt_logs.py`: 1435 consistent, 0 stale.

## g292 (2HG, seed 26100292, gi 292, 30 life, 2 heads): UNRESOLVED
- Pay-first T6 vs off T5 at d5 b400 / b1280 / b5120 / b0 (`g292_esc.out`, `mk_g292.py`), plus d8 b0 (handoff2). The
  pay-first digest c444b63873a7de26 is the same at every budget (converged); off is de38987a6c5c3d1e at b1280+.
- NC (`MTG_NC_SEARCH=1`) at d8 b0 ran 15 h, and at d5 b20-b1280 ran 7.5 h. Neither finished, and the coordinator
  killed both. So it is not shown to be clairvoyance, variance, or fixed. The next step, if wanted: a logged pair at
  d5 b0 (`--game-log-dir`) to find the first diverging decision.

## Open items
1. USER: accept g292 as an unresolved single 2HG game (the 2HG aggregate is -1 held-out and 0 in the tier), or have it
   root-caused first?
2. Overnight GT is left for the periodic overnight run.
3. The coordinator lands the branch through the push queue. This agent pushed `wip/maze-summit` only.
