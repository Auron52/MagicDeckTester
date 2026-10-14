# KittyEquipment -- cast-order draft (2026-10-14)

Shipped list: `decks/KittyEquipment/KittyEquipment.cod` (v1). Candidate lists `v2-puresteel-hammer/` and
`v3-adelbert-gauntlets/` share the provider and the order (USER 2026-10-04: *"We should probably make the order
generic for both"*). Their coverage is covered under section 6.

## 1. Status: (A) USER-agreed order, shipped default ON

* The ruling is in USER 2026-10-04 ("ONE order for both lists"), plus the 2026-10-05 Dwalin amendment, in
  `docs/design/cast-order-rankings.md:1171-1206`.
* The code is `EquipmentProvider::CastOrderRank` (`src/ai/DecisionProviders.cpp:26251-26301`), gated by
  `MTG_KE_ORDER`, which defaults ON (`:26160`). The same flag gates `LandDropCastOrderRank` = 0,
  `ActivationOrderRankFor` (other abilities 10, then equips 20, then Jitte modes 30), `OrderOpaqueCastsByRank`,
  and `MainPhaseOverride` (removal to Main2).
* Removal is never cast: `NeverCast` (DecisionProviders.h:1700) drops it at enumeration.
* No USER order is OFF.

## 2. What plays today (v1)

The report's numbers are composite keys of the form `class*4096 + (mv&0xF)*256 + (name_hash&0xFF)`. Decoded:

| pos | card | main | rule (class, key) |
|---|---|---|---|
| 0 | land drop | m1 | `LandDropCastOrderRank` 0. **Boros Garrison is held until after the casts** (`karoo_deferred`, TurnSolver.cpp:39636-39693), so it bounces a land that is already tapped. |
| 1 | Sol Ring | m1 | c1 mana rock (4450) |
| 2 | Puresteel Paladin | m1 | c3 `draw_on_equipment_etb` / metalcraft (12868) |
| 3 | Stoneforge Mystic | m1 | c6 Equipment tutor / tap-put (25318) |
| 4-7 | Kor Duelist, Kemba, Armored Skyhunter, Balan | m1 | c7 creature: by mv, then by hash (28945, 29658, 29714, 29918) |
| 8-15 | Bonesplitter, O-Naginata, Shadowspear, Colossus Hammer, Lightning Greaves, Umezawa's Jitte, Grafted Wargear, Loxodon Warhammer | m1 | c8 Equipment: by mv, then by hash (33065 ... 33637) |
| -- | Swords to Plowshares, Unexpectedly Absent | m2 | `NeverCast`. Their c10 rank is inert. |
| trail | Stoneforge put, then equips, then Jitte modes | -- | `ActivationOrderRankFor` 10 / 20 / 30, after every cast |

* **Ties:** none between distinct cards in v1, v2 or v3. I checked the three reports: 0 duplicate keys. Copies
  of one card share a key, and they are identical, so their order cannot matter.
* **ORDER-OPAQUE:** no card on any of the three lists is `OrderingOpaque` (ManaPayment.cpp:2722), so the report's
  rank is what actually plays.
* **Probes confirm the order is what plays.** I ran 9 single games at d3/b10 (seeds 1001-9009). Every multi-cast
  main followed the rank order:
  * s6006 T3: Stoneforge, REVEAL, Kor Duelist, Colossus Hammer, DRAW, equips.
  * s9009 T3: Bonesplitter, DRAW, Shadowspear, DRAW, Colossus Hammer, DRAW, equips.
  * s3003 T3: Kor Duelist, then Bonesplitter.
  * s2002 T4: one O-Naginata drew two cards with two Paladins out, so a second Paladin also wants to be cast
    ahead of the Equipment. It is.

## 3. Proposed order: keep the shipped order unchanged

| pos | card(s) | main | range | why (card text / plan) |
|---|---|---|---|---|
| 0 | land drop | m1 | -- | USER: *"land goes first"*. Garrison is already held until after the casts. A land drawn off a Puresteel trigger is still playable, because the search offers `land=none` plans and a continuation may play a newly drawn land. So "first" costs no line. |
| 1 | Sol Ring | m1 | -- | {1} for {C}{C}: it nets +1 mana the turn it lands, so it funds every later cast. It is also artifact #N for metalcraft. |
| 2 | Puresteel Paladin | m1 | -- | "Whenever an Equipment you control enters, you may draw a card." It must precede every Equipment, cast or put (Stoneforge, or Skyhunter's combat dig). Metalcraft makes equip {0}. A second copy also goes here, since each Paladin triggers separately. |
| 3 | Stoneforge Mystic | m1 | -- | Its ETB puts an Equipment into HAND, so it goes before the casts it feeds. Its {1}{W},{T} put is an activation in the trailing pass (USER: *"ability comes after the equipment"*). |
| 4 | Kor Duelist | m1 | -- | Host. There is no ETB or on-cast trigger among the hosts, so their relative order cannot change the board. Cheapest first. |
| 5 | Kemba, Kha Regent | m1 | -- | Host. Its upkeep cats count attached Equipment, which order within a turn does not affect. |
| 6 | Armored Skyhunter | m1 | -- | Host. Its attack-dig fires in combat. Main 1 keeps Puresteel's draw live for the put. |
| 7 | Balan, Wandering Knight | m1 | -- | Host. Attach-all is an activation in the trailing pass. |
| 8-15 | Bonesplitter, O-Naginata, Shadowspear, Colossus Hammer, Lightning Greaves, Umezawa's Jitte, Grafted Wargear, Loxodon Warhammer | m1 | -- | Every cost is generic, so after the hosts they spend what the W pips left. With a Paladin out, each one draws. Cheapest first gives the most draws per mana. Equips come at the end of main 1 (USER). |
| -- | Swords to Plowshares, Unexpectedly Absent | m2 | -- | USER 2026-10-04: never cast in a goldfish. |

* No ranges are proposed. Nothing on v1 changes affordability mid-sequence except Sol Ring, and Sol Ring is
  already first.

## 4. What changes and why it matters

**No rank change.** I looked hardest at the hosts-before-Equipment slot, and it is the only call worth surfacing:

* **It reverses your 2026-08-25 instruction for Kor Duelist.** You said *"It just shouldn't be played until you
  have finished drawing"*, quoted at DecisionProviders.cpp:26154. The 08-25 order put Equipment ahead of the
  hosts, because under the generic shape order-aware condemnation then lost 6 of 3,000 games.
* **The 10-04 ruling put the hosts first again**, for Sigarda's Aid ("creatures before the equipment"). Sigarda's
  Aid is not on v1.
* **On v1 today this costs no line, for two reasons.**
  1. Site 6 (the Puresteel draw) is deferred until after every cast in the plan. So a host's position inside
     the plan never changes what the draws can inform.
  2. Condemnation only drops a host the plan skipped. The sibling plan "P + host" casts that host at its own
     slot for the same mana, and `BpTurnManaSettled` re-admits the host when the draw is a land that changes
     the mana.
* **Measured:** the combined GT of 2026-10-04 under this order shows kitty searched game-turns of smoke -24,
  regression -99 and overnight -511, with 0 slower games. That run bundled other changes, so the order cannot be
  credited with the gain. It is evidence only that the order costs nothing.

Non-rank items that matter for the review:

1. **The report is unreadable as shipped.** `CastOrderTierName` (DecisionProviders.cpp:26955) still maps the
   retired ranks 6/7/8/30. So every row prints "(unclassified)" and you are reviewing five-digit keys.
2. **Latent tie risk.** The within-class tie-break is `name_hash & 0xFF`. On a future list with 6 names at one mv,
   about a 6% chance of a collision, which would bring back a silent hand-order tie. Today's three lists are clean.
3. **v2/v3: Sram, Senior Edificer is classified m2.** It is inert today (`uses_second_main=no` on both). But if
   that flag ever flips, Sram would be deferred behind the very Equipment casts it draws from.
4. **Stale code text.** DecisionProviders.h:1880-1881 says "default OFF". DecisionProviders.cpp:26100-26157
   describes the superseded "Equipment in front of the hosts" order. The unused agent-filled `MTG_KE_ORDER_FULL`
   (:26164-26250, OFF) is still present ("delete on request").

## 5. Questions for the USER

1. Keep v1's order exactly as shipped, with no rank changes? **Default: yes.**
2. On v1 (no Sigarda's Aid), your 10-04 "hosts before Equipment" supersedes your 08-25 Kor Duelist note ("not
   until you have finished drawing")? **Default: yes.** It is outcome-neutral under today's condemnation guards.
3. Pin any on-cast or on-ETB draw watcher (Sram, Puresteel) to Main1 in the provider, so the v2/v3 Sram m2
   label can never bite? **Default: yes.**
4. Delete the unused `MTG_KE_ORDER_FULL` (off since 10-04)? **Default: yes.**
5. Fix the report labels (decode `rank/4096` into the class name) and widen the hash tie-break to 16 bits?
   **Default: yes.** Neither changes play on today's lists.

## 6. Generic-order findings (and v2/v3 coverage)

* **The land-drop line reads "no cantrip in the deck, so nothing wants to precede it".** That test is keyed on
  card params, so it cannot see conditional cantrips owned by a permanent in play: Puresteel or Sram makes every
  Equipment a draw. It is the same blind spot `equipment-etb-draw-breakpoint.md` records for the breakpoint
  classifiers. It is harmless here (USER ruled land first, and the search can hold the drop), but the report
  text is wrong for this deck.
* **`ClassifyMainPhase` labels Sram m2 and Adelbert m2, but Puresteel and Cid m1.** A creature whose value is a
  trigger on this main's later casts (`draw_on_cast_subtypes`, `draw_on_equipment_etb`) must sit in the same
  phase as those casts. That is principle 3 (enablers before payoffs) on the phase axis. Generic fix: classify
  such a watcher Main1 whenever its watched subtype is in hand.
* **The generic tiers have no watcher-before-watched or ETB-after-targets rule.**
  * Puresteel, Stoneforge and the hosts tie at creature 10 (so hand order decides).
  * Equipment ties with removal at 20.
  * An ETB that acts on permanents already in play (Dwalin's hone) needs the opposite of "creatures before
    noncreatures". That is exactly what cost kittyv2 s4004 gi136 and s5005 gi92 a turn each at d8/b0 before the
    10-05 amendment.
  * A generic tier could be keyed on the same params used here.
* **v2/v3 coverage:** every nonland main-deck card on both lists lands in a param class, with no ties.
  * v3 order: Sigarda's Aid c2, Puresteel c3, Sram c4, Adelbert c7, then the Equipment c8 ({0}s first, then the
    {1}s), then Dwalin c9.
  * v2 differs only by Cid at c5 instead of Adelbert.
  * Skateboard's mandatory ETB "tap target permanent" picks itself (a no-op), so its position is free.

## 7. Implementation notes

* **Slots are keyed on params already (house style holds):**
  * c1: `mana_rock` and not a creature.
  * c2: `attach_equipment_on_etb`.
  * c3: `draw_on_equipment_etb`, or `metalcraft_equip_zero_artifacts > 0`.
  * c4: `draw_on_cast_subtypes` non-empty.
  * c5: `reduces_spell_subtype`.
  * c6: `tap_put_from_hand_cost`, or `tutor_to_hand` with "Equipment" in `tutor_types`.
  * c9: `hone_counters_on_enter_or_attack`. It is tested before c7.
  * c7: `IsCreature()`.
  * c8: `is_equipment`.
  * c10: anything else.
* **Condemnation context.** Site 6's continuation condemns any old hand card ranked below the LAST Equipment the
  plan cast (TurnSolver.cpp:39479-39483, 4256-4303). Equipment-late therefore means "after the draws, re-offer
  only new cards and later-ranked Equipment". That is sound because a sibling plan exists for each skipped card.
* **CastOrderRank:** no change.
* **CastOrderTierName:** decode `rank/4096` into c1..c10.
* **MainPhaseOverride:** return Main1 for a card with `draw_on_cast_subtypes` non-empty or
  `draw_on_equipment_etb`, gated with `KittyOrderEnabled`.
* **Hash widening:** `cls<<20 | (mv&0xF)<<16 | (name_hash&0xFFFF)` keeps the order monotone. Check that
  `CastOrderKey`'s `rank*4` stays below INT_MAX (about 4.6e7 at c10 -- fine).
* **No engine limitation blocks this order.**
* **Probe pitfall:** `--game-index` alone does not change the deal. gi 0-3 at seed 1001 played byte-identical
  games, so vary `--seed` instead.
