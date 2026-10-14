# Bruna: cast-order draft (2026-10-14)

## 1. Status: **(C)**, an agent-proposed order the USER has never reviewed
`MTG_BRUNA_ORDER` (`BrunaProvider::CastOrderRank`, DecisionProviders.cpp:22985-23016) is default OFF. It was proposed at analyze-deck Stage 4 on 2026-10-05 (analysis-Bruna.md:187-198) and left open with the recommendation "keep generic" (:310; bruna-state-2026-10-06.md:143). No USER ruling on it is recorded anywhere. That proposal measured neutral: d5 b20 gave 4 better / 4 worse over 400 games, and d0 gave 20 better / 11 worse (-0.006). Both runs used the pre-2026-10-06 sideboard. This draft builds on that proposal and **drops the "keep generic" recommendation**: the probes in section 4 show the generic order losing three concrete lines.

## 2. What plays today (generic tiers via DeckProvider, `--cast-order-report`)
| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | first (no cantrip). At depth > 0 it is searched as part of the plan. The engine drops a Karoo (Azorius Chancery) after the casts. |
| 1= | Sol Ring, Wild Growth | m1 | rank 5: rock / ramp land Aura (DecisionProviders.cpp:3142, :3151). **TIE**, so plan order decides |
| 2= | Birds, Avacyn's Pilgrim, Mother of Runes, Somberwald Sage, Bruna (+ wished Linvala, Troyan, Vexing Shusher) | m2* | rank 10, creature. **TIE** |
| 3= | Glittering Wish, Open the Armory, Lightning Greaves, Arcanum Wings, Prodigious Growth, Mythic Proportions, Eldrazi Conscription, Colossification (+ wished Auras, Detention Sphere, regrowths) | m1 | rank 20, other noncreature. **TIE** |

m2* is the baseline. Haste access (Greaves in hand or on the battlefield) moves it to m1. **The m1/m2 column has no effect on Bruna today**: BrunaProvider does not override `ClassifiesMainPhases` (TurnSolver.cpp:20396), so the search tries every cast in both mains. No card in the main deck or the sideboard is `OrderingOpaque` (checked against cards.json params), so the rank sort governs every Bruna cast set, and a tie is settled by plan (hand) order.

