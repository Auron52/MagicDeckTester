# Midrange Burn: existing-card and inherited-provider audit

Audit 2026-10-29. Live Scryfall fetched with `curl -fsS https://api.scryfall.com/cards/named?exact=...`; code inspection only, no builds/tests/code changes. This is a fix proposal, not validation.

## Existing definitions versus live oracle

| Card | Live source | Finding |
|---|---|---|
| Lightning Bolt | https://scryfall.com/card/msc/806/lightning-bolt | Printed cost/type and three damage match. `targeting:any` selects opponent by default; engine's target representation and human chooser govern other legal targets. General any-target completeness remains a global limitation, not established by this audit. |
| Searing Blaze | https://scryfall.com/card/ddi/67/searing-blaze | Local oracle is obsolete: current first target is player **or planeswalker**; second is a creature controlled by that player or that planeswalker's controller. Amount 1/3 and land-entered predicate match. Model supports paired creature/controller-player but not paired planeswalker choice. |
| Monastery Swiftspear | https://scryfall.com/card/bro/144/monastery-swiftspear | Cost, Human Monk types, 1/2, Haste and Prowess match. `SpellEffects.h:40320` FireProwess grants both temporary stats for each noncreature CAST; EffectHandler:1103 and TurnSolver:58078 fire it at cast. Suspend itself must not trigger prowess; the later free cast must. This is an integration check for new Rift Bolt. |
| Mountain | https://scryfall.com/card/mdd/298/mountain | Basic Land — Mountain; produces R; matches. |
| Fiery Islet | https://scryfall.com/card/mh1/238/fiery-islet | **Incorrect cost model:** printed mana ability pays one life; local `tap_self_damage:1` models damage. Draw ability costs 1, taps and sacrifices the source correctly in AIEngine::PerformDig and TurnSolver dig paths. |
| Light Up the Stage | https://scryfall.com/card/dsc/166/light-up-the-stage | Cost 2R and spectacle R match. Spectacle uses opponent loss, not damage-only. Two cards are staged through next turn inclusive; engine does not increment draws. Executor stages into staged_cards then hand plumbing; rollout directly uses staged hand entries. Stage expiry uses `< current turn`, retaining permission through the last legal turn. |

### Searing Blaze: precise fix boundaries

Current `AIEngine.cpp:9041` Multi chooses a creature and puts its controller in the player target; `TurnSolver.cpp:58418` also damages the selected creature's controller. Thus **own creature + ourselves is legal**, and earlier suspicion that the fallback damaged the opponent was disproved. Keep this behaviour consistent in both worlds. Do not invent an opponent-face target when selecting an own creature.

1. Replace stale local oracle with the current Scryfall text, account for Landfall keyword metadata if field audit requires it.
2. A complete target model needs a paired first target: controller-player OR a planeswalker that controller controls, plus the selected creature. Validate the relationship when targets are chosen. At resolution retain legal surviving targets independently (CR 608.2b), not all-or-nothing based on creature survival.
3. `EffectHandler.cpp:755` currently treats every Permanent as creature-style marked damage. For a walker target use loyalty loss and state-based death, not `target.damage += damage`.
4. Viewer must expose the first-target player/walker choice and the corresponding creature targets, not only the creature click. Own Chandra plus own creature makes this clause reachable on Midrange Burn, so it is not defensible as absent-opponent-state simplification.
5. Tests should include 1/3 damage to player+creature and walker+creature, own pair, illegal mismatched pair refusal, and one target disappearing before resolution. No such tests were run here.

Additional existing narrowing: `TurnSolver.cpp:12313` FindOwnProwessBurnTarget and `33540` collection gate restrict own-target casts to a ready prowess attacker and surviving own creature. This is engine-owned narrowing rather than a legal-target check. It can hide an own-target cast to trigger a Chandra emblem or enable Incinerator reach, including a lethal self-target creature choice. Treat the global correction separately from merely correcting oracle metadata.

### Fiery Islet: precise fix proposal

