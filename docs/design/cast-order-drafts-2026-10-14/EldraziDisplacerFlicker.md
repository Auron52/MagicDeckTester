# EldraziDisplacerFlicker -- cast-order draft (2026-10-14)

Provider `EldraziFlicker` (`EldraziFlickerProvider::CastOrderRank`, src/ai/DecisionProviders.cpp:37468).
Decklist decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod. Evidence: card data from cards.json,
three single-turn scenario probes (depth 3, budget 0, under a second each), and the USER's own sequencing
mined from all 24 hand-played references (`build/scratch/castorder/edf_ref_orders.py`; only pairs that
span SEPARATE decisions count, since a plan's action list is not its execution order).

## 1. Status: **C**, an agent-built order the USER has never reviewed as a whole, with two USER-shaped parts inside it

No USER order exists, and none is OFF.

* **The land-Aura sub-order (-990..-974) IS USER-directed.** Score = 8*mv + 2*strict pips + hybrid pips - 1000,
  which gives Wild Growth -990, Fertile Ground -982, Trace of Abundance -981, Overgrowth -974. It ships under
  `MTG_EDF_AURA_CHEAP_FIRST` (default ON). USER, 2026-09-08 (commit c3103a1e, quoted at
  DecisionProviders.cpp:37503/37516): *"Trace should probably go before overgrowth. Both because of the
  higher mana cost and because overgrowth is not very good at paying the cost for trace"* and *"Same with
  the easier to cast ones. So Fertile Ground before Trace of Abundance."*
* **Ranks 5 / 6 / 7 are agent-built.** Untappers are at 5, Training Grounds at 6, outlets at 7. The report
  prints them as "(unclassified)", and it labels 5 "MANA ROCK", which is wrong. Payload-before-reducer
  (`MTG_EDF_PAYLOAD_FIRST`, ON, 2026-09-15) comes from the USER's own played lines (s12_gi11 T4 and
  s6_gi5 T4; analysis-EldraziDisplacerFlicker.md:7663). It was adopted under the "references first" brief
  and was never shown to the USER as an order.
* **Main phase is USER-ruled: single main.** USER 2026-09-08 (analysis-EDF.md:3913): *"There should be no
  need to use multiple mains"* and *"The more important thing is that we don't have extra mains to
  search."* `MTG_EDF_M2` is OFF and `uses_second_main=no`, so the report's m2 on every card has no effect.
* `docs/design/cast-order-rankings.md` (EDF section) still says "No implemented non-land cards". That is stale.

## 2. What plays today

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| 0 | land drop | m1 | first at depth 0, searched (folded into the plan) at depth > 0. A land fetched by Living Wish takes the deferred drop. |
| 1 | Wild Growth (-990) | m1 | `is_land_aura`, cheapest first (USER) |
| 2 | Fertile Ground (-982) | m1 | same |
| 3 | Trace of Abundance (-981) | m1 | same; shroud is no longer pinned last (USER) |
| 4 | Overgrowth (-974) | m1 | same |
| 5 | **Cloud of Faeries = Peregrine Drake (TIE)** | m1 | `etb_untap_lands > 0`. The tie goes to HAND order (`CastOrderLessRanked` only breaks ties cheapest-first between two rituals). |
| 6 | Training Grounds | m1 | `reduces_creature_activation` |
| 7 | **Eldrazi Displacer = Emiel the Blessed (TIE)** | m1 | `blink_cost`; hand order |
| 10 | Essence Depleter, Dimensional Infiltrator, Vexing Shusher (sideboard, via Wish) | m1 | generic creature |
| 20 | **Living Wish = Eladamri's Call (TIE)** | m1 | generic noncreature. `tutor_to_hand` arms the post-cast re-solve (`MTG_ACQ_RESOLVE`), so the fetched card is cast AFTER the plan. |

