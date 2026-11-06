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
