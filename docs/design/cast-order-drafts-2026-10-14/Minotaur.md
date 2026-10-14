# Minotaur: cast-order draft (2026-10-14)

## 1. Status: (D) generic tiers only

* `MinotaurProvider` (DecisionProviders.h:2771) derives from `DeckProvider` -> `GenericProvider` and overrides
  only `CleanupDiscardCandidates` and `Certificate`. There is no `CastOrderRank`, no `MainPhaseOverride` and no
  `MTG_MINOTAUR_*ORDER` flag.
* There is no USER order and no USER ruling on cast order: analysis-Minotaur.md and
  minotaur-discard-policy-proposal.md do not mention one. The deck's section of cast-order-rankings.md is stale.
  It prints `provider: AntiLifegain` and lists only Aether Vial.
* **Rank 8 "(unclassified)" is Ragemonger's tier**, the generic SUBTYPE-COST-REDUCER tier.
  * `GenericProvider::CastOrderRank` returns 8 for a coloured-pip subtype reducer
    (`reduces_subtype_colored_subtype` + `reduces_subtype_colored_cost`, DecisionProviders.cpp:3093). Goblin
    Warchief's `reduces_spell_subtype` (:3157) also gets 8.
  * An agent added the tier in 816b96e1 (2026-08-28) after USER viewer rejections s3/gi2 and s4/gi3. The USER
    never reviewed it as an order.
  * It shows "(unclassified)" because the report's tier-name table (src/main.cpp:7888-7904) has no `case 8`
    (and no `case 9`).
* Order-opaque check: no card in this list is a draw, staging, cascade, retrace, impulse or solo-target card.
  So every set is rank-sorted, and the report's ranks are what plays.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | the land drop | m1 | Played before every cast (`fold_land`). A Rakdos Carnarium is held until after the casts (`karoo_deferred`). A Bloodstained Mire is played and cracked at the drop. |
| 1 | Ragemonger | m1* | rank 8, subtype cost reducer |
| 2 | **TIE:** Gnarled Scarhide, Burning-Fist, Deathbellow Raider, Slaughter-Priest, Boros Reckoner, Neheb, Rageblood Shaman, **Fanatic of Mogis**, Kragma Warcaller, **Sethron** | m1* | rank 10, creature. The tie is broken by HAND order: `CastOrderLessRanked` (ManaPayment.cpp:2660) breaks ties cheapest-first only between two mana rituals. The report's mv/name sort is display only. |
| 3 | Aether Vial; Gnarled Scarhide (Bestowed) | m1* | rank 20, other noncreature. The bestow face is an Aura, so it gets 20; it is not in the report. |

* Aether Vial puts are sequenced at their card's rank, like casts (TurnSolver.cpp:42611, USER 2026-10-06).
* \*The report labels six bodies and Ragemonger "m2", but the column is inert. The deck has
  `uses_second_main=no`, so `MainPhaseFilterActive` returns false (TurnSolver.cpp:20388) and nothing is ever
  deferred. `MTG_ORDER_M1_FIRST` is off, so the label does not break ties either.
* Probe: 320 games at the shipped play setting had 0 main-2 actions.

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | First. The deck has no draws, so nothing wants to precede it. Carnarium stays after the casts (engine, correct: "return a land you control" should bounce a spent land). |
| 1 (8) | Ragemonger | m1 | - | "Minotaur spells you cast cost {B}{R} less". It goes before every Minotaur it discounts (principle 2), including Sethron. The enumerator's same-turn credit already assumes this order (`SameTurnColoredReducerPipCredit`). A tie between two Ragemongers is a tie between identical cards. |
| 2 (9) | Sethron, Hurloon General | m1 | - | "Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 Minotaur." Every Minotaur cast or Vial-put after it brings a token. It goes after Ragemonger because Ragemonger takes {R} off it, and every observed same-turn pair could only pay reducer-first. |
| 3 (10, tie) | Kragma Warcaller, Rageblood Shaman, Neheb, Boros Reckoner, Slaughter-Priest, Burning-Fist, Deathbellow Raider, Gnarled Scarhide (creature) | m1 | - | A true tie. Every ability in this group is a static (haste for Minotaurs, +1/+1 lord, the "one or fewer cards in hand" anthem) or a combat/sacrifice trigger. None reads casts or entries, and none changes another card's cost, so in a one-main turn the order cannot change the outcome. Colour allocation belongs to the whole-turn prepay (`BatchPrepayMainCasts`), not to the order. |
| 4 (12) | Fanatic of Mogis | m1 | - | "When this enters, it deals damage to each opponent equal to your devotion to red." It goes after every red pip that enters this turn (Boros Reckoner 3, Rageblood and Sethron 2, every other Minotaur 1). It also lands after Sethron, so it brings a token. |
| 5 (20) | Gnarled Scarhide (Bestowed) | m1 | - | Aura, +2/+1. It goes after the creatures so it can enchant one that Kragma hastes this turn. It has no red pip. |
| 5 (20, tie) | Aether Vial | m1 | - | It enters with 0 counters, so it cannot deploy the turn it is cast. Its cost is a generic {1} with no pips, so its position cannot matter. The tie with the bestow face is order-free. |

