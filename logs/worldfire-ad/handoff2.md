# Worldfire APPLY-DROP handoff 2 (2026-11-26 ~03:20 UTC)

Worktree `.claude/worktrees/worldfire-ad`, branch `wip/worldfire-ad` (pushed; HEAD 55c6c1538). Push ONLY wip/worldfire-ad.
Never push/pull/rebase phase-1-2-deck-analyzer. Never touch references/.

## Done (commits)
- 4238d6010 built + unit; Niv hole-1 test pins MULTI off in its hole demo and asserts the rung's own catch.
- a8735ea32 NEW lever MTG_LINE_KEEP_RITUAL_TIGHT (default ON, heurarm slot): the exact cheaper form of RITUAL --
  (a) tight joint: board alone pays bill + rest net of the ritual float (necessary condition => exact);
  (b) the crit gate counts the ritual float as the rest's supply. Probe: tight vs loose digest-identical (28/28 jobs).
- 55c6c1538 gate results + design doc Measurements section (docs/design/worldfire-apply-drop-2026-11-25.md).
- Unit 4197/4197 on the measurement binary. Repros gi447 12->2 (the 2 are undeclared), gi65 2->0, gi125 8->0.
- CPU instructions:u (6-deck probe): off 562.89G, tight 565.89G (+0.53%), loose 575.91G (+2.31%).
- Gate (Worldfire 23,000 games): APPLY-DROP off 6,924 -> on 2,019 (-70.8%); LOO (6k): off 1767, on 548,
  -MULTI 1376, -SUSPEND 822, -RITUAL 939, loose 548. Table logs/worldfire-ad/gate_report.txt.
- The subagent harness BLOCKS writing logs/worldfire-ad/report.md; the report went in the final message and the
  numbers are in the design doc.

## Running jobs
- Play A/B: chain pid 3736489 (bash) waits on `flock build/scratch/claude_box.lock` then runs
  `logs/worldfire-ad/run_ab.sh /workspaces/MagicDeckTester/build/scratch/worldfire-ad/bin_m/mtg /workspaces/MagicDeckTester/build/scratch/worldfire-ad/ab.json /workspaces/MagicDeckTester/build/scratch/worldfire-ad/ab_out`. DONE marker: `/workspaces/MagicDeckTester/build/scratch/worldfire-ad/ab_out/DONE` (rc=...).
  Manifest: 9,480 jobs / 94,800 games, seed base 4870000, arms off / on (all 63 decks: std 400/arm, 2HG 200/arm;
  Worldfire std+2HG 3000/arm) + loose (TIGHT=0) on ritual decks cyclone/dragonstorm/hinata/shadowborn/worldfire.
  Estimated ~3 h at 32 threads once it holds the lock. Check heartbeat in $S/ab_out/wins.err in its first 10 min.

## Remaining, in order
1. When ab_out/DONE appears: `python3 logs/worldfire-ad/ab_report.py /workspaces/MagicDeckTester/build/scratch/worldfire-ad/ab_out --list-slower` (off vs on; net in
   turns, unwon=9, + = slower) and `--arms loose,on` for the ritual decks. Report net, t, units/cpu per deck.
   cpu from batch.out is wall-derived and noisy on this box; for CPU quote the perf instructions above (or re-run perf
   on a mover deck in the lane).
2. Expand the sample on decks that move (a second pooled batch, fresh offsets, under the box lock).
3. Every slower game: replay at --depth 8 --budget-ms 0 both arms (lane, <=2 slots; seed = 4870000+gi, --game-index
   gi, deck/profile from ab.json, flags via env MTG_LINE_KEEP_CRIT_MULTI/SUSPEND/RITUAL=0 for off). Non-recovering ->
   MTG_NC_SEARCH=1. If a single lever causes a loss cluster, root-cause it.
4. Add the A/B table to the design doc; commit + push wip/worldfire-ad.

## Notes / traps
- A binary copied under build/scratch/<x>/ reads the PRIMARY checkout's src/cards/data unless src/decks symlinks sit
  beside it: /workspaces/MagicDeckTester/build/scratch/worldfire-ad/src and /workspaces/MagicDeckTester/build/scratch/worldfire-ad/decks -> worktree are in place for bin_m.
- Builds: touch edited headers and wait ~200 s before building (stale-PCH from 9p mtime lag happened once).
- gh is not authenticated in this session: Windows CI for the wip pushes was not checked.
- Remaining Worldfire APPLY-DROP: 1,085 of 2,019 are undeclared-action drops; the declared ones include single-cast
  {Ponder} lines whose boards differ on a Flooded Strand crack (s30000196 T4) and multi-ritual lines.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm, 2026-11-18; storage lands).
- Cut CPU. Suite headroom never justifies a fix's CPU cost. If the extensions add measurable CPU, look for an exact
  cheaper form (a cheap gate before the scratch-board work).
- Adoption bar: average at play settings across decks. Every slower game must recover at `--depth 8 --budget-ms 0`,
  or be shown clairvoyant with `MTG_NC_SEARCH=1`. Ignore seed noise. d0 barely counts.
- MTG_LINE_KEEP_CRIT adoption, 2026-11-25: "If it is slightly better, more correct in your understanding and doesn't
  have much CPU cost let's do it."
- Box rules: ONE big engine process at a time under flock build/scratch/claude_box.lock (MTG_RSS_CAP_GB=40,
  MTG_PGO=0, threads=nproc) -- the coordinator re-stated this 2026-11-26: every whole-box run goes under the lock.
  Small jobs: scripts/box_lane.sh --slots N<=2. Builds under flock build/scratch/claude_build.lock ./build.sh.
  No timeout, no pgrep -f / pkill -f. Hand off past ~250K context.
