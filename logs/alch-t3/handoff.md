# alch-t3 handoff (2026-11-19, ~21:40 UTC) -- Accomplished Alchemist T3 line, heuristic + search halves

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/alch-t3`, branch `alch-t3-1119`, rebased onto
`origin/phase-1-2-deck-analyzer` (fb9cb5e03). Backup pushed: `backup/2026-11-19/alch-t3`. Plan + census +
design: `docs/design/aa-staff-in-hand-loop-cast-draw.md` (read the whole doc, incl. the new "Census" and
"The search half" sections at the end).

## Done (commits)
- 25f7a66da  heuristic half (kLoopPayCastDraw, rule row FIN-STAFF-HAND-DIG, MTG_AA_LOOP_CAST_DRAW). BUILT
  (binary copied to `build/scratch/alch-t3/bin_h1/{mtg,mtg-test}`), NOT yet tested except one probe below.
- 256188898  plan doc.
- 3b17a2a16  search half WIP (UNBUILT, UNTESTED): MTG_BP_UNTAP_FLOAT + heurarm routing + unit tests + doc.

## Census (bin_h1 binary, post-sac hand-off `--choices 1,0,0,-1,1,1,0,-1,136,0,2,0 --choices-then-auto`,
`--depth 8 --budget-ms 0 --ignore-play-profile`, `MTG_FS_ROOT_DUMP=3 MTG_SITE9_TRACE=3`)
- Heuristic OFF (`MTG_AA_LOOP_CAST_DRAW=0`): win T4. Root scans only `{Staff of Domination}` (site-9
  variants exist but the continuation list is EMPTY: Alchemist tapped, float 1), `{Crab Umbra(x1)}` with NO
  site-9 variant, and the T4 line. Heuristic ON: win T3 from the same hand-off (the heuristic half works there).
- Count axis: generic UntapActivationCounts offers K=1 only (max_affordable = 4 div 3; refund ignored). Not
  decisive: any K ends the main with the float unspendable.
- DECISIVE CUT = site-9 delta rule: casts apply before trailing activations, so an untap's refund is spendable by
  nothing in the plan; after Umbra x1 the Alchemist is untapped again, kActLifeYield / kActUntapper unchanged,
  kActUntapLoopFunded needs a provider loop NEED -> PostEntryActivationPending false -> no continuation.
- Outputs: `build/scratch/alch-t3/census/` (old_*.err = primary-checkout binary, h1_cd{0,1}.* = bin_h1).

## Search-half design (edited in 3b17a2a16)
- `kActUntapFloat = 221` (TurnSolver.cpp enum + CollectActivationKeys after the kActUntapLoopFunded block): float
  > 0 beside an own live mana creature (UntapTargetIsLiveManaCreature) that an own activated untapper can aim at
  (CollectCreatureUntapAbilities non-empty), value = float total in bits 40+. Reader `BpUntapFloatOn()` (next to
  BpSpeakerGateOn), heurarm slot BP_UNTAP_FLOAT, default ON.
- PlanOpensBreakpoint: marks site 9 for any plan holding an ActivateUntap aimed at an own mana creature (after
  the Raggadragga clause).
- Budget staging: re-open sequences are the wave walker's compound slots (MTG_BP_DEV_SEQ, wave = deviation count,
  priced) -- nothing gated. Count axis left unchanged (K=n reachable as n K=1 re-opens); if the acceptance test
  fails because the deviation chain is too long, consider a refund-aware count axis admitted in a later wave.
- `LoopCastDrawOn()` (SpellEffects.h) now reads through heurarm slot AA_LOOP_CAST_DRAW (both slots appended to
  HeuristicArm.h enum + kNames).
- Unit tests appended to test/unit/test_accomplished_alchemist.cpp: "Untap refund float: ..." (key level, and
  both-worlds "untap once, continuation casts Kalonian Hydra"; OFF arm must not). May need adjusting once built.

## Queued jobs (both waiting on BOX.lock behind the other agent's adopt-1119 build, PID 1348520)
- PID 1353897 (shell 1353896): `flock BOX.lock bash build/scratch/alch-t3/step1.sh build/scratch/alch-t3/step1`
  -- step 1 on bin_h1 (units, combo_off all 52, aa_* scenarios, repro seed 12345681 gi0 keep 7 at play settings,
  post-sac hand-off). Output/DONE marker: `build/scratch/alch-t3/step1/summary.txt` (last line `DONE`).
- PID 1353958: `flock BOX.lock env -C <worktree> ./build.sh` -- builds 3b17a2a16. Log
  `build/scratch/alch-t3/build2.log`; done when it prints `>>> done` (compile errors possible: unbuilt code).
  (All build/scratch paths are under /workspaces/MagicDeckTester/.)

## Remaining, in order
1. Read step1 summary; fix any heuristic-half failure (expect T3 on the repro and the scenario fixture).
2. When build2 is done: `flock BOX.lock bash build/scratch/alch-t3/chain2.sh` (units incl. new tests, combo_off,
   aa scenarios, ACCEPTANCE: heuristic OFF full game seed 12345681 gi0 keep 7 at d8 b0 must win T3; plus flags-off
   and heuristic-on arms, play-setting repros). Output `build/scratch/alch-t3/chain2/summary.txt`, DONE marker
   `chain2/DONE`. Fix failures.
3. `flock BOX.lock bash build/scratch/alch-t3/chain3.sh`: held-out 4-arm A/B (ab.sh: seeds 61001/63001 x1000,
   d5 b20, arms base/heur/search/both, MTG_DUMP_WINS per-game in ab1/batch.stderr) then smoke+regression
   `--both` (tiers.sh -> build/scratch/alch-t3/tiers/both.txt). DONE marker `chain3/DONE`.
4. Per-game audit; root-cause every slower searched game (must recover at --depth 8 --budget-ms 0); every deck
   aggregate net <= 0. Only the Alchemist deck holds untappers, so every other deck must be byte-identical.
   Accept smoke/regression GT only when explained (`env -C <wt> bash test/regression.sh --both --accept`), then
   `python3 test/check_gt_logs.py`. Never the overnight tier.
5. Commit in logical steps; update the doc with results; push only `backup/2026-11-19/alch-t3`.

## USER rulings (verbatim, from the brief)
- "Once the Boars adoption is done we should return to the Alchemist issue. I would like to know why we can't find it at any budget. That seems like something we will need to fix."
- "As-in, we should improve the heuristic and the search itself."
- "As-in the heuristic is our answer to this working at a lower budget, but the search should be capable at a high budget. It sounds like neither are the case right now?" (Answer: correct, neither works today.)
- Standing: "everything should be findable at unlimited or large budgets. Adding more budget should fill in more of these lines proportionally." Waves (budget-staged admission) are the sanctioned shape; a gate that refuses a line at every budget is not. "Only heuristics in the provider are allowed to interfere with the search and there just to prune options."
- Clairvoyant wins are not regressions (a line that needs known draws gets no fix).
- OPEN USER question, do NOT change it: `BurstPendingActColour` (src/core/ManaPayment.cpp) pays the activation with white where blue may be right. Leave it as is; if it blocks the acceptance test, report that with evidence instead.

## Box/process rules carried from the brief
ONE heavy job at a time: wrap every build/tier/batch/A-B in `flock /workspaces/MagicDeckTester/build/scratch/BOX.lock`.
Build only via ./build.sh; no `timeout`, no `cd X && ...`, no `pkill -f`, no `while pgrep -f`; never rebase while a
build in this tree runs or is queued; never touch references/; subagents do not push the shared branch.

## UPDATE: step1 finished (bin_h1, heuristic half only) -- build/scratch/alch-t3/step1/summary.txt
- combo_off: 51/51 pass, aa_co_17 offered=1 verified=1 (rule LIFE-GAINED+LOOP-UMBRA+FIN-STAFF-HAND-DIG).
- aa_* scenarios: all rc=0; aa_staff_in_hand_umbra_loop_seed12345681_t3 = T3.
- Post-sac hand-off at play settings: T3.
- **Full game, seed 12345681 gi0 keep 7 at play settings: still avg 4.0000 (T4), NOT T3.** The heuristic fixes the
  post-sac board but the search does not reach that board on T3 by itself (Living Wish -> Boar, Boar, Umbra, sac
  ordering earlier in the turn). Next step: hand off at earlier T3 frames (scripts/ref_handoff.py --turn 3 --frame N)
  to find which decision is missed.
- **Units: rc=139 (SEGFAULT), 96 failed / 3173 passed.** Possibly an artefact of running the COPIED mtg-test from
  build/scratch (path-relative resources?), possibly real -- re-run `build/Release/mtg-test` in the worktree
  (after build2) and compare with origin's failure set before trusting either way. Log: step1/units.txt.

## UPDATE: build2 finished rc=0 (search half 3b17a2a16 compiles; build/Release/{mtg,mtg-test} in the worktree).
No queued or running alch-t3 jobs remain. Next: `flock /workspaces/MagicDeckTester/build/scratch/BOX.lock bash
/workspaces/MagicDeckTester/build/scratch/alch-t3/chain2.sh` (step 2 of "Remaining").