Vial puts take the same slots automatically. A Vialed Ragemonger stays first, a Vialed Fanatic stays last, and a
body Vialed in after Sethron makes a token.

## 4. What changes and why it matters

1. **Fanatic moves last. This change costs real turns.**
   * Seed 9005 / game-index 5 (shipped play setting), T4: the engine plays Cavern, Vials in Ragemonger, casts
     Fanatic for {3}, then Neheb for {1}. Fanatic deals 5 and the opponent ends combat at **1**.
   * With Neheb first, Fanatic deals 6 and the 12-damage swing is lethal **on T4, not T5**. The same game with
     `MTG_SEARCH_ORDER=1` casts Neheb then Fanatic and wins T4.
   * Today's order makes that kill unfindable at any budget, because both cards are rank 10 and the tie falls
     to hand order.
   * Fanatic entered before a red permanent on 13 turns in 320 probe games, losing 1-3 damage each time
     (examples: s1001 gi48 T4, a Vialed Rageblood after Fanatic, -2; s12000 gi25 T4, -3).
2. **Sethron moves ahead of the bodies.**
   * Seed 12095 / gi 95, T4: Rageblood is Vialed in before Sethron is cast, so one 2/3 token is missed.
   * With a Kragma Warcaller in play, each missed token is a hasty attacker for 4 or more (2 power, +2 from the
     Kragma trigger, +1 per Rageblood).
   * Today this depends on hand order. 1 of the 6 observed Sethron-after-a-Minotaur turns was fixable.
3. **Ragemonger stays ahead of Sethron, now on purpose.**
   * The other 5 of those 6 turns were Ragemonger, then Sethron, and in every one reducer-first was the only
     payable order. Example: s1001 gi40 T4 has 4 mana; Ragemonger {1} plus Sethron {3} is 4, while Sethron
     first is 4+1=5.
   * So a "Sethron before everything" rank would lose those casts. That is why Sethron goes at 9 and not
     below 8.
4. **The tier-10 tie becomes a declared tie.** Hand order still breaks it, but this draft states why the order
   inside it cannot matter, instead of leaving a tie nobody chose.
5. **Land drop: no change, with one line it cannot express.**
   * A Mire cracked at the drop cannot pump a Slaughter-Priest cast later that turn ("whenever you sacrifice a
     permanent, +2/+0"). It only matters when that fresh Priest can attack (Kragma, or Sethron's haste
     activation).
   * 19 fetch-before-Priest turns in 320 games, none with a hasty Priest, so 0 damage lost. Engine limitation,
     see section 7.

## 5. Questions for the USER

1. **Sethron right after Ragemonger and before every other Minotaur** (cast or Vialed)? Default: **yes**.
2. **Fanatic of Mogis last among the creatures**, so its devotion sees every red pip that entered this turn?
   Default: **yes**.
3. **Ragemonger before Sethron always**, even when one mana is spare? Sethron first would trade 1 mana for 1
   extra token, which needs about 8 mana in one turn; every observed case was mana-forced. Default: **always**.
