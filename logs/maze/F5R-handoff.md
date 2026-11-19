# F5R handoff (F5R -> next agent)

Task (coordinator e865fc0d): make the pay-refine inside the owed-activations joint payment
(`TurnSolver::OwedActsJointPrepay` -> `LineJointPrepayCore` -> `TapForCostDirect` -> `PayRefineAfter` ->
`manaoracle::RefinePayment` -> `RefinePaymentImpl` / `PayEnum::Dfs/Verify`) much cheaper while keeping its results.
Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f5r`, branch `wip/maze-f5r` (branched at 7a59f73dc).
Scratch `/workspaces/MagicDeckTester/build/scratch/maze/f5r/`. Patches + tools are copied into `logs/maze/f5r/`.

## USER ruling (VERBATIM)
"For 4 it seems like we need to have something like the refine, but make it cheaper? I'm not entirely sure, but we
are looking for better results overall."
(MTG_PAY_REFINE is a USER adoption; no skip lever. Exact speedups preferred; an approximate one needs a held-out A/B,
per-deck net <= 0, every slower game recovering at d8 b0, and the decision reported to the USER.)

## Commits
- None with code yet. The worktree is deliberately CLEAN at 7a59f73dc (+ this handoff commit) because the queued
  base build (below) must build the unmodified tree. The change lives in patch files until then.

## Profile findings (binary mtg.f5g = same play as 7a59f73dc; d3 b10 seed 192014 gi13, one thread, unloaded box)
- Game 13.4 s wall here (the 42 s in F5c was a contended cold run). Refine = 13.3% of samples; ~93% of refine time
  is the joint's whole-line bills.
- `MTG_PAY_REFINE_STATS`: triggered 5,545, memo hits 2,946, misses 2,599, refined (found) 164, 1,942 ms.
- `MTG_PAY_REFINE_TRACE` (`trc13/err.txt`): 2,599 misses = 497k verify probes; **84 refines hit the 4,096-probe cap
  and hold 344k (69%) of the probes**, ~10 ms each. They are whole-line bills like `{7}{G}` / `{6}{G}` / `{7}` over
  6-7 Gates incl. Baldur's Gate (`{2},{T}`: X of one colour, a FED face) and Talon Gates (`{1},{T}`: any, fed).
- Mechanism: in a capped refine **~3,573 of 4,097 verifies are `PayEnum::ExtendFed` probes** (a paying set + one
  more fed converter tap, x5 colour faces, since every colour is "useful" in this deck) -- supersets whose release
  can never beat the paying set's. The cap then cuts the first-pass enumeration. The SAME tap set + bill in a
  different battlefield order gives found=0 vs found=1 rel=2 (e.g. `T=7 {7}{G}`: 3 orders found=0, 1 order found
  rel=2) -- the cap, not the board, decides; quality is being lost too.
- Per probe (PayAndRecord): PerformTaps/ApplyEnginePreTap ~49% (HumanPreTapOfferFor strings, ControlledSubtypeColors
  for Gond Gate), MidTurnStateKeyOfPayment (the result key) ~28%, the float-only payment ~16%, board copy ~6%;
  ReachesByTapping ~14% of the refine.
- Memo: the 2,599 misses are only **87 distinct refine signatures** (~43k probes). They miss because battlefield
  ORDER differs (order changes enumeration order -> cap/tie answer, so an order-free key is NOT exact) and because of
  non-payment state (hand etc.; the payer DOES read the hand, e.g. RestrictedIneligOwedLater) -- narrowing the memo
  key is unsound; not pursued.
- NOTE (identity): every TapForCostShared bumps `workctr::kPay` -> `t_priced`, which the default-ON priced decision
  ceiling (MTG_DECISION_PRICED_X=100) reads. ANY reduction in refine probes can move play where that ceiling trips.
  For a payment-choice identity check run both arms with `MTG_DECISION_PRICED_X=0`; with default flags expect digest
  moves only where a ceiling trips or a refine was capped.

## The fix (bound1.patch, ManaOracle.cpp only; extra2.patch = HeuristicArm.h doc + unit test)
"Release bound", inside the existing lazy-surplus gate (`MTG_PAY_REFINE_LAZY` on && every face regular):
1. `PayEnum`: a paying set's fed extensions are parked (`defer_fed`, like the surplus `defer`) instead of verified
   eagerly; `Hopeless(n)` = `rel_taps - (|cur|+n) < *rel_best` prunes Dfs nodes, ExtendFed and ExtendSurplusNow
   steps that cannot reach the best release accepted.
2. `PayAndRecord` reports each recorded result (`on_record`: index, new?, repeat option) and keeps `rep` (result key
   -> index). `RefinePaymentImpl` ranks results ONLINE with the unchanged `consider`, so the bound tightens during the
   Dfs. Order tuples: first pass (i,2,0); fed extension of the set parked at pos: (pos,0,j); surplus (pos,1,j) -- the
   eager order. A fed extension re-making a result the first pass recorded LATER takes over that record and place
   (eager recorded it first). Fed slots are expanded before surplus slots (as eager had all fed in the first pass).
3. So: identical choice to the old enumeration whenever the old one finishes under the cap; where it was capped, the
   new one reaches further (same precedent as the lazy surplus, see the PAY_REFINE_LAZY slot doc and
   docs/design/mana-completeness-oracle.md 13.1). `MTG_PAY_REFINE_LAZY=0` still gives the eager enumeration.
4. Diagnostic `MTG_PAY_REFINE_BOUND_VERIFY=1` (RefineImplChecked wrapper): every refine recomputed with eager fed / no
   bound (the old path, via `t_refine_eager_fed`), mismatches printed `[pay-refine-bound] MISMATCH (capped)` and
   summarised `=== PAY REFINE BOUND VERIFY: compared= mismatched= (with a capped side N) capped= ===`. Expect
   mismatches ONLY with a capped side. (Recompute pays -> moves t_priced; use only for counting.)
5. extra2.patch: unit test "pay refine: the release bound finds the same released Gates in every battlefield order
   (Maze's End F5R)" appended to test/unit/test_mana_oracle.cpp (7 rotations of Planar Nexus / Gond / Boros /
   Baldur's / Talon / Sea(G) / Citadel(B) Gates, {7}{G}; CHECK bound release >= eager and same in every order). It is
   a PROBE: untested -- check its MESSAGE output and that the engine's payment does overtap; adjust if not.
Both patches compile (`-fsyntax-only` on ManaOracle.cpp); not yet built, run or unit-tested.

## Queued build (ONE job, leave it running)
- heavy_run PID **1290372** (parent bash 1290371) runs `build/scratch/maze/f5r/tools/bld.sh base` under
  build/heavy.lock. bld.sh (read when the lock is acquired) builds in sequence in the f5r worktree:
  1. **base** (clean 7a59f73dc) -> `$S/mtg.base`, `$S/mtg-test.base`, log `$S/build.base.log`, marker `$S/build.base.DONE`
  2. **diag** (`git apply diag.patch`, build, `git apply -R`) -> `$S/mtg.diag`, marker `build.diag.DONE`
  3. **bound1** (`git apply bound1.patch`, build, revert) -> `$S/mtg.bound1`, `$S/mtg-test.bound1`, marker `build.bound1.DONE`
  ($S = /workspaces/MagicDeckTester/build/scratch/maze/f5r.) **Do not edit the worktree's src until
  build.bound1.DONE exists** (it applies/reverts patches on the live tree). If the PID vanished without markers,
  relaunch `scripts/heavy_run.sh $S/tools/bld.sh base`.
- At handoff (~06:20 UTC) it had not yet acquired the lock (~19 jobs queued).

## Measurement plan
1. **diag** (`mtg.diag`, base + scratch-only diagnostics, never commit): `tools/one.sh $S/mtg.diag $S/diag13 192014 13 3 10
   MTG_F5R_DIAG=1` (single thread, light). Prints `=== F5R DIAG narrow_hits= board_diff= payer_diff= hand= lib= turn=
   spells= gy= payer= ===` (which key part makes same-input refines miss the memo), `[f5r-table]` (the source/face
   table of every refine with >=1000 probes), and `[f5r-cap] ... capped: found rel probes | full: found rel probes ms
   same=` (each capped refine re-run with cap 1e8: does the cap lose answers, and what the full enumeration costs).
   Use it to confirm the fed-extension mechanism and how many capped refines are found=0 even uncapped (those the
   bound does NOT make cheaper -- if many, the next lever is an exact "nothing can be released" pre-check).
2. **bound1 single repros, cold, light** (one thread each): `tools/one.sh $S/mtg.<base|bound1> $S/r_<tag>_13 192014 13
   3 10 MTG_PAY_REFINE_STATS=1 MTG_PAY_REFINE_TRACE=1` and the same for `192041 40 3 10`. Compare refine ms, probes
   per capped refine, wall (`/usr/bin/time` line in err.txt), and the win turn.
3. **Bound verify**: bound1 with `MTG_PAY_REFINE_BOUND_VERIFY=1` on the wall manifest (`$S/wall/m.json`, 430 games:
   d5 b20 191001 x200, d3 b10 192001 x150, 2HG d5 b20 194001 x80) and on the smoke tier
   (`MTG_BIN=$S/mtg.bound1 MTG_PAY_REFINE_BOUND_VERIFY=1 bash test/regression.sh --smoke` in maze-f5r, under
   heavy_run). Pass = every mismatch has a capped side.
4. **Identity**: smoke tier with `MTG_BIN=$S/mtg.base` and with `MTG_BIN=$S/mtg.bound1` (default flags), copy
   `test/results/smoke.env` aside after each, diff per key. Any moved key: re-run that case with
   `MTG_DECISION_PRICED_X=0` on both arms; still moved => a capped refine changed its answer (play change) -> part (b).
   Units: `$S/mtg-test.bound1` (all) and `-tc "*F5R*"` for the new test.
5. **Per-arm wall** (SEPARATE cold batches, never pooled arms): `scripts/heavy_run.sh $S/tools/wall.sh base bound1`
   -> `$S/wall/<tag>.{out,err,DONE}` (err has `/usr/bin/time` wall + PAY REFINE stats). Compare ms/game per shape.
6. If any Maze's End (or other) digest moved: held-out A/B (e.g. the abp shapes in
   build/scratch/maze/f5/abp, base vs bound1 as separate binaries or an env-pinned arm with MTG_PAY_REFINE_LAZY is
   NOT equivalent -- it turns off the lazy surplus too; prefer two binaries), per-deck net <= 0, d8 b0 on every slower
   game, report to the USER as a play change of a USER-adopted mechanism.
7. Then commit bound1 + extra2 on wip/maze-f5r (rebase, never merge; push wip/maze-f5r only), report: mechanism,
   fix, identity/A-B, per-arm wall before/after, hashes.

## Coordination / rules
F5-5 runs in maze-f5p7 (wip/maze-f5p7) -- do not touch. Keep the change in ManaOracle.cpp (+ HeuristicArm.h doc,
test_mana_oracle.cpp). Box: one heavy job at a time via scripts/heavy_run.sh, MTG_BUILD_JOBS=12, --threads $(nproc),
MTG_RSS_CAP_GB=20, no timeouts, waiters on PID / DONE markers. Handoff again at ~250K.
