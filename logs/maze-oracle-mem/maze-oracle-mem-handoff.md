# Maze's End mana-oracle memory runaway: handoff (2026-11-19)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-oracle-mem`, branch `wip/maze-oracle-mem`
(from 22d11f7b5). Scratch `/workspaces/MagicDeckTester/build/scratch/claude_maze_oracle_mem/` (= $S).

## Brief (verbatim rules the parent gave)
- Find why Maze's End games with MTG_MANA_ORACLE=2 blow through memory; fix soundly. The oracle is an AUDIT:
  it must not change play (mana_clean compares OFF/ON digests). "A bound must be a memory budget that, when hit,
  records the decision as CAPPED/CAPPED-DECISION (existing classes) rather than silently dropping work; prefer
  making the enumeration itself smaller by a sound fold ... where that is exact."
- Verify: single-game RSS before/after; a pooled 32-thread oracle-ON batch of the first ~200 Maze's End games
  completes under the cap (MTG_RSS_CAP_GB=40); play digests unchanged oracle 0 vs 2 on those games; unit tests pass.
- ONE engine process at a time; every build/test/engine run via
  `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock <cmd>`. Build only with ./build.sh in the
  worktree; wait ~200 s after edits before building. No `timeout`, no pgrep -f/pkill -f.
- Commit to wip/maze-oracle-mem (may push THAT branch). NEVER push phase-1-2-deck-analyzer. Rebase, never merge.
- Coordinator (later): "inside a lock hold, run independent single-game diagnostics concurrently (e.g. up to 4 at
  once with per-process MTG_RSS_CAP_GB caps summing to <= 36 GB, or pool them into one `--batch --threads N` when
  per-game RSS is not the measurement), and keep each lock hold as short as you can."
- Coordinator: second repro to verify against: Merfolk Turns, deck "decks/Merfolk Turns/Merfolk Turns.cod",
  profile alongside, seed 30000013, game_index 13, MTG_MANA_ORACLE=2 (alone took a 16-thread probe past 40 GB;
  build/scratch/mana_clean/claude-queue-20261119f/probe.merfolk_turns.*). Include it in the final report.
- 250K context -> hand off (this file).

## Structure found (measured)
`Realize` (src/ai/ManaOracle.cpp) keeps a full GameState (~21 KB on Maze's End boards: sizeof GameState 1712,
Card 136, Permanent 328) per OUTCOME and per FRONTIER. Under the 400,000-apply CAPPED retry
(MTG_MANA_ORACLE_NODES_RETRY) one plan holds ~100k of each. MEM trace, seed 30000000 T6 m1
{land=Heap Gate + Sylvan Scrying + Burgeoning + Manabond}: outcomes=97,402, peak frontier layer=99,839,
~3.9 GB held, RSS 3.9 GB on ONE thread; a later plan took the game past the 8 GB cap. The apply ceiling and the
priced decision budget bound time, not memory. (docs/design/mana-oracle-memory-time.md already noted "outcomes
keeps a full board per apply ... revisit only if a deck shows it" -- at the old 20,000 retry.)

Baseline single-game RSS (old code + trace, 1 thread, MTG_RSS_CAP_GB=8; trailing 1 = killed by the cap):
see base_summary.txt next to this file: gi 0,2,3,4,7,11 died > 8 GB; gi 1 1.97 GB, 5 0.89, 10 2.84, 12 3.92,
13 6.49 GB (gi 33/35/41 were still running at handoff -- $S/base/summary.txt).

## Fix at b5bb31b61 (WIP: NOT YET BUILT OR TESTED)
1. Board accounting (`BoardAcct`, thread_local t_board_live) on Outcome and FrontierRec, deterministic estimate
   from contents (`BoardBytes` + `ScriptBytes`). `MTG_MANA_ORACLE_BOARD_MB` (default 768 per thread, 0 = off):
   Realize's apply loop stops as CAPPED when the thread's held boards pass it (`Realization::mem_capped`);
   escalation retries skip a mem-capped search; CAPPED text names the memory budget; summary line
   "=== MANA ORACLE board memory: ...".
2. EXACT online Pareto prune: frontier key/resources computed at capture (`FrontKey`, `ParetoCovers` = ParetoKeep's
   pairwise rule), dominated frontier boards released immediately. `MTG_ORACLE_PARETO_ONLINE_CHECK=1` recomputes
   ParetoKeep and aborts on any difference (argument in the code comment).
3. EXACT hit-only release: in hit_only Realize a non-hit, non-Clean outcome's board is dropped (`Outcome::released`,
   Clean() stays false); kept under MTG_ORACLE_ORDER_FOLD_CHECK.
4. Diagnostic `MTG_MANA_ORACLE_MEM_TRACE=<n>` prints per large Realize: outcomes, released, peak fronts/kept,
   distinct outcome boards, peak_live, rss.

## Running at handoff
- chain1 ($S/chain1.sh, PID 3104518, holds the box lock): baseline repro, Maze gi 0..41 subset, finishing gi 33/35/41.
  Marker: `done` line in $S/chain1.status; results $S/base/summary.txt, traces $S/base/g*.err.
- chain2 (launcher PID 3111621, waiting on the lock): $S/chain2.sh = build worktree -> $S/mtg.fix, mtg-test
  ($S/units2.log), then CONCURRENTLY: base Merfolk gi13 (old binary $S/mtg.trace, 6 GB cap) -> $S/base_merfolk/,
  fix Merfolk gi13 (6 GB) -> $S/fix_merfolk/, fix Maze games in 7 streams at 3 GB cap -> $S/fix_{a..g}/ (each with
  summary.txt + DONE), all with MEM trace and the Pareto online check. Marker: `done` in $S/chain2.status.
  Build errors (if any) are in $S/build2.log -- the chain stops after a failed build.

## Remaining, in order
1. Read chain2: build/units OK? Any `PARETO-ONLINE MISMATCH` abort in fix_*/g*.err (must be none)? Per-game RSS
   vs base; MEM lines: how much the online prune released (peak_kept vs peak_fronts), distinct_core vs outcomes
   (if outcomes are heavily duplicate boards, an exact dedupe is a further option -- but consumers read o.script
   (TapsHiddenCard, printed scripts, Clean()), so only a script-equivalent dedupe would be exact), how many
   searches are mem_capped.
2. Tune if needed (budget default; 32 x 768 MB must stay under 40 GB with the engine's ~5 GB).
3. Remove nothing diagnostic-only that is useful; keep MEM trace off by default.
4. Pooled verification in ONE lock hold: Maze's End gi 0..199 (seeds 30000000+gi), 32 threads, MTG_RSS_CAP_GB=40,
   MTG_MANA_ORACLE=2 with $S/mtg.fix (must complete; check the heartbeat), and MTG_MANA_ORACLE=0 with the same
   binary; compare per-game digests ON vs OFF, and OFF(fix) vs OFF(base $S/mtg.base = build/PGO at HEAD 22d11f7b5)
   -- must be identical. Add Merfolk gi13. Manifest shape: {"jobs":[{"name","deck","profile","games":1,"seed",
   "game_index"}]} one job per game.
5. Commit on wip/maze-oracle-mem with the measurements in the message; write a short docs/design note (or extend
   docs/design/mana-oracle-memory-time.md with a section 3); push wip/maze-oracle-mem only.
6. Owed before landing on the shared branch: smoke + regression GT unchanged (oracle is off in the tiers, so
   expected byte-identical), Windows CI, and a mana_clean re-run of Maze's End and Merfolk Turns; the user should
   be told that Maze's End plans that used to be uncomputable (OOM) now report CAPPED (memory budget) and fail
   the gate honestly.
