# F5R-3 handoff (pay-refine: sound ReachesByTapping pre-check + budget-charged cap, Maze's End)

Worktree `.claude/worktrees/maze-f5r3`, branch `wip/maze-f5r3` (based on wip/maze-shared 9007948bc, because the Maze
batch had not landed on origin/phase-1-2-deck-analyzer yet; rebase onto it once `git log
origin/phase-1-2-deck-analyzer --oneline | grep -m1 F5R` finds it). Scratch `build/scratch/maze/f5r3/`.
Predecessor: `logs/maze/F5R2-handoff.md` (in maze-shared / maze-f5r).

## USER ruling (2026-11-20, VERBATIM)
"Okay, sure. I have no issue with trying out experiments and adopting or rejecting them based on results."
(It answered: "next step: a sound pre-check for ReachesByTapping first, and the budget-charged cap in any case".)
Earlier, F5R-2's: "For 4 it seems like we need to have something like the refine, but make it cheaper? I'm not entirely
sure, but we are looking for better results overall." MTG_PAY_REFINE itself is a USER adoption.

## Commit
- WIP commit on wip/maze-f5r3 (this file + the src changes below). NOT built yet, NOT measured. Every new lever is
  default OFF, so the tree should be play-identical to 9007948bc (verify on smoke digests once built).

