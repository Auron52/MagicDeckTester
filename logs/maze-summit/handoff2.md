# Maze's End Guild Summit pay-first, handoff 2 (written 2026-11-22 ~15:50 UTC, for a Codex agent with no context)

Read `logs/maze-summit/handoff.md` first (the full background, the USER rulings VERBATIM, and the rules). Then read root
`CLAUDE.md`. This file records only what has changed since then.

## State
- Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-summit`, branch `wip/maze-summit`.
- `9d15c0d69`: the pay-first tap set (MTG_MAZE_SUMMIT_PAY_FIRST, default OFF). The first A/B FAILED: d5 b20 net +15
  (6 faster / 21 slower), 2HG 0, d0 -17.
- The next commit on the branch (see `git log`): two TEMPORARY A/B levers on top of pay-first, both default OFF, both
  heurarm slots (so a pooled batch can set them per job):
  - `MTG_MAZE_SUMMIT_PF_NAMED`: inside a plan apply (CurrentPlanTraits() != null), the pay-first bill carries the plan's
    OWN pending trailing activations (PlanTraits::act_mana_total net of g_act_line_paid/current; their {T} sources in
    act_src_nums are set aside) INSTEAD of the speculative Maze's End {3} reserve. Outside a plan apply, the reserve
    stays. So the reserve applies only where the line really activates Maze's End (fix 1 of handoff.md).
  - `MTG_MAZE_SUMMIT_PF_ALL3`: the menu keeps the "all" pin as a third entry while an untapped Maze's End of ours
    exists: {unneeded with reserve, none, all} (fix 2).
  - Unit tests for both are in `test/unit/test_mazes_end_summit_pay.cpp`. The 5 Summit cases pass (125 assertions).
- Repro finding (sg logs, d5 g65/68/80/127): the arms diverge at T1/T2, before any Summit resolves, so the loss works
  through the LOOKAHEAD's valuation of future Summits, not through one Summit pick. Both causes (the speculative
  reserve in rollouts, the lost "all" option) are consistent with that. The 3-arm A/B separates them.
- Frozen binaries for this commit: `/workspaces/MagicDeckTester/build/scratch/claude_maze_summit/b2/{mtg,mtg-test}`.

## Running jobs
- The full unit suite, in the lane (1 slot): output `build/scratch/claude_maze_summit/unit3.log`, marker `unit3.DONE`.
- The 3-arm A/B under the box lock (it waits on flock until the lock frees): manifest `ab2.json` (made by
  `logs/maze-summit/mk_ab2.py ab2.json 2500 1000 4000`; FRESH seeds: d5 25100000+gi, 2HG 26100000+gi,
  d0 27100000+gi; arms off / named / all3; d0 has no all3 arm, because the menu is not searched at d0).
  Output `ab2.out` / `ab2.err`, marker `ab2.DONE` (all under `build/scratch/claude_maze_summit/`).
  Analyse each arm: `python3 logs/maze-summit/an_ab2.py <ab2.out> named` and `... all3`.

## Next steps
1. Check the heartbeat in `ab2.err` within 10 minutes of the batch starting.
2. Pick the arm for which every row nets <= 0 (searched first, then d0, then CPU). If neither does, root-cause the
   slower games (the arms diverge early, so compare the first diverging decision's search), and do NOT drop the USER's
   design.
3. Replay every slower game of the chosen arm at `--depth 8 --budget-ms 0` paired with off (one pooled lane batch,
   `abandon_units` set). Escalate the budget where d8 b0 cannot settle. Only CLEAR clairvoyance (MTG_NC_SEARCH=1) may stay.
4. Adopt: fold the winning lever into pay-first (delete the loser and both temporary slots), flip
   `MTG_MAZE_SUMMIT_PAY_FIRST` to default ON, delete the dead off-path menu, and fix test_mazes_end_p1d.cpp's {0,1,4}
   expectation. Then rebase onto origin/phase-1-2-deck-analyzer, rebuild, run the units, run `test/regression.sh --both`
   under the box lock, accept GT with an attributing note, and run `python3 test/check_gt_logs.py`. Update the ledger.
   Push wip/maze-summit ONLY.

## USER decision to surface (not settled)
ALL3 reintroduces the "all" option that the USER's ruling questioned ("Why do we need to search tapping all gates?").
The reason it may be needed: Guild Summit is a BREAKPOINT (it draws), so the line after it is planned only after the
draws, and the Maze's End reserve is speculative at resolution time. If ALL3 wins and NAMED does not, the USER must
be told which one was adopted, and why.

## UPDATE 2026-11-22 ~18:55 UTC
- 3-arm A/B DONE (`ab2.out`, fresh seeds):
  | arm | d5 b20 (n 2500) | 2HG (n 1000) | d0 (n 4000) | cpu d5 / 2HG |
  |---|---|---|---|---|
  | NAMED | +8 (18 faster / 26 slower) FAIL | +3 (6/9) FAIL | -57 (99/50) | 1.003 / 0.985 |
  | ALL3 | **-1 (10/9)** | **-1 (3/2)** | = plain pay-first: -17 (43/23, old seeds 27000000+gi) | 1.003 / 0.999 |
  ALL3 slower: d5 g331 g513 g607 g973 g1094 g1585 g1667 g2021 g2349 (seed 25100000+gi); 2HG g137 g292 (26100000+gi).
- ADOPTION prepared and committed (`git log`: "pay-first tap set DEFAULT ON, with the all pin ..."): ALL3 folded in
  unconditionally, NAMED deleted, both temporary heurarm slots removed, MTG_MAZE_SUMMIT_PAY_FIRST default ON (=0 is the
  hatch), P1d tests updated. The branch is REBASED onto origin/phase-1-2-deck-analyzer (HeuristicArm.h conflicts resolved:
  both sides kept). wip/maze-summit was force-pushed (with lease) after the rebase.
- RUNNING: the d8 b0 paired replay (off vs all3, the frozen b2 binary) of the 9 + 2 searched slower games and the 23 d0 slower
  games of the first A/B: manifest `d8.json` (`logs/maze-summit/mk_d8.py`), under the box lock, `abandon_units` 2e7.
  Output `d8.out`, marker `d8.DONE`. Analyse with `python3 logs/maze-summit/an_ab2.py d8.out all3` (a recovered game
  is not slower at d8 b0; an abandoned game, avg 0, needs an escalated-budget replay instead).
- RUNNING: the lane rebuild of the rebased adoption commit: log `build3.log`, marker `build3.DONE`.
- NEXT: when both finish: run the units (lane, 1 slot), then `test/regression.sh --both` under the box lock
  (MTG_PGO=0, MTG_RSS_CAP_GB=40), accept with an attributing note, run `test/check_gt_logs.py`, update the ledger, and push.
