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

## 2026-11-25 21:26 UTC: Maze's End stopped (USER), Worldfire finish running

USER: *"Let's stop maze's end to finish Worldfire. The timing for Maze's End also sounds unreasonable."*
- The Maze's End mullgen was stopped at 21:26 (rc 143): 4.2%+ of the floor. Before the stop, the journal (19.9 MB) and its
  backups, gencache and profile were copied to `.claude/worktrees/maze-gen/logs/maze_journal_stop-20261125T2126*/`.
  The journal is also still in place beside the deck. A resume at the same recipe (fast, d1 b1, K=33) picks it up.
- Stage 4 `valueleaf.sh finish decks/Worldfire` started 21:26:53 on 15,618 rows (`wf_finish.log`, harness task b5wobur0o).

### Why Maze's End costs what it does
- **K=33: equivalence discovery merged NOTHING.** Every distinct name is its own bucket, including the ten 1-of
  Guildgates, which differ only in colour pair. 7,358,894 size-7 hands (Worldfire K=21 is about two orders smaller).
- **Rollouts are expensive:** ~18k units each at d1 b1 (0.40x the d5 b20 play cost), so ~69 rollouts/s on 32 threads.
- A merge of the ten Guildgates into one bucket (x10) gives K=24 and **1,032,329** size-7 hands (7.1x fewer).
  Because every member is a 1-of, a hand's count in that bucket is always its number of DISTINCT Guildgate names,
  so Maze's End's ten-gate count stays exact. What it loses is the colour pair (which spells the hand can cast, and
  which Guildgate to bottom). This is a BUCKET RULING, which is the USER's. Not applied.

## 2026-11-25 21:40 UTC: Maze's End bucket ruling (USER), regeneration queued

USER: *"I wouldn't group all guildgates together, but I would group things that obviously produce just Green and
just Blue, either, neither and both together. Then there are untapped lands and some special ones, but this should
still cut down the numbers somewhat."*
- `decks/Maze's End/Maze's End.buckets.json` (64ff0e83b on wip/maze-gen-claude): tapped G-only {Selesnya, Gruul, Golgari};
  tapped U-only {Azorius, Dimir, Izzet}; tapped neither {Rakdos, Orzhov, Boros}; tapped both {Simic, Manor Gate x2,
  Sea Gate x2}; tapped either {Cliffgate x2, Black Dragon Gate, Citadel Gate}; untapped {C}/{1}:any {Heap Gate, Planar
  Nexus, Talon Gates of Madara}. Alone: Maze's End, Gond Gate, Baldur's Gate, Basilisk Gate, Thran Portal, Gateway Plaza,
  The Black Gate. Expected K=21, 576,099 size-7 hands (was 7,358,894), so the floor is ~4.6 h at ~69 rollouts/s.
- G/U only: Gates Ablaze (R) hits creatures only, so red is inert in a goldfish.
- `expected_buckets` cleared from the profile (mull_gen d1 b1 kept) so mullgen re-records K under the ruling. The K=33
  journal and gencache were moved (not deleted) to `logs/maze_journal_stop-20261125T212636Z/k33-originals/`.
- `build/scratch/overnight/chain_maze2.sh` waits for WF_FINISH.DONE, then mullgen, then the value leaf (MAZE2.DONE).
