# Soldiers: cast-order draft (2026-10-14)

Deck: `decks/Soldiers/soldiers.cod`. It is W/x Human Soldiers on Aether Vial, and every creature in it is a Human Soldier.
Provider: `SoldiersProvider : VialProvider`. Single main phase (`uses_second_main=no`).

## 1. Status: (C), an agent-proposed order the USER has never reviewed

- `MTG_SOLDIERS_ORDER` is an agent-authored total order (commit aab6e343, 2026-10-04). Its default is OFF:
  `EnvOn("MTG_SOLDIERS_ORDER")` at `src/ai/DecisionProviders.cpp:8968`.
- It was put to the USER as a question in `docs/design/cast-order-rankings.md:1208` and
  `docs/design/analysis-soldiers.md:1431` (question A, "Default taken: OFF"). No USER answer is recorded anywhere.
- Two places call it the "user-proposed Soldiers order": `docs/design/vial-put-interleaving.md:42` and the comment in
  `test/scenarios/soldiers_vial_after_tutor_continuation.json`. That label is wrong. The USER never authored or reviewed it.
- No USER order exists for this deck, so nothing is OFF that the USER asked for.

## 2. What plays today (flag OFF, which means generic tiers)

| pos | card(s) | main | rule |
|---|---|---|---|
| land | the land drop | m1 | Played first. The deck has no cantrip. |
| 10 (TIE x16) | every creature | m1 | Generic "creature 10". The tie falls to plan-vector order, so nobody chose this order. |
| 20 | Aether Vial | m1 | Generic "other noncreature 20". |

- Since 2026-10-06 (commit 7173178f9, a USER ruling), an Aether Vial put is sequenced at its creature's rank. The 16-way
  tie therefore also decides where each put goes.
- The report labels Esper Sentinel, Cathar Commando and Thalia as m2. Those labels are inert: there is no second main.

## 3. Proposed order: the 10-04 order with one amendment (Thalia's Lieutenant moves from 17 to 4)

The land drop goes FIRST, before every cast:
- The deck has no draw spell.
- Fortified Beachhead enters untapped if you reveal a Soldier card from hand, and every creature in the 60 is a Soldier.
  So dropping it before the casts never costs the untapped entry.
- The 60's only draw is Silent Clearing's `{1},{T},sac: draw`. That is a land ability. It runs in the trailing dig loop
  under the generic dig gate, which the cast order does not reach.

| pos | card | main | range | why |
|---|---|---|---|---|
| 1 | Champion of the Parish | m1 | - | "Whenever another Human you control enters, put a +1/+1 counter." Every later Human this turn is +1, whether cast or Vial-put. |
| 2 | Recruiter of the Guard | m1 | - | Its ETB tutor (toughness <=2) can find Champion or Lieutenant. Early, so the fetched card joins the continuation at its own rank and a fetched Champion still precedes this turn's Humans. |
| 3 | Ranger-Captain of Eos | m1 | - | ETB tutor for MV <=1 (Champion, Sentinel, Officer). Same reason. Its order against Recruiter is free (see the note below). |
| 4 | Thalia's Lieutenant | m1 | - | AFTER Champion: Champion before Lieutenant is worth +1 (Lieutenant's ETB counters Champion, and Champion's watcher also fires). AFTER the tutors, so a fetched Champion precedes it. Placement among the other Humans: see §4.4. |
| 5 | Aether Vial | m1 | - | The deck's ONLY noncreature spell. It goes before Thalia, because "Noncreature spells cost {1} more" binds our own Vial. |
| 6 | Recruitment Officer | m1 | - | Free. Its `{3}{W}` dig is a trailing activation whatever its cast rank. |
| 7 | Esper Sentinel | m1 | - | Free (its trigger is opponent-facing, so inert). |
| 8 | Thalia, Guardian of Thraben | m1 | - | After Vial. Otherwise free. |
| 9-13 | Coppercoat Vanguard, Cathar Commando, Jirina, Harbin, Brutal Cathar | m1 | - | Free. |
| 14-17 | Field Marshal, General Kudro, King Darien XLVIII, Rick | m1 | - | Free (static anthems). |