Add neutral-default integer `tap_life_cost` to CardParams/parser; set Islet to `tap_life_cost:1`, remove `tap_self_damage`. Mana ability activation requires life >= cost (paying exactly all life is legal, followed by SBA loss). It must **pay** life before adding mana, never enqueue damage, fire damage triggers, or allow prevention. The ability itself is a mana ability. Paying the draw ability's generic cost may use another Islet and pay that other Islet's life; sacrificing the draw source itself does not charge its mana-ability cost.

Cover all payment routes rather than only the visible tap function:

- `SpellEffects.cpp:4465–4520`: DFS source activation and backtracking. Reject sources unable to pay, subtract life as cost independently of pain, and restore snapshots on failed branches.
- `SpellEffects.cpp:6135–6371`: memo tap replay and aggregate self-life delta. Reject cached solutions whose life costs cannot be paid under the current life total (ensure key/eligibility or replay validation includes it); never replay life-cost loss as armed damage.
- Shared direct/float taps in SpellEffects.h and ManaPayment source availability must exclude unaffordable activations. Search all `tap_self_damage` consumers before closing: they currently stand in for three different facts (ordering, life deltas, damage events), and copying that field's meaning blindly repeats the bug.
- `DamageEvents.h:195` PainForTap must return zero pain for Islet. A Manabarbs tap trigger still occurs independently because the permanent is a land.
- Tests: life 0 refuses; life 1 pays to 0 then loses; life 2 becomes 1 with either colour; Tamanoa/prevention do not affect payment; untapped source cannot fund its own sacrifice-draw cost; failed/memoized payments remain identical in executor and rollout.

## Stage permission and second main

`GoldFishRunner.cpp:72` enables second main from spectacle_cost, so LUTS already enables it on this deck. `EffectHandler.cpp:923` and `TurnSolver.cpp:58743` stage the two cards with expiry current turn + 1. Rollout expiry at TurnSolver:82069/85033 uses a strict `<` comparison. Staged cards represent exile permission, not actual hand: discard/hand size paths must continue excluding `m_is_staged`. No field change is needed on LUTS from this audit. The major second-main defect risk is inherited provider filtering, below.

## Inherited Burn hooks: exact incompatibilities and recommendation

MidrangeBurn derives from the provider it actually rode, preserving baseline play, but that baseline is not validated for these new cards. Do not globally change Burn. Prefer **explicit Midrange-only delegates to GenericProvider** for Burn-specific judgement hooks while this pool is analyzed; measure and disclose this baseline change. A later Midrange policy can reintroduce only justified/approved narrowing.

| Hotspot | Risk on Midrange | Recommended Midrange override |
|---|---|---|
| BurnProvider::RestrictsMain2Casts / Main2CastAllowed, cpp:12254/12267 | Rejects any new spell in main 2 unless a same-main LUTS was cast. Main-1 burn/combat can reduce Incinerator's cost; main-2 Incinerator and Rift Bolt suspend can be real lines. | `RestrictsMain2Casts() -> false`; `Main2CastAllowed(...) -> true` (or Generic delegate). |
| HoldsSacLandBurn, cpp:11976 | If Fireblast is represented with Action::sacrifice_land, rejects nonlethal Fireblast unless spectacle exception or stage expiry. Ignores freeing mana via Incinerator reduction and Chandra emblem triggers. | Delegate GenericProvider::HoldsSacLandBurn (false). New Fireblast's alternate sacrifice field should be inspected; do not claim this hook fires without confirming its Action fields. |
| CastOrderRank/TotalByName/ActionRank, cpp:12107–12162; OrderOpaqueCastsByRank, SacLandCastsInOrder | Original order puts ordinary burn before unnamed Chandra/Grim/Incinerator. Burn before Chandra loses emblem-trigger damage; Chandra mana first may enable burn; printed six-MV Fireblast/spectacle Skewer ranks do not reflect their real cost or use. | Delegate entire rank/tier/total-name/action-rank/opaque/sac-order family to GenericProvider; let searched capability vary sequencing. Inspect generic ordering search independently—delegation alone does not prove every permutation is searched. |
| PreferHoldLandDrop, cpp:12021 | Banks at >=3 lands because original deck tops at MV2. Here Chandra costs 4, Incinerator may need 6, Fireblast consumes two Mountains, and Islet sacrifice consumes land. | Generic delegate. |
| LandPlanTieKey / LandDropAfterDrawEngine / HeldDropOpensDrawContinuation, cpp:12175/12214/12252 | LUTS-specific ordering assumes unnamed spells occur after LUTS and ignores Chandra's mana/impulse modes. | Generic delegates for all three. |
| CleanupDiscardCandidates/ProtectionWaived/ShedStable, cpp:12383 onward | Original keep quota and threat ladder authored for low-curve Burn; risks shedding lands needed by Chandra/Fireblast or undervaluing planeswalkers/Grim. | Generic delegates until Midrange bucket proposal is authored and measured. No silent adoption of Burn buckets. |
| BpChainSlotOptIn, header:1200 | Burn's no-chain-slot arm earned adoption on Burn, not this card pool's impulse/mana chains. | Generic delegate for baseline; measure the optimization later. |

