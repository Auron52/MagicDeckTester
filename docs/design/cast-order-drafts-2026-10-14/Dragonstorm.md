# Dragonstorm: cast-order draft (agent review, 2026-10-14; for USER review)

## 1. Status: **(A) the USER's order, shipped default ON** (the agent's readings of it are not yet confirmed)

- `MTG_DRAGONSTORM_USER_ORDER` (`DragonstormProvider::UserOrderOn`, DecisionProviders.cpp:11053) is
  `EnvOn(.., true)`. Its ranks are at :11059-11074, the spliced-cast rank at :11076, and `ArrangeCastOrder` at
  :11178. The five follow-up levers are all default ON and on the same order: `MTG_DRAGONSTORM_ORDER_OPAQUE`
  (:11433), `_BP_LEFTOVERS` (:11095), `_IRENCRAG_CLOSER` and `_DRAGON_ORDER` (EngineFlags.h:139/:183), and
  `MTG_LINE_SPARE_GENERIC_FIRST`. GT includes all of them: 9347f97b "accept GT for the final tree (... Dragon order)".
- Sources of the order: the USER's "Canonical go-off cast order" (dragonstorm-search-pruning.md:130), the USER's
  rules 1-5 (gi22-durdle-and-irencrag-apex.md:20), and the USER's Medallion ruling of 2026-10-10
  (dragonstorm-user-cast-order.md:15).
- **Still open:** readings 1-9 in dragonstorm-user-cast-order.md:29 are the agent's, marked *"each a choice the
  USER should confirm"*. I found no confirmation in docs/design. See Q2.
- **No USER order is OFF.**

## 2. What plays today (checked against the code and six traced shipped games, d5 b20)

The deck has a single main phase (`uses_second_main=no`), so the m1/m2 column is inert and every cast is in
main 1. Utvara's "m2" in the report is a label that nothing reads.

| rank | card(s) | rule that puts it there |
|---|---|---|
| land | land drop | First: the deck has no cantrip, and Apex's exiled lands are not playable (`m_impulse_no_land`). At depth > 0 the drop is folded into the plan. |
| -- | Lotus Bloom | Never cast from hand (suspend only). Suspending it and its `SacForMana` are applied before the casts (`CastOrderingCandidates` :11309). Its rank of 20 is never read. |
| 110, then moved | Ruby Medallion | `reduces_spell_color`. `ArrangeCastOrder` step 3 moves it to the EARLIEST slot at which the whole line pays under the exact walk, and never past Irencrag. |
| 120 / 140 / 150 / 160 | Rite of Flame / Pyretic / Desperate (plain) / Seething Song | `IsManaRitual`, ordered by printed MV. A splice base sorts after a plain ritual of the same cost. |
| 170-178 | Desperate Ritual, spliced | `CastOrderActionRank` puts it after Seething, with more splices first. Step 1 moves a plain copy behind it only when splice legality needs that. The search also tries "spliced before Seething" (USER rule 5). |
| 180 | Apex of Power | `impulse_exile`: an enabler. The set is order-opaque, but the provider opts in (`OrderOpaqueCastsByRank`), so it is still ranked. After the breakpoint, leftovers are merged by the same order. |
| 190 **(TIE)** | Scourge of Valkas, Lathliss, Karrthus, Kolaghan, Utvara | Creature. The search tries every Dragon order at depth > 0 (`AddDragonOrders`). At depth 0 and in the horizon rollouts, **hand order decides** (§4.1). |
| 200 | Irencrag Feat | `max_casts_after`. It is tested before `IsManaRitual`, because Irencrag is both. Step 2 puts it immediately before a lone Dragon closer. After it resolves, only Dragonstorm or a Dragon is offered (`CastUnderCastCap`). |
| 210 | Dragonstorm | `tutor_to_battlefield`: last |

Verified by trace (`MTG_USER_ORDER_TRACE=1`, seeds 7200001-6; logs in build/scratch/castorder/probe_Dragonstorm/):
- s7200002 T1, "Rite, Medallion": Medallion moved behind Rite because one land could not pay {2} first.
- s7200006 T6, "Medallion, Rite, Desperate, Seething, Dragonstorm".
- s7200002 T3, "Pyretic, Desperate x2, Apex | Pyretic, Seething, Scourge, Dragonstorm": a hard-cast Dragon after the
  rituals and before Dragonstorm.
- s7200001 T6, "Seething x2, Apex | Pyretic, Desperate, Apex (exiled) | Rite, Desperate, Dragonstorm".

In all six games, every cast followed the order. No ritual was cast after Apex in the same list, and no Irencrag
was cast before Apex.