4. **Fetch after a fresh, hasty Slaughter-Priest**: this needs an engine slot to delay the crack. Park it as a
   design note? Default: **park** (0 of 19 observed cases).
5. **Ship it ON on your approval** (with the usual scoped Minotaur A/B, net <= 0, as input), or measure first?
   Default: **A/B first, then you decide**.

## 6. Generic-order findings

* **The report has no label for ranks 8 and 9** (main.cpp tier table), so Ragemonger and Warchief show as
  "(unclassified)". Suggested labels:
  * 8: "SUBTYPE COST REDUCER: before the creatures it discounts".
  * 9: the watcher / dork-growth / Vial tiers.
* **There is no generic "watcher before the creatures it watches" tier for token engines.** `MTG_WATCHER_ORDER`
  (default OFF, :3181) covers only `dragon_ping_on_enter` and `haste_on_flying_enter`. Sethron (and Lathliss)
  carry `etb_other_subtype_creates_tokens`. The put-order code already ranks that param first
  (`ClassifyEtbOrder`, :10769), but casts never use it. That param belongs in the watcher tier; it would need a
  measurement on the Lathliss decks.
* **There is no generic "board-counting ETB payoff goes last" tier.** `etb_damage_devotion_color` reads the
  board at ETB, so it wants every permanent it counts in play first. Generic tier 10 ties it with its own
  enablers, so hand order decides. The same holds for any "ETB: X = creatures/devotion" card.
* **A stale comment.** The tier-8 comment says "the tier-10 tiebreak is cheapest-first". That was true when it
  was written (816b96e1); ties now fall to plan order (ManaPayment.cpp:2660). The tier's reason still holds.
* **The m1/m2 column on a single-main deck reports "m2" for bodies that can never be deferred.** When
  `uses_second_main=no` the report should print m1, or say the column is inert. The USER reviews this artifact.
* **A side note on principle 2.** The colour reducer (`reduces_spell_color`, rank 16) sits AFTER creatures
  (10). A creature deck with a Ruby Medallion would cast its red creatures before the Medallion. The subtype
  reducer (8) gets this right, and the colour reducer does not.

## 7. Implementation notes

* Add `MinotaurProvider::CastOrderRank`, ranking by params, behind `MTG_MINOTAUR_CAST_ORDER` (`EnvOn`, `=0`
  reverts):
  * `reduces_subtype_colored_*` -> 8 (unchanged).
  * creature with `etb_other_subtype_creates_tokens` -> 9.
  * `!etb_damage_devotion_color.empty()` -> 12.
  * Everything else uses `GenericProvider::CastOrderRank` (creatures 10, the bestow Aura face and Vial 20).
* Add `CastOrderTierName` for 8, 9 and 12, and a `MainPhaseOverride` returning Main1 for every cast. The
  override changes only the report; play is unchanged because the filter is already off.
* The executor and the rollout share `CastOrderLess`, and Vial puts take their card's rank. So the new ranks
  reach the cast loop, the rollout and Vial deployment with no other code.
* Keep Sethron above 8. If it ranked below Ragemonger, `SameTurnColoredReducerPipCredit` would still price the
  plan as reducer-first, and the apply would drop casts.
* No ranges are needed. Every observed Ragemonger/Sethron conflict was mana-forced, and nothing else here
  changes cost with position.
* **Engine limitation:** a fetch is played and cracked as one step at the land drop (AIEngine.cpp `fold_land`
  / `PerformFetch`). The line "cast Priest, then crack the Mire" cannot be expressed. Fixing it would need a
  deferred-crack slot like `karoo_deferred`. Low value.
* **A possible rules bug to verify against mtg-rules (not order).** The bestow face copies the card's creature
  subtypes (CardDatabase.cpp:214), although its comment says the types are dropped. So Ragemonger's "Minotaur
  spells" discount applies to a bestow cast ({3}{B} -> {3}). Under CR 702.103b a bestowed spell is an Aura
  spell, not a creature spell, and so it is probably not a Minotaur spell.
* **Probes:** about 320 single games (`--games 1 --threads 1`, `--cards-json` absolute, value_play setting),
  logged under build/scratch/castorder/probe_Minotaur/. The scan script is probe_minotaur_scan.py.
