# Boars value-leaf phase A memory investigation (2026-11-17)

## Reproduction

The frozen Boars row generation at `245c26f2a` was OOM-killed once and then paused with its rows banked. On the second run, 12 busy workers grew from 3.6 GB RSS at ten minutes to 18.1 GB at 80 minutes. The reported FSL pool remained at 0 MB and the plan pool at about 81 MB held, so those pool gauges did not explain RSS.

The isolated `boars_rows_00000_115` game (seed 900115, game index 115), using the frozen Boars profile and exhaustive keep table, did not finish after five minutes with `MTG_BP_DEV_SEQ` on. Jemalloc reported about 259 MB of live heap: 170 MB below breakpoint continuation derivation (`BpEnumEntryFor`) and 63 MB in growing wave vectors. With `MTG_BP_DEV_SEQ=0`, the same game completed in about five seconds. This is a search expansion, not allocator high-water retention.

The value-row label ladder calls `FSLineTail` directly for each candidate and horizon. It bypasses `FullSearchLine`'s two-pass compound-deviation search. A losing candidate can therefore exhaust a large compound wave before the ladder checks another candidate that wins at the same horizon.

## Changes under test

The isolated `fix/boars-devseq-memory` worktree has three changes:

1. The earliest-only label ladder probes the ordinary tree across the whole horizon before entering compound search. An observed win is exact at that horizon because every shallower horizon was fully refuted. For a remaining candidate, it uses `FullSearchLine`'s suppressed first pass and dirty-cache handoff before searching compound lines. **(The per-candidate two-pass half was removed at integration; see below.)**
2. `BpWaveWalker` releases slots when a deviation wave has completely retired. It retains the cumulative slot offset, so compound prefix-cache keys and diagnostic slot IDs have their original values, and it retains ordinary-slot identities for duplicate checks. Live hand-out order is unchanged.
3. The breakpoint continuation cache uses at most 128 entries during offline label search; ordinary play keeps the 8,192-entry default. `MTG_BP_ENUM_CACHE_CAP` remains an upper bound. On ten matched Boars games, 128 entries was about 20% faster than the default and reproduced all 46 rows and ten digests. Sixteen entries was about 39% slower than 128, with the same rows and digests.

The 128-entry cap plus the first two changes used about 81 MB RSS after seven minutes on the isolated frozen seed. The original binary with the same cap used about 230 MB after eight minutes. Forty completed matched Boars games produced all 190 rows and 40 game digests identically with the repaired binary; the Goblins compound-deviation witnesses (gi144 and gi3447 at depth 8, unlimited budget) still win on turn 3. These are focused comparisons, not a replacement for broader adoption validation. The frozen row bank must not silently mix source revisions.

## Current generation and alternatives

The frozen worktree briefly resumed phase A at 00:55 UTC with `MTG_BP_ENUM_CACHE_CAP=128`, `MTG_VLQ_ROWS_THREADS=24`, and a 20 GB RSS watchdog. It was stopped when host available memory fell to roughly 1.7 GB, before the process watchdog fired. The user then directed that the value-leaf batch remain stopped and that only individual tests run. Its 9,942 saved rows remain in `logs/vlq_boars/rows/all.rows` in the frozen worktree. The source code in that worktree has not changed. No value-leaf process is running.

`MTG_LABEL_HORIZON=2` completed the isolated seed in seconds and produced the same rows as the one-deviation search there, but moved 5 of 109 comparable labels later in a 24-game banked sample. Doubling its playout width to 16 and raising its playout depth to 3 did not recover any of those five labels (the deeper arm was also much slower). Horizon 3 matched all 105 comparable rows from 23 completed games; the 24th was a long tail. Neither horizon is being mixed into the existing exact row bank.

Next (superseded by the integration section below): complete focused breakpoint unit tests when host memory permits, then decide whether to adopt the source-level fix for future generation. The broad test-binary build was stopped with less than 3 GB host memory available; no full value-leaf batch will be restarted under the current instruction. The row bank is safe across a failed batch; phase A is marked done only after the queue and slow-game audit complete.

## Integration onto `phase-1-2-deck-analyzer` (2026-11-17, Claude)

