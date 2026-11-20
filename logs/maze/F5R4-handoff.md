# F5R-4 handoff (pay refine: pre-check, budget-charged caps, ReachesByTapping subset; Maze's End)

Worktree `.claude/worktrees/maze-f5r3`, branch `wip/maze-f5r3` (pushed). It is rebased onto origin/phase-1-2-deck-analyzer
08b13f602 ("Pay-refine Dfs node cap"), which sits after the Maze batch-1 landing 8fbbc5023.
Scratch: `build/scratch/maze/f5r3/` (tools/, run/<name>/{out,err,DONE}, trc/<bin>/).
Predecessor: `logs/maze/F5R3-handoff.md`.

## Commits on wip/maze-f5r3 (oldest first, on 08b13f602)
- af8a7297e F5R-3: budget-charged probe cap (MTG_PAY_REFINE_BUDGET_CAP), per-layer refine trace
- 06da90732 F5R-3 handoff note (9p ENOMEM re-queue)
- 216bf7bad F5R-4: MTG_PAY_REFINE_PRECHECK + priced/folded ReachesByTapping (MTG_PAY_REFINE_RBT_PRICED), test hooks
- 69583cafb F5R-4: the 08b13f602 Dfs node cap is priced too under MTG_PAY_REFINE_BUDGET_CAP; unit tests
- 7e9e9a280 F5R-4: MTG_PAY_REFINE_RBT_SUBSET, its unit test, MTG_RBT_DEBUG diagnostic print (TIP before this file)
- (this file's commit)
After 69583cafb: a test-hook fix (SetRefineFreeProbesForTest no longer scales the node cap). It is inside 7e9e9a280.

## Levers (all heurarm slots + EngineFlags readers, all default OFF)
1. MTG_PAY_REFINE_BUDGET_CAP (PayEnum::ProbeOut / NodeOut). The first 4,096 probes and the first node_cap Dfs nodes are
   free; past them the walk charges the decision's budget (kPayRefine, `u_pay_refine`): 1 unit per probe, and 1 unit per
   64 nodes. It stops only when the budget is exhausted or overrun, or when gamework::Poll trips. At a null budget it
   stops at the ceilings: probes MTG_PAY_REFINE_CEILING (1<<20), nodes 64x that. A budget-cut refine is not memoised.
   NOTE: DecisionBudget() is also null at the executor's committed apply, so the executor runs uncapped to the ceilings.
2. MTG_PAY_REFINE_PRECHECK. In a release layer with |taps|-L > 4 (and !no_trade, and RBT_PRICED/RBT_SUBSET both off),
   it skips consider() and the surplus supersets. It is sound but saves no probes on Maze (see measurements).
3. MTG_PAY_REFINE_RBT_PRICED (ReachesByTappingPriced). The face combinations are folded by (per-source effect, summed
   pool delta), an identity fold under the post-tap memo's claim; MTG_PAY_REFINE_RBT_VERIFY=1 checks it against the
   unfolded enumeration. The ">4 released" refusal and the 256-combination guard become prices: 256 checks are free when
   at most 4 sources are released, and every other check is 1 unit; MTG_PAY_REFINE_RBT_CEILING (1<<20) is the safety
   ceiling.
4. MTG_PAY_REFINE_RBT_SUBSET (the root-cause fix, NOT YET MEASURED). ReachesByTapping first tests the candidate as it is
   (CoversBoards(c,e,true)), then lets each released source be left untapped. CoversBoards already reads an untapped
   source as covering a tapped one, and a deck with a same-phase untapper (no_trade) already accepts that way. The old
   code re-tapped EVERY released source.
Stats under MTG_PAY_REFINE_STATS: `=== PAY REFINE CAP/PRECHECK/RBT ===` lines.
Test hooks: manaoracle::LastRefineRunForTest, SetRefineFreeProbesForTest, ReachesByTappingForTest,
TurnSolver::SwapDecisionBudgetForTest.
Unit tests in test/unit/test_pay_refine.cpp: the budget cap (free / null / exhausted / room / not memoised), RBT priced
(5 Plains; <=4-source parity), RBT subset (Cascade Bluffs + Island). WARNING: the subset test's "defect pinned"
CHECK_FALSE lines are probably wrong. Cascade Bluffs has a free {C} face, so the old rule taps it for {C} and covers.
Expect those two CHECK_FALSEs to fail in units.f5.txt; replace them with a source that has no free face, or drop them.

## Measurements (430-game wall manifest wall.json: Maze's End 280 d5b20 + 150 d3b10; base = 08b13f602 + levers off)
- Trace (trc/f4): 744,890 refines, 50 capped, all 50 found=0, max probes 4097. Every capped refine is a whole-line bill
  ({9}{G}, {10}{U}, {11}{U}, {12}{U}, {9}{G}{G}...) over ~9 Gates with Baldur's Gate and Planar Nexus. ALL probes are
  first-pass verifies in layer 5 (release 4), e.g. `lay=5:4097:657:657:0:657`. Every record there is refused by
  ReachesByTapping with kind "no face combination covers" (rbt=0/657), and the engine's payment floated NOTHING
  (`float -`).
- Pre-check soundness (run/verify: PRECHECK=1, BOUND_VERIFY=1, BOUND_VERIFY_UNCAPPED=1): compared 744,890, mismatched 49,
  ALL 49 with a capped side, 0 outside capped. capped=50 (unchanged). considers_skipped=106,803, surplus_slots_skipped=0.
  In 49 of 50 the uncapped eager DOES find a payment (e.g. {11}{U}: Cliffgate x2, Planar Nexus, Gond Gate, Sea Gate,
  Baldur's Gate). So the pre-check is sound but does not reduce the capped refines; the loss is ReachesByTapping.
- Levers on (run/leverson: PRECHECK + BUDGET_CAP + RBT_PRICED + RBT_VERIFY, on wall.json plus 30 d3 b0 + 10 d5 b0):
  CAP past_free=66, budget_cut=49, ceiling=0, units=15,479, max_probes=5,573, max_probes_unbudgeted=4,939 (vs a 1<<20
  ceiling), past_node_cap=0, max_nodes=56,061, max_nodes_unbudgeted=20,620. RBT calls=949,445, released_gt4=27,271,
  checks=10.3M, max_checks=720, budget_cut=31,436, ceiling=0. No [rbt-fold-LOST] lines (the fold is exact on this run).
  27 SLOW-GAME lines vs 16 in the verify run (the leverson run includes 40 b0 games). Wall is NOT A/B'd yet.
- Galadriel claude_s10_gi9 replay with all three levers ON (galadriel_on.txt): ok, T matches, ~44 s for the stage
  (08b13f602 reports 2 s with the fixed node cap; a levers-OFF timing of the same command was not taken).
- Units f4: 3776/3777 passed; the one failure was the free-probe test hook, fixed in 7e9e9a280.

## Running / queued (started by F5R-4)
- Background task bdx3i2g0h: `heavy_run bld12.sh f5` (PID 2980343 when written), then `heavy_run stage3.sh`.
  Markers: build.f5.DONE (rc=), stage3.log ("units rc=", STAGE3_DONE), run/rbtdbg/DONE.
  stage3 = units (units.f5.txt), then wall.json with MTG_RBT_DEBUG=1 -> run/rbtdbg/err.txt `[rbt-dbg]` lines: why each
  re-tap combination fails (apply FAIL(reason) vs cover diff: life / float / tapdiff), the first 400 lines.
  If the PID dies with no marker: re-run `bash scripts/heavy_run.sh $S/tools/bld12.sh f5`, then `bash scripts/heavy_run.sh $S/tools/stage3.sh`.

## What remains, in order
1. Read run/rbtdbg/err.txt and find WHY re-tapping all four released Gates fails when nothing floated (every Gate has a
   free face). If it is a real board difference rather than an untappable source, RBT_SUBSET's no-tap test still covers
   it, but confirm it is sound. Fix the subset unit test (above). Remove or keep the MTG_RBT_DEBUG print (diagnostic only).
2. Re-run the verify (BOUND_VERIFY + UNCAPPED) with RBT_SUBSET=1: expect capped ~0. Report the capped refines remaining.
3. Digest screen on all decks: `bash test/regression.sh --both` with each candidate lever set vs GT (VPC no longer needs
   VPC_SKIP: the Galadriel fix 08b13f602 is in base).
4. ONE pooled held-out A/B on every deck that moves: arms innermost via per-job `flags` (heurarm booleans), arm order
   rotated per seed, same_seed_inflight 0, MTG_PGO=0, read with scripts/ab_wall.py; units + wall per deck.
   Arms: base / RBT_SUBSET / BUDGET_CAP / RBT_PRICED (/ PRECHECK) as separate arms, plus the combination.
   Then d8 b0 (`--depth 8 --budget-ms 0`) recoveries of every slower game.
   Also measure BUDGET_CAP against 08b13f602's fixed node cap (coordinator request).
5. Adoption: flip a default ON only if per-deck net <= 0, every slower game recovers at d8 b0, and it is not more
   expensive. If it costs more, give a full quality-vs-time table and do NOT adopt.
6. Scoped GT accept (`--deck=`), `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`.
7. Push wip/maze-f5r3 only; `gh run watch <id> --interval 60` in the background.

## USER rulings (VERBATIM)
- 2026-11-20: "Okay, sure. I have no issue with trying out experiments and adopting or rejecting them based on results."
  (It answered "next step: a sound pre-check for ReachesByTapping first, and the budget-charged cap in any case".)
- F5R-2's: "For 4 it seems like we need to have something like the refine, but make it cheaper? I'm not entirely sure,
  but we are looking for better results overall." MTG_PAY_REFINE itself is a USER adoption.
- On a trade: "We need a full quality vs time comparison with this, like with every other trade."
- CLAUDE.md: "everything should be findable at unlimited or large budgets. Adding more budget should fill in more of
  these lines proportionally." The >4 rule, the 256 guard, the 4096 probe cap and 08b13f602's node cap are all fixed
  caps read at every budget. The node cap (08b13f602) is ANOTHER SESSION'S landed fix: do NOT change it. PROPOSE the
  priced version (lever 1, which already covers it) and measure it against 08b13f602.

## Coordinator rules carried
- Every heavy command goes through `bash scripts/heavy_run.sh`, ONE heavy stage per call. MTG_BUILD_JOBS=12 on every build
  (tools/bld12.sh). `export MTG_RSS_CAP_GB=20`. A parent priority gate may SIGSTOP queued flock waiters; that is
  expected, so do not touch it. Push wip/* only, never the shared branch. No timeouts. Do not kill runs you did not
  start. Hand off at ~250K.
- The 9p "Cannot allocate memory" build failure is a flake: retry it.