## 3. Proposed order
| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | land drop | m1 | - | First. No card in the list draws. Wish and Armory fetch only nonland cards, and at depth > 0 the land is searched together with the tutor target. Karoo stays after the casts (existing behaviour), so the land it returns has already been tapped. |
| 1 | Wild Growth | m1 | - | Ramp. It must enchant an UNTAPPED land and be paid from a different source, so it goes ahead of anything that could tap its host. That includes Sol Ring, whose {1} can then come out of the host's doubled tap (probe P1). |
| 2 | Sol Ring | m1 | - | Producer. Its {C}{C} pays the generic pips that follow (Greaves {2}, Aura generic). |
| 3 | Glittering Wish, Open the Armory (+ wished Auroral Procession, Reborn Hope) | m1 | - | Principle 1: information first, as in the old proposal. m1 because the fetched card must be in hand before combat: Bruna's attack trigger puts a fetched Aura from hand for free; Armory -> Colossification feeds the combat Wings swap; Wish -> Bruna/Linvala + Greaves attacks now. Tie is safe: each fetched card is cast only in the deferred re-solve after the whole plan, and their pools are disjoint (sideboard / library / turn-start graveyard). |
| 4 | Lightning Greaves | m1 | - | **Before every creature (was LAST in the old proposal).** Casting it shrouds nothing, because the equip is a separate trailing action. Being on the battlefield before a fresh dork lets the unlock hoist haste that dork before the creature it pays for (P3). m1 gives haste to this turn's attacker. |
| 5a-d | Birds of Paradise > Avacyn's Pilgrim > Somberwald Sage > Troyan (wished) | m2; m1 with haste access | - | Dorks go before the bodies they pay for (P2). Cheapest first, so an unlocked 1-drop can help pay for a 3-drop. Birds goes before Pilgrim because Birds' any-colour mana pays Pilgrim's {G}, and Pilgrim's {W} cannot pay Birds. Sage goes before Troyan because Sage's creature-only mana pays Troyan, and Troyan's MV>=5-only mana can never pay Sage. |
| 6 | Mother of Runes, Bruna, Linvala, Vexing Shusher (wished) | m2; m1 with haste access | - | After the dorks, before the Auras: an Aura spell needs its host on the battlefield. Tie is safe: none of them makes mana or has an ETB or cast trigger. Bruna is m1 only with Greaves; then she attacks and gathers every Aura in hand, graveyard and battlefield for free. |
| 7 | Arcanum Wings | m1 | - | First Aura, on the attacker before combat, so the in-combat swap can put Colossification in (its tap is harmless on an attacker). Neutral in the engine relative to the payloads: swaps run after every cast (the site-9 trailing pass and combat). |
| 8 | Prodigious Growth, Mythic Proportions, Eldrazi Conscription (+ wished Indrik Umbra, Almost Perfect, Unflinching Courage, Steel of the Godhead; Detention Sphere) | m1 | - | Onto this turn's attacker. Tie is safe: each targets a creature already on the battlefield, and none changes another's legality or cost. Almost Perfect sets base P/T (layer 7b) underneath every +N/+N (7c). Steel reads the host's colour, which nothing here changes. Sphere is a no-op. When Bruna attacks this turn, casting any of these is dominated by her free put from hand. |
| 9 | Colossification | **m2 (cast)** | - | Last. Its ETB tap cannot affect the other casts: a tapped creature is still a legal Aura target, and a mana host keeps its mana through the pending ETB tap. A main-1 CAST onto a would-be attacker costs that attack (scenario `bruna_colossification_cast_taps_attacker`). Its main-1 routes are PUTS (Bruna's gather, the combat swap), not casts. |

## 4. What changes and why it matters
These are single-game probes. Scenarios are in `build/scratch/castorder/probe_Bruna/scen/`, run from the repo root via `probe_Bruna/run_scen.sh`, so cards.json loaded: games were won and every cast is logged. The tie falls to plan order, which these probes show follows hand order.
1. **Wild Growth before Sol Ring** (today a tie at 5). **P1:** T2, two Forests, hand Sol Ring, Wild Growth, Birds, Greaves. With Sol Ring listed first, both d0 and d3/b200 cast Sol Ring + Birds + Greaves and leave Wild Growth in hand. With Wild Growth listed first, all four are cast. The line is lost to an order nobody chose.
2. **Dorks before the creatures they pay for** (today a tie at 10). **P2:** T5, Greaves on the battlefield, 4 Forests + Botanical Sanctum + Birds (one W source), hand Bruna, Sage, Colossification, Conscription. With Bruna listed first, the engine casts Sage + equip and delays Bruna to T6. With Sage listed first it casts Sage, unlocks it, and lands Bruna on T5 off Sage's WWW.
3. **Greaves before the creatures, not after them (generic) or last (old proposal).** **P3:** T6, Sol Ring + 4 Forests + Botanical + Birds, hand Sage, Bruna, Greaves, Colossification, Conscription. Generic and `MTG_BRUNA_ORDER=1` both play Sage + Greaves on T6 and Bruna on T7, even at d3 b0. The unlock can only fire after Greaves is on the battlefield, and both orders cast Bruna before Greaves. The proposed slot reproduces P2's Sage-first state, which predicts Bruna on T6. This is predicted, not measured (no build). The old "Greaves last" reason (shroud stops later Aura spells) is already handled where shroud really comes from, the equip: equips trail the casts, the shroud release fires before the Aura, and an unlock onto an Aura target is deferred (sweep B, `aee1aa79` / `7414e9c8`).
4. **Colossification cast moves to m2** (the report marks it m1). Play does not change today, because the column is advisory and the attack projection already prices the tap. It is the label the USER reviews, and if the filter is adopted it becomes a sound collapse of a dominated pre-combat cast.
5. **Tutors move from 20 to slot 3.** No engine effect: `MTG_ACQ_RESOLVE` defers the fetch's re-solve until after all of the plan's casts. This placement is kept from the old proposal to follow principle 1.

Not measured as a whole. The order needs the usual held-out A/B, every deck net <= 0, before adoption.

## 5. Questions for the USER
1. Move Lightning Greaves from LAST to **before every creature**? The shroud concern lives in the equip, which already trails the casts. Default: **yes**.
2. Cast Colossification only in **main 2**? Its pre-combat value comes only from puts: Bruna's gather and the Wings combat swap. Default: **yes**.
3. Opt Bruna into the main-phase filter (`ClassifiesMainPhases`) so the m1/m2 column actually applies? A Main2 class moves a cast to main 2; it never deletes it. Default: **yes, after the A/B**.
4. Tutors right after the mana producers (principle 1), knowing it changes nothing in the engine today? Default: **yes**.
5. Apply generic findings G1-G3 (below) to the generic tiers, or to Bruna only? Default: **Bruna only now; generic after a suite A/B**.

## 6. Generic-order findings
* **G1. A land Aura and a rock tie at 5** (DecisionProviders.cpp:3142 / :3151), so plan order decides. The land Aura should come first: its host must stay untapped and cannot pay for its own Aura, while the rock's generic cost can come from the doubled host tap (P1). Touches Fungus. EldraziDisplacerFlicker has its own order. The open ledger question about `MTG_LAND_AURA_RAMP_FIRST` going generic (analysis-Bruna.md:379) is still unanswered.
* **G2. Mana dorks tie with every creature at 10.** A dork the unlock hoist can haste must precede the creatures it pays for (P2). Suggested fix: a dork sub-tier just ahead of 10. `CastCheapestFirstWithinTier` covers only `IsManaRitual`, so dorks never get it.
* **G3. A haste Equipment at 20 sits after every creature.** When the Equipment is cast in the same turn, a creature-paying unlock cannot be expressed (P3). Measure the fix on the Greaves decks: FiveColour, Angels, Giants, Goblins, Dragons, Mirrorwing.
* **G4. An `aura_etb_tap_host` Aura is classified m1,** but a cast onto a would-be attacker costs that attack. It should be Main2 for casts.
* **G5. A `tutor_to_hand` card's rank carries no information effect,** because the re-solve is deferred until after the plan. "Tutor first" stays cosmetic until a tutor re-solves inline.

## 7. Implementation notes
* Replace the body of `BrunaProvider::CastOrderRank` behind the existing (never-adopted) `MTG_BRUNA_ORDER`, still default OFF until the USER agrees. Param-only, checked in this order:
  * `is_land_aura && land_aura_extra_mana>0` -> 3
  * `mana_rock && !creature` -> 5
  * `tutor_to_hand` -> 6
  * `is_equipment && equip_grants_haste` -> 7
  * creature with `!produces.empty()` (dork) -> 8-11, by: MV ascending; then unrestricted 5-colour before 1-colour; then `creature_mana_only` before `mana_only_spell_min_mv`. These need distinct ranks, since there is no within-tier key for non-rituals.
  * other creature -> 12
  * `aura_swap_cost` -> 15
  * `aura_etb_tap_host` -> 22
  * `is_aura` -> 20
  * anything else -> 20
  * Update `CastOrderTierName` to match.
* m1/m2: add `MainPhaseOverride` returning Main2 for `aura_etb_tap_host`. Dorks and bodies keep the generic classifier, which already applies the haste pull through `HasteAccessThisTurn`. All of this is inert until `ClassifiesMainPhases()` is true (question 3).
* No range is proposed. One might look necessary for Greaves (early for the unlock, late for payment), but it is not: the batch prepay settles payment jointly, and when it declines on an unlock plan, `ManaUnlockColorReserve` holds the payoff's colour.
* **ENGINE LIMITATION (a defect under this deck's standing rule).** A plan carries one Equip per Equipment ("mutually exclusive per plan", Greaves card note). So the legal line "equip Greaves -> fresh Sage, tap for WWW, cast Bruna, re-equip Greaves -> Bruna ({0}, sorcery speed, repeatable), attack and gather" cannot be found at any budget. In P2 (Sage first) at d3 **b0**, the engine re-points Greaves only in main 2 and wins on T6; real play wins on T5 (5+20+10 vs 20). This caps what slots 4-5 can deliver: today the unlock lands Bruna a turn early, but she cannot attack that turn.
