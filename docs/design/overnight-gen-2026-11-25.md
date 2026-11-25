# Overnight generation chain, 2026-11-25 (checkpoint)

USER request 2026-11-25: "run a chain that continues the value-leaf for Worldfire and then works on Maze's end mulligan
profile and value-leaf overnight? I may stop it partway tomorrow."

Chain: `build/scratch/overnight/chain_gen.sh` (status: `build/scratch/overnight/status.txt`, DONE marker beside it).
Strictly serial, each stage under `build/heavy.lock`; it starts after the P18 PW1 screen (`build/scratch/p18g/PW1.DONE`).

| Stage | Worktree | Command | State |
|---|---|---|---|
| 1 Worldfire value leaf (RESUME) | `.claude/worktrees/worldfire-mullperf` (wip/worldfire-mullperf 28ce1c001; frozen 17103dfb0, src ae02efc3) | `bash scripts/valueleaf.sh run decks/Worldfire` | 15,610 rows banked in `logs/vlq_worldfire` (phase A, stopped 11-25 00:39); queue copied aside before resuming |
| 2 Maze's End mulligan profile | `.claude/worktrees/maze-gen` (wip/maze-gen-claude at origin f9e182ad4) | `bash scripts/mullgen.sh run "decks/Maze's End"` | report `logs/Maze_s_End_mullgen/VALIDATION.txt` |
| 3 Maze's End value leaf | same | `bash scripts/valueleaf.sh run "decks/Maze's End"` | only if stage 2 exits 0 with VALIDATION PASSED |

Pre-launch checks for Maze (closeout doc): suite_gate OK (`mazesend`); ref_bench and verify_deck results below.

After the chain: commit each adopted artifact in its own worktree (gzipped forms), size the deck's overnight rows at its
keep-table adoption, accept smoke/regression GT, and land through the push queue in one batch.

## Status 2026-11-25 18:35 UTC

- **Stage 1 (Worldfire value leaf) STOPPED at phase A, 17:28 UTC, on a defect in the fail-out bar, not a real signal.**
  The resume queued only the 3 games still running when the first run stopped. Two of them hit the 4 h cap
  (seed 901151 gi 151, 869M units; seed 901056 gi 56, 676M units, no rows from either) and one finished in 1.8 h
  (seed 900263 gi 13, 349M units, T6). The bar divided by the resumed manifest: "2/3 = 66% super slow". Over the
  phase's 2,500 games it is 2 (0.08%), under kitty v3's 0.12%. Fixed in `scripts/valueleaf.sh` (3ddd5026d on
  wip/worldfire-mullperf): slow games bank in `rows.slow.txt` across runs, and the denominator is the phase's
  full game count. `logs/vlq_worldfire/rows.slow.txt` is seeded from both runs (2/2500 at the 2 h line).
  Remaining: `bash scripts/valueleaf.sh finish decks/Worldfire` (phases B..E on the 15,620 banked rows). A plain
  `run` would replay the 2 capped games for another 4 h each and write nothing. This needs the box, so it waits on Maze.
- **Stage 2 (Maze's End mulligan) RUNNING since 17:28 UTC** (exhaustive phase from ~18:13). Settings: d1 b1, K=33,
  which is the largest K in the repo (next: 21). That gives 7,358,894 size-7 cells (+3.9M sub-table batches) at
  floor R=2 = 14.7M floor rollouts at ~65/s. So **the floor alone is ~60 h**, before refinement. The journal is
  resumable. K=33 comes from 22 distinct gate names, which Maze's End's win condition really distinguishes.
- Stage 3 (Maze's End value leaf) will not be reached tonight.