## 3. Proposed order

The USER's order is unchanged. The only proposed change is a sub-order inside the Dragon tier.

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | No draw precedes it, and an Apex-exiled land cannot be played. Before the casts. |
| (pre) | Lotus Bloom suspend / sacrifice | m1 | - | Suspend costs {0} and is not a cast, so it adds no storm. The sacrifice floats 3 into the turn's pool, which lasts through the main phase, so taking it first loses nothing. |
| 1 | Ruby Medallion | m1 | [slot 0 .. just before Irencrag] | USER: "as early as possible while still maintaining the line". It discounts Pyretic, Desperate, Seething, Apex, the Dragons and Dragonstorm. It cannot discount Rite ({R} has no generic), so "Rite, Medallion" costs the same as "Medallion, Rite". |
| 2 | Rite of Flame | m1 | - | USER order. Copies are identical, so their escalation is the same in any order. |
| 3 | Pyretic Ritual | m1 | - | USER order: same cost and output as Desperate, but it cannot splice, so it goes first and keeps the Desperates in hand. |
| 4 | Desperate Ritual (plain) | m1 | - | USER order. Moves behind a spliced copy only when splice legality needs it (reading 4). |
| 5 | Seething Song | m1 | - | USER order |
| 6 | Desperate Ritual (spliced; more splices first) | m1 | searched: before Seething | USER rule 5: "splice-after Seething preferred ... individual-before is the fallback. Needs search." |
| 7 | Apex of Power | m1 | - | USER rule 3: an enabler. After the rituals, before any Dragon, Irencrag or Dragonstorm. |
| 8a | **Lathliss, Dragon Queen** | m1 | searched | "Whenever another nontoken Dragon you control enters, create a 5/5". It must be on the battlefield before the Dragons it counts. This is band 0 of the deck's own put-order rule (`ClassifyEtbOrder`, :10767). |
| 8b | **Scourge of Valkas** | m1 | searched | "Whenever this creature or another Dragon you control enters, it deals X". Each Dragon entering after it pings once more (band 1). |
| 8c | Karrthus, Kolaghan, Utvara **(tie, order-free)** | m1 | searched | None of them reads another Dragon's entry: haste is a static ability, Utvara triggers on attack, and Karrthus's ETB is inert in a goldfish. Which one follows Irencrag is a payment question (Karrthus needs {B}{G} and Kolaghan {B}, which Irencrag's seven red cannot pay). The searched orders and the payable fallback answer it. |
| 9 | Irencrag Feat | m1 | - | USER: second-to-last, before the closer. Immediately before the last Dragon when the line has no Dragonstorm. Never before Apex. |
| 10 | Dragonstorm | m1 | - | USER: last. Hard-cast Lathliss and Scourge already on the battlefield see every storm put. |

## 4. What changes and why it matters

1. **The Dragon tie (8a/8b) decides the d0 runner and the horizon playout.** I built a board to show it:
   build/scratch/castorder/probe_Dragonstorm/scen/dragons_*.json. It has turn 5, 11 Mountains, opponent at 6,
   and a hand of Scourge + Lathliss.
   - d0, hand [Scourge, Lathliss]: Scourge is cast first. It pings 1 + 2 = 3, with no token, so the opponent is
     at 3 and there is **no kill**.
   - d0, hand [Lathliss, Scourge]: the 5/5 token enters before Scourge's ping, so it pings 3 + 3 = 6. **Kill on
     T5.**
   - d3, either hand order: T5, because `AddDragonOrders` finds Lathliss first.

   In general, Lathliss before Scourge deals 2n+6 against 2n+3 (n = Dragons already in play) and adds a 5/5.
   Scourge before any other Dragon adds a ping. The search keeps every Dragon order, so nothing becomes
   unfindable. What changes is the canonical first try, which wins ties and is the order the d0 runner and every
   rollout realise. gi47 (Round 3) is this class.
2. **Found while verifying. This is not an order change, but it sits at the seam between the order and a prune:
   the reducer-sink exemption burns rituals.** In s7200001 T4 (shipped d5 b20) the hand had 7 cards and no
   payoff. The game cast **Medallion | Rite | Pyretic | Desperate | Desperate**, leaving 6 red unspent. Lands
   paid the Medallion at slot 0, so none of the four rituals funded it.
   - With `MTG_PAYOFF_PRUNE_REDUCER_SINK=0`, the same game casts the Medallion alone and keeps the rituals. They
     go into the T6 chain, which then casts 13 spells instead of 9.
   - The win turn is T6 either way: a tie that the search broke toward burning cards.
   - Cause: `SubsetWastesAccelerant` (TurnSolver.cpp:10262) accepts any Medallion in the subset as every ritual's
     sink. Under the USER order, though, the Medallion goes to its earliest payable slot, so a ritual after it
     funds nothing.
   - Proposed fix: count only rituals placed before the Medallion in the arranged order (rituals it actually
     needs). See Q3.
3. **Hand-cast Apex vs exiled Apex is also a tie at 180** (the same continuation can hold one Apex in hand and
   another in the exile). Hand-cast first needs 10 mana; exiled-first needs 20. The exact-walk payable fallback
   already re-orders this, so I am not proposing a change, only recording that the tie's order does matter.
4. Out of scope for me: d0 splice legality is being fixed by another agent.

## 5. Questions for the USER

1. **Hard-cast Dragons among themselves: Lathliss, then Scourge, then the rest (the same order Dragonstorm
   already uses for its puts)?** Default: **yes**. Every Dragon order is still searched at depth > 0.
2. **Confirm the readings of your order as built** (dragonstorm-user-cast-order.md:31-58)?
   - Medallion at the earliest slot where the whole line pays, never after Irencrag.
   - Several Medallions each placed as early as possible (they may end up apart).
   - A plain Desperate moves behind the spliced cast only when legality needs it.
   - Irencrag goes immediately before the last hard-cast Dragon when there is no Dragonstorm.

   Default: **confirm**.
3. **Should Ruby Medallion justify only the rituals that pay for it** (§4.2), behind its own lever and measured
   on shipped and d3? Default: **yes**.

## 6. Generic-order findings (from this deck's `=0` arm, dragonstorm-user-cast-order.md)

- **Creature 10 before ritual 15** is wrong for any creature that a same-turn ritual pays for. In s7600233 T3 the
  generic order cast Scourge first, unpaid, then burned three rituals. The "prowess catches later casts"
  rationale applies only to creatures with a cast trigger.
- **Restrictor 18 before "other noncreature" 20** realises Irencrag -> Apex, which the USER called a trap. The
  generic rule should be: a cast restrictor goes immediately before the turn's last cast, and always after an
  impulse-exile or staging enabler.
- **Reducer 16 after rituals 15** goes against principle 2. The Medallion discounts three of the four rituals.
  The earliest-payable placement (`ArrangeCastOrder` step 3) is the general answer, not a fixed tier.
- **The creature tier ignores on-other-ETB watchers.** `OrderEntriesByEtbValue` is already the shared, deck-agnostic
  primitive (DecisionProviders.cpp:10776), and the generic creature tie could use it the same way.
- Two report nits:
  - A suspend-only card (Lotus Bloom) is listed with a hand-cast rank.
  - On a deck with `uses_second_main=no`, the m2 label could print as inert.

## 7. Implementation notes

- Slots are keyed by params (house style):
  - `reduces_spell_color`: 110
  - `max_casts_after`: 200. Test it first, because Irencrag is also a ritual.
  - `impulse_exile`: 180
  - `tutor_to_battlefield`: 210
  - `IsManaRitual`: 120 + 20*(mv-1), plus 10 for `splice_onto_arcane`
  - `Action.splice_count`: 170+
  - creature: 190
- **Q1 change (one line, own lever, `EnvOn(.., true)` + an appended heurarm slot):**
  - In the creature branch of `DragonstormProvider::CastOrderRank`, return `186 / 188 / 190` from
    `ClassifyEtbOrder(p)`: `etb_other_subtype_creates_tokens` gives 186, `dragon_ping_on_enter` gives 188, and
    everything else gives 190. `ClassifyEtbOrder` is in the same translation unit.
  - `AddDragonOrders` still permutes the Dragon slots, and `ArrangeCastOrder` step 2 still puts Irencrag before
    whichever Dragon is last. Both are unchanged.
  - Lockstep: `ExtraLethalDamage` (:11007) enters the hard-cast Dragons in `casting` (subset) order. Route them
    through `OrderEntriesByEtbValue` so the lethal model sees the order that will be cast.
- **Q3 change:**
  - In `SubsetWastesAccelerant`, `reducer_pays` should hold only when some ritual comes before the reducer in the
    arranged order. That means `CastOrderLess` + `ArrangeCastOrder` on the subset, with the Medallion's slot
    greater than the ritual's.
  - Otherwise a ritual-and-reducer subset with no other payoff stays waste.
  - This applies to the search branch list only, as now, so d0 is unaffected.
- No engine limitation blocks the proposal. The conditional positions (the Medallion's slot, and Irencrag before
  a lone Dragon) are already expressed outside the scalar rank in `ArrangeCastOrder`.
