# Maze's End: Guild Summit pay-first tap set, handoff (2026-11-22 ~12:45 UTC)

Written for a Codex agent with no prior context. Repo `/workspaces/MagicDeckTester`. Read `CLAUDE.md` first (it binds
Codex too), then `.claude/skills/coding-conventions.md`, `.claude/skills/regression-testing.md`, and the Maze's End
ledger `docs/design/analysis-Maze's End.md` (decisions 2, 8, 10, 30 and the P1d "B1" section).

## Where the work lives
- Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/maze-summit`, branch `wip/maze-summit`, based on
  `origin/phase-1-2-deck-analyzer` at `4a7f65983`.
- Commit `9d15c0d69` (WIP, flag default OFF): "Maze's End: Guild Summit pay-first tap set (MTG_MAZE_SUMMIT_PAY_FIRST)".
- Build: `build/` in the worktree symlinks to `/workspaces/MagicDeckTester/build/wt/maze-summit` (Release built at
  9d15c0d69). A frozen copy of that binary is `/workspaces/MagicDeckTester/build/scratch/claude_maze_summit/mtg.ab`.
- Scratch: `/workspaces/MagicDeckTester/build/scratch/claude_maze_summit/` (manifests, outputs). Summaries and
  scripts: `logs/maze-summit/` (this file, `mk_ab.py`, `an_ab.py`).

## USER ruling (verbatim, 2026-11-22)
"Why do we need to search tapping all gates? Isn't that just a special case of fitting it into the turn's plan? Heap
Gate is quite unimportant compared to any other effect, so I'm not particularly worried about holding that back. In this
deck Black Gate and Basilisk Gate don't do anything special. Baldur's Gate makes some sense. An order is probably
workable here, but generally speaking we shouldn't need to make much in terms of decisions of this sort if we are only
considering all unneeded and none. We can just pay our mana costs and then tap the rst."

Other standing USER rules the brief restated: the bar is that every deck nets <= 0 ("+1 is not neutral. 0 is
neutral."), searched AND d0, and every slower game recovers at `--depth 8 --budget-ms 0`. If d8 b0 cannot settle a
game (unit cap), replay it at an escalating budget instead ("I don't like leaving gaps"). Only CLEAR clairvoyance
(MTG_NC_SEARCH=1) may stay unrecovered. Cut any CPU cost. Searched impact matters most (USER 11-22): size the searched
samples properly and report them first. d0 must still pass. Do NOT drop the USER's design if it fails the bar:
root-cause the slower games and fix them. No greedy pick inside the search window. The tap set is a mana-payment
consequence (the standing exemption). Keep "none" searched.

## What was built (commit 9d15c0d69)
- `src/ai/ManaPayment.{h,cpp}`: `LinePayKeepSet(s, bill, cands, unavailable, keep)`. It finds an inclusion-minimal
  set of candidate Gates a real colour-aware payment of `bill` needs, with every other source spent first. The judge
  is the complete backtracker on a scratch board (`LineBillPayableOn`; a left-out Gate is tapped on the scratch).
  Gates are left out in tap-first order. Returns -1 if the bill is unpayable even with every Gate.
- `src/ai/DecisionProvider.h`: a new hook `EtbTapSubtypePayFirst(s, ctl, def, elig)`, appended at the end of the
  class. The default returns -1 (byte-identical).
- `src/ai/DecisionProviders.{h,cpp}` (search `MazeSummitPayFirstOn`): the Maze override. The bill is
  `g_line_unpaid_cost` net of `g_line_reducer_credit`, plus Maze's End `{3}` when the hold has no activations and an
  untapped Maze's End exists (its `{T}` is marked unavailable). If that is unpayable, it retries without the reserve,
  and returns -1 if still unpayable. It reorders `elig` so the unneeded Gates come first and returns their count.
  `EtbChoiceCandidates`: with the flag on, the menu is `{0 (unpinned = all unneeded), 1 (none)}`.
- `src/core/SpellEffects.h` ("GUILD SUMMIT"): an unpinned count = PayFirst (else the old default). A pin with
  0 < n < avail is also ordered by PayFirst, which keeps the Summit-twin fold sound.
- `src/ai/HeuristicArm.h`: slot `MAZE_SUMMIT_PAY_FIRST`, appended at the end of the enum and kNames.
- `test/unit/test_mazes_end_summit_pay.cpp` (in CMakeLists): the colour case, Baldur's ahead of single Gates plus the
  Maze reserve, and the 2-entry menu. All pass. FULL unit suite: 3891/3891 pass (`logs/maze-summit/unit2.log`).

## Measurement so far (held-out A/B, one pooled batch, both arms per job via "flags", MTG_PGO=0)
Manifest: `build/scratch/claude_maze_summit/ab.json` (made by `logs/maze-summit/mk_ab.py`). Seeds: d5 25000000+gi,
2HG 26000000+gi, d0 27000000+gi (game_index = gi). Output: `ab.out`. Analysis: `python3 logs/maze-summit/an_ab.py <out>`.

| row | n | net turns (on - off) | faster / slower | units ratio | cpu ratio |
|---|---|---|---|---|---|
| d5 b20 (play settings) | 2000 | **+15 (FAILS)** | 6 / 21 | 0.996 | 1.008 |
| 2HG d5 b20 | 500 | 0 | 5 / 5 | 0.991 | 0.982 |
| d0 | 4000 | -17 | 43 / 23 | n/a | 1.029 |

The d5 slower games (off -> on): g65 5->6, g68 5->6, g80 5->6, g127 4->5, g140 5->6, g172 4->5, g462 7->8, g492 4->5,
g587 6->7, g904 5->6, g1088 5->6, g1317 4->5, g1376 5->6, g1383 5->6, g1531 5->6, g1556 5->6, g1646 5->6,
g1801 5->6, g1833 5->6, g1897 5->6, g1944 4->5. The d0 slower games: g170 g266 g336 g360 g532 g561 g767 g872 g891
g920 g1081 g1589 g2222 g2505 g2919 g2981 g3105 g3113 g3529 g3646 g3730 g3959 g3985. The 2HG slower games: g231 g345
g357 g372 g478. A re-run of the 21 d5 slower games (`rp1.json` / `rp1.out`) reproduces all 21 (deterministic).

## Leading hypothesis for the d5 regression (UNVERIFIED)
The deleted "all" pin was the only menu option that IGNORED the speculative Maze's End `{3}` reserve. With an
untapped Maze's End and a line that does not activate it (hold has no acts), "all unneeded" keeps up to three Gates
for an activation the line never makes, where "all" drew those cards. So "a plan ending at the Summit already gets all
Gates" does not hold once the reserve applies. Candidate fixes, to measure in this order:
1. Apply the Maze reserve only where it is the line's real cost, i.e. when the applying line names a Maze's End
   activation. Inside the window the plan is known, so this needs the line's trailing actions (see
   `OwedActsJointPrepay` / `g_line_hold_has_acts`, TurnSolver.cpp ~72800). Keep the reserve for the unpinned
   beyond-horizon playout default, which keeps the old meaning.
2. Or keep a third option ONLY when the reserve binds: `{unneeded (with reserve), none, unneeded without reserve}`.
   That is still "pay the line's costs, tap the rest", with the speculative reserve searched. It collapses to two
   entries when no untapped Maze's End exists.
First CONFIRM the hypothesis from the logs (below): find the T-1 Summit decision in each slower game, and check whether
an untapped Maze's End was present and whether "off" took the "all" pin.

## Running job at handoff time
- A lane job running 4 single-game logged repros (d5 g65 g68 g80 g127, both arms, budget 20, profile depth):
  `bash scripts/box_lane.sh --slots 2 -- build/scratch/claude_maze_summit/sg.sh 65 68 80 127`, lane PIDs 897375 /
  897376 (it was still waiting for lane admission). Outputs: `build/scratch/claude_maze_summit/sg/g<gi>_<arm>/`
  (`out.txt` plus the game log in the dir; arm 0 = off, 1 = on). DONE marker: `build/scratch/claude_maze_summit/sg.DONE`.
  Wait with `until [ -f .../sg.DONE ]; do sleep 30; done` (never pgrep -f). If the CLI's `--log-dir` writes no
  readable per-turn log, look at `src/main.cpp` ~12100-12300 for the verbose/trace flags.

## Remaining steps, in order
1. Confirm the hypothesis from the sg logs (diff the two arms' turn logs at the first divergence: the Summit turn).
2. Implement the fix (fix 1 preferred) behind the same flag, keeping the USER's design. Wait ~200 s after src edits
   (9p clock lag), then rebuild in the lane:
   `cd .claude/worktrees/maze-summit && bash /workspaces/MagicDeckTester/scripts/box_lane.sh --slots 2 -- ./build.sh`.
   Run the units: `bash scripts/box_lane.sh --slots 1 -- /workspaces/MagicDeckTester/build/wt/maze-summit/Release/mtg-test`.
3. Re-run the pooled A/B on FRESH seeds (change the bases in mk_ab.py, e.g. 25100000 / 26100000 / 27100000). Size
   the searched d5 row at >= 2000 games. Run it under the box lock:
   `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 MTG_PGO=0 <copied mtg> --batch <manifest> --threads $(nproc) > ab2.out 2> ab2.err`.
   Launch it detached (setsid nohup) with a DONE marker, from the worktree (the manifest paths are relative). Check
   the `[batch] heartbeat` line in the first 10 minutes.
4. Bar: every row net <= 0. Then put every slower game at `--depth 8 --budget-ms 0`, paired with off, in one pooled
   lane/lock batch with `abandon_units` set. Escalate the budget for any game d8 b0 cannot settle. Only CLEAR
   clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered.
5. If it passes: flip the default ON (`EnvOn("MTG_MAZE_SUMMIT_PAY_FIRST", true)` plus the HeuristicArm comment).
   Delete the dead "all" path (`return { 0, 1, base.back() }` in `MazesEndProvider::EtbChoiceCandidates`) and fix the
   P1d unit test that expects `{0,1,4}` (test_mazes_end_p1d.cpp). Rebase onto `origin/phase-1-2-deck-analyzer` (never
   merge), rebuild, run the units, then run `test/regression.sh --both` under the box lock. Accept GT with a one-line
   `--accept-with-regressions "<note attributing every changed game>"`. Then run `python3 test/check_gt_logs.py`.
6. Update the ledger (`docs/design/analysis-Maze's End.md`): decisions 2/10/30 (and 8, B1) are superseded by the
   ruling above, quoted verbatim. Commit and push `wip/maze-summit` ONLY. NEVER push phase-1-2-deck-analyzer and never
   join the push queue. The parent session lands it.

## Rules to keep
ONE big engine process at a time on `build/scratch/claude_box.lock`. The Maze oracle gate filler yields within ~60 s.
Small work goes through `scripts/box_lane.sh --slots N` (N <= 2; an mtg batch must pass `--threads <= N`; a backgrounded
`cd X && cmd &` keeps its cd only inside that list). No `timeout` wrappers. Waiters key on a PID or DONE file. Never
touch `.claude/worktrees/batch-1121` (the Maze gate runs from it). references/ is commit-only. The regression run's
VPC replays the Maze references; human play is unchanged by design (the hook returns -1 under HumanPlayActive).
