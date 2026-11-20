# RC-1 handoff: Maze's End depth-0 rollout-cache drift (2026-11-20 ~05:15 UTC)

Branch `wip/maze-rc1`, based on wip/maze-shared aa16513e7. The fix is committed **UNBUILT and UNTESTED** (the build
was still queued behind the heavy lock at handoff). Companion branch `wip/maze-rc1-p5b` = wip/maze-p5b2 (9900a1917,
the land-order fold) + the same fix ported, for step 5 (the P5b-4 A/B re-run). Scratch: `build/scratch/maze/rc1/`.

## Mechanism (step 1) -- found, with evidence from the existing binary
Binary used: `build/scratch/maze/p5b3/mtg.diag3` (p5b2 tip, fold OFF). Repro script `build/scratch/maze/rc1/run1.sh
<bin> <tag> <seed> <gi> <2hg> [env...]` (single thread, d1 b1, MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1, fold off).

| g491 2HG (seed 313492) variant | leaf-verify STALE | nowin STALE |
|---|---|---|
| default | 6 (cached 8 / fresh 7) | 3 ("SAME-STATE DRIFT") |
| MTG_SOLVE_MEMO=0 | 8 | 0 |
| MTG_SOLVE_MEMO_VERIFY=1 | 6 | 0 |
| MTG_DECISION_PRICED_X=0 / MTG_ROLLOUT_GAME_CACHE=0 | 6 / 6 | 3 / 3 |
| **per-job flag MTG_SOLVE_CHARGE=false** (batch `rc1/b1/m_off.json`) | **0** | **0** |
| --budget-ms 0 | 0 | 0 |
(env MTG_SOLVE_CHARGE=0 does NOT turn it off: the Maze provider's SolveChargeWeightOptIn()=1 wins; use the per-job flag.)
g945 std (seed 303946) shows 0 stale with the fold off on this binary.

**Cause: the charged greedy walk (MTG_SOLVE_CHARGE; MazesEndProvider opts in at W=1, SnowProvider at W=16)**. Inside a
budgeted depth-0 rollout the greedy subset walk consumes the rollout budget and stops at its best-so-far once
`Exhausted()/Overrun()` reads true (TurnSolver.cpp, the `g_greedy_charge_budget` block in the walk, ~line 63005). That
read books a bind (`budgetbind::t_binds`), but two caches store the cut result as if it were the budget-free answer:
1. **Leaf table WIN store** in `SimulateToEnd` stored any win with no watermark (the no-win store and the rollout game
   cache both require no trunc + no bind). A rollout run after the spend wins later (8) than with budget left (7) and
   that 8 was served -> the 6 leaf-verify stale hits (persist with the solve memo off).
2. **Greedy Solve memo** (`solvememo::t_cache`, `TurnSolver::Solve`) stored the cut walk's plan unconditionally and
   served it to later solves of that position in the decision, the unbudgeted verify re-rolls included -> the store-time
   no-win verify agreed (it was served the same cut plans), a later replay in another context did not = "SAME-STATE
   DRIFT" (gone with the memo off or in verify mode).
Not yet done for step 1: the trace diff of the two rollouts of one stored state. The new binary carries the diagnostic
for it: `MTG_LEAF_VERIFY=1` now prints STORE-TIME MISMATCH and, on a stale hit, replays the STORED state (KEY HOLE vs
SAME-STATE DRIFT); `MTG_LEAF_VERIFY_TRACE=<key h1 hex>` fs-sim-traces those replays (stale lines print `key=`).
Run it with `MTG_LEAF_WIN_BIND_GUARD=0 MTG_SOLVE_MEMO_BIND_GUARD=0` to see the old defect.

## Fix (step 2) -- committed, unbuilt
Both default ON, both per-job heurarm slots (pool A/B arms in one batch); `=0` = old rule, A/B control only.
- `MTG_SOLVE_MEMO_BIND_GUARD` (slot SOLVE_MEMO_BIND_GUARD): `TurnSolver::Solve` does not store when `t_binds` moved
  during `SolveUncached` or `decisionwork::Exceeded()`; counter `bind_refusals=` on the `[solve-memo]` line.
- `MTG_LEAF_WIN_BIND_GUARD` (slot LEAF_WIN_BIND_GUARD): `SimulateToEnd` stores a win only when trunc and binds are
  unchanged since entry and the decision meter is not spent. Restructured so a refused win does NOT fall into the
  no-win `else` branch.
- Inert at unlimited budget (binds never move), so d8 b0 replays are identical on both arms.
- Files: src/ai/TurnSolver.cpp (solvememo::BindGuardOn + g_bind_refusals; Solve store; LeafWinBindGuardOn + leaf
  verify diagnostics near t_nowin_stored_states ~line 1143; win store in SimulateToEnd; test seams
  SolveMemoCutProbeForTest / LeafWinCutProbeForTest after FslEdgeNoReachCountForTest), src/ai/TurnSolver.h (seam
  decls after SimulateToEndSpentTwiceForTest), src/ai/HeuristicArm.h (2 slots + names at the end of the lists),
  test/unit/test_rollout_cut_store.cpp (2 cases: Vampires provider, Bloodghast + Mountain + Bolt, opp 4, T6; asserts
  cut != uncut and the later unbudgeted query gets the uncut answer), CMakeLists.txt.
- UNVERIFIED assumptions in the test: that a walk cut at its first visit passes (no Bolt) so the cut rollout wins T7
  vs T6. If the REQUIRE precondition fails, pick a board where it holds; do not weaken the assertion.
- Engine stays heuristic-free: only cache admission changes.

## Queued build (leave it; it is a child of the RC-1 session, so check it survived)
`scripts/heavy_run.sh build/scratch/maze/rc1/build_both.sh`, heavy_run PID 2134180 (parent shell 2134179), queued
05:11 UTC behind the idgate overnight batch (132k games, `build/scratch/idgate/ovn1`), a p5d5 d8 run and a regression
tier -- expect hours. It builds maze-rc1 then maze-rc1-p5b. Logs `rc1/build_rc1.log`, `rc1/build_p5b.log`; DONE
markers `rc1/build_rc1.done`, `rc1/build_p5b.done` ("<tree> exit <code>"); `rc1/heavy_build.log`. If PID 2134180 is
gone with no markers, requeue: `cd build/scratch/maze/rc1 && MTG_RSS_CAP_GB=20 scripts/heavy_run.sh ./build_both.sh`.
NOTE: it compiles the working tree, which equals the committed tree at handoff.

## What remains, in order
1. Build OK -> unit suite (incl. test_rollout_cut_store) + scenarios on maze-rc1. Fix the test board if the
   precondition fails.
2. Verify: g491 2HG + g945 std with `run1.sh <new bin>`: 0 leaf/nowin stale with the guards on; reproduce with both
   =0. Do the trace diff (STORED replay lines + MTG_LEAF_VERIFY_TRACE) for step 1's write-up. Broader Maze sample: a
   pooled batch of ~100 games std+2HG d1b1/d3b10 under MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1, all stale counts 0.
3. A/B: manifest ready, `rc1/ab/manifest.json` (15,622 jobs; generator `rc1/mkab.py`): Maze std+2HG at P5b-4's 4 shapes
   on fresh seeds 401001-414000, Snow d5b20/d3b10 400+400 (seeds 421001+), plus a digest SCREEN of every other suite
   deck's budgeted smoke cases (10 d5 / 6 d3 games, seeds 501001+). Arms fix vs old, innermost. Run as ONE batch via
   heavy_run, `--threads $(nproc)`, MTG_RSS_CAP_GB=20, out to `rc1/ab/run.out|err`, from the maze-rc1 worktree.
   Analyse with `python3 rc1/ana.py rc1/ab/run.out` (per cell, per deck, SLOWER list). Screen decks whose digests move
   need their own held-out A/B (second batch: a real data dependency). Bar: per-deck net <= 0.
4. d8 b0 for every slower game: `python3 rc1/mkd8.py rc1/ab/manifest.json rc1/ab/run.out -o rc1/d8/manifest.json`,
   one batch; the fix is inert at b0, so check each game reaches the old arm's turn or better.
5. P5b-4 re-run on the maze-rc1-p5b binary: manifest `build/scratch/maze/p5b3/ab/manifest.json` (fold off vs on), but
   its deck paths point at the maze-p5b2 worktree (fine: same decks). Analyse with `p5b3/ana.py`. Report whether the
   fold now passes; do NOT adopt it.
6. Commit results to `logs/maze/RC1-results.md` (git add -f), push wip/maze-rc1 (never the shared branch), and
   watch Windows CI with `gh run watch <id> --interval 60` in the background.

## User/parent rulings carried (from the RC-1 brief)
Every heavy command goes through `scripts/heavy_run.sh`; `export MTG_RSS_CAP_GB=20`; scratch in
build/scratch/maze/rc1/; waiters on PID (`kill -0`) or DONE markers, never `pgrep -f`; push wip/maze-rc1 only, NEVER
the shared branch; no timeouts; do not kill runs you did not start; sound fix default ON; engine heuristic-free in
the search window; A/B bar per-deck net <= 0 and every slower game recovers at --depth 8 --budget-ms 0; hand off at
~250K.

## UPDATE ~07:47 UTC: first build attempt failed on 9p ENOMEM, requeued
Both builds exited 1 with only `Cannot allocate memory` on a header read (the 9p order-4 allocation failure, not RAM
and not a compile error; TurnSolver.cpp had not finished compiling). Logs moved to `rc1/try1/`. Requeued the same
`heavy_run.sh ./build_both.sh`: heavy_run PID **2267322
2267323**, same markers `rc1/build_rc1.done` / `rc1/build_p5b.done`.
If it fails the same way again, just retry; do not cap jobs on that basis.

## UPDATE ~09:36 UTC: the requeue failed the same way; NOT requeued again
Second attempt: both trees exit 1, again only `Cannot allocate memory` on header reads (SpellEffects.h), ~100-125
objects in, TurnSolver.cpp never reached -- so the fix has still never been compiled. Logs in `rc1/try2/`. No build is
queued now. RC-2: check `dmesg` for the 9p allocation failure, then requeue
`cd build/scratch/maze/rc1 && MTG_RSS_CAP_GB=20 bash scripts/heavy_run.sh ./build_both.sh` (markers as above).
