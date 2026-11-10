# sb-sacpayer handoff (2026-11-06)

Worktree `/workspaces/MagicDeckTester2/.claude/worktrees/sb-sacpayer`, branch `sb-sacpayer` (LOCAL, unpushed),
based on origin/phase-1-2-deck-analyzer 6abc9ef80. Spec: `docs/design/sac-for-mana-on-demand.md` (its
"Implementation (2026-11-05)" section describes what was built).

## USER rulings (verbatim, as given in the brief)
- "It should be land -> Dark Ritual -> other spells. Lotus bloom should be sacrificed on demand by the mana system. not a direct activation." (only the Lotus sentence is in scope)
- On Lake of the Dead: "For 2 I agree that it should not be sacrificing the land unless we need it. We should sort that out if possible."
- "Yes, it should be on-demand and only done that way." -> the payer is the ONLY route: retire the Lotus Bloom / Lake of the Dead SacForMana plan action and delete `MTG_IDLE_CRACK_FOLD` (`FoldIdleLandCracks`) with it. Other SacForMana outlets (Generator Servant, Treasure, Skirk-style) keep their current route.
- "Any order is fine from my perspective, so after seems good." (lands after the Shadowborn main-phase change; rebase onto origin before final validation)
- Coordinator 2026-11-06: main-phase levers LANDED on origin as dfd675afb (DEFAULT ON); rebase onto it before the next measurement; its GT includes Shadowborn movers + 4 EDF/Bruna origin-drift rows.