Integrated in worktree `.claude/worktrees/boars-devseq-integ`, branch `boars-devseq-integ`, by cherry-picking `4d8ab5f23` onto origin `f2156d4e6` (69 commits newer than the fix's base `245c26f2a`, including ~1,300 new `TurnSolver.cpp` lines; the cherry-pick was clean). Scratch binaries and logs: `build/scratch/boars-integ/` on the build volume. All comparisons below are on that rebased tree, with the baseline being origin `f2156d4e6` (the same tree with `TurnSolver.cpp` reverted to its parent).

**Shipped: the early-win probe, the wave-slot retirement, and the 128-entry label cache cap. Removed: the per-candidate two-pass (`label_tail`).** It was sound but measured no benefit anywhere and cost work:

| Measurement (rebased tree) | baseline | full fix | probe off | two-pass off | cap off |
|---|---|---|---|---|---|
| Melira Pod seed 910003 gi3 (label rows), wall | 900 s cap, row dropped | 33 s | 900 s cap | 32 s | 30 s |
| Hinata seed 910005 gi5, units | 3,277,391 | 3,876,555 | 3,605,963 | 3,547,983 | 3,876,555 |
| Boars 40-game sample (Codex's fixcheck manifest), units | 1,601,803 | 1,670,309 | 1,626,603 | 1,668,473 | -- |
| Boars seed 900115 gi115, 30 min, game-line stores (progress) | 1,513 | 1,949 | 1,589 | 1,949 | -- |

* Rows: identical as a multiset wherever both arms complete. Over the 48-game cross-deck sample (Goblins, Burn, Dragonstorm, Melira Pod, Hinata, Fluctuator, Landfall, Anti-Lifegain; seeds 910000-910005), 206 rows match and the full fix adds 3 rows from the Melira game the baseline could not finish. On the Boars 40-game sample, all 189 rows and 40 play digests are identical in all four arms.
* The probe is the part that pays: it resolves the Melira game (900 s+ to ~32 s) and is what makes the extra seed-115 progress. Removing the two-pass leaves both results unchanged and takes about 0.33M units (~9%) off the Hinata game.
* The cap changes no units (identical unit counts with and without it), so it is a pure memory/speed knob, consistent with "caches must not change units".
* The probe now honours the `MTG_LABEL_COLD_CACHE` diagnostic, so that the probe, like the main loop, gets fresh caches per candidate under it.

**Seed 115 is NOT resolved on the rebased tree.** Every arm hit a 30-minute cap. A jemalloc heap profile of the full fix (Profile build, `MTG_POOL_ALLOC=0`, about 15 minutes in) attributes 88% of the 407 MB live heap to `BpWaveWalker::DevSeqOpen` (via `Report`, under `EnumerateEarliestWins -> FSLineTail -> FSLineWin`): the compound-slot lists and the never-pruned `m_seq_opened` dedup keys of an unsuppressed compound walk. The likely reading is a label horizon at which no candidate wins (the probe misses) and the refutation must walk the full compound tree. The `BpEnumEntryFor` growth that dominated on `245c26f2a` is gone. This is real search expansion, not a leak, and none of these changes targets it. Possible directions (not started): store `m_seq_opened` as 128-bit hashes (a constant-factor saving), or a transposition at breakpoint level for compound prefixes (see `bp-empty-arm-deferred-site.md` 6.1). Single-game RSS here varies with box-wide memory pressure: the same binary read 794 MB once and 219 MB once at identical progress, so use jemalloc live heap, not RSS, to compare.

Validation: focused unit tests (`test_bp_dev_seq`, `test_dev_seq_tail_skip`, `test_bp_wave_empty`) pass on both the full and final code. Smoke and regression ran as one pooled batch (see the commit for the result). The label ladder is not on any tier's path. The frozen Boars row bank (`sunday-boars`, `245c26f2a`) is untouched, and no value-leaf generation was run (user instruction).

### Integration checkpoint (2026-11-17 ~04:30 UTC)

* Worktree `.claude/worktrees/boars-devseq-integ`, branch `boars-devseq-integ` (local only). HEAD `7fe73c634` = the Codex fix cherry-picked onto origin `f2156d4e6`. **Uncommitted:** `src/ai/TurnSolver.cpp` (two-pass `label_tail` removed; the probe honours `MTG_LABEL_COLD_CACHE`) and this doc. `build/Release/mtg` = that final code (copy `build/scratch/boars-integ/mtg.final`); `mtg.base` = origin, `mtg.new` = the full Codex fix, `mtg.diag` = the full fix with env switches `MTG_DIAG_{LABEL_CAP128,PROBE,TWOPASS}` (never committed).
* Smoke+regression (one pooled run, the `mtg.new` binary): smoke 179 pass / 40 fail, regression 285 pass / 61 fail. **Ground truth on origin is stale:** 7 `src` commits since the last GT accept `340ef4683` (`1f8924717`..`5f67ad34f`, mana payment), and d0 rows fail too, which the walker cannot touch. Running now: the same tier manifest on `mtg.base` -> `build/scratch/boars-integ/tiers_base.batch.log` (new arm: `tiers_new.batch.log`). Next: diff per-job digests. If they are identical, the fix is play-neutral: commit, rebase, push (no GT accept; GT for those 7 commits belongs to the session that landed them), and watch CI. If they differ, audit the changed games.
