# Prepay floating-{C} fix (MTG_PREPAY_FLOAT_C_PHYS) -- handoff for a fresh (Codex) agent

Repo rules: read /workspaces/MagicDeckTester/CLAUDE.md first (box lock, no timeout, no merge, push wip branch only).
Prior root-cause notes: /workspaces/MagicDeckTester/logs/hinata-gi548/handoff.md.

## Done
- Worktree /workspaces/MagicDeckTester/.claude/worktrees/hinata-gi548, branch wip/hinata-gi548, rebased onto
  origin/phase-1-2-deck-analyzer (94cd1e06b). Commits: 58690a9f6 (fix, default OFF + MTG_PREPAY_C_TRACE diag),
  628fcc728 (flag DEFAULT ON: `EnvOn("MTG_PREPAY_FLOAT_C_PHYS", true)`; `=0` = old illegal booking, A/B lever only).
  src tree a7c19c1db77c64aaa2b8e3abf56cc81232042caf.

## Running
- Step 1 DONE (chain1 rc=1 = keys moved, expected): tiers smoke+regression at 628fcc728, log
  build/scratch/claude_prepayc/regr.log. Moved (all digest-only except one): hinata smoke d3/d5, hinata regr d5 s2002/s3003,
  hinata2hg smoke d5, hinata2hg regr d5 s2002 5.4133 -> 5.4000 (faster), edf smoke d0, niv regr d0/d5 s2002/d5 s3003,
  niv2hg regr d5 s2002. Tier audit: searched slower=0 (smoke 0 faster, regr 1 faster), d0 slower=0.
- Step 2 RUNNING: A/B batch build/scratch/claude_prepayc/ab1/ (run.sh, PID 1005245, waits on box lock; status file
  `status`; DONE marker `DONE` holds rc; out ab.out/ab.err; per-job .wins/.units in ab1/wins). Binary mtg.fix (=628fcc728).
  Shapes (seed 81000, held out): hinata d3b10/d5b30/2hg-d5b30 x2000, hinata d0 x5000; niv d3b10/d5b20/2hg-d5b20 x2000,
  niv d0 x5000; edf d3b10/d5b20 x2000, edf d0 x5000. Jobs `<deck>_<shape>__on|off`.
  Report: `python3 test/paired_arms.py build/scratch/claude_prepayc/ab1/wins --base off --arm on --list-moved`.
## Next (in order)
1. Read regr.log / test/results to find which tier keys moved (smoke + regression vs GT at 94cd1e06b).
2. Held-out A/B on the decks that moved: ONE pooled `mtg --batch` manifest, binary mtg.fix, arms
   flags {"MTG_PREPAY_FLOAT_C_PHYS": true} vs false, held-out seeds (not 2002/3003 or overnight seeds; prior agent used
   63003 for Hinata), job names `<deck>__on`/`<deck>__off`, `--game-log-dir`, MTG_DUMP_UNITS=1; compare with
   `python3 test/paired_arms.py <wins_dir> --base off --arm on`. Run under the box lock with MTG_RSS_CAP_GB=40,
   `--threads $(nproc)`. Example manifest shape: build/scratch/claude_maze_summit/ab.json.
   Searched shapes at each deck's tier depth/budget (test/regression_cases.sh rows: "deck depth seed games budget"),
   **1,000-2,000 games per searched shape**, plus d0.
3. Bar: every deck aggregate net <= 0 turns vs off; every SLOWER searched game recovers at `--depth 8 --budget-ms 0`
   (escalate 4x/16x/64x budget if b0 can't settle); only CLEAR clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered.
   Report CPU (core-s / units) ON vs OFF; quantify extra units where the illegal kill used to stop search.
   Single-game replays go in the lane: `bash scripts/box_lane.sh --slots 2 --cap-gb 8 -- <cmd>`.
4. If bar passes: `test/regression.sh --both --accept="<one-line note>"` (check how existing notes are worded;
   carry still-binding notes), `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`,
   commit, push wip/hinata-gi548 ONLY. If it fails: root-cause slower games, report, don't accept.
5. Optional: diagnose sim/real mismatch (sim attempts Soulfire follow-up cast the real game skips).

## USER rulings (verbatim, via coordinator)
- "I'm more concerned about areas that help searched." -> size searched shapes 1,000-2,000 games per shape on moved
  decks; report searched per-deck result FIRST, then d0, then CPU. d0 still has to pass the every-deck bar.
- USER 12:35: Claude usage is at 5%, so keep running until it runs out, but keep a Codex handoff current.

## Measurements so far
- (prior agent) gi548 s63003 d8 b0: ON -> T5 legal under d5N/d5E0/d4E (~390K units); OFF byte-identical to base (T6).
