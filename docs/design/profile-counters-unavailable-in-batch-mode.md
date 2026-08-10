# `MTG_PROFILE` counters were silently unavailable in `--batch` mode

**Status:** FIXED 2026-08-09/10. Two defects, not one. Kept as a record because both failure modes
produce a *plausible measured result* rather than an error.
**Found:** during the Goblins value-leaf cost A/B.

## Why this mattered

`CLAUDE.md` **mandates** pooling long or multi-item work into ONE `mtg --batch` rather than a loop of
small invocations. Deterministic counters are the sanctioned instrument for perf claims, because on a
heavy-tailed deck **wall clock is not evidence** (a ladder measurement once reported the same
artifact as both 0.57x slower and 26x faster). So the counters were missing from exactly the mode the
repo requires for the runs big enough to need them — and anyone following both instructions landed
here, with wall clock as the only remaining instrument.

## Defect 1 — the report was never reached

`PROF_REPORT(std::cerr)` was called from one place, `src/main.cpp:4007`, on the **single-run** path.
The `--batch` handler starts at `src/main.cpp:3541` and **returned at `src/main.cpp:3623`**, ~400
lines earlier. A batch run never reached it: exit 0, normal `played=/avg=/digest=` lines, a stderr
stream with only `[play]`/`[batch]` chatter, and **no counter block at all**. A harness parsing
counters off that gets a clean, EMPTY cost table — which reads exactly like "the two arms cost the
same", the conclusion such a measurement is usually run to test.

**Fix:** `PROF_REPORT(std::cerr);` immediately before the batch path's `return 0;`.

## Defect 2 — the workers never flushed (the one that actually mattered)

Adding the report was necessary and **not sufficient**: the block then printed with **every counter
zero**. The hot path is deliberately lock-free `thread_local` storage (`prof::t_counters`), folded
into the global aggregate only by `FlushThread()`. `GoldFishRunner.cpp:366` calls
`PROF_FLUSH_THREAD()` at the end of each worker lambda; `BatchRunner`'s worker lambda did not. Since
**every** batch game runs on a worker, nothing ever reached `g_total`.

This is the more dangerous of the two. Missing counters are conspicuous; an all-zero table is a
number, and "0 search nodes" can be misread as a cheap arm rather than a broken instrument.

**Fix:** `PROF_FLUSH_THREAD();` at the end of `BatchRunner`'s worker lambda, before the join
(plus the `../ai/Profiler.h` include, which `BatchRunner.cpp` lacked).

Both macros expand to nothing unless `MTG_PROFILE` is defined (`src/ai/Profiler.h:262`), so Release
codegen is unchanged.

## Verification performed

1. **Batch == summed single-run, exactly.** Same 32,000 Goblins games (16 x 2000, seeds 7000000+),
   batch at 8 threads vs 16 single-run processes summed. The search is deterministic, so this is an
   exact equality, not an approximation:

   | counter | batch | single-run summed |
   |---|---|---|
   | GameState deep copies | 9,231,836 | 9,231,836 |
   | Search nodes (steps) | 58,921,594 | 58,921,594 |
   | EnumeratePlans calls | 21,897,307 | 21,897,307 |
   | ApplyPlanDirect calls | 66,231,369 | 66,231,369 |

2. **Release play unchanged.** Goblins live sidecar, seed 600000 x 1000 games:
   pre-fix `avg=3.8880 digest=d11d2739cf7ac814`, post-fix `avg=3.8880 digest=d11d2739cf7ac814`.

To re-verify after any future change:

```bash
cmake -S . -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-instr -j
build-instr/mtg --batch <manifest> --threads 24 2>&1 >/dev/null | grep -A5 'PROFILE (deterministic'
```

## Consumer note

`test/valueleaf_cost_ab.sh` still uses chunked single-run invocations, which remain valid (its
numbers match the batch path exactly, per the table above). It can be simplified to one pooled
`--batch` per arm now that batch reports. Two parsing hazards found writing it are worth keeping in
mind for any counter consumer, since both yield plausible wrong numbers rather than errors:

1. Counter labels contain parentheses (`Search nodes (steps)  : N`). A label character class without
   `(` drops the headline counter while the rest of the table renders normally.
2. Concatenated per-process reports repeat every key, so a parser must **accumulate**; assigning
   reports the last chunk's counters as the whole arm.