These are recommendations pending orchestrator review/measurement, not adopted changes. Delegating judgement hooks does not change correctness-only certificate stance; retain Midrange's own NotAssessed answer.

## Implemented Islet correction (awaiting build/test)

Implemented at parent request using the smaller `tap_is_life_payment` discriminator while retaining `tap_self_damage` as the numeric tap bill. Islet alone opts in. CardParams/parser, shared source affordability, greedy/manual/DFS payment eligibility, and cache keys now distinguish the life cost. ArmedManaTap deducts it without pain marks; Manabarbs still receives the land-tap mark. Damage-only assignment/deferral optimizers fall back on boards with life-payment sources rather than pricing them as Tamanoa/Purity gain; sweep does not tap Islet for a fictitious damage gain. Tests added to test_prevent_damage.cpp for prevention/triggers, zero/exact life affordability, repeated cache-shaped payments, two-Islet insufficient payment rollback, and Manabarbs. No build or test executed by this agent; parent owns validation. The broader integer tap_life_cost proposal above is superseded by this smaller discriminator implementation.


## Implemented Blaze / fixed-burn targeting (awaiting build/test)

Current Scryfall Blaze oracle and Landfall metadata updated. Shared FixedBurnPin helpers use stable target IDs, carry each legal (player/walker, same-controller creature) pair in the existing tutor_target plan axis, validate targets before casting, and resolve surviving targets independently. Ordinary fixed any-target burn uses the same path for players, creatures and planeswalkers; walker hits remove loyalty through DealPermanentAbilityDamage. The suffix is composed with Fireblast's sacrifice prefix rather than replacing it. Enumeration includes pending hand creatures/walkers so a same-plan cast can supply the target. Human play asks the existing target decision twice, filtering the second set to the first target's controller. Incinerator's target chooser has only opponent-controlled creatures/walkers. Chain Lightning copies now share the target and loyalty resolver; continuation uses the damaged target's actual controller. Focused pair legality, independent survival, human prompts, loyalty and composed-pin tests added to test_prevent_damage.cpp. No build/test run by this agent.


## Provider-owned target-axis adoption scope

`DecisionProvider::SearchesFixedBurnTargets()` explicitly owns the historical narrowing (default false): legacy Any burns target the opposing player, and legacy Multi burns retain the existing opponent-creature / surviving-own-creature prowess target policy. `MidrangeBurnProvider` opts into the complete axis. Human play and `MTG_UNPRUNED` / `MTG_UNPRUNE=fixedburntarget` always expose all legal choices. The legality/resolution helpers remain fully capable; the gate selects provider policy and prevents unrelated decks from silently inheriting a more expensive action fan. This legacy narrowing can miss creature/walker/self-target lines; Midrange does not inherit it. No measurement of the unrestricted axis on unrelated decks was claimed.