## Done (commits)
- 45ef98611 on-demand payer + gate supply + retired action + MTG_IDLE_CRACK_FOLD deleted + tests + viewer_protocol_check compat + human-line `sacout=` drop + card notes + design doc.
- f1bfef72d FIX: commit the crack IN PLACE (first cut assigned the trial GameState back over `state`, which dangled
  callers' zone references -> heap corruption / segfault in smoke). One heap scratch board for trials.
- Pieces: `IsOnDemandSacDef`, `OnDemandSacCrackableNow`, `OnDemandSacSupplyNow`, `AddOnDemandSacToPool` (SpellEffects.h);
  `GameState::deck_has_ondemand_sac` stamped in `GoldFishRunner::StampDeckTraits` (default TRUE);
  `TapForCostShared` wrapper over `TapForCostSharedCore` (ManaPayment.cpp); supply credited in AvailableManaPool,
  AvailableManaPoolNoAttackers, BuildNonCreaturePool, BuildColorFeasibility, ComputeAvailableColors,
  UntappedManaUpperBound; CollectActions skips on-demand sources.
- TEMPORARY A/B selector `MTG_SAC_ON_DEMAND` / heurarm `SAC_ON_DEMAND` (=0 -> stamp false = the shipped action
  route). Also read in main.cpp's human-line `sacout=`/`seq=` parse. REMOVE BOTH before landing (USER: only route).

## Measurements so far
- Unit: 2596/2596 pass (incl. 5 new "On-demand*" cases in test/unit/test_shadowborn_lakeprof.cpp), on 45ef98611.
- Scenarios: 418/418 on 45ef98611+parse fix (the Lake human-line scenario needed the `sacout=` drop).
- Crash probe after f1bfef72d: the 150 smoke jobs that had crashed, at 20/100 games each -> all ran, rc=0.
- Smoke / A/B / regression: NOT YET MEASURED on a working binary (first smoke crashed; the relaunched chain was
  stopped before it started, for this handoff and the rebase).

## Remaining, in order
1. `git -C <wt> fetch && git rebase origin/phase-1-2-deck-analyzer` (onto dfd675afb or later); rebuild under the lock
   (`flock -x /workspaces/MagicDeckTester2/build/scratch/sb-heavy.lock ./build.sh`); unit + scenarios.
2. Smoke (`MTG_PGO=0 bash test/regression.sh --smoke` under the lock). Expect only dragonstorm*/shadowborn* rows to
   move (FiveColour must NOT: its Black Lotus is conjured, not mainboard). Explain every moved row.
3. Held-out A/B: `build/scratch/sb-sacpayer/mkab.py` writes `ab1/manifest.json` (640 jobs, seeds 91001+1000*d,
   dragonstorm d0x3000/d3b10x1000/d5b20x600, shadowborn d0x3000/d3b10x600/d5b20x400 per arm, arms base/od via
   flag MTG_SAC_ON_DEMAND, ignore_play_profile). Run ONE batch under the lock with
   `--game-log-dir build/scratch/sb-sacpayer/ab1/games`; report with `python3 build/scratch/sb-sacpayer/abrep.py
   <games dir> [max_turns] -v` (per deck/depth: d, se, slower/faster, net).
   `build/scratch/sb-sacpayer/chain1.sh` chains smoke -> A/B -> regression (status in chain.status, DONE marker);
   re-copy the rebuilt binary to ab1/mtg.ab first.
4. Every slower game: single-game repro at `--depth 8 --budget-ms 0 --ignore-play-profile` (seed = 91001+1000*d+gi
   per the manifest's seed+offset, plus --game-index gi) both arms; it must recover.
5. Regression (`MTG_PGO=0 bash test/regression.sh`), incl. viewer_protocol_check: 8 Dragonstorm references record a
   Lotus crack (6 with sacout, 2 with float_color only) -> must still replay via the new compat tier. Then
   `scripts/ref_bench.py` for Dragonstorm/Shadowborn.
6. If the bar clears (each deck net <= 0, slower games recover at d8 b0): remove the temporary selector, rebuild,
   smoke+regression `--accept` per the skill, `python3 test/check_gt_logs.py`, push, watch CI in background, report
   Windows. If not: do NOT push; root-cause losing games, report with fixes; adoption is the USER's call.

## Open items / caveats to report
- Lake of the Dead has no CRACK face in the human pay-click (pre-tap) flow; only plain {B}. Lotus keeps Route::SacAction.
- Lake plain-vs-crack uses `g_line_unpaid_cost` (the line's remaining mana value vs AvailableManaPool after the plain
  tap) -- a payment choice under the exemption; check slower Shadowborn games for it.
- Lump colour order: cost's shortest pip, then line pips, hand pips, WUBRG (payment choice).
- Batch prepay unchanged (it already declined on a non-empty float, so crack lines were per-cast before too).

## Running jobs
None of mine. (Chain PID 3188692 was stopped before it took the lock.)


---------------------------------------------------------------------------------------------------------
# HANDOFF #2 (2026-11-06 ~01:45 UTC) -- supersedes "Done / Measurements / Remaining" above where they differ

## Done (branch sb-sacpayer, LOCAL, unpushed; rebased onto origin/phase-1-2-deck-analyzer dfd675afb, clean)
- b23675331 payer + retired action + MTG_IDLE_CRACK_FOLD deleted (was 45ef98611 pre-rebase)
- 92864668a crack-in-place fix (was f1bfef72d); ddeb7f6fa first handoff notes
- a832f5128 test fix after rebase: the ArmScope pins (SB_RITUAL_PAYOFF/SB_MAIN1_RULE off) git auto-merged onto the
  two payer tests; moved to "On-demand: no SacForMana plan action ... a cast that needs the crack is offered"
  (the default-ON main-1 rule folded the Overlord cast). Unit: 2613/2613 after it (5/5 On-demand*). Scenarios 419/419.
- 0f46edde0 WIP, UNBUILT + UNTESTED: fix for the Dragonstorm d0 regression (below). TurnSolver.cpp only:
  SubsetWastesAccelerant gains `implicit_accel`; SolveUncached computes od_payoff_sup (OnDemandSacSupplyNow when
  payoff_prune_on && deck_has_ondemand_sac) and, after the colour-exact check in consider(), rejects a subset that
  has no payoff (storm-hold rule) AND whose cost is NOT payable from eff_cg/eff_nc_cg minus the on-demand supply.
  Solve only (d0 runner + horizon playout); EnumeratePlans' payoff-prune twin NOT changed (search now admits
  implicit-Lotus no-payoff subsets = a widening; decide whether to mirror it).
  The build of 0f46edde0 was cancelled while waiting on the lock: build/Release/mtg is STILL a832f5128's binary.

## Measurements (binary a832f5128 == ab2/mtg.ab, MTG_PGO=0)
- Smoke (build/scratch/sb-sacpayer/smoke2.log): 164 pass / 8 fail, ONLY dragonstorm*/shadowborn* rows move
  (FiveColour unchanged, as expected). shadowborn d0 7.782->7.381, d3 5.10->5.12, d5 4.86->4.85, 2hg d3 5.32->5.34;
  dragonstorm d0 4.979->5.109 (WORSE), d3 4.50->4.50 (digest only), d5 4.460->4.447, 2hg d5 4.70->4.68.
- Held-out A/B (build/scratch/sb-sacpayer/ab2/, games/*.wins; report: python3 build/scratch/sb-sacpayer/abrep.py
  build/scratch/sb-sacpayer/ab2/games 0 -v), base = MTG_SAC_ON_DEMAND=0 (old action route; fold was default OFF so
  base == origin play), od = payer:
    dragonstorm d0 n=3000 4.9807->5.0727 d=+0.0920 se=0.0095 slower=214 faster=66 net=+276   <-- FAILS the bar
    dragonstorm d3 n=1000 4.3440->4.3330 d=-0.0110 se=0.0046 slower=2 faster=13 net=-11 (slower gi 137, 435)
    dragonstorm d5 n=600  4.4183->4.4050 d=-0.0133 se=0.0047 slower=0 faster=8 net=-8
    shadowborn  d0 n=3000 7.7577->7.3027 d=-0.4550 se=0.0225 slower=181 faster=737 net=-1365
    shadowborn  d3 n=600  4.8967->4.8750 d=-0.0217 se=0.0116 slower=14 faster=26 net=-13
                (slower gi 94 218 237 332 342 345 358 368 422 442 458 479 530 551)
    shadowborn  d5 n=400  4.7925->4.7925 d=0 se=0.0100 slower=5 faster=5 net=0 (slower gi 30 116 254 343 351)
  Seeds: seed = 91001 + 1000*depth + gi, --game-index gi (manifest jobs: seed base+off, game_index off).
- ROOT CAUSE of the Dragonstorm d0 loss (repro: build/scratch/sb-sacpayer/rp/, ds8b/ds8o game logs,
  proj8b/proj8o.txt = MTG_SOLVE_PROJ_TRACE=4): seed 91009 gi8 d0, base T4 / od T7. At T4 with Lotus Bloom on the
  battlefield and Dragonstorm + Apex in hand, base's {Lotus crack, Scourge} subset was pruned by the STORM-HOLD
  rule (SubsetWastesAccelerant, s_storm_hold: with a storm payoff in hand a fair Dragon does not justify an
  accelerant), so it ran the ritual/Apex/Dragonstorm line and won. On od the crack is implicit, the subset is just
  {Scourge} (supply=5 incl. the credited Lotus), the rule cannot see the Lotus being spent, Scourge (eval 800)
  beats the go-off line (eval 400), and the payer cracks the Lotus for it. 0f46edde0 is the proposed fix.

## Remaining, in order
1. Build 0f46edde0 under the lock; unit + scenarios; repro seed 91009 gi8 d0 both arms (expect od T4 again) with
   ab-style env MTG_SAC_ON_DEMAND (the selector still exists).
2. Re-run the A/B on the new binary: copy build/Release/mtg to a NEW dir (e.g. ab3/), `python3 mkab.py ab3/manifest.json`,
   ONE batch `--threads 24 --game-log-dir ab3/games` under flock. Check dragonstorm d0 net <= 0; shadowborn d0 should
   be unchanged by the fix (payoff_prune_on is Dragonstorm-only).
3. d8 b0 repro (`--depth 8 --budget-ms 0 --ignore-play-profile`, both arms) of EVERY slower searched game of the
   final A/B (and a sample of d0 slower games). Clairvoyance-only losses are accepted (USER). Root-cause any other.
4. Regression on the OD binary (viewer_protocol_check: 8 Dragonstorm references with a Lotus crack must replay), then
   scripts/ref_bench.py for Dragonstorm / Shadowborn.
5. Delete MTG_SAC_ON_DEMAND (HeuristicArm.h slot SAC_ON_DEMAND + name list entry; GoldFishRunner.cpp:1395 line;
   main.cpp two `EnvOn("MTG_SAC_ON_DEMAND", true)` reads in the human-line sacout parse) -> byte-identical default.
6. Land: unit, scenarios, smoke, regression; --accept only dragonstorm*/shadowborn* movers (any other mover must be
   explained; origin drift only if origin's binary reproduces it); python3 test/check_gt_logs.py; rebase (never merge)
   onto origin/phase-1-2-deck-analyzer; push; watch CI in background, report Windows.
7. If the bar is not met: do NOT land; report numbers, mechanism, proposed amendment (adoption is the USER's call).

## USER rulings (verbatim)
- "It should be land -> Dark Ritual -> other spells. Lotus bloom should be sacrificed on demand by the mana system. not a direct activation."
- On Lake of the Dead: "For 2 I agree that it should not be sacrificing the land unless we need it. We should sort that out if possible."
- "Yes, it should be on-demand and only done that way."  (-> the payer is the ONLY route; delete the selector before landing)
- "Any order is fine from my perspective, so after seems good."
- Brief (via coordinator): losses that come only from clairvoyance are accepted.

## Running jobs
None. chain2 (PID 3285147) finished: build/scratch/sb-sacpayer/chain2.status = "smoke rc=1 / ab rc=0 / DONE".
The queued build of 0f46edde0 (PID 3317473, waiting on the lock, never started compiling) was killed.

## Open question for the USER
- The payer widens the SEARCH for Dragonstorm: EnumeratePlans' payoff prune no longer drops a no-payoff subset funded
  by an implicit Lotus crack (it used to drop {crack, X} with no payoff). 0f46edde0 restores the rule in the greedy
  policy only. Mirror it in EnumeratePlans too? Default taken: no (a widening is not lossy; searched d3/d5 improved).

## Codex continuation (2026-11-06 ~06:50 UTC) -- provisional, DO NOT LAND

Built 0f46edde0 and verified unit 2613/2613, scenarios 419/419, d0 repro seed 91009 gi8 T4.
Added the Dragonstorm d0 go-off verification: suspended Lotus can increment spells before main 1;
the go-off candidate now gets a direct apply and combat check even when the damage projection
misses a fetched haste attacker (seed 91148 gi147). Both isolated repros recovered T4.

Held-out paired A/B `build/scratch/sb-sacpayer/ab4` (same seed matrix as ab2, 640 jobs):
Dragonstorm d0 n3000 net -17 turns (22 slower/42 faster), d3 n1000 net -12 (2 slower/14 faster),
d5 n600 net -8 (0 slower/8 faster). Shadowborn d0 n3000 net -1365 (181 slower/737 faster),
d3 n600 net -13 (14 slower/26 faster), d5 n400 net 0 (5 slower/5 faster).
All six cells meet the aggregate net <=0 bar. `ab4rc/` rechecked EVERY 21 searched slower
games at d8 b0 in one paired batch: FOUR still slower -- Dragonstorm d3 gi137 5->6;
Shadowborn d3 gi94 5->6, gi345 4->5; Shadowborn d5 gi351 4->5. The adoption bar
therefore FAILS. No push, GT accept, regression, or ref benchmark on this candidate.

`ab4rc/repro/` has direct game logs for those four. `ab4rc/trace/` has FSW traces.
Shadowborn gi345: base T2 Swamp, T3 Lake+Stone+Apostle, T4 win; on-demand T2 Lake,
T3 Stone, T5 win. At T2 depth3, the base's Swamp branch sees T4; on-demand's does
not. Both branch lists offer T3 Lake+Stone+Apostle. The on-demand apply CAN reach
the same T3 battlefield (Lake tapped, Stone, three Apostles, hand Sire/Culling/Overlord),
but the traced T3 continuation scores no win; the ripple/library continuation differs.
A proposed fix to include trailing activation cost in the Lake plain-tap check was
built/tested on all four seeds and recovered NONE; it was removed. Do not assert that
Lake plain-tap is the cause of the searched loss without more evidence.

Dragonstorm gi137: base suspends opening-hand Lotus Bloom on T1 and wins T5 after
its T4 arrival funds a second Scourge; on-demand leaves it in hand and wins T6.
T1 search offers Suspend in BOTH arms, but base rates the suspended T1 line T5
at d5; on-demand rates it no-win through d5. `ab4rc/trace/ds137t4_*` will show
why its T4 Scourge continuation fails under on-demand; inspect that next.

Temporary MTG_SAC_ON_DEMAND selector has been removed from final source in the
worktree (HeuristicArm, GoldFishRunner, main), but this tree is committed locally and
UNPUSHED pending the d8 exceptions. It built and passed unit 2613/2613, scenarios
419/419 before the discarded activation-cost experiment; rebuild after source
reverts before further validation. The last local commit contains the
combat go-off verification plus selector removal.

## Codex continuation (2026-11-06, after the above) -- still provisional

Dragonstorm d3 gi137 is now recovered at d8 b0, turn 5. The old source's
`LumpColourOrder` chose W for a `{2}{R}{R}{R}` Scourge payment: its pool estimate
already credited Lotus and Sandstone Needle, so every colour had zero reported
shortage and the WUBRG tie-break won. That forced the actual payment to consume
Needle's last depletion counter. The turn-5 firebreathing then had only two red
mana and missed lethal. Ranking the *current bill's coloured pips* after shortage
chooses R, preserves Needle, and recovers the win. An experimental extra depletion
hold did not fix any d8 case by itself and was removed. All diagnostic traces and
source prints were removed. The colour-only source was rebuilt; all four d8 seeds
were rerun: Dragonstorm 5 (recovered), Shadowborn gi94 6, gi345 5, gi351 5
(still slower). No full regression or GT accept until those three are understood.

Shadowborn gi345 detail: T2 Swamp branch at d3 has tail 4 under the base and no
win under on-demand. Both consider Profane Tutor suspend in T2 main 2. T3 Lake +
Thrumming Stone + Apostle reaches three Apostles under both; the old plan includes
an explicit Lake crack action. T4 candidate continuations diverge around ripple and
Lake mana. Compare the T2 main-2 Profane branch's hidden T3/T4 library and float
state next; `build/scratch/sb-sacpayer/ab4rc/trace/sb345m2_{base,new}.err` and
`sb345t4p_{base,new}.err` carry the relevant traces.

Further gi345 diagnosis: with a temporary two-arm selector and TurnSolver diagnostics,
the relevant T3 m2 state has the SAME full 128-bit `BuildDedupKey`
`a77909523f59bd4/bb00fa44af245e45`, the same three Apostles, no float, the same
hand, one suspended card, and library top `19,24,44,25,26,45,5,27` in both arms.
Both reach the SAME T4 state key `35d95f3a4c6418e3/6abdbfdb6043ba25`, hand
`39,3,1,28,31,43` and library top `55,46,11,2,33,37,32,42`. Memo off and
game-line-cache off do not recover OD. The base's T4 win plan is EXACTLY:
`Swamp; Shadowborn Apostle, Shadowborn Apostle, SacNamedTutorPut(Jetmir),
SacForMana(Lake of the Dead)`; the explicit Lake crack is pre-applied before
casts. OD's corresponding plan reaches many Apostles but the activation no-ops;
combat leaves opponent at 17. The Lake is tapped for plain {B} with the new Swamp
tapped too, so the activation cannot pay. `ActLineHoldMask` and `PlanTraits` are
present on some payments but not enough to preserve the future activation through
the ripple breakpoint. `LineRestAfter` + pending `act_mana_total` did not recover
any of the three Shadowborn losses; an experimental crack-first-on-needed-cost
arm recovered gi94 (5) but not gi345 (5) or gi351 (5). These experimental source
edits and the temporary selector were restored after the tests. The next fix must
preserve a Lake crack option across the breakpoint, without reinstating a direct
plan action or a greedy pick inside the search window.

## Codex continuation (2026-11-06 ~10:45 UTC): searched early payer branch

An experimental `Plan::early_ondemand_crack` state pin now offers an early Lake crack as a searched payment policy. It is carried through plan keys, dedup and dominance, reset at turn start, and consumed by `TapForCostShared` on the first payable bill. It does not add a `SacForMana` plan action. The first build recovered gi94 but not gi345/gi351 because `BatchPrepayMainCasts` tapped Lake before the first bill and bypassed the per-cast payer. Declining joint prepay when the pin is set recovered all four original slower d8/b0 seeds: Dragonstorm gi137 T5, Shadowborn gi94 T5, gi345 T4, gi351 T4. Focused On-demand unit tests pass.

Held-out ab5 used the unchanged ab4 baseline games and reran the new arm on the same 8,600 seeds. All 27 new searched-depth slower games recovered against the original baseline at d8/b0. The aggregate bar still failed: Shadowborn d3 +5/600 and d5 +3/400 total turns. Search work grew about 11–12%. A sound fan reduction omits plans with no actions, which cannot call the payer in that segment; any later paid continuation gets its own branch. The ab6 rerun: Dragonstorm d0 -17/3000, d3 -13/1000, d5 -8/600; Shadowborn d0 -1385/3000, d3 -2/600, d5 **+2/400**. The four original d8 seeds still recover. Thus the candidate is **not accepted, not pushed**, and no GT was changed. The remaining concern is budgeted Shadowborn d5 search cost/ordering, not missing unbudgeted coverage. Current local source has no temporary FSW diagnostic text; it is built, and focused tests pass. Full regression/reference gates remain pending acceptance-bar convergence.

An additional, predeclared disjoint Shadowborn d5 b20 sample (`build/scratch/sb-sacpayer/ab7/`, seed 200001, 1,600 matched games) confirms a persistent cost: base 4.788125, new 4.796875, net **+14 turns**, 34 slower / 23 faster, paired SE 0.00537 turns/game. Combined with ab6's +2/400, this is +16/2,000. Do not treat the first 400-game loss as noise. A further fan reduction or different placement is needed before adoption.

Correction: the ab6 empty-plan reduction is not generally sound. A precombat plan with no main-phase action can still carry the pin into combat, where more than one paid activation could make the first Lake crack relevant. The generic enumerator must not drop that branch without a proof. The `p.actions.empty()` filter has therefore been removed from the local source (it is now back to the ab5 fan shape, with diagnostic prints removed). The ab6/ab7 measurements remain useful budget-cost evidence, but their reduced fan is **not a landing candidate**. No rebuild has been run after this source revert.

## Codex continuation: provider prune and Lake prepay hold screen

Added a default-OFF `MTG_SB_EARLY_LAKE_PRUNE` provider hook. With the switch on, only ShadowbornProvider omits the early-crack policy branch for idle plans or a lone Apostle cast without a live ripple instance. The generic provider still offers all branches. A same-binary paired 600-game d3 / 400-game d5 screen (`build/scratch/sb-sacpayer/provider-prune/`) found prune vs full fan: d3 net -7 turns (zero slower, six faster), d5 net -1 (zero slower, one faster). The pruned results equal ab6: 4.8933 d3, 4.7975 d5, so **d5 remains +2/400 against the old payer**. The four original d8 seeds still recover with prune enabled. The hook remains default OFF, unadopted, and its deck-specific prune would need a fresh held-out A/B and user judgment if it became competitive.

Also tried reserving a crackable Lake's plain tap in `BatchPrepayMainCasts`' one-shot hold mask. Same 1,000 games, with and without the provider prune, gave **zero changed win turns** in both depth cells. The source change was removed; the payer already avoids the ordinary tap in these measured batches. The last built binary still contains this inert Lake-hold trial, while the local source has it reverted. Rebuild before further validation. No regression/GT/push was done.

Update ~11:30 UTC: rebuilt the current source with `./build.sh` after removing that Lake-hold trial; Release build completed. Focused `On-demand*` tests pass (6/6, 197 assertions), and `git diff --check` is clean. The provider prune remains default OFF and unadopted. The last held-out budgeted Shadowborn result still fails the net-turn bar, so a full regression, GT update, and push remain inappropriate for this candidate.

Update ~13:10 UTC: tried a coverage-preserving Shadowborn provider ordering of the early Lake-payment policy before its ordinary sibling (`MTG_SB_EARLY_LAKE_FIRST`, default OFF, temporary). The rebuilt late arm reproduced ab5 exactly on all 600 d3 and 400 d5 games. Early ordering changed two d3 games with net 0 relative to late, and one d5 game by +1 turn. Against the saved old-action baseline: d3 remains +5/600; d5 worsens from +3/400 to +4/400. Work units: d3 late 6,981,123, first 6,920,652; d5 late 7,392,705, first 7,421,341. The ordering hook was removed from source after measurement. A proposed broader provider prune for multiple Apostle casts was also discarded before measurement: a tapped Swamp victim and untapped Lake can make the first early crack necessary for later casts, so it is not sound. No adoption, GT, or push.

Update ~13:35 UTC: a further proposed single-cast closed-list prune was discarded before build or measurement. A lone Dark Ritual, Culling the Weak, or tutor cast can create a later paid continuation, so the proposed dominance claim was unsound. The local provider source has been returned to the earlier default-OFF Apostle-only experiment. The ab10 manifest is unused. No adoption, GT, or push.

Update ~14:15 UTC: ran the default-OFF Apostle-only prune enabled on the predeclared disjoint 1,600-game Shadowborn d5 b20 sample (`build/scratch/sb-sacpayer/ab11`). All 1,600 wins files pair with the saved old-action baseline `ab7/base.err`: base 4.788125, prune 4.795000, **net +11 turns**, 32 slower / 24 faster. The full on-demand fan was +14 on the same sample, so the prune saves three turns but still fails the per-deck net <= 0 bar. This rejects the provider-prune adoption route on an independent held-out sample. No regression/GT/push for the payer branch.

Update ~14:25 UTC: new candidate adds SacNamedTutorPut's activation bill to `LineCastCostTotal`, subtracts it after payment in both apply paths, and declines joint prepay for an on-demand plan containing that activation. Rationale: the T4 lost plan owes two Apostle casts AND the subsequent `{1}{B}` activation, but the old line hold counted only casts; prepay could tap Lake before the per-cast payer saw the full obligation. The Release simulator build passed. All four original d8/b0 slower seeds still recover (Dragonstorm T5; Shadowborn gi94 T5, gi345 T4, gi351 T4). Paired 1,000-game budgeted Shadowborn screen against saved old-action baseline still fails: d3 +7/600 (21 slower, 16 faster), d5 +2/400 (7 slower, 4 faster). This candidate is not accepted. Next experiment: see whether the more complete hold permits dropping the expensive early Lake-payment policy branch for Shadowborn while retaining these recoveries. No GT or push.

Update ~14:31 UTC: mode 2 of `MTG_SB_EARLY_LAKE_PRUNE` experimentally omits *all* Shadowborn early-policy branches (unpruned oracle still gets them). On the same 1,000 paired budgeted games it improves vs old payer: d3 -15/600, d5 -2/400. However the d8/b0 known seed gi94 regresses T5 -> T6 (the other three recover), so the full omission fails the coverage bar. The gi94 full game logs in `build/scratch/sb-sacpayer/acthold5/{fan,none}/trace1` show the divergence begins on T1: the full fan sees a T3 Thrumming Stone + Apostle line, T4 tutor/ripple, T5 kill; without the future early-policy branch the search casts an Apostle in T1 main2 and reaches T6. A selective fan around ripple/tutor/activation lines is the next experiment. No adoption.

Update ~15:15 UTC: a provider policy that keeps early Lake crack branches only for Thrumming Stone and live ripple casts passed the held-out aggregate screen when combined with a `ComboCardValue` for Diabolic Intent in the precise board state where it fetches a missing ripple granter and Lake can power it. The tutor value repaired disjoint seed gi1323 at d8/b0: old payer T5, new payer T5. The four original d8/b0 seeds also still recover. The final selector-free provider source has been written; full build and regression gates are underway.

Measurements on the predeclared 8,600-game paired sample (`build/scratch/sb-sacpayer/acthold10/fullab`) against `ab4/games/base__*.wins`: Dragonstorm d0 -17/3000 turns, d3 -13/1000, d5 -8/600; Shadowborn d0 -1381/3000, d3 -7/600, d5 -2/400. Disjoint Shadowborn d5 b20 (`acthold10/disjoint`, seed 200001) is -7/1600. All 19 slower games in the first 1,000 Shadowborn budgeted games and all 20 slower games in the disjoint 1,600 recover against the old-action payer at d8/b0 (`acthold10/recheck` and `acthold10/disjoint_recheck`). The old binary is `ab4/mtg.ab` with per-job `MTG_SAC_ON_DEMAND=false`; the new arm uses the current payer binary. These comparisons establish the local adoption bar before the final source cleanup and regression GT update; rerun focused cases after the selector removal build.
