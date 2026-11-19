# Maze's End P5b-3 handoff: the land-order fold skips a twin only after an UNCUT evaluation

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-p5b2`, branch `wip/maze-p5b2`.
- Base: 9df8e1769 (P5b-2 results).
- WIP commit: **e2b2516ee**. The code is complete and syntax-checks clean (`-fsyntax-only` on TurnSolver.cpp and the
  test file). It is NOT yet built, unit-tested or measured.

Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/p5b3/`.
Read `logs/maze/P5b2-results.md` first: it has the background, the P5b-2 A/B and the gi661 root cause.

## Design (MTG_FOLD_LAND_ORDER_SEEN, still DEFAULT OFF)

- Every search-loop dedup set keeps its EXACT `BuildDedupKey`, composed as before, so the flag-off behaviour and set
  contents are unchanged.
- The land-order twins live BESIDE each set in a `LandTwinSet`, keyed by the canonical key (`SeenKeyGiven`).
  - The twin map is consulted only when the exact key is fresh.
  - A twin is skipped only if the entry's bit 0 says that some twin's evaluation finished UNCUT.
  - Otherwise the candidate is searched, and a window is opened on that key.
  - The site calls `Close()` when the candidate's evaluation ends. If the cut mark did not move (and the line is not
    truncated), the entry becomes complete. A later uncut twin therefore UPGRADES a cut entry.
- A window that is never closed stays cut. That covers a candidate skipped by another fold, pruned, or returned
  early.

**The cut mark (`LandTwinMark`) is CUT if anything moved between Check and Close:**
- `g_fs_trunc_events`
- `budgetbind::t_binds`
- `g_condemn_drops`
- `g_sibling_defer_skips` (new): the EOT closure `EotStateSet::Visit` collapse and the `domin::Check` prune. These
  defer a line to a sibling that may lie outside the window.
- `g_lt_cut_reads` (new): a WIN served from a memo that a cut search stored:
  - `FSLineEntry::cut`, set in `FSLineStoreWin` from the innermost `LandTwinNodeFrame`. The field sits in
    `nowin_bound`'s padding; a `static_assert` checks that sizeof is unchanged.
  - The rollout TT's `MarkCutWin` / `IsCutWin` (`TranspositionTable.h`, side set `m_cut_win`).
  - Both are written only with the fold live.
- At close: `decisionwork::Exceeded`, `PricedExceededQuiet` or `gamework::Abandoned`.
- `g_fs_hexits` is deliberately NOT a cut. It is the ID pass minimum, a function of the board and the cutoff. It is
  censused as `skip_hexit`.

Census: `MTG_LAND_TWIN_STATS=1` prints `[land-twin] checked/skipped/(hexit)/cut-twin-searched/closed
clean/cut/upgraded/unclosed` at exit.

Locations in `src/ai/TurnSolver.cpp` at e2b2516ee:
- the design comment and structs at 35370-35545:
  - `g_sibling_defer_skips` at 35407;
  - `g_lt_cut_reads` at 35413;
  - `LandTwinMark` at 35414;
  - `LandTwinNodeFrame` at 35433;
  - `LandTwinSet` at 35471;
  - `LandTwinDup` at 35528 and 35537;
- `LaWaveDup` at 93792, which now takes `LandTwinSet&`;
- the TT hit at 109138 and the TT store at 109522;
- `SeenKeyGiven` at 109864;
- `LandTwinSkipsForTest` at 109919;
- the FSLineEntry `static_assert` at 110434;
- `LocalFslMemoHit` `e.cut` at 112627.

`Close` sites:
- FSLineTail m2: 115925;
- FSLineWin:
  - node child: 118801;
  - main plan, after both dork-combat tails: 119158;
  - wave: 119513;
  - tranche: 119665;
- SolveWithLookahead:
  - main candidate, after SimulateToEnd: 127753;
  - wave: 128019;
  - tranche: 128145.

Every former `SeenKey(...)` dedup insert now goes through `LandTwinDup`. The etb verify diagnostic is back on the exact
key.

Test hook: `TurnSolver::LandTwinSkipsForTest(seq, how)` (TurnSolver.h). The unit case `"Maze's End P5b-3: a land-order
twin is skipped only after an uncut evaluation"` is at test/unit/test_mazes_end_p5b.cpp:379.

