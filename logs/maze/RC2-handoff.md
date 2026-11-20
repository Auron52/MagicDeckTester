# RC-2 handoff: Maze's End depth-0 rollout-cache fix (2026-11-20 ~14:50 UTC)

Continues `logs/maze/RC1-handoff.md`. Scratch: `build/scratch/maze/rc2/` (RC-1's is `rc1/`).

## Branches (rebased onto batch 1 = origin/phase-1-2-deck-analyzer 8fbbc5023; pushed, wip only)
- `wip/maze-rc1`: e201e33a9 (the RC-1 fix, transplanted with `rebase --onto 8fbbc5023 aa16513e7`; the old base was
  rewritten upstream), then coordinator handoff notes 9a6e8c2ae / 354bb458c, then **b01b594fd** (RC-2 diagnostic:
  `MTG_LEAF_VERIFY_TRACE` also traces the ORIGINAL rollout of the traced key; STORE-TIME MISMATCH prints
  `binds_d/trunc_d/exceeded`; print-only. Its commit message says UNBUILT, but it IS built: `rc2/mtg.rc2diag`).
- `wip/maze-rc1-p5b`: **3ebba2691** = 8fbbc5023 + the P5b chain (59becded8..9900a1917 replayed) + the RC port. The only
  conflicts were HeuristicArm.h slot-list appends, resolved keep-both (tail: SUMMIT_TWIN_FOLD, FOLD_LAND_ORDER,
  FOLD_LAND_ORDER_SEEN, SOLVE_MEMO_BIND_GUARD, LEAF_WIN_BIND_GUARD, matching the names table).
- Backups of the pre-rebase tips: `backup/maze-rc1-pre-rebase` (039319457), `backup/maze-rc1-p5b-pre-rebase` (2e87ffd25).
- Windows CI green: e201e33a9 (44689629592), 3ebba2691 (44689629949), b01b594fd (44725488207). The pre-rebase run
  44679704717 also passed the full ctest `unit` (including test_rollout_cut_store).
- Builds: three tries. try1/try2 failed with 9p order-4 "Cannot allocate memory", caused by uncapped `./build.sh`
  (32 cc1plus filling RAM; the coordinator's diagnosis). `rc1/build_both.sh` now uses `MTG_BUILD_JOBS=12`; try3
  passed on both trees. Pinned binaries: `rc2/mtg.rc1` (e201e33a9 tree), `rc2/mtg.rc2diag` (b01b594fd),
  `rc2/mtg.p5b` (3ebba2691).

## DONE, with measurements
1. **Units + scenarios** (chain `rc2/chain1.sh`, log `rc2/chain1.log`): full mtg-test exit 0 (`rc2/units.log`).
   `-tc='RC-1*'` passes with the guards on. With both guards =0 it FAILS exactly as designed (`rc2/rc1_off.log`):
   memo case `served_is_full` false / `served_is_cut` true; leaf case `CHECK(7 == 6)`. Both REQUIRE preconditions
   hold, so the test reproduces the stale hit. Scenarios ran inside smoke.
2. **Smoke** on e201e33a9 (`rc2/smoke.log`): 234/235 unchanged, 1 changed: `hinata_smoke_d5_s1001` gi83 5->6
   (searched slower; d8 b0 below recovers). `test/check_gt_logs.py` exit 0.
3. **Verify, single games** (`rc2/run1.sh`, single thread d1 b1, LEAF+NOWIN verify; dirs `rc2/diag/*`):
   | game | guards on | both off |
   |---|---|---|
   | 2HG g491 (seed 313492) | 0 leaf / 0 nowin / 0 store-time | 0 / 2 nowin / 2 store-time (depth-1) |
   | std g945 (seed 303946) | 0 / 0 / 0 | 0 / 0 / 1 store-time |
   Lever flips on g491: ONLY the leaf guard off -> 8 leaf stale (cached 8 / fresh 7) and 12 store-time mismatches
   (stored 8 / fresh 7). ONLY the memo guard off -> 2 no-win SAME-STATE DRIFT, 0 leaf. Both off -> the leaf stale
   vanishes because the memo serves the cut plans to the verify re-rolls too (RC-1's account). Guards on:
   `[solve-memo] bind_refusals=23333`.
4. **Trace diff, key c426b596060660d1** (2HG g491, leaf guard off, `rc2/diag/tr_leafoff/err.txt` lines 43-61):
   ORIGINAL stored rollout from t4 d0 (board bf=[Baldur's Gate, Amulet, Selesnya GG, Burgeoning, Manor Gate, Gond Gate,
   Exploration, Black Dragon Gate, Maze's End] hand=[Cliffgate]). T4-T7 main 1 play EMPTY plans (land drop only):
   the charged walk was cut before it considered anything, so it passed. Win T8. Store line `binds_d=6 trunc_d=0
   exceeded=0`. The FRESH replay of the same state: T4 Maze's End(x5)>Citadel Gate, T5 Exploration, T6 Sylvan
   Scrying + Maze's End(x5)>Thran Portal, T7 Exploration + Maze's End(x2)>Gateway Plaza, win T7. Every store-time
   mismatch prints binds_d 5-6 with trunc_d 0, which is exactly the watermark the guard refuses on. Mechanism CONFIRMED.
5. **Held-out A/B** (`rc2/ab/manifest.json`, regenerated on the rebased suite: 15,674 jobs incl. 1dropburn; ONE
   pooled batch, `rc2/ab/run.out|err`; `python3 rc1/ana.py rc2/ab/run.out`):
   | cell | n | old -> fix | better/worse | net | units/g | same-digest |
   |---|---|---|---|---|---|---|
   | maze-2hg-d1b1 | 1000 | 5.4320 -> 5.4310 | 1/0 | -1 | +6.71% | 996 |
   | maze-2hg-d1b3 | 1000 | 5.4530 -> 5.4540 | 0/1 | +1 | +5.68% | 996 |
   | maze-2hg-d3b10 | 400 | 5.3750 = | 0/0 | 0 | +5.41% | 400 |
   | maze-2hg-d5b20 | 400 | 5.2900 = | 0/0 | 0 | +4.63% | 399 |
   | maze-std-d1b1 | 1000 | 5.6540 = | 0/0 | 0 | +6.46% | 996 |
   | maze-std-d1b3 | 1000 | 5.6080 = | 0/0 | 0 | +4.69% | 998 |
   | maze-std-d3b10 | 400 | 5.5975 = | 0/0 | 0 | +4.03% | 400 |
   | maze-std-d5b20 | 400 | 5.5425 = | 0/0 | 0 | +4.76% | 400 |
   | snow d3b10 / d5b20 | 400+400 | = | 0/0 | 0 | +3.32% / +5.35% | 800 |
   Per deck: maze-std net 0 (6 digests moved, units +4.93%), maze-2hg net 0 (1/1, 9 moved, units +5.49%), snow net 0
   (0 moved, units +4.50%). Every other suite deck in the screen: 0 digests moved (units within +-0.4%, cyclone2hg
   +3.8% with no play change). **Bar met: per-deck net <= 0 everywhere.** SLOWER: maze-2hg-d1b3 g978 (5->6).
6. **d8 b0 recovery** (`rc2/d8/manifest.json`, `rc2/d8/run.out`, DONE `rc2/d8/DONE` "d8 exit 0"): maze-2hg-d1b3 g978
   (seed 413979) fix T5 = old T5, same digest 5a8a98c9d025fb5a, 12,826,646 units on both; hinata smoke gi83 (seed 1084)
   fix T5 = old T5, digest 171db73f2b961f7f, 355,919 units on both. Both recover, and the guards are inert at b0.
7. **Broader verify** (`rc2/vfy/manifest.json`, 150 games, fix arm, MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1, mtg.rc2diag):
   Maze std+2HG d1b1 x30, d3b10 x25; Snow d3b10 x25, d5b20 x15. **0 no-win stale, 0 store-time mismatch, but 3 leaf
   STALE hits (2 keys), all SAME-STATE DRIFT at depth=1 with cached EARLIER than fresh (8 vs 9, 7 vs 8).**
   Localized with per-game processes (`rc2/loc/run_loc.sh`, `rc2/loc/summary.txt`) to ONE game: **maze-2hg-d3b10 g22**
   (`MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2`, seed 434023, gi 22, d3 b10). It reproduces single-thread. Lever flips
   (`rc2/diag/g22_*`): base 3, both guards OFF 3, MTG_SOLVE_MEMO=0 3, MTG_DECISION_PRICED_X=0 3, MTG_ROLLOUT_GAME_CACHE=0
   **1**, MTG_UNBUDGETED_LEAF_MEMO=0 **6**, ROLLOUT_M1_MEMO / GAME_LINE_CACHE / INWIN_CACHE / FS_NOWIN_CACHE /
   EXH_LEAF_MEMO / ENUM_MEMO / PAY_PROBE_MEMO =0 all 3; --budget-ms 0 -> **0**.
   **Finding (open, NOT the RC-1 mechanism, pre-existing: same count with the guards off):** the STORED replay re-runs
   the identical saved GameState copy with no budget and no table. It returned the stored value at store time (no
   store-time mismatch), and a later replay of the same copy returns a later win. So hidden per-game state that a
   budgeted game leaves behind changes a depth-1 UNBUDGETED search. No single cache flag removes it. The rollout game
   cache contributes (3 -> 1); budget 0 removes it. Repro: `DEPTH=3 BUDGET=10 rc2/run1.sh rc2/mtg.rc2diag <tag> 434023
   22 1`. Probably the "rollout-cache history drift" P5b-4 called flag-independent.

## RUNNING at handoff
- **P5b fold re-run** (step 4): background task `b9rogu492` (`rc2/runs2.sh`) -> heavy_run PID 2607751 -> mtg PID
  2650321 (`rc2/mtg.p5b --batch rc2/p5b/manifest.json --threads 32`, cwd maze-rc1-p5b). Started ~14:44 UTC. Out
  `rc2/p5b/run.out|err`, DONE marker `rc2/p5b/DONE` ("p5b exit N <time>"). 11,200 jobs, P5b-4's manifest repointed at
  maze-rc1-p5b (decks identical to maze-p5b2). Arms `_off` (MTG_FOLD_LAND_ORDER_SEEN=false) vs `_on` (true).
  Analyse: `python3 build/scratch/maze/p5b3/ana.py build/scratch/maze/rc2/p5b/run.out` (per cell + TOTAL std/2hg net +
  SLOWER). The task belongs to the RC-2 session; RC-3 should wait on the marker:
  `while [ ! -e rc2/p5b/DONE ]; do sleep 60; done`.
- Nothing else is running. The localization (task bjqcq7c91), d8 (in b9rogu492), verify chain (blelkd85f) and A/B
  (benw5dkef) all finished.

## What remains, in order
1. When `rc2/p5b/DONE` lands: run p5b3/ana.py and report the std/2HG net. P5b-4 before the fix: std net -3 (4/1), 2HG
   net +1 (0/1), which FAILED the bar. **Do NOT adopt the fold** (report only).
2. **Regression tier** on wip/maze-rc1 (the worktree build = b01b594fd, print-only vs e201e33a9):
   `cd .claude/worktrees/maze-rc1 && MTG_RSS_CAP_GB=20 bash scripts/heavy_run.sh bash test/regression.sh` (run ONCE).
   Then analyse every moved game: searched SLOWER -> d8 b0 both arms (`mtg --batch` with `depth 8 budget_ms 0
   ignore_play_profile abandon_units 20000000`; old arm = flags MTG_SOLVE_MEMO_BIND_GUARD/MTG_LEAF_WIN_BIND_GUARD
   false). **Per the coordinator: NO GT accepts on the branch** (smoke or regression); the batch-2 integrator accepts
   once over the combined tree. Report the moved keys and analysis instead. Smoke's single move is already analysed
   (hinata_smoke_d5_s1001 gi83 5->6, recovers at d8).
3. `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`.
4. Optional, surface to the parent: the g22 drift finding above. It needs its own investigation and is not part of
   this fix. If deferred, it belongs in a docs/design note.
5. Results doc `logs/maze/RC1-results.md` (git add -f) is not written yet; this handoff holds every number.

## Rulings carried (verbatim from the RC-2 brief / coordinator / CLAUDE.md)
- "Every heavy command goes through `scripts/heavy_run.sh` (one heavy job on the box at a time). Set `export MTG_RSS_CAP_GB=20`. Scratch goes in build/scratch/maze/rc1/." (RC-2 used rc2/.)
- "Push wip/maze-rc1* only. NEVER push the shared branch; the parent integrates this into the next batch."
- "No timeouts. Do not kill runs you did not start."
- "Keep context small: wait with background waiters, never polling turns."
- "Bar: per-deck net <= 0 on every deck, and every slower game recovers at `--depth 8 --budget-ms 0`."
- "Re-run P5b-4's fold A/B on the wip/maze-rc1-p5b binary. Report std/2HG net. Do NOT adopt the fold."
- Coordinator: "rebase the RC fix branches (wip/maze-rc1, wip/maze-rc1-p5b) onto 8fbbc5023 (rebase, never merge), rebuild + units + smoke, run check_gt_logs, push wip/* only, and report the rebased tips for batch 2."
- Coordinator: "Make every build in your retry loop use MTG_BUILD_JOBS=12".
- Coordinator: "skip the `--deck`-scoped GT accepts on your branch. The batch-2 integrator rebases and accepts GT once over the combined tree, so branch-local accepts would be thrown away. The regression tier and the per-moved-game analysis are still needed."
- CLAUDE.md (USER): adoption bar "*+1 is not neutral. 0 is neutral.*"; heavy_run.sh now tail-releases only a single `--batch` command, so wrap EACH heavy stage of a chain in its own heavy_run and call it as `bash scripts/heavy_run.sh` (the primary checkout's copy was mode 644 at one point).
- Overnight window (13:30-21:00 UTC under PST): a long run in it needs a 2-hourly session-only `CronCreate` timer. RC-2 had no CronCreate tool, so the parent should arm one if RC-3 cannot.
