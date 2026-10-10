# Bruna s5005 gi759: the executor never fired a continuation's Greaves release (executor vs search)

Status: **FIXED 2026-10-10** (`CastSectionUnlock`, ManaPayment.h). Found 2026-10-10 while validating the Bruna
width collapses (fbd63d2f). This doc's first version (fbd63d2f) diagnosed a PAYMENT gap; that was the visible
symptom on T5, not the cause. The cause is on T4 and has nothing to do with which creature pays.

## Repro

```
build/Release/mtg decks/Bruna/Bruna.cod --profile decks/Bruna/Bruna.profile.json \
  --cards-json src/cards/data/cards.json --seed 5764 --game-index 759 --games 1 \
  --depth 8 --budget-ms 0 --ignore-play-profile           # + MTG_FD_ORACLE=1 MTG_FD_TRACE=1
```

Pre-fix: `[fd-diverge] seed=5764 realized_win=7 predicted_win=6`, and at exit
`[enchant-retarget] 1 searched Aura target(s) illegal at resolution (fallback host used)`. Overnight seed block
5005, gi 759 (d3 b20 row: T6 before the width collapses, T7 after).

## The mechanism (measured, not inferred)

The line is committed at T1 (d8 b0 proves the T6 kill there). Its T3 plays Somberwald Sage + Lightning Greaves
and equips Greaves to the Sage (haste). Its T4 casts Open the Armory, which finds Arcanum Wings -- a breakpoint.

* **The search's continuation at that breakpoint** is the unbranched-canon default (`Plan::cont_canon`):
  `[Arcanum Wings -> Sage, equip Greaves Sage -> Birds of Paradise #9]`. The Sage wears Greaves, so its shroud
  forbids our own Aura spell (CR 702.18a / 303.4a). ApplyPlanDirect applies every continuation through
  `apply_plan_actions`, whose cast section runs `ApplyManaUnlockEquips` before the first cast; its
  SHROUD-RELEASE half (Bruna sweep B, 45266160) moves Greaves to Birds #9 first, then Wings lands on the
  Sage. Traced: node board `36e56` (Greaves on the Sage), post-apply `36e9 ... 1a56` (Greaves on Birds, Wings on
  the Sage). A canon continuation's board activations are neither applied nor RECORDED
  (`apply_continuation_activations` returns first), so the committed line's T4 record was `bp[Arcanum Wings]`
  alone -- the release that made the target legal was not in it.
* **The executor** replays the record (`replay_recorded`): it casts Wings at the Sage, which still wears
  Greaves. The target is illegal on resolution, `ResolveEnchantTarget`'s fallback puts Wings on Birds #9 (the
  one `[enchant-retarget]` of the run). None of the executor's continuation appliers (`replay_recorded`,
  `resolve_draw_breakpoint`, the pod and site-9 passes) ever fired the unlock/release at all -- only the MAIN
  plan's cast loop did. A SEARCHED continuation records its activations, but the replay dispatched them in its
  trailing pass, after the casts: the same cast into shroud.
* From there the boards differ. On T5 the committed swap (`swap in Eldrazi Conscription`) puts Conscription on
  Wings' host -- the Sage in the search's world, Birds #9 in the executor's -- and the recast Wings `{1}{U}` is
  paid with Seaside Citadel + Birds #9 in BOTH worlds (same payer, same tie-break by battlefield order). In the
  search's world Birds #9 is just a mana source and the 10-power Sage attacks; in the executor's world Birds #9
  is the 10-power host, so tapping it removed the attack and T6 became T7.

So the "four lands + the 0-power Birds #10 could have paid both" observation is true of the executor's
DIVERGED board, but the search never planned that board. There was no payment lockstep gap; the payer is
shared (`TapForCostShared`) and made the same choice on both boards.

## Why the inert-equip fold exposed it

Without `MTG_EQUIP_INERT_FOLD`, the old line parked Greaves on Birds #9 with an idle move, so on T4 the Sage was
not shrouded and Wings' continuation needed no release. The fold removed the idle move (correctly), which put
Greaves on the Sage, which made the release necessary -- and the release was the part the executor dropped.

## The fix

One object for every cast section, in both worlds: `CastSectionUnlock` (src/ai/ManaPayment.h). It installs the
list's `PlanReserveSources` and fires `ApplyManaUnlockEquips` once on construction (the up-front fire) and again
on `Fire()` after each non-sacrifice cast / Vial put -- exactly the two statements `apply_plan_actions` and the
executor's main cast loop each wrote by hand before.

