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