## Running job

**UPDATE: the build FINISHED `exit 0`, with no errors (Release binary at e2b2516ee). Step 1 is done; start at step 2/3.**


The Release build is queued under the heavy lock (several other sessions' heavy jobs were ahead of it):
- `heavy_run.sh ./build.sh` PID **1722181**, wrapper shell 1722180;
- log: `build/scratch/maze/p5b3/build1.log`;
- DONE signal: a final line `exit <rc>` in that log.

Wait on it with:

```
until grep -q '^exit' build1.log; do sleep 30; done
```

## Remaining steps, in order

1. **Build.** On `exit 0`, check that the log has no errors. On failure, fix and rebuild with
   `MTG_BUILD_JOBS=12 scripts/heavy_run.sh ./build.sh`. An ENOMEM error is a 9p glitch: retry.
2. **Optional quick check, outside the lock, 1 thread.** Replay std gi661 (seed 183662, gi 661, d1 b1) with
   `MTG_FOLD_LAND_ORDER_SEEN=1 MTG_LAND_TWIN_STATS=1`.
   - It should play T5 like flag-off (P5b-2's on-arm went T6).
   - The census should show skips > 0 and cut-twin-searched > 0.
3. **Units, scenarios and the held-out A/B, already scripted.**
   - Run `scripts/heavy_run.sh build/scratch/maze/p5b3/stage2.sh`, with an absolute path and `MTG_RSS_CAP_GB=20` set
     in the script.
   - It runs `mtg-test` (all units), then `test/scenarios.sh`, then the 11,200-job A/B in one pooled batch.
   - A/B details:
     - manifest `ab/manifest.json`, built by `mkab.py`;
     - arms innermost, `same_seed_inflight: 0`;
     - FRESH seeds: std 301001-304001 + gi, 2HG 311001-314001 + gi. Maze scratch used up to 238300.
   - Progress goes to `stage2.progress` (DONE marker); `ana.txt` holds the per-cell results.
4. **d8 b0 replay of every slower game.**
   - Build the manifest with `python3 mkd8.py ab/run.out d8/manifest.json`. The seeds are already updated.
   - Run it pooled under heavy_run.
   - Bar: every slower game recovers at `--depth 8 --budget-ms 0`.
5. **Ref bench.** `scripts/ref_bench.py --deck maze_s_end` at d5 b20, flag ON: all 11 must tie or beat the USER.
6. **If it passes, flip the default ON.**
   - The default is `EnvOn("MTG_FOLD_LAND_ORDER_SEEN", false)` in `FoldLandOrderSeenFlag`, plus the
     `HeuristicArm.h:1022` comment.
   - Run smoke+regression to list the moved keys (`test/regression.sh --smoke --regression`).
   - Do NOT accept GT; the parent's integration push owns the Maze accept.
   - Push `wip/maze-p5b2` only.

   **If it fails:** root-cause the failing games and report.
7. **Report:**
   - the cut-signal design;
   - units removed per shape (`ana.txt`);
   - the per-deck A/B, with bar per-deck net <= 0;
   - the d8 results;
   - ref_bench;
   - the commits.

## Machine rules (from the brief)

- One heavy job at a time, through `scripts/heavy_run.sh` at full `nproc`, with `MTG_RSS_CAP_GB=20` and
  `MTG_BUILD_JOBS=12`. One-thread single-game traces run outside the lock.
- `./build.sh` only. No timeouts.
- Waiters use `run_in_background` on a PID or a DONE marker, never `pgrep -f`.
- Judge cost on units, not wall.
- Subagents do not push the shared branch.

## USER rulings (verbatim, as given in the brief)

- "the top priority are optimizations and fixes"
- "Wasted work is wasted work regardless of the situation."
- "A budget may re-price work, never make a line unfindable"
- The USER's carve-outs (Gateway Plaza, colour Gates, Gond Gate, The Black Gate, Thran Portal, lands under two Amulets,
  Guild Summit's Gate/non-Gate split) stay as in P5b2-results.md.
- From P5b2-results.md: "the order doesn't matter much here unless the second land is played after other things or we
  are paying for a cost like Gateway Plaza".