* ApplyPlanDirect's `apply_plan_actions` and AIEngine's main cast loop now build it (behaviour unchanged).
* AIEngine's four continuation appliers build it too: `replay_recorded` (per recorded main-level segment,
  opened at the first cast-like record, fired after each cast's own nested replay, closed before the trailing
  activations), `resolve_draw_breakpoint`, the pod pass and the site-9 pass.
* An unbranched-canon continuation's cast section RECORDS the Equips it fires into the breakpoint sink
  (`ApplyManaUnlockEquips`' new `fired` out-param), so the committed line carries the release; the search's
  apply and every score are untouched -- only the record grows. A searched continuation already records its
  activations.
* `resolve_draw_breakpoint` logs the Equips a canon continuation's section fired (it dispatches no
  activations, so they would otherwise leave no log line).

Not a heuristic and not a choice: the release / unlock is the move the search already made, and the shared
function fires it under the same condition in both worlds. Payment is unchanged.

## Verification (2026-10-10, A/B vs the pre-fix tip c5ec4d5d built in its own worktree)

* **The repro wins T6** at d8 b0 and at d3 b20 (was T7 at both), no `[fd-diverge]`, no `[enchant-retarget]`;
  the committed line's T4 record is now `bp[k13:Lightning Greaves k0:Arcanum Wings]`.
* **Scenarios** (both FAIL on the pre-fix tree): `bruna_wings_greaves_release_canon` (from gi759's T2: the
  canon route, T7 -> T6) and `bruna_wings_greaves_release_searched` (from its T4: the T4 root SEARCHES the
  continuation, records cast-then-move, the old replay cast first -- no win by T6 -> T6). 160/160.
* **Unit** `Continuation cast section: a recorded Greaves release lands before the Aura it frees (s5005
  gi759)`; mtg-test 562/562.
* **Smoke, full tier:** 121 PASS byte-identical; 1 play-changed at the same score (`bruna2hg d3` gi15: a
  canon continuation's haste unlock -- Greaves onto the second Somberwald Sage -- now realised). 0 slower.
* **Regression, full tier:** 167 PASS byte-identical. FiveColour d3 s3003 gi126 **T6 -> T5** (a pre-existing
  `[fd-diverge]` predicted 5 / realised 6: the continuation's haste unlock now fires, Maelstrom Archangel is
  cast) and gi149 play-changed (same unlock, same T6). Bruna d3 s3003 gi42 / gi123, d5 s2002 gi33, d5 s3003
  gi42: play-changed at the same score -- each a release or unlock now realised (the pre-fix arm prints an
  `[enchant-retarget]` on gi123 and gi33: an Aura cast into Greaves' shroud). 0 slower anywhere, so no
  recovery runs were owed.
* **`[fd-diverge]` on the 725 searched Bruna suite games** (smoke + regression d3/d5 + 2HG, one pooled batch
  per arm, `MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`): 1 before, 1 after (the same land-light seed 3018 game); 0
  nonconv either way.
* **Viewer protocol `--strict`:** 556 refs, 171 ok / 385 repaired / 0 play-drift / 0 board-diverged / 0
  enum-gap -- and every per-reference line (status, win turn, repairs) is identical to the pre-fix arm's, so
  no recorded human line moved and no recording stamp is needed.
* **Overnight** (run once, before the USER's 2026-10-10 directive that the overnight tier is not run between
  fixes; NOT accepted -- its GT and the gi759 provenance note are left to the periodic overnight run): every
  non-Bruna row byte-identical; Bruna searched rows 0 slower / 5 faster (s5005 gi759 7 -> 6, gi780 6 -> 5,
  s4004 gi288 7 -> 6, gi585 5 -> 4, gi904 unwon -> 8) / 58 play-changed, d0 rows byte-identical. Of the 63
  moved games, 50 first differ at a newly realised Greaves move, 8 at an Aura landing on its searched host
  instead of the fallback (Wings / Unflinching Courage / Almost Perfect), 2 at a cast the newly hasted dork
  paid for, and 3 in the opening (the London-bottoming playouts run the same executor).

## What this is NOT

The attack-body tap order (`MTG_ATTACK_BODY_TAP_ORDER`: equal-rank mana creatures tap lowest attack power
first) would also have saved the DIVERGED T5 by tapping Birds #10 instead of the Conscription-wearing Birds #9.
It is ON in human play and OFF autonomously pending the held-out A/B the USER asked for (EngineFlags.h); this fix
does not touch it, and gi759 does not need it once the executor realises the search's board.