No EDF card is ORDER-OPAQUE (none has `draw`, staging, cascade and so on), so the rank sort is real here. **But
on a go-off turn the rank is not what plays.** The COMBO OFF route (`EdfComboRouteTrial` -> `DeployPieces`,
DecisionProviders.cpp:29759; the USER's 2026-09-16 spec) casts in its own order:

1. the land (a {C} source or a draw land first)
2. the land Auras, **the any-colour ones first, then hand order**
3. the untappers, cheapest first, with tap-ahead
4. Training Grounds, if it and the outlet are both payable
5. the outlet (Emiel when there is at most one {C} source)
6. then bank, dig, Wish, finish

The rank governs development turns and every turn the route does not fire on.

## 3. Proposed order (single main: every cast is m1)

| pos | card | main | range | why |
|---|---|---|---|---|
| 0 | land drop | m1 | - | The new land is an Aura host. A land that enters tapped (Conservatory, Kitchen, rad Mariposa, Chancery) is one an untapper's ETB can then untap. No draw spell wants to come first. A Wish for a land keeps the deferred drop, as the USER played s4 T3 and s7 T4. |
| 1 | Wild Growth | m1 | - | {G} for an extra {G}, so it costs nothing net on an untapped host and never needs to move. It is the cheapest funder (USER). |
| 2 | Fertile Ground | m1 | [2 .. after Drake] | USER: easier to cast than Trace. It costs 1 mana net this turn (2 in, 1 back). |
| 3 | Trace of Abundance | m1 | [3 .. after Drake] | USER: before Overgrowth. **Set rule: goes after any other Aura in the plan that shares its host** (shroud, CR 303.4a / 702.18a). |
| 4 | Overgrowth | m1 | [4 .. after Drake] | Dearest Aura, gives only GG. Costs 1 mana net this turn. |
| 5 | Cloud of Faeries | m1 | - | **Cheapest untapper first.** Under the tap-ahead an untapper refunds N whole lands, so it never loses mana, and casting the cheap one first lowers what the next cast needs. In all 4 reference turns that cast both, the USER cast Cloud first (s1 T3, s6 T4, s8 T3, s11 T6). The only Drake-then-Cloud pairs are a second Cloud recast mid-loop (s1 T3). |
| 6 | Peregrine Drake | m1 | - | Its 5-land refund pays for TG and the outlet (payload first; USER s12 T4, s6 T4). |
| 7 | Training Grounds | m1 | - | It discounts ACTIVATIONS, and every activation comes after the casts, so its slot only affects payment. It sits after the untappers so their refund pays it (USER s6 T4: Cloud, Drake, Drake, Call, TG, Displacer). |
| 8 | Eldrazi Displacer | m1 | - | The outlet, after what funds it. Order against Emiel affects only payment. Pinned by mana value. |
| 9 | Emiel the Blessed | m1 | - | Her {G/W} counter trigger is always declined (USER 2026-09-30), so casting her before or after Displacer changes nothing on the board. The USER cast Displacer first (s4 T4, s12 T4). |
| 10 | Essence Depleter, Dimensional Infiltrator, Vexing Shusher | m1 | - | Finishers: their drains and exiles are activations after the casts. Shusher is never wanted. |
| 11 | Living Wish | m1 | - | The fetched card is cast in the re-solve after the plan, so the Wish's own slot affects only payment. The search can still cast something AFTER the Wish through the continuation (`MTG_BP_NEW_ONLY` keeps any continuation that uses the fetched card). Auras before it, as the USER played s1 T3: Trace, Wish -> Cloud, Cloud. |
| 12 | Eladamri's Call | m1 | - | Same reasoning. It searches a different zone, so the two tutors do not interact. After the Wish, as the USER played s6 T4. |

Every remaining tie is between copies of one card. The pins at 8/9 and 11/12 are provably payment-only;
they are fixed for determinism and to match the USER's play.

## 4. What changes and why it matters

1. **Cloud before Drake (the rank-5 tie).** This costs a kill. Probe
   `probe_EldraziDisplacerFlicker/t4_{cloud,drake}_first.json`: Yavimaya Coast + Wild Growth, Kitchen and
   Adarkar Wastes (4 mana), with Displacer and Essence Depleter in play, and {Cloud, Drake} in hand.
   * Hand [Cloud, Drake]: **T4 win.** Cloud nets +1 off the Aura'd land, bringing mana to 5, then the Drake
     resolves. The Drake loop nets +1 per blink, where Cloud's alone nets 0.
   * Hand [Drake, Cloud]: **no T4 win.** The walk prices the Drake first, has 4 of the 5 it needs, and drops
     the only kill line. That happens at every budget, with the route ON and OFF. The route does not rescue
     it: it casts Cloud, then `skip Peregrine Drake: refund 4 < cost`.
2. **Ranges on the three Auras that cost mana net this turn** (Fertile Ground, Trace, Overgrowth). Probe
   `aura_drake.json`: 5 untapped lands, {Fertile Ground, Drake} in hand. `MTG_DBG_SEQ` shows the pair walked
   Fertile Ground first and failing every time (28 times); Drake first pays. So {Aura, Drake} on a 5-mana
   board can never be found. Today that costs tempo (the Aura lands a turn late); it costs a kill whenever
   the kill needs both. The USER's 24 references never hit it (Auras came first in every turn before the
   loop), so this is lower priority than item 1, and it needs engine work (section 7).
3. **Overgrowth + Trace on ONE land in one turn.** Today that is impossible: Trace (-981) resolves first
   and shrouds the land. The USER accepted this trade on 2026-09-08 because the rank cannot see hosts. A
   host-aware rule at the level of the whole cast set restores the stack and keeps Trace ahead of
   Overgrowth everywhere else. It is pure expressibility: the only order it changes is one that is illegal
   today. The USER does stack Auras on one land (s11 T3, s1 T3).
4. **The go-off route does not use the USER's Aura order.** It casts any-colour Auras first, then by hand
   order, so on the turns that matter most the deck plays an order nobody chose (Q4).
5. **The ties at 7 and 20 are pinned.** No behaviour change is claimed: they are payment-only, and the pins
   match the USER's play.

## 5. Questions for the USER

