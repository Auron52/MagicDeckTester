# SUMF handoff: the Guild Summit twin fold (Maze's End)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sumf`, branch `wip/sumf` (from `origin/wip/maze-shared`
9171fde50, `HEAD:src` 1baf8fba5). The code is committed as ONE WIP commit (see `git log -1` on wip/sumf). **It has
NOT compiled yet**: the first build is still queued on the heavy lock (below). Do not trust it until it builds and
the units pass.

## The finding (re-measured on the current tree)
Baseline binary `build/scratch/sumf/mtg.base` = a copy of `.claude/worktrees/maze-shared/build/Release/mtg`, built
06:08 UTC from the same src tree (last src commit 04:04, src clean). s3 T4 frame-0 repro, 1 thread, outside the lock:
```
cd /workspaces/MagicDeckTester/build/scratch/sumf
MTG_BIN=$PWD/mtg.base H2_ERR=$PWD/base_s3b.err MTG_TRACE=search MTG_ROLLOUT_STATS=1 MTG_TURN_CENSUS=1 \
  MTG_CAND_CENSUS=1 MTG_DUP_DIFF=1 MTG_DEDUP_CENSUS=1 \
  python3 h2.py references/Maze_s_End/claude_s3_gi2.json 4 -- --depth 5 --budget-ms 20 --threads 1
```
(`h2.py` = `build/scratch/maze/r1/handoff2.py` with R = this worktree; it reuses r1's resolved caches. Note
MTG_TURN_CENSUS=1 writes a file named `1` into the cwd's repo root: move it out, never commit it.)
- BEFORE: `T4 pass=1 done win=6 cost=4939` (F7-2 quoted 5,103 before P7), units_total 5,034, la_cand 1,100,
  la_bp_wave 1,209, greedy_walk 1,643. Win T6.
- `[dup-diff] duplicates=511` of 1,100 la_cand candidates. Pure Summit twins `A+{Guild Summit|k0} B+{Guild Summit|k0|xN}`:
  **426** (plain 140+91+37+12 = 280; PL[bp] 71+53+14+4+4 = 146), always the LATER candidate (first = unpinned).
  ~32 more are Summit+Talon mixes (not pure twins, not folded).
- KEY FACT the F7-2 note got wrong: the post-apply dedup skips only `PlanDupSkippable` plans (axis variants /
  land_slot). A plain pinned-Summit plan is NOT one, and `MTG_CAND_DEDUP` / `MTG_COST_REFRAME` are default OFF, so
  the 280 plain twins pay unit + apply + combat + rollout (`[dup-diff] POST-COMBAT unpinned: n=864 dup=279`).
  The fold should therefore save more than 1 unit per twin. The PL[bp] twins ARE dup-skipped (1 unit + apply each).

## The fold design (all uncommitted-to-shared, on wip/sumf)
- `src/core/SpellEffects.h` ~8898-8910: thread-local witness `g_summit_tap_resolutions` (monotonic),
  `g_summit_tap_last_avail / _n / _pinned`; written in the Guild Summit ETB block ~13826 after the clamp + human chooser.
- `src/ai/TurnSolver.cpp` ~35520-35720: comment block + `SummitTwinFoldOn()` (`MTG_SUMMIT_TWIN_FOLD`, DEFAULT ON,
  heurarm slot `SUMMIT_TWIN_FOLD` so an A/B pools per job), `SummitTwinFoldVerifyOn()` (`MTG_SUMMIT_TWIN_FOLD_VERIFY`),
  stats atomics, `TurnSolver::SummitTwinFoldsForTest()`, and `struct SummitTwinFold` (EtbcFold's shape):
  * Build: plans with exactly ONE Guild Summit CastFromHand; base = chosen_x 0; twin = chosen_x >= 1 matched to an
    EARLIER base by `Plan::operator==` with that action's chosen_x masked (plus bp_self / bp_wave0, as EtbcFold).
    Excludes bounce/dig/put-back/etbcounter families and the host's cp-share / put-fold / etb-fold / etbc participants.
  * Record (after the base's apply): clean = EtbcFold::Witness unchanged (no units, no bp channel, no trunc/drop/bind,
    no searched-only suppression) AND exactly one Summit resolution, unpinned. Banks avail, n, dug.
  * Skip: twin declined iff base clean and `clamp(pin, 0, avail) == n`.
- Loop wiring (la_cand host only, SolveWithLookahead): Build right after `etbc_fold.Build` (~126311, materializes a lazy
  list first); the decline block just BEFORE the `ConsumeAt(kLookaheadCand)` unit charge (~126835; EtbcFold's skip
  bookkeeping: per-apply resets, g_apd_last_dug, w0len_record, ++candidates_done); Witness/Record/Check at both apply
  sites (pre-combat non-cp-share ~126980-126991, main-2 ~127278-127288). Stats line `[rollout-stats] summit_twin_fold
  matched= skipped= (verify ok= MISMATCH=) | applied anyway: count_differs= base_* ...` printed beside etbc_fold.
- Soundness argument (also in the code comment): twin and base differ only in that chosen_x, read only as etb_kx at
  resolution (Summit has no {X}); same state at resolution; WHICH Gates is EtbTapSubtypeOrder of the count alone, so
  equal count = equal tapped set and draws = equal state; clean base = apply is a function of state + plan. First-
  reached kept (`plan.value` is in the equality, so the strict tie-break keeps the earlier). Declined before the unit
  charge = a re-price (bounce/dig/put-back shape), inert at unlimited budget. NOT wired: FSLineWin host (fs_pre 42
  units on s3) and the wave walker.
- OPEN RISKS to check first: (1) "matched" may be ~0 if some Plan field (value, a signature string) carries chosen_x;
  (2) "base_hit_bp" may dominate (a Summit draw may open a hand-entry breakpoint), leaving coverage low -- then the
  next step is a bp-aware variant, which is NOT exact when a breakpoint precedes the Summit resolution (a continuation
  changes the rest-of-line default count), so only fold breakpoints AFTER the resolution, and verify.
- Unit tests: `test/unit/test_summit_twin_fold.cpp` (added to CMakeLists.txt after test_mazes_end_p6.cpp): witness +
  one-count-one-tapped-set (Baldur's Gate taps last); per-plan identity (count lands -> same digest, differs -> different,
  both kinds present); SolveWithLookahead d2 unlimited, fold on vs off (heurarm Arm) -> same plan/win/state and the fold
  fires. Never compiled -- fix compile errors first.

## Jobs running / markers
- Build: `scripts/heavy_run.sh ./build.sh` PID **1542413** (cwd this worktree), log
  `/workspaces/MagicDeckTester/build/scratch/sumf/build0.log`; DONE marker = a line `EXIT <rc>` appended to that log.
  It was queued behind ~12 heavy-lock waiters at 12:40 UTC and builds whatever src is on disk when it starts (the
  fold code). My background waiters on that EXIT line die with my session; re-arm one
  (`until grep -q EXIT .../build0.log; do sleep 30; done`). The binary lands at `build/Release/mtg` in this worktree.

## Remaining, in order
1. When build0 lands: if it failed, fix compile errors, rebuild via `MTG_RSS_CAP_GB=20 MTG_BUILD_JOBS=12
   scripts/heavy_run.sh ./build.sh`. Copy the binary to `build/scratch/sumf/mtg.stf`.
2. Units: `build/Release/mtg-test` (all; 0 failed), incl. the three new cases.
3. s3 census AFTER (same command, MTG_BIN=mtg.stf), plus once with `MTG_SUMMIT_TWIN_FOLD_VERIFY=1` (MISMATCH must be
   0) and once with `MTG_SUMMIT_TWIN_FOLD=0` (must equal mtg.base byte-for-byte: T4 cost 4939). Report T4 pass-1 cost,
   units_total, la_cand, dup-diff Summit classes, `summit_twin_fold` stats line, win turn.
4. Smoke + regression: `MTG_PGO=0 MTG_RSS_CAP_GB=20 scripts/heavy_run.sh bash test/regression.sh --both` in this
   worktree. Only mazesend / mazesend2hg keys may move (the fold arms only on a list holding a pinned Guild Summit);
   anything else moving is a defect. NEVER `--accept`.
5. If byte-identical play + fewer units: commit (that is the clean win). If play moves: held-out A/B, one pooled batch,
   arms innermost: `python3 build/scratch/sumf/mkab.py build/scratch/sumf/ab/manifest.json` (arms off =
   `{"MTG_SUMMIT_TWIN_FOLD": false}` vs stf; seeds 221001/222001/223001/224001, rows d5 b20 x1500, 2HG d5 b20 x400,
   d3 b10 x400, d0 x500), run `MTG_RSS_CAP_GB=20 scripts/heavy_run.sh build/scratch/sumf/mtg.stf --batch <manifest>
   --threads $(nproc)` > ab/run.out, analyse `python3 build/scratch/sumf/anab.py ab/run.out off`. Bar: per-deck net <= 0;
   every slower game recovers at d8 b0 (`mkd8.py ab/run.out ab/manifest.json ab/d8.json off stf`, one batch).
6. `MTG_BIN=build/scratch/sumf/mtg.stf python3 scripts/ref_bench.py --deck maze_s_end` at the shipped d5 b20: all 11
   tie or beat the USER (baseline per F7-2: 11/11 tied).
7. Push wip/sumf only. Do NOT accept GT, do NOT push the shared branch. Report: units before/after, tier movement,
   A/B, ref_bench, commits.

## USER rulings (verbatim, as given in the brief)
- "the top priority are optimizations and fixes"
- "Wasted work is wasted work regardless of the situation."
- Guild Summit: cards over the Treasure.
- Machine rules from the brief: one heavy job at a time through `scripts/heavy_run.sh` at full `nproc` threads with
  `export MTG_RSS_CAP_GB=20` and `MTG_BUILD_JOBS=12`; one-thread probes outside the lock; `./build.sh` only; no
  timeouts; waiters on a PID or DONE marker, never `pgrep -f`; scratch under `build/scratch/sumf/`; handoff at ~250K.
