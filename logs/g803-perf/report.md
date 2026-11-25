# g803 perf report: Maze's End 2HG s38200803 gi803 (d5 b20)

Branch `wip/g803-perf`. The binaries are Release builds of the tip (`build/scratch/g803-perf/bin_after/mtg`) and the
origin d88b4bfe0 Profile build (`bin_before/mtg`, which has the same -O3 codegen). Every run used MTG_PGO=0, 1 thread
per arm, in the small-job lane. Scratch is in `/workspaces/MagicDeckTester/build/scratch/g803-perf/`.

## Mechanism (from handoff.md)
At T5, 95% of CPU went to FSLineTail's single-main BoundaryFan. A d0 tail ran at `cutoff=4` while its node was at T5,
so it was dead: the root already held a T5 win. The tail still walked a crossing-land odometer of about 2e8
crossings (2 opponent land plays x 4 Burgeonings, menus 7-12 wide), at about 40 us per crossing. Every child FSLineWin
was refused at entry (`turn > cutoff`) without charging a unit, so no budget ever tripped. Origin ran for more than
3700 s of CPU on this one decision and was killed.

## Fixes on the tip
1. `MTG_DEAD_TAIL_FAN_SKIP` (src/ai/TurnSolver.cpp, default ON). When `state.turn_number > cutoff`, an FSLineTail
   skips its boundary fans. There are 4 sites; the single-main site returns no-win.
2. `MTG_BURGEONING_IDLE_FOLD` (src/core/SpellEffects.h, default ON). After an idle Burgeoning trigger, the
   duplicate "do nothing, then put" twin is dropped. A unit test is in test/unit/test_mazes_end_i3.cpp.

## g803
| arm | digest | avg | units | user CPU | wall |
|---|---|---|---|---|---|
| origin d88b4bfe0 | -- | -- | (T1-T4 = 21098/24407/12211/11508) | >3700 s, killed in T5 | -- |
| tip (both ON) | 9e65ecd029edeb1a | 5.0 | 70,306 (T5 = 1,082) | 5.76 s | 4.7 s |
| tip, fold OFF | 9e65ecd029edeb1a | 5.0 | 70,306 (T5 = 1,082) | -- | 6.9 s |

T1-T4 units are identical to origin. The 69,801 figure in the handoff came from the older maze-rank3 binary, not from
origin, which never finished T5. Origin's own T5 unit count is therefore unmeasured (a skip-OFF control would take
about 1 h of one core). The skip argument is proven on the batch below instead.

## Identity batch (ident2.json: Maze std s1001/2002/3003, Maze 2HG s1001/2002/3003, Blood Sun std + 2HG at 8 games each; 712 games)
Every job's digest and every per-game `.wins` line (game-log digest) is IDENTICAL in all arms. Units:

| job | before | tip (both ON) | skip only (fold=0) | fold only (skip=0) |
|---|---|---|---|---|
| bloodsun_d5_s1001 | 11,813,068 | 11,814,755 | **11,813,068** | -- |
| bloodsun2hg_d5_s1001 | 12,710,553 | 12,714,798 | **12,710,553** | 12,714,798 |
| maze_d5_s1001 | 12,637,815 | 12,632,383 | **12,637,815** | 12,632,383 |
| maze_d5_s2002 | 19,789,976 | 19,789,976 | (unmoved) | -- |
| maze_d5_s3003 | 20,576,996 | 20,570,133 | **20,576,996** | -- |
| maze2hg_d5_s1001 | 2,886,178 | 2,886,178 | (unmoved) | -- |
| maze2hg_d5_s2002 | 12,204,716 | 12,203,033 | **12,204,716** | 12,203,033 |
| maze2hg_d5_s3003 | 13,114,010 | 13,097,369 | **13,114,010** | 13,097,369 |

Verdict:
- **DEAD_TAIL_FAN_SKIP is byte-identical on digests AND units across all 712 games.** It is a pure CPU cut, and it
  is the change that fixes g803 (the fold alone did not; see handoff.md).
- **BURGEONING_IDLE_FOLD moves units in 6 of 8 jobs** while leaving play identical: 19 of 1,440 (seed, turn) census
  rows move, by -9,701 to +15,091. It reduces the work in live fans, which shifts budget reads. That makes it a play
  change in the GT sense, even though no game moved on this sample. Do NOT accept GT on it here; that is the
  parent's decision.

## CPU (user seconds, 1 thread per arm, both arms concurrent, the box loaded with another session's 32-thread run)
| batch | before | tip (both ON) |
|---|---|---|
| ident2 (712 games) | 4845.8 s (wall 1:29:37) | 4713.6 s (wall 1:27:23), -2.7% |
| g803 | >3700 s (unfinished) | 5.8 s |

Per-job CPU is noisy under this contention: the attribution pair saw differences of about 5%, in both directions, on
identical work. The batch gain is small because this sample holds no g803-scale dead fan. The win is the tail case.

## Unit tests
`build/Release/mtg-test`: 4175/4175 test cases passed (the `[maze]` argument does not filter in doctest, so this was
the full suite). `-tc="Maze's End I3*"`: 8/8 passed.

## Follow-ups (from handoff.md, unchanged)
(a) The whole dead FSLineTail could return at entry. That is sound, but its m2 loop charges units, so it would be a
play change. (b) The crossing is unpriced, so the priced ceiling cannot see a wide live fan either. (c) Duplicate
orders of commuting Burgeoning puts need the provider's LandEntryOrderClass premise.