1. **Cloud of Faeries before Peregrine Drake** (cheapest untapper first, as you played it every time)? Default: **yes**.
2. **Fertile Ground / Trace / Overgrowth get a range**: cast first as now, and moved behind the untappers
   only when the turn cannot otherwise be paid? Wild Growth costs nothing net, so it keeps its fixed slot.
   Default: **yes** (it needs the section 7 engine change).
3. **Restore the same-land Overgrowth-then-Trace stack** (Trace goes last only when it shares a land with
   another Aura in the plan)? Default: **yes**.
4. **Should the COMBO OFF route deploy Auras in this same order** (Wild Growth, Fertile Ground, Trace,
   Overgrowth) instead of any-colour first? Default: **yes**, behind a flag, measured on the 24 references.

## 6. Generic-order findings

* **A rank tie between two casts that produce mana goes to hand order.** On EDF that decided whether a T4
  kill existed. `CastOrderLessRanked` (ManaPayment.cpp:2645) breaks ties cheapest-first only between two
  rituals (`IsManaRitual`). An `etb_untap_lands` creature is the same kind of cast: under the tap-ahead it
  gets back at least what it costs. Extend the tie-break to it. Do not extend it to every creature: the
  Scourge ETB-order lesson stands.
* **The payable-order fallback cannot see an untapper's refund or a land Aura's bonus.** `FirstUnpayablePos`
  and `ApplyPayableCastOrderAt` (`MTG_PAYABLE_ORDER`) project against `AvailableManaPool` with neither
  credited, so they cannot rescue item 1 or item 2. The one rescue that does handle this, the
  untapper-hoisted retry (TurnSolver.cpp:7899-7918, `MTG_SEQ_UNTAPPER_FIRST`), is `HumanPlayActive`-only.
  So autonomous play keeps a prune the human path already proved wrong (s12_gi11 T4, 2026-09-10).
* **The `--cast-order-report` misleads on a provider deck.**
  * Provider ranks print as "(unclassified)" or under a borrowed generic label (EDF's untappers at 5 read
    "MANA ROCK").
  * A single-main deck shows m2 on every card.
  * Ties are not flagged.
  * The combo route's go-off order is not shown at all.
  * Suggested fixes: a provider `CastOrderReason(state, def)` hook, a "single main -> all m1" banner, a tie
    marker, and a note when `ComboRouteEnabled()`.

## 7. Implementation notes

* **Slots by params** (no names):
  * Auras: `is_land_aura`, scored as today; the shroud rule keys on `land_aura_grants_shroud`.
  * Untappers: `etb_untap_lands > 0`, ordered by mana value within the tier.
  * Training Grounds: `reduces_creature_activation`.
  * Outlets: `blink_cost`, ordered by mana value.
  * Tutors: `tutor_to_hand`, broken by the Aura score's strict-pip term (Wish {1}{G} = 1 before Call
    {G}{W} = 2).
* **Q1 (code change in `CastOrderRank`):**
  * Untappers become 3..5 by mana value (Cloud 3, Drake 5); Training Grounds stays 6; outlets become 7..8
    (Displacer 7, Emiel 8).
  * Finishers stay at the generic creature 10. Tutors become 20 / 21.
  * Keep `MTG_EDF_PAYLOAD_FIRST=0` meaning "reducer first" (the reducer takes 2).
* **Q3:** add `EldraziFlickerProvider::ArrangeCastOrder`, a stable move that puts a
  `land_aura_grants_shroud` Aura behind every other land Aura in the set with the same `enchant_target`.
  * Both applies and `SubsetPayableSequential` (TurnSolver.cpp:7614) already call it, so the executor and
    the rollout stay in lockstep by construction.
  * `CastOrderRank` gets no host, so this cannot be done with a rank.
  * Check that the shared land is actually offered to both Auras (`LandAuraHostCandidates` keeps the best
    two hosts, read from the pre-plan board). The fixture `test/scenarios/edf_shroud_blocks_second_aura.json`
    is the place to extend.
* **Q2 is an engine change, not a rank change:**
  * (a) The order projection must credit an `etb_untap_lands` cast with the yield of its N best lands
    (exact up to colour, given the tap-ahead), and a land Aura on an untapped host with
    `land_aura_extra_mana`.
  * (b) `ApplyPayableCastOrder[At]`'s domain must admit an order that holds an untapper or a land Aura.
  * Or: make the untapper-hoisted retry autonomous, with an EDF `ArrangeCastOrder` doing the hoist when
    that projection says the rank order fails.
  * The `MTG_ORDER_RANGE` ladder cannot carry it as it stands: it has the same projection gap, and
    `LadderProjectable` does not decline untappers, so it would misjudge them rather than skip them.
* **Q4:** in `DeployPieces` step 2 (DecisionProviders.cpp:29792), pick the Aura with the lowest
  `CastOrderRank` instead of `land_aura_produces.empty()`-first.
* **Side finding (route, not order):** `DeployPieces` step 3 (DecisionProviders.cpp:29846) skips a second
  untapper whose refund on the cast is less than its cost, even when that untapper's LOOP is the
  net-positive one. On the t4 probe the Drake nets +1 per blink against Cloud's 0, so the route misses the
  kill the search finds once Cloud is first in the order.
