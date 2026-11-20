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

## UPDATE (agent 2, ~21:45 UTC): FULL-GAME ROOT CAUSE FOUND -- the Crab Umbra HOST is never searched
Bisect (bin_h1 = heuristic half, play settings, `--claude-play --seed 12345681 --game-index 0 --max-turns 10
--choices <prefix> --choices-then-auto`; prefix of `1,0,0,-1,1,1,0,-1,136,0,2,0`; outputs build/scratch/alch-t3/bisect/):
- n=4 (T2 start) T4; n=8 (T3 start) T4; n=10 (after "land=Forest; Living Wish -> Brindle Boar", Boar fetched) T4;
  n=11 (after the human's "Brindle Boar, Crab Umbra -> Accomplished Alchemist") **T3**; n=12 T3.
- n=10 stays T4 at b200, b2000 AND `--depth 8 --budget-ms 0 --ignore-play-profile` (heuristic ON) -> a completeness hole.
- `MTG_PLAN_DUMP=3 MTG_PLAN_DUMP_MAX=300 ... --budget-ms 2000` (the dump needs a FINITE budget) at n=10: 35 plans,
  only 5 distinct action lists; `MTG_FS_ROOT_DUMP=3` shows the Umbra only as `Crab Umbra->#200010` (the Boar, beside
  the Boar cast) or `Crab Umbra->#4` (Arbor Elf). **No plan puts the Umbra on the Alchemist (#1).**
- WHY: the AURA CAST HOST RANKING (USER 2026-10-06, SpellEffects.h ~58390; TurnSolver.cpp ~19125) keeps ONE host per
  plan class: the autonomous signature keys a creature Aura by NAME (`cre_host_sig` off), the dedup keeps the best
  AuraHostKey (combat damage; tie-break "fewer Auras on creature MANA sources" = AGAINST the Alchemist), and
  MTG_AURA_HOST_LATE_BIND (TurnSolver.cpp ~83497, BuildAuraLateBind / BindAuraLateHosts fast path "first legal host
  that is not a ManaDork") binds the same pick earlier. `CreatureAuraHostInert` (~61349) says of Crab Umbra "its host
  matters only to the host key" -- FALSE: `{2}{U}: Untap enchanted creature` re-uses the HOST's {T}; on the Alchemist it
  is the whole combo. Same defect class as Curiosity (Niv) and Guilty Conscience, which were exempted
  (aura_damage_trigger_draw / aura_host_damage_back keyed `#A<host>` in the signature).
- So the heuristic half + search half (kActUntapFloat) only help once the Umbra is already on the Alchemist (n=11).

## THE FIX (designed, NOT yet written -- next agent implements)
Flag `MTG_AURA_UNTAP_HOST_SEARCH` (DEFAULT ON; heurarm slot AURA_UNTAP_HOST_SEARCH appended to HeuristicArm.h enum +
kNames). An Aura with `params.untap_enchanted_cost` (Crab Umbra) has a host-dependent effect for every host with a {T}
use; hosts without one are goldfish-inert for the untap (no blockers) and stay one class under the USER's ranking.
1. Helper (SpellEffects.h, near AuraHostIsManaCreature): `AuraUntapsHost(def)` = creature aura with
   untap_enchanted_cost; `UntapHostHasTapUse(const CardParams&)` = `!produces.empty() || tap_damage_cost ||
   tap_draw_cost || tap_lifegain_cost || tap_investigate_cost || self_ping_cost` (the untap enumerator's own test,
   TurnSolver.cpp ~49283; reuse it there too). Host def by number: own battlefield creature, else hand card.
2. BuildAuraLateBind (~83552): for an untap-aura group (flag on) partition gp into KEEP (hosts with a tap use: own
   search options) and BIND (the rest); late-bind only BIND when it has >= 2 members (product over BIND sizes), rep
   chosen among BIND (first on battlefield), lb.hosts = BIND only; new gp = KEEP members + rep, in original order.
3. Signatures: add `|| (flag && AuraUntapsHost(d))` to the `#A<host>` condition in PlanSignatureFast (~83218) AND
   the legacy builder (~88847, lockstep). Then each tap-use host is its own dedup class; the BIND rep is one class.
4. Leave CollapseGreedyCreatureAuraHosts / RetargetSolveAuraHosts (greedy Solve = permitted beyond the horizon) and
   CreatureAuraHostInert as they are (say so in the comment).
5. Unit test: GenericProvider board with Alchemist (X>=4) + Arbor Elf + a vanilla creature, Crab Umbra in hand:
   EnumerateMainPlans holds Umbra->Alchemist AND Umbra->Arbor as distinct plans with the flag on; =0 -> one host.
6. Then the acceptance (chain2.sh: MTG_AA_LOOP_CAST_DRAW=0 full game d8 b0 must win T3) and chain3 (A/B: add an arm
   with MTG_AURA_UNTAP_HOST_SEARCH=0 to ab.sh -- it is a budgeted play change for the Alchemist deck and any deck
   holding an Aura with untap_enchanted_cost: grep cards.json; only Crab Umbra today).
SURFACE TO USER: this overrides the USER's host ranking for one card class (the ranking stays for every host without
a tap use). Precedent: Curiosity / Guilty Conscience were exempted the same way.

## Units on the worktree's own build (3b17a2a16): see build/scratch/alch-t3/units/s1.txt (bin copied to
build/scratch/alch-t3/bin_s1/). The step-1 run (96 failed + SIGSEGV) used the COPIED bin_h1 mtg-test -- compare.
- RESULT: 3719 cases, **1 failed** (the step-1 "96 failed + SIGSEGV" was the COPIED bin_h1 mtg-test run against
  newer test sources/data -- an artefact). New "Untap refund float" tests PASS.
- The 1 failure is REAL and caused by the search half: `Crab Umbra: the search offers the untap with a searched
  count; executor == rollout` (test_accomplished_alchemist.cpp:910) `ro.floating_mana.Total() == 4` got **202**. The
  test takes the LAST chosen_x==2 plan, which is now a site-9 continuation variant (PlanOpensBreakpoint marks any
  ActivateUntap on a mana creature). Its continuation keeps re-opening on kActUntapFloat -- each refund is a NEW float
  value -- with NOTHING in hand to spend it on (~100 re-opens, float 202). Two actions:
  (a) the test should pick `p.bp_choice < 0` (it asserts the base plan's executor==rollout);
  (b) MORE IMPORTANT, a work/termination concern: kActUntapFloat should arm only when the float can buy something
      it could not before (e.g. a hand card / activation whose cost exceeds the plan-start pool and is <= the new
      float+pool), or the re-open chain on a self-funding loop is pure waste (and at b0 potentially huge). Fix this
      in CollectActivationKeys (key value = the cheapest newly affordable thing, not the raw float) before the A/B.

## UPDATE (agent 3, 2026-11-20 ~07:00 UTC): both fixes BUILT + COMMITTED 163e81f56; full game still T4 -- a THIRD hole
Done:
- 163e81f56: (1) kActUntapFloat value = min(float, UntapFloatSpendCap) (dearest hand spell / mana-costed ability of
  ours; X spells at X=0; untap abilities not counted); no key when cap = 0. (2) MTG_AURA_UNTAP_HOST_SEARCH (default ON,
  heurarm AURA_UNTAP_HOST_SEARCH): UntapAuraKeepHosts (own creatures + hand creatures with UntapHostHasTapUse), #A key in
  PlanSignatureFast + legacy (lockstep, `untap_keep` pointer param), BuildAuraLateBind KEEP/BIND partition. Unit fix at
  :910 (bp_choice < 0) + 2 new cases; the old late-bind equivalence case pins the new lever OFF.
- chain2b (build/scratch/alch-t3/chain2b/summary.txt): units 3721/3721 PASS; combo_off 51/51; aa_* scenarios unchanged.
- Probes at d8 b0 (heuristic OFF unless noted): hand-off n=10 (after the human's "Forest; Living Wish -> Boar") **T3**
  (was T4 at every budget before the host fix); n=12 T3; play setting n=10 T3. FULL GAME seed 12345681 keep 7: **T4** in
  every arm (search, nohost, nofloat, both; d8 b0 and d5 b20). Bisect (bisect2/, d8 b0 heur OFF): n<=8 T4, n=10 T3.
So the remaining hole is the T3 decision ITSELF (Forest + Living Wish, nothing else). Two candidate cuts, both found by
reading traces (bisect2/rd8.err = MTG_FS_ROOT_DUMP=3 MTG_SITE9_TRACE=3 at n=8):
 (a) ROUTE "root plan Crab Umbra->Alchemist + Living Wish": site 9 opens (the Umbra is a new untapper), the continuation
     casts the Boar -- and the PAYER taps the ALCHEMIST for it (re-open state: Arbor Elf(T), Forest#22(T), Alchemist(T),
     float 1) although Forest#22 + Arbor Elf untapping Forest#20 (Utopia Sprawl's host, 2 mana) pay {2}{G} exactly. With
     the Alchemist tapped the post-sac Umbra untap ({2}{U}) is unpayable from float 1: line dead. The human's line kept the
     Alchemist untapped (Wish = Forest#20 + Sprawl; Boar + Umbra = Forest#21 + Forest#22 + Arbor->Forest#20). This is the
     mana-payment exemption area (src/ai/ManaPayment.cpp TapForCostSharedOnceImpl / ManaSourceRank); a candidate rule:
     a creature an own untap Aura is attached to (or a live loop host) is tapped LAST. Not done; a play change.
 (b) ROUTE "root plan Living Wish alone": the Boar arrives in hand -> SITE 10 continuation (deferred put-in-hand). In
     the ROLLOUT (TurnSolver.cpp ~79880 site 9 loop, then ~80105 the deferred site 3/5/6/10 apply) site 9 runs BEFORE the
     deferred continuation and never after it, so a site-10 continuation that casts an untapper (Umbra) or a sac outlet
     (Boar) gets no post-entry re-plan for their activations. rd8.err has NO [site9] line for the Wish-alone base plan.
     Executor twin: AIEngine.cpp site 9 ~9312, resolve_draw_breakpoint ~6420 -- check its order before changing either.
     A fix = run the site-9 check (snapshot before the deferred continuation, PostEntryActivationPending after) after the
     deferred continuation, in BOTH worlds, lockstep. Structural; budget-staged by the existing wave walker.
  Next agent: confirm which (one claude-play hand-off forcing the human menu's Wish-alone plan 136 + target 0 at n=8 is
  exactly n=10, which wins -- so (b) is the search-structure hole: the same line is found as a ROOT plan but not as a
  site-10 continuation). Then fix, rebuild, re-run chain2b's acceptance block.
Running (agent 3 launched): chain3b = `flock BOX.lock bash build/scratch/alch-t3/chain3b.sh` (launcher PID 1529923,
  flock 1529930; queued behind adopt-1119 step4 at launch): ab2.sh (5 arms base/heur/search/nohost/all, seeds 61001/63001
  x1000, d5 b20 -> build/scratch/alch-t3/ab2/report.txt, per-game MTG_DUMP_WINS in ab2/batch.stderr) then tiers.sh
  (smoke+regression --both -> build/scratch/alch-t3/tiers/both.txt). DONE marker build/scratch/alch-t3/chain3b/DONE.
  It tests 163e81f56 only; a third fix needs its own A/B + tiers again.

## UPDATE (agent 4, 2026-11-20 ~08:05 UTC): tasks 1-3 CODED, committed a227a482b, NOT YET BUILT; chain4 queued
### USER rulings (verbatim; all earlier ones above still stand)
- 2026-11-20, on the generic MTG_AURA_UNTAP_HOST_SEARCH branching: "Why do we need branching here? Just have a heuristic specific to the deck and we are done. Obviously in this deck Accomplished Alchemist is the target."
- 2026-11-20, on BurstPendingActColour (burst floated WHITE, Umbra {2}{U} unpayable, Combo Off route rolls back): "Yes, we need to fix this." (supersedes the "leave it as is" open question above)
- Earlier (11-19): "I would like to know why we can't find it at any budget." / "we should improve the heuristic and the search itself." / "the heuristic is our answer to this working at a lower budget, but the search should be capable at a high budget."

### What a227a482b does (unbuilt)
1. Generic MTG_AURA_UNTAP_HOST_SEARCH DELETED (heurarm slot, UntapAuraKeepHosts, UntapAuraHostKeyed, #A clause in
   PlanSignatureFast + legacy, BuildAuraLateBind KEEP/BIND partition, AuraUntapsHost). Kept: refund-float cap
   (UntapFloatSpendCap), UntapHostHasTapUse (used by the ActivateUntap target scan).
   ROOT CAUSE found: an engine term already ranked Umbra-on-Alchemist (MTG_AA_UMBRA_HOST in AuraHostBoardKey, 11-10,
   AuraHostKey::combo) but BindAuraLateHosts' FAST PATH (taken for host-inert Auras) read only the dork tie-break, so
   late-bound plans bound Arbor Elf / the Boar. Fix: the term moved to the provider -- DecisionProvider::
   RanksCreatureAuraHosts()/CreatureAuraHostRank(s, aura, host) (DecisionProvider.h, after DamageBackAuraHostCandidates);
   AccomplishedAlchemistProvider returns 1 for untap_enchanted_cost Aura on mana_per_life_gained host (EnvOn
   MTG_AA_UMBRA_HOST default ON). AuraHostBoardKey sums it (every ranking consumer: dedup, late bind, Solve collapse,
   ResolveEnchantTarget). Fast path now picks first non-dork among best-ranked hosts: lever MTG_AURA_LATE_BIND_RANK
   (heurarm AURA_LATE_BIND_RANK, default ON; =0 = origin fast path). Units: new "Crab Umbra host: the deck's provider
   puts it on the Alchemist..." and "...an Alchemist cast earlier in the same plan takes it..."; the late-bind
   equivalence case now runs with the provider term live (it checks fast path == full key).
2. Hole (b): MTG_SITE9_AFTER_DEFERRED (EngineFlags.h reader Site9AfterDeferredOn, heurarm SITE9_AFTER_DEFERRED, default
   ON). Rollout (TurnSolver.cpp deferred while-loop, ~80115 snapshot before bp_searched_plan(deferred_site_index()),
   block after apply_continuation_activations, INSIDE the sink so a committed line records it and the executor's
   replay_segment realises it): when bp_seen was 0 before the deferred and == 1 after, PostEntryActivationPending vs the
   pre-continuation snapshot -> site-9 chain (targeted only; untargeted -> g_bp_seen_latent for MTG_BP_DEV_SEQ), own
   PlanTraits scope per continuation. Executor no-commit twin: AIEngine resolve_draw_breakpoint, snapshot at entry
   (s9d_on: bp_depth 0, choice, bp_seen_exec 0, !fd_plan_committed), the continuation-apply block wrapped in
   `for (s9d_pass...)`, chain test + cands resolution at its end. Known small asymmetry: rollout's s9 traits carry from
   the deferred continuation's scope, executor's from the main scope.
3. MTG_BURST_ACT_COLOUR_PENDING (heurarm BURST_ACT_COLOUR_PENDING, default ON) in ManaPayment.cpp: helper
   BurstBoardUntapColour -- the first coloured pip (that the source makes) of an own untap ability aimed at the burst
   source (CollectCreatureUntapAbilities + UntapUnitCost), used when no plan act-pip shortfall and no line demand
   (LineDemandAnyPipColor with a Colorless sentinel); also now reached with no plan-traits scope. Callers pass &bp/&p.
   Decks it can move: only AA (burst sources: Alchemist, Somberwald Sage; Boars/Bruna hold Sage but no untapper).
   Site 9 after deferred is ENGINE-WIDE (any deck with a deferred continuation casting an activated permanent):
   expect other decks to move in the tiers.

### Running job
- chain4: launcher PID 1576839 = `flock BOX.lock bash build/scratch/alch-t3/chain4.sh` (queued behind adopt-1119's
  run_attr.sh, flock PID 1547503). Does: ./build.sh, units, combo_off, aa_* scenarios, acceptance arms at d8 b0 heur OFF
  (acc_all, acc_norank, acc_nos9d, acc_noburst, acc_nofloat, acc_heur, acc_verify = MTG_AURA_LATE_BIND_VERIFY=1),
  hand-offs ho8/ho10, play-setting runs play_off/play_heur/play_base. Output build/scratch/alch-t3/chain4/
  (summary.txt, runs.txt, units_fail.txt, *.out/*.err). DONE marker: build/scratch/alch-t3/chain4/DONE
  (contains FAILED if the build failed -> fix compile errors, re-run with the same command).
  (All build/scratch paths are under /workspaces/MagicDeckTester/.)

### Remaining, in order
1. Read chain4: fix compile/unit failures (watch: HeuristicArm slot order, AIEngine loop braces, ManaPayment includes).
   Acceptance = acc_all must be T3 (heuristic OFF, d8 b0); report play setting (play_off / play_heur). acc_verify.err
   must show no `[aura-late-bind-verify] MISMATCH`. If still T4: bisect (bisect2.sh shape, MTG_SITE9_TRACE=3 looks for
   "rod"/"exd" lines) and check hole (a) (payer taps the Alchemist for the Boar) -- a ManaPayment order rule with its
   own flag is allowed.
2. A/B: `flock BOX.lock bash build/scratch/alch-t3/ab4.sh` (written; 6 arms base/rank/s9d/burst/prior/all, seeds
   61001/63001 x1000, d5 b20 -> build/scratch/alch-t3/ab4/report.txt, DONE marker ab4/DONE).
3. smoke + regression --both (tiers.sh shape); s9d may move other decks -> A/B those decks s9d on/off; per-game audit of
   every slower searched game at --depth 8 --budget-ms 0; accept GT in the worktree only after; check_gt_logs.py.
4. Update docs/design/aa-staff-in-hand-loop-cast-draw.md + this file; commit; push backup/2026-11-20/alch-t3 only.

### Measurements so far
- ab2 (163e81f56, generic host search): search -0.0695 t=-8.91 wall -12.5%; nohost -0.0075; heur +0.0000.
  The deck-rule replacement should reproduce most of the "search" gain (the host choice) -- to be measured by ab4.

### chain4 RESULT (landed 10:25 UTC; build of a227a482b rc=0)
- Units 3722: 1 FAILED -- test_accomplished_alchemist.cpp:416 "Crab Umbra chooses Accomplished Alchemist over an
  attacking Harbinger" (AuraCastHostByRank != alchemist). Cause: that test relied on the OLD engine term (gated on
  deck_reads_lifegain_in_play); the rank now comes from the provider, so the test board needs the AA provider
  (s.m_provider = &AaProv(); AaProv is defined ~line 1127, later in the file -- move/forward-declare it). Not fixed.
- combo_off 51/51 PASS; aa_* scenarios ran (see chain4/summary.txt).
- ACCEPTANCE STILL FAILS: full game seed 12345681 keep 7 at d8 b0 heur OFF = T4 in EVERY arm (acc_all, norank, nos9d,
  noburst, nofloat, heur, verify); play setting T4 (play_off, play_heur, play_base). Hand-off ho10 (after Forest +
  Living Wish -> Boar) T3; ho8 (T3 start) T4. acc_verify.err: check for `[aura-late-bind-verify] MISMATCH` (none in
  summary). So the T3-decision hole remains with all three fixes on: next step is a trace at n=8
  (MTG_FS_ROOT_DUMP=3 MTG_SITE9_TRACE=3; look for "rod"/"exd" site-9-after-deferred lines -- if none, the
  Wish-alone variant never reaches the deferred s9 (check the DEV_SEQ latent record / bp_seen==1 guard / whether site
  10 is counted for that plan), and check hole (a) (payer taps the Alchemist for the Boar on the Umbra+Wish root route).
- ab4 NOT run (pointless until acceptance passes, though it would still measure the rank/burst changes).

## UPDATE (agent 5, 2026-11-20 ~13:10 UTC): hole (a) FIXED (tap order); the remaining T1 hole is an MTG_INWIN_CACHE soundness bug vs MTG_BP_DEV_SEQ
### USER rulings (verbatim; all earlier ones above still stand)
- "I would like to know why we can't find it at any budget." / "we should improve the heuristic and the search itself." / "the heuristic is our answer to this working at a lower budget, but the search should be capable at a high budget."
- On the Crab Umbra host: "Why do we need branching here? Just have a heuristic specific to the deck and we are done. Obviously in this deck Accomplished Alchemist is the target." (host = deck provider rule; generic MTG_AURA_UNTAP_HOST_SEARCH stays DELETED)
- On BurstPendingActColour: "Yes, we need to fix this."
- CLAUDE.md hard rule: no greedy pick inside the search window; heuristics only in a DecisionProvider, only to restrict or order options.

### Done this agent (WIP commit below; BUILT = build/Release in the worktree, build6.log EXIT 0)
1. Unit fix: test_accomplished_alchemist.cpp "Crab Umbra chooses Accomplished Alchemist over an attacking Harbinger" now sets
   `s.m_provider = &aa_prov` (local static AccomplishedAlchemistProvider). NOT yet re-run (units owed on this build).
2. HOLE (a) FIXED -- MTG_AA_ALCH_TAP_LAST (DEFAULT ON; heurarm slot AA_ALCH_TAP_LAST appended to HeuristicArm.h enum + kNames):
   AccomplishedAlchemistProvider::ManaSourceRank override (DecisionProviders.h/.cpp, after LifeScaledManaInOrderWalk):
   mana_per_life_gained creature -> max(inherited, kGrowableManaCreatureTapRank + 4 = 69), i.e. behind the whole creature band.
   Evidence (chain4 binary, ho8 trace build/scratch/alch-t3/t5/ho8.err lines 3056-3069, "rod" = state after the site-10
   deferred continuation): every Living-Wish-alone continuation left `Accomplished Alchemist#1(T)` with Arbor Elf untapped
   (e.g. 3058: `float=0 bf=[Forest#20(T),Arbor Elf#4,Forest#21(T),...,Accomplished Alchemist#1(T),Forest#22(T),Brindle Boar]`):
   the Boar's last pip took the Alchemist's 1 (exact) instead of Arbor's Sprawl re-tap (G+U, the U pays the Umbra).
   The card's own note listed this PARTIAL ("an Alchemist should tap LAST ... belongs in the deck's own provider").
   Result (chain5a, build5): ho8 (T3-start hand-off) d8 b0 heur OFF: T4 -> **T3**; acc_heur (full game heur ON d8 b0) and
   play_heur (d5 b20) **T3** (were T4). acc_all (full game heur OFF d8 b0) still T4; play_off T4.
3. MTG_DVS_TRACE (diagnostic, print-only, default off; TurnSolver.cpp near g_fsline_nest + 5 fprintf sites: the two latent
   records, inwin no-win store/widen/hit).
4. build/scratch/alch-t3/ab4.sh updated: every arm sets MTG_AA_ALCH_TAP_LAST; new arm `tlast`; `all` has it =1. NOT run.
   build/scratch/alch-t3/chain5.sh written (units, combo_off, aa scenarios, acceptance arms incl. acc_notlast/play_notlast).

### Why the FULL game still wins T4 (heur OFF, d8 b0) -- ROOT CAUSE located
- Bisect (build/scratch/alch-t3/bisect5/, claude-play prefixes of 1,0,0,-1,1,1,0,-1 at d8 b0 heur OFF): n=1,2 T4; n=3,4 T3;
  n=7,8 T3 (n=6 T4 is a hand-off artefact). Full game / n=2 pick Utopia Sprawl GREEN on T2 (events: "Utopia Sprawl -- chose
  Green"); T3 needs BLUE (the Umbra's {U} and {2}{U} have no other blue source but the Alchemist). The colour IS searched
  (EtbChoiceCandidates fans x1/x2/x3/x5); the T1 search simply never finds the T3 win: `[search] T1 pass=3 done win=11`,
  pass 4 win=4, then the MTG_BP_DEV_SEQ second pass runs at cost 0/0/1 (`[dev-seq] ... pass2=1 pass2_better=0`,
  sites fs_pre=1, compound n=0) -- it reads cached answers instead of walking.
- **MTG_INWIN_CACHE=0 -> T3**: the n=2 hand-off AND the full game (seed 12345681 keep 7 d8 b0 heur OFF) win **T3**.
  Lever matrix with MTG_INWIN_CACHE=0 (full game): all on T3; ALCH_TAP_LAST=0 T4; AURA_LATE_BIND_RANK=0 T4; BP_UNTAP_FLOAT=0 T4;
  SITE9_AFTER_DEFERRED=0 T3; BURST_ACT_COLOUR_PENDING=0 T3 (so s9d and burst are NOT needed for this game).
  MTG_GAME_LINE_CACHE=0 alone: T4. MTG_BP_DEV_SEQ=0: T4. MTG_BP_DEV_SEQ_ALL=1: T4.
- MTG_INWIN_CACHE_VERIFY=1 at n=2: `[inwin-cache] DISAGREE job=- turn=2 depth=2 cutoff=3 cached=11 fresh=3 nest=2`
  (verified=2 DISAGREE=1 MATERIAL=1) and with verify the search returns T3.
- MTG_DVS_TRACE=1 (build/scratch/alch-t3/bisect5/dvs.err, cmd = n=2 hand-off + MTG_DVS_TRACE=1 MTG_INWIN_CACHE_VERIFY=1
  MTG_TRACE=search). Exact lines:
    9:   [dvs] iw-store T2 depth=1 cutoff=11 b=2 fsnest=2 rnest=0 enum=0 sup=1 tp=1 dlat=0
    856: [dvs] iw-store T3 depth=1 cutoff=11 b=3 fsnest=3 rnest=0 enum=0 sup=1 tp=1 dlat=0
    858: [search] T1 pass=3 done win=11 cost=2988 used=3098
    861: [dvs] iw-store T3 depth=2 cutoff=11 b=3 fsnest=3 rnest=0 enum=0 sup=1 tp=1 dlat=0
    862: [dvs] iw-widen T2 depth=3 cutoff=11 b=3 (was 2) fsnest=2 rnest=0 sup=1 tp=1 dlat=0      <-- the unsound proof
    863: [search] T1 pass=4 done win=4 cost=4 used=3102
    866: [dvs] iw-hit T2 depth=2 cutoff=3 bound=3 fsnest=1 rnest=0 sup=0 tp=1                  <-- pass 2 reads it
    867: [dvs] iw-hit T3 depth=1 cutoff=3 bound=3 fsnest=2 rnest=0 sup=0 tp=1
  Latent records DO happen in pass 1 (tabulated: latent9m T3 sup=1 pend=1 x144, latent9d T3 sup=1 x64), but the T2 node
  that widened the no-win proof to 3 (ladder pass 4, cost 4 = answered from caches) had dlat=0: its walk met no latent
  ITSELF because its children were answered from memo entries of nodes that DID meet one, and that read did not make it
  dirty. inwin's store guard (`complete && !dvs_sup`, TurnSolver.cpp ~119858) therefore filed a ONE-DEVIATION refutation as a
  full-tree proof, and pass 2 (sup=0) served it at T2 depth 2 cutoff 3, so pass 2 never walked to the compound slot.
  Same family as MTG_DEV_SEQ_DIRTY_READ (which covers only lc reads with lc->dvs_mode==1 && dvs_dirty non-empty,
  ~116690) and the gamelc SupTag scheme (~119715).
- HYPOTHESIS / candidate fix (not written): an inwin entry made inside a SUPPRESSED pass (g_devseq_suppress > 0) must not be
  readable by an unsuppressed pass. Either (i) store under a SupTag'd key whenever g_devseq_suppress > 0 (read by suppressed
  passes only, like gamelc::SupTag), or (ii) find which read path skipped the dirty mark (lc hit without dvs_mode, the T3
  depth-2 store at line 861 with dlat=0 is the first suspect: what answered ITS children?) and mark it. (i) is the robust
  one; check gamelc's clean-node stores for the same hole (they also store plain-tagged when dvs_sup is false).
  Must be an engine-wide soundness fix with its own lever (e.g. MTG_INWIN_DEVSEQ_TAG, default ON), A/B + tiers (all decks
  can move: inwin and dev-seq are generic).

### Remaining, in order
1. Implement the inwin/dev-seq fix; rebuild (./build.sh under BOX.lock); run `bash build/scratch/alch-t3/chain5.sh
   build/scratch/alch-t3/chain5` under BOX.lock (units incl. the :416 fix, combo_off, aa scenarios, acceptance arms).
   Acceptance = acc_all T3 (heur OFF d8 b0); report play_off/play_heur (d5 b20). Check acc_verify for MISMATCH lines.
   Optionally run once with MTG_INWIN_CACHE_VERIFY=1 to confirm DISAGREE=0.
2. Add the new lever to ab4.sh arms (base =0, own arm, all =1); run `flock BOX.lock bash build/scratch/alch-t3/ab4.sh`
   (output ab4/report.txt, DONE marker ab4/DONE).
3. smoke + regression --both (`env -C <wt> bash test/regression.sh --both`, tiers.sh shape) under BOX.lock; inwin fix and s9d
   are engine-wide -> A/B every deck that moves (lever on/off); per-game audit of every slower searched game at
   --depth 8 --budget-ms 0; accept GT in the worktree only after; python3 test/check_gt_logs.py.
4. Update docs/design/aa-staff-in-hand-loop-cast-draw.md (tap-last, the inwin hole) + this file; commit named paths; push
   ONLY backup/2026-11-20/alch-t3. Never the shared branch.
5. Consider removing MTG_DVS_TRACE once the fix lands (diagnostic only) or keep it documented.

### USER-owned questions to surface (with numbers when measured)
- MTG_AA_ALCH_TAP_LAST is a deck payment-ORDER rule (provider, order only); adoption needs the ab4 tlast arm numbers.
- MTG_SITE9_AFTER_DEFERRED and MTG_BURST_ACT_COLOUR_PENDING are not needed for seed 12345681 once tap-last + the inwin fix
  are in; whether to keep them rests on ab4 (s9d is engine-wide).

### Running jobs: NONE from this agent (all probes finished; legacy-search probe bmdiep2p4 was stopped -- own experiment).

## UPDATE (agent 6, 2026-11-20 ~15:40 UTC): inwin hole ROOT-CAUSED + FIXED (57b56d62c); acceptance T3; ab4 done; tiers queued
### Root cause (not a missing read-taint: the HORIZON EXIT)
The pass-4 T2 node (dlat=0) never read a dirty entry: it took the FIRST-VERIFIED-WIN HORIZON EXIT on a T4 win (cost 4).
The exit's "no earlier win" is the ID premise = the shallower ladder passes' refutations, which in the dev-seq FIRST
pass are ONE-DEVIATION refutations (pass 3 met the T3 latent occurrences). Untracked dependency -> node clean -> inwin
widened "no win by T3", gamelc filed it plain, carry kept it. Same hole for all three consumers.
### Fix: MTG_DEV_SEQ_HEXIT_TAINT (default ON, heurarm DEV_SEQ_HEXIT_TAINT), commit 57b56d62c
FsHexit(state, win) replaces all 9 `++g_fs_hexits`; in a suppressed pass with a latent met before `win` (g_devseq_pass_lmt,
pass-wide; DvsPassLmtScope at both Suppress scopes; every latent record folds in via DvsNotePassLatent) and win > own
turn -> ++g_devseq_latent_n + node mlt. Unit "deviation sequences: a first-pass horizon exit past a latent turn is dirty"
(DevSeqHexitTaintForTest). Design note: docs/design/devseq-hexit-taint.md (uncommitted draft).
### Measurements
- chain6 (build/scratch/alch-t3/chain6): units 3723/3723 pass; combo_off 51/51; aa scenarios as before.
  acc_all (heur OFF, d8 b0, inwin ON) **T3**; acc_notaint T4; acc_inwin0 T3; play_off (d5 b20, heur OFF) **T3**;
  play_heur T3; acc_notlast/norank/nofloat T4; nos9d/noburst T3. INWIN/GAME_LINE_CACHE_VERIFY on acceptance: DISAGREE=0;
  DEV_SEQ_VERIFY: no TWO-PASS LATER; [dev-seq-hexit] tainted=3.
- verify sample (iwv/: 59 smoke searched cells x 6 held-out games): inwin verified=950 DISAGREE=3 MATERIAL=0; gamelc
  verified=5069 DISAGREE=196 MATERIAL=0 (kinds: cut 15 / later 1 / win-found-past-cutoff 180); tainted=34.
- ab4 (d5 b20, 61001/63001 x1000, 200 chunks): rank -0.0665 t=-8.89 wall -25.1%; s9d -0.0010 t=-0.63; burst 0.0000;
  prior -0.0075 t=-2.83; tlast -0.0100 t=-3.64 wall -5.1%; taint -0.0010 t=-1.42; all -0.0880 t=-10.99 wall -31.7%;
  allnt -0.0845; all vs allnt (paired) -0.0035 t=-1.96 (9 better / 1 worse).
### Running: tiers6 (smoke+regression --both) PID in build/scratch/alch-t3/tiers6.pid, out tiers6/both.txt, DONE tiers6/DONE
### Next: classify movers; multi_ab.py (build/scratch/alch-t3/multi_ab.py) lever on/off for moving decks held-out ONE batch;
audit slower searched games at d8 b0; accept GT in worktree; check_gt_logs.py; docs; push backup/2026-11-20/alch-t3 only.

## UPDATE (agent 6, ~17:30 UTC): HANDOFF (context limit). Read the agent-6 section above first.
### USER rulings (verbatim; bind the next agent)
- "I would like to know why we can't find it at any budget." / "we should improve the heuristic and the search itself." / "the heuristic is our answer to this working at a lower budget, but the search should be capable at a high budget."
- On the Crab Umbra host: "Why do we need branching here? Just have a heuristic specific to the deck and we are done. Obviously in this deck Accomplished Alchemist is the target."
- On BurstPendingActColour: "Yes, we need to fix this."
- CLAUDE.md hard rules: NO GREEDY PICK INSIDE THE SEARCH WINDOW (heuristics only in a DecisionProvider, only to restrict/order); A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A LINE UNFINDABLE (completeness at large budget is the invariant; a cache answering with an under-explored proof is a completeness hole).
### Done (commits on alch-t3-1119, nothing pushed)
- 57b56d62c MTG_DEV_SEQ_HEXIT_TAINT fix + unit (see agent-6 section). docs/design/devseq-hexit-taint.md = draft
  (mechanism/fix/soundness written; "Measured" section still empty) -- committed with this handoff.
- build/scratch/alch-t3/ab4.sh now has arms taint / allnt (results above).
### Tiers (tiers6, binary = 57b56d62c build): smoke+regression --both rc=1, 349 pass / 19 FAIL (combined movers; GT here predates
  ALL branch changes 163e81f56..57b56d62c, so movers are the whole branch, not only the taint). Output
  build/scratch/alch-t3/tiers6/both.txt; per-game non-AA changes build/scratch/alch-t3/tiers6/changed.json.
- AA cells all faster: smoke d0 6.2230->6.1180, d3 5.02->4.90, d5 4.94->4.84, 2hg d5 5.14->4.92; regression d0 6.185->6.105,
  d3 4.90->4.80, d5_s2002 4.9543->4.8514, d5_s3003 5.0400->4.9371.
- Non-AA movers (32 changed games, mostly digest-only): FASTER: cyclone2hg smoke gi10 6->5, cyclone reg d5_s3003 gi125 5->4,
  goblins2hg smoke gi17 5->4, goblins reg d5_s2002 gi3 4->3, martyr reg d3/d5 gi5 6->5, mazesend reg d5_s2002 gi166 7->6
  gi179 6->5, mazesend smoke gi55 6->5, melira reg d5_s2002 gi110 5->4, d5_s3003 gi126 5->4. SLOWER: snow smoke d5 gi81 6->7.
- Searched SLOWER audit (build/scratch/alch-t3/audit6/out.txt; arms tier / tier_notaint / tier_base / d8b0 / d8b0_notaint / d8b0_base;
  base = rank,s9d,burst,tlast,taint,float all 0):
  | game | old tier | new tier | new d8b0 | base d8b0 | taint matters? |
  | alch smoke d3 gi12 | 6 | 7 | 7 | 6 | no |
  | snow smoke d5 gi81 | 6 | 7 | 6 (recovers) | 6 | no |
  | alch reg d5_s3003 gi40 | 5 | 6 | 6 | 5 | no |
  | alch reg d5_s3003 gi87 | 4 | 5 | 5 | 4 | no |
  | alch reg d5_s3003 gi143 | 6 | 7 | 6 (recovers) | 6 | no |
  => the taint moves NONE of the slower games; gi12/gi40/gi87 do NOT recover at d8 b0 and are caused by another branch
  lever (AA provider/engine: rank, s9d, burst, tlast or float). Attribution running (below).
### Running jobs (unlocked, light, own experiments; kill freely)
- attr6: per-lever attribution of the 32 non-AA changed games at tier settings, arms on/taint0/s9d0/rank0/float0/burst0/base.
  PID in build/scratch/alch-t3/attr6/pid, output attr6/out.txt (digest per job `<case>_gi<N>__<arm>`). No DONE marker: done when PID exits.
- audit6b: gi12/gi40/gi87 at d8 b0, each lever off alone (`_off1_<LEVER>`) and each lever alone with taint on (`_only1_<LEVER>`).
  PID audit6b/pid, output audit6b/out.txt.
### Remaining, in order
1. Read attr6 + audit6b: name the lever behind each non-AA mover (expected: s9d / rank / float are engine-wide; taint only
   where a first pass met a latent) and behind gi12/gi40/gi87. ROOT-CAUSE the three AA games that stay slower at d8 b0
   (they break the adoption bar "every slower game must recover at --depth 8 --budget-ms 0") and fix or report.
2. Held-out pooled A/B of the moving decks, lever on/off: build/scratch/alch-t3/multi_ab.py (one lever per run, extend to
   arms if several levers move). NOTE: d5 tier cells carry NO `depth` key (depth from play profile) -- multi_ab.py's filter
   `depth <= 0 or budget_ms <= 0 -> skip` must be changed to skip only explicit depth 0 / budget 0 cells.
3. After the audit is clean: `env -C <wt> bash test/regression.sh --both --accept` is NOT allowed to re-run -- accept promotes
   tiers6's run (check that the harness's last run is tiers6; if a later run happened, re-run tiers first). Then
   python3 test/check_gt_logs.py.
4. Fill docs/design/devseq-hexit-taint.md "Measured", update docs/design/aa-staff-in-hand-loop-cast-draw.md (tap-last, the
   hexit hole, acceptance), commit named paths, push ONLY backup/2026-11-20/alch-t3.
### Open USER questions (with numbers)
- MTG_AA_ALCH_TAP_LAST adoption (provider tap ORDER): ab4 tlast alone -0.0100 t=-3.64 (22 better / 4 worse chunks), wall -5.1%;
  needed for the T3 acceptance (acc_notlast T4). Recommend adopt.
- MTG_SITE9_AFTER_DEFERRED: ab4 -0.0010 t=-0.63 (6/4), wall -3.2%; not needed for seed 12345681 (acc_nos9d T3); engine-wide.
  Recommend keep only if its attribution of the slower games is clean.
- MTG_BURST_ACT_COLOUR_PENDING: ab4 exactly 0.0000 (200 chunks tied), wall -0.3%; not needed for the acceptance (acc_noburst T3).
  The USER asked for it ("Yes, we need to fix this."), so it stays unless the USER says otherwise.
- MTG_DEV_SEQ_HEXIT_TAINT: a soundness fix (default ON); ab4 alone -0.0010 t=-1.42, within all -0.0035 t=-1.96 (9/1).
### ATTRIBUTION RESULTS (landed ~17:45 UTC; attr6/out.txt, audit6b/out.txt; jobs finished, nothing running)
- Non-AA movers: EVERY changed game is reverted by MTG_SITE9_AFTER_DEFERRED=0 (s9d) alone; the taint moves only melira reg
  d5_s3003 gi126 (digest only, still T4). s9d on non-AA: FASTER cyclone2hg gi10 6->5, cyclone d5_s3003 gi125 5->4, goblins2hg
  gi17 5->4, goblins d5_s2002 gi3 4->3, martyr d3/d5 gi5 6->5, mazesend gi166 7->6 / gi179 6->5 / smoke gi55 6->5, melira gi110
  5->4 / gi126 5->4; SLOWER snow smoke gi81 6->7 (recovers 6 at d8 b0). The rest digest-only.
- AA slower games that do NOT recover at d8 b0 are s9d too: gi40 (reg d5_s3003) and gi87: s9d off -> old result (5 / 4), s9d
  alone -> slower (6 / 5). gi12 (smoke d3): s9d + tap-last TOGETHER -> 7 (either off -> 6; either alone -> 6).
- So: the taint is clean in the tiers. The remaining bar failure is MTG_SITE9_AFTER_DEFERRED (agent 4's, engine-wide):
  3 AA games stay slower at d8 b0 (gi12, gi40, gi87). Next agent: ROOT-CAUSE s9d on gi40/gi87 at d8 b0 (single games,
  build/scratch/alch-t3/audit6b/manifest.json has the exact job specs) before accepting GT, or surface the s9d decision to the USER
  (its ab4 value is -0.0010 t=-0.63, and acceptance does not need it). Do NOT accept GT until this is settled.
