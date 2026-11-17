# Boars value-leaf phase A memory investigation (2026-11-17)

## Reproduction

The frozen Boars row generation at `245c26f2a` was OOM-killed once and then paused with its rows banked. On the second run, 12 busy workers grew from 3.6 GB RSS at ten minutes to 18.1 GB at 80 minutes. The reported FSL pool remained at 0 MB and the plan pool at about 81 MB held, so those pool gauges did not explain RSS.

The isolated `boars_rows_00000_115` game (seed 900115, game index 115), using the frozen Boars profile and exhaustive keep table, did not finish after five minutes with `MTG_BP_DEV_SEQ` on. Jemalloc reported about 259 MB of live heap: 170 MB below breakpoint continuation derivation (`BpEnumEntryFor`) and 63 MB in growing wave vectors. With `MTG_BP_DEV_SEQ=0`, the same game completed in about five seconds. This is a search expansion, not allocator high-water retention.

The value-row label ladder calls `FSLineTail` directly for each candidate and horizon. It bypasses `FullSearchLine`'s two-pass compound-deviation search. A losing candidate can therefore exhaust a large compound wave before the ladder checks another candidate that wins at the same horizon.

## Changes under test

The isolated `fix/boars-devseq-memory` worktree has three changes:

1. The earliest-only label ladder probes the ordinary tree across the whole horizon before entering compound search. An observed win is exact at that horizon because every shallower horizon was fully refuted. For a remaining candidate, it uses `FullSearchLine`'s suppressed first pass and dirty-cache handoff before searching compound lines.
2. `BpWaveWalker` releases slots when a deviation wave has completely retired. It retains the cumulative slot offset, so compound prefix-cache keys and diagnostic slot IDs have their original values, and it retains ordinary-slot identities for duplicate checks. Live hand-out order is unchanged.
3. The breakpoint continuation cache uses at most 128 entries during offline label search; ordinary play keeps the 8,192-entry default. `MTG_BP_ENUM_CACHE_CAP` remains an upper bound. On ten matched Boars games, 128 entries was about 20% faster than the default and reproduced all 46 rows and ten digests. Sixteen entries was about 39% slower than 128, with the same rows and digests.

The 128-entry cap plus the first two changes used about 81 MB RSS after seven minutes on the isolated frozen seed. The original binary with the same cap used about 230 MB after eight minutes. Forty completed matched Boars games produced all 190 rows and 40 game digests identically with the repaired binary; the Goblins compound-deviation witnesses (gi144 and gi3447 at depth 8, unlimited budget) still win on turn 3. These are focused comparisons, not a replacement for broader adoption validation. The frozen row bank must not silently mix source revisions.

## Current generation and alternatives

The frozen worktree briefly resumed phase A at 00:55 UTC with `MTG_BP_ENUM_CACHE_CAP=128`, `MTG_VLQ_ROWS_THREADS=24`, and a 20 GB RSS watchdog. It was stopped when host available memory fell to roughly 1.7 GB, before the process watchdog fired. The user then directed that the value-leaf batch remain stopped and that only individual tests run. Its 9,942 saved rows remain in `logs/vlq_boars/rows/all.rows` in the frozen worktree. The source code in that worktree has not changed. No value-leaf process is running.

`MTG_LABEL_HORIZON=2` completed the isolated seed in seconds and produced the same rows as the one-deviation search there, but moved 5 of 109 comparable labels later in a 24-game banked sample. Doubling its playout width to 16 and raising its playout depth to 3 did not recover any of those five labels (the deeper arm was also much slower). Horizon 3 matched all 105 comparable rows from 23 completed games; the 24th was a long tail. Neither horizon is being mixed into the existing exact row bank.

Next: complete focused breakpoint unit tests when host memory permits, then decide whether to adopt the source-level fix for future generation. The broad test-binary build was stopped with less than 3 GB host memory available; no full value-leaf batch will be restarted under the current instruction. The row bank is safe across a failed batch; phase A is marked done only after the queue and slow-game audit complete.
