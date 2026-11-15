# Fungus v3: landing Burst and fade-count menu

This is the Q9 and Q16 check requested in `fungus-cast-order-proposal-2026-11-13.md` before changing the v3 cast order.

## Q9: why a new Burst waited until main 2

`test/scenarios/fungusv3_burst_landing_main1.json` starts main 1 with Crossroads on the board and Season plus Burst in hand. Before this change, the solver cast both enchantments in main 1, activated Burst twice in main 2, and dealt no damage that turn. The existing `fungus_site9_adventure_continuation.json` passed because its Utopia Mycon cast opened site 9; that was not evidence that Burst itself opened the site.

The new permanent's fade activation was absent from `CollectActivationKeys`, and `CardHasPostEntryActivation` did not fan a Burst-casting plan. The new key makes site 9 notice a Burst with live counters, while the card predicate makes a Burst cast eligible for a continuation. `MTG_BP_FADE_POST_ENTRY=0` restores the old behavior. With the switch on, the v3 fixture casts Season and Burst, pops Burst twice in main 1, then attacks for a turn-4 win. The count menu sees the live Season (`C=14`, two tokens per pop) and live Crossroads, and offers `k=2`.

On the existing Fungus smoke/regression rows, 28 searched games win earlier and none win later; d0 is unchanged. All 545 scenarios pass, and the strict viewer sweep reports 343 ok, 493 repaired, one beaten, and no drift or gap. A fresh paired held-out run uses seeds 8750001, 8760001, 8770001, 8780001 and 8790001. The two arms share one 44,000-game batch. New minus old turn-games: v3 shipped -104/4000 (108 faster, four slower), v3 d3 -87/4000 (95/8), v3 d0 0/10000, 2HG shipped -61/2000 (67/6), 2HG d3 -70/2000 (71/3). Loss values 9 and 99 agree; no game changes win/unwon status. All 21 slower games recovered their old win turn at depth 8 with no budget (21-job batch, 0 residuals). Scratch manifest and logs: `build/scratch/n_fungus_ab/`.

## Q16: what the fade-count menu sees

`ReadFadeBoard(state)` prices `FadeKLandmarks` against the board at the current `CollectActions` call. A root action menu cannot see a Season or Sporecrown to be cast later in the same plan. In the new site-9 continuation, the call follows those casts and therefore reads their token doubling and anthem from the live board. The dead-token action filter separately credits Sporecrown copies in hand; that only preserves a candidate and does not adjust the count menu's damage projection.

The projection does not model Shroofus's combat-damage token trigger: `FadeBoardRead` has no field for it, and the `dmg_now`/`dmg_next` functions add token power, existing attackers and Slimefoot drain only. A hasted Saproling connecting under Shroofus can therefore be worth more than the menu's projected damage. This may keep a useful higher `k` off a menu when the low landmark does not itself win.

The menu also has no phase input. `ReadFadeBoard` computes ready attackers even in postcombat main, and `FadeKLandmarks` still tests `dmg_now_lo >= opp_life` there. Thus a postcombat activation may be narrowed to a supposed “this-turn” combat kill after that combat has already happened. No count-menu change is included in the Q9 patch; these two pricing gaps need targeted fixtures and a separate A/B.