**Why slots 6-17 are free.**
- None of them has an enter-trigger that reads other entrants, a cost effect (Thalia's tax only touches Vial), or a
  within-turn order dependency:
  - Static lords and Rick's conditional anthem are continuous (CR 611.3) and use counts, not order.
  - Harbin's five-Soldier pump is read at declare-attackers.
  - Brutal Cathar's daybound uses the spell count, and its face is fixed at entry.
  - Kudro's and Jirina's ETBs, and Sentinel's trigger, are inert.
- Everything cast this turn is summoning sick, so where this turn's counters land cannot change this turn's damage.
- Recruiter against Ranger-Captain is free for the same reason: both are Humans, both come after Champion, and each opens
  its own continuation.
- I keep the measured 10-04 ranks for this block only as the canonical total order, which condemnation needs.

**Main phase:** m1 for every card, and no second main.
- In a goldfish with no haste, a post-combat cast gains nothing.
- A pre-combat Human entry adds a counter to an attack-ready Champion or Lieutenant, and lords and Rick's four-Human count
  pump this turn's attack.

## 4. What changes and why it matters

1. **Champion first (it is tied at 10 today).** This decides lethal with the same cards and the same mana.
   - Re-probed today on the current tree at shipped play settings (d5/b20 + keep table + value leaf):
     **s8800308 gi308**, T3: Cavern, then {Vial-put Coppercoat, cast Coppercoat, cast Champion}.
   - Generic order: Champion enters last as a 1/1, the T4 swing is 14 (opponent at 1), and the kill is T5.
   - Proposed order: Champion enters first and gets +2, the T4 swing is 16, and the kill is T4.
   - gi10 and gi302 (seed base 8,800,000) also go T5 to T4. On gi10 the T3 line is "Champion, THEN Coppercoat"; on gi302
     it is Champion instead of Sentinel on T2.
   - In the 10-04 oracle study all three were **unreachable at d8 b0** under the generic order, so the generic order
     is a hard prune here, not a budget effect.
2. **Tutors before the bodies.** A fetched Champion must still precede the turn's other Humans. The repo already has a
   fixture for this: `soldiers_vial_after_tutor_continuation`.
   - Four Plains, Vial on 2, hand Recruiter + Cathar Commando.
   - Recruiter fetches Champion, Champion is cast, then Commando is Vial-put: lethal on T5.
   - The generic tie puts the Commando ahead of the Champion, and the kill slips to T6.
3. **Vial before Thalia.**
   - Generic ranks Thalia (creature, 10) ahead of Vial (20). Since 10-06 that also applies to a Vial-PUT Thalia, so the
     second Vial costs {2}.
   - The payable-order fallback rescues the case where that order cannot pay. What it cannot rescue is the {1} lost when
     the taxed order still pays but the mana was wanted afterwards.
   - Example: Officer on board, 7 mana. Vial, then Thalia, then the `{3}{W}` dig costs exactly 7. In the generic order
     the dig is unaffordable.
   - This is a smaller effect than item 1.
4. **Amendment: Thalia's Lieutenant from LAST (17) to 4.**
   - The 10-04 rationale ("its ETB counters this turn's Humans too") misses its second clause: "Whenever another Human
     you control enters, put a +1/+1 counter on this creature."
   - For any Lieutenant/Human pair the total is exactly +1 in either order. Lieutenant first collects the counter
     itself; Lieutenant later spreads it.
   - So among non-Champion Humans its position cannot change the total. Only two cases are not order-neutral:
     - Champion before Lieutenant gives +2 instead of +1. Both orders already satisfy this.
     - A **duplicate legend** cast later the same turn (Thalia, Harbin, Kudro and Rick are 2-ofs). It enters and dies to
       the legend rule before a late Lieutenant's ETB can see it, but an early Lieutenant's watcher still counts it, for
       +1. The engine models this (`DuplicateEntryOrDeathHasUpside`).
   - So early weakly dominates last. I have no game in hand that shows the case. It is rare.
5. **The 10-04 measurement predates both rules it now runs under**, so re-measure before flipping the default.
   - Measured then: -0.003 turns/game at play settings, 0 slower games at d3 and d5, +0.006% units.
   - Since then: Vial puts are sequenced by rank (10-06), which this order now drives, and the keep table and value leaf
     were adopted (10-08).
   - My single-game probes say the effect is still there: 3 of 3 oracle games are T5 under generic and T4 under the
     proposed order at today's play settings.

## 5. Questions for the USER

1. Adopt this Soldiers order with default ON (`=0` turns it off), after one held-out paired A/B on the current tree?
   **Default: yes.**
2. Thalia's Lieutenant at 4, after the tutors (my amendment), or LAST (the measured 10-04 slot)?
   **Default: after the tutors.**
3. Treat slots 6-17 as one free block, with their order kept only as a canonical tie-break? **Default: yes.**
4. Everything Main 1, no second main? **Default: yes.**
5. Should the generic tiers learn §6 items G1 and G2 (enter-watchers first, a noncreature-spell taxer after noncreature
   spells), measured per deck? **Default: yes.**

## 6. Generic-order findings

- **G1. Enter-watchers tie with the creatures they watch.**
  - Affected params: `own_creature_enters_self_counters`, `any_creature_enters_lifegain`,
    `own_creature_enters_lifegain[_toughness]`, `any_creature_enters_self_counters_power`.
  - Affected cards: Champion, Lieutenant, Youthful Valkyrie, Soul Warden, Essence Warden, Soul's Attendant, Auriok
    Champion, Suture Priest, Daxos, Bishop of Wings, Righteous Valkyrie, Verdant Sun's Avatar, Hamletback Goliath,
    Vaultborn Tyrant.
  - They want to be on the battlefield before the creatures they watch. The 10-way creature tie makes that
    plan-vector luck. Here that is a measured hard prune (§4.1), and since 10-06 it also governs Vial puts.
  - Proposed: a "creature-enters watcher" tier just below creature 10. This is principle 3 read the other way: the
    payoff must be on the battlefield before the events it counts.
- **G2. `noncreature_spell_tax` (Thalia only) ranks as a creature**, ahead of noncreature spells. A symmetric cost
  INCREASER belongs after the spells it taxes. That is principle 2 mirrored.
- **G3. ETB creature tutors-to-hand (`tutor_to_hand` on a creature) tie at 10.** Ranking them first among creatures
  (acquisition before the casts it informs) lets a fetched watcher or lord join at its own rank (§4.2).
- **G4. `ClassifyMainPhase` sends a vanilla creature to m2** even when its entry pumps an attack-ready watcher (Sentinel,
  Commando, Thalia here). Only the runtime scaling-attacker pull rescues it. This is inert on this single-main deck but
  wrong in principle.

## 7. Implementation notes

- **Adoption:** change `EnvOn("MTG_SOLDIERS_ORDER")` to `EnvOn("MTG_SOLDIERS_ORDER", true)` at
  `DecisionProviders.cpp:8968`. The heurarm slot `SOLDIERS_ORDER` is at `HeuristicArm.h:307`. Then re-accept smoke and
  regression GT for soldiers/soldiers2hg; overnight waits for its periodic run.
- **Param keys already in place** (`SoldiersProvider::CastOrderRank`, `DecisionProviders.cpp:8989`):

  | card | key |
  |---|---|
  | Lieutenant | `etb_each_other_own_creature_counters` (tested FIRST, because it also carries Champion's watcher param) |
  | Champion | `own_creature_enters_self_counters` |
  | Vial | `upkeep_adds_charge` |
  | Thalia | `noncreature_spell_tax` |
  | Recruiter | `tutor_to_hand && tutor_max_toughness >= 0` (the default is -1) |
  | Ranger-Captain | `tutor_to_hand` |
  | Officer | `activated_dig_count` |

- **The amendment:** Lieutenant goes from `return 17` to `return 4`. Shift Vial to 5, Officer to 6, Sentinel to 7,
  Thalia to 8, the bodies to 9-13 and the lords to 14-17. Update `CastOrderTierName` to match.
- **House style:** the free block is a NAME table (`kBodies`). Because its order provably cannot matter, it can become a
  param-derived tie-break, using the Equipment precedent `cls*4096 + (mv&0xF)*256 + (name_hash&0xFF)`. The classes:
  - `vanilla_creature`: Sentinel, Commando
  - `daybound_back_name`: Brutal Cathar
  - `sac_outlet_self_only`: Jirina
  - `attack_with_n_threshold`: Harbin
  - `lord_effect`: Coppercoat, Field Marshal, Kudro, Darien, Rick
- **No engine limitation blocks the order.** One thing to know: Recruitment Officer's activation runs in the trailing
  pass, after all casts (`TurnSolver.cpp` `apply_plan_actions`). A Champion found by that dig is therefore cast after
  this turn's other Humans, unless the base plan is the dig alone. That line is still expressible through the site-10
  continuation, which re-plans the whole hand.
- **Stale text to fix when this is adopted:**
  - The two "user-proposed" labels in §1.
  - Thalia's cards.json bracket note. It still describes `VialOrderMatters` and the puts-last twin, which were deleted
    on 10-06 (7173178f9).
  - The code comment on Lieutenant's rationale.