## What is in the tree (files / places)
1. TRACE (diagnostic, MTG_PAY_REFINE_TRACE only), `src/ai/ManaOracle.cpp`:
   - `t_trc_rbt[2]` (after `t_trc_site`): ReachesByTapping refusals by kind: [0] more than 4 sources released
     (`released.size() > 4`), [1] no face combination covers.
   - the `[refine-trc]` line now also prints `rbt=a/b probes=N capped=0/1 lay=L:probes:records:rej6:rbtbig:rbtnone,...`
     (one entry per release layer that spent probes; RefinePaymentImpl's layer loop).
2. BUDGET-CHARGED CAP, lever `MTG_PAY_REFINE_BUDGET_CAP` (heurarm slot PAY_REFINE_BUDGET_CAP, before SUMMIT_TWIN_FOLD in
   `src/ai/HeuristicArm.h` enum + kNames; reader `PayRefineBudgetCapOn()` in `src/ai/EngineFlags.h`, default OFF):
   - `PayEnum::ProbeOut()` replaces the three `if (++probes > cap) { capped = true; ... }` sites in Verify (base pay, the
     fed-order permutations, the leftover split). The first `cap` (4,096) probes stay FREE (identity for every refine that
     finished under the old cap). Past them, with `budget_cap`: stop at `ceiling` (MTG_PAY_REFINE_CEILING, default 1<<20,
     min 4096); stop if `gamework::Poll()`; stop if `TurnSolver::DecisionBudget()` is non-null and `Exhausted()||Overrun()`
     (sets `budget_cut`); else charge 1 unit via the new `TurnSolver::ChargePayRefineUnits(b, 1)` (null budget = charges
     nothing; CacheChargeNoteNull like ChargeAaRouteUnits). DecisionBudget() is nullptr at an unlimited root AND at the
     executor's committed apply, so both run uncapped up to the ceiling.
   - new unit site `unitsite::kPayRefine` ("pay_refine", appended last) + census column `u_pay_refine`
     (`src/ai/TurnSolver.cpp`; declaration in `src/ai/TurnSolver.h` next to ChargeAaRouteUnits).
   - RefinePaymentImpl sets `pe.budget_cap`/`pe.ceiling` (not on the BOUND_VERIFY eager recompute); a `CapCensus` RAII
     sets `t_refine_budget_cut` and counts stats; RefineImplChecked preserves the first run's cut flag; RefinePayment
     does NOT memoise a budget-cut refine (answer depends on the budget, not the boards).
   - MTG_PAY_REFINE_STATS prints `=== PAY REFINE CAP: past_free= budget_cut= ceiling= units= max_probes=
     max_probes_unbudgeted= ===` -> the "max observed at b0" number the brief asks for.
   - ManaOracle.cpp passes `-fsyntax-only`; TurnSolver.cpp / tests not compiled yet.
3. NOT YET WRITTEN: the pre-check (goal 1), unit tests (budget cap: null budget finishes past 4096 / tiny budget cuts and
   is not memoised; pre-check exact), flipping defaults ON.

## Pre-check design (analysis so far; confirm with the trace before coding)
- Under `bound` (all faces regular) a layer-L candidate releases exactly T-L sources, and ReachesByTapping refuses
  outright when more than 4 are released (`released.size() > 4`) unless no_trade (CoversBoards direct). So with
  !no_trade every candidate of a layer with T-L > 4 is refused: sound to skip `consider` there, and sound to skip
  SurplusStep verifies in such layers (pure candidates, no continuation).
- NOT sound to skip first-pass verifies in those layers (vmemo: later layers must not grow a paying set, to stay exact
  vs eager) or FedStep links (a paying link gates the next fed link). So the probe saving of (1) is only the surplus
  supersets; the trace's `lay=` field shows whether the capped refines' probes sit there, in first-pass sets, or in
  fed chains. If they sit in first-pass face variants (many face multisets of one tap set giving one result), the
  next lever is a refine-side pre-tap memo (PayEnum::memo_on exists, MinimalPayments-only today; exact by its own
  MTG_ORACLE_TAP_MEMO_VERIFY) -- check that it cannot skip a record the eager order needs (the `at` repeat logic).
- Hypothesis to check: Baldur's Gate's fed {2},{T} makes X = other Gates of one colour, so small sets pay {11}{U} and
  sit at release >= 5 (all refused by the >4 rule). Separate observation for the USER, not in scope: the >4 rule and
  the 256-combination guard in ReachesByTapping are themselves constant caps read at every budget.
- Prove with MTG_PAY_REFINE_BOUND_VERIFY=1 MTG_PAY_REFINE_BOUND_VERIFY_UNCAPPED=1 on build/scratch/maze/f5r3/wall.json
  (430 games, F5R-2's wall manifest repointed to this worktree): 0 mismatches outside capped refines; count capped.

## Queued job (leave it; the parent paused its heavy-lock waiter so the Maze batch lands first)
- `bash scripts/heavy_run.sh build/scratch/maze/f5r3/tools/chain1.sh`, heavy_run PID 2155977 (harness task bnccc72s5).
- chain1.sh = tools/bld.sh trc (builds THIS worktree's source as it is when it starts -> `mtg.trc`, `mtg-test.trc`,
  log `build.trc.log`, marker `build.trc.DONE` rc=) then tools/trc.sh trc (wall.json at nproc threads with
  MTG_PAY_REFINE_TRACE=1 MTG_PAY_REFINE_STATS=1; refine lines with probes >= 1000 -> `trc/trc/big.txt`, summary ->
  `trc/trc/summary.txt` "refines N capped C capped_found0 F maxprobes M", other stderr -> `trc/trc/err.txt`) then
  `chain1.DONE`. Do not edit src in a way that breaks compile before it starts building.
- My waiters (bhpi9hlld on build.trc.log, monitor b3b5g4cgn) die with this agent; re-arm on `chain1.DONE`.

## What remains, in order
1. Read trc/trc/big.txt `lay=` / `rbt=`: where the 60 capped refines' probes go. Design + code the sound pre-check
   (lever MTG_PAY_REFINE_PRECHECK, heurarm slot, default OFF first); unit test; prove 0 verify mismatches vs uncapped eager;
   report capped refines remaining.
2. Unit tests for the budget cap. Run wall.json with MTG_PAY_REFINE_BUDGET_CAP=1 at budget 0 (and the shipped
   budgets) under STATS: report max_probes_unbudgeted vs the 1<<20 ceiling (ceiling hits must be 0).
3. Screen all decks' digests: smoke+regression (`bash test/regression.sh --both`) with both levers on vs GT. Held-out
   A/B on every deck that moves: ONE pooled `mtg --batch`, arms innermost via per-job `flags` (booleans), arm order
   rotated per seed, same_seed_inflight 0, MTG_PGO=0, read with scripts/ab_wall.py; units + wall per deck. Bar:
   per-deck net <= 0, every slower game recovers at --depth 8 --budget-ms 0. Better AND cheaper = adopt (flip defaults
   ON); anything costlier = quality-vs-time table for the USER, no agent adoption.
4. Scoped smoke+regression GT accept (`--deck=`), `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`.
5. Push wip/maze-f5r3 only (never the shared branch); `gh run watch <id> --interval 60` in the background.
Rules: every heavy command via `bash scripts/heavy_run.sh` (not executable here: call it with bash);
`export MTG_RSS_CAP_GB=20`; no timeouts; hand off at ~250K.
