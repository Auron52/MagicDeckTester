# Dragons: cast-order draft (2026-10-14)

Deck: `decks/Dragons/Dragons.cod`, provider `DragonsProvider`. Lands: 18 Mountain, 3 Gruul Turf, 2 Haven of the Spirit Dragon.

## 1. Status

**(A) A USER-agreed order ships default ON.** Gaps: the rulings were never written into docs/design, and five within-slot details were agent fill-ins.

* **The rulings (2026-09-04).** They are quoted in `DragonsProvider::CastOrderRank`'s header comment (`src/ai/DecisionProviders.cpp:21353`). The code is gated by `EnvOn("MTG_DRAGONS_ORDER", true)` at :21397.
  * First ruling: "Dragon tempest should indeed be before everything else, Lathliss after and Scourge after that... We could fully fill out this order."
  * Second ruling, which supersedes the Tempest-first part and the Shaman/Incubator tie: "the rocks should come before Dragon Tempest and after land", and "we would also want a full order where all cards are separate in this design".
* **Adoption.** Commit `1731450b` shipped the bucket order: vs generic, 18 faster / 1 slower per 1000 games at value-play settings, 5.585 -> 5.568; all three GT tiers were rebaselined. Commit `4953a7fa` shipped the total order: 0 outcome moves vs the bucket order, d0 regression 5.9830 -> 5.9750.
* **Not recorded in docs/design.** The rulings live only in that code comment, the two commit messages and private agent memory.
  * `docs/design/cast-order-rankings.md` §Dragons still shows the pre-review Generic table (5 of 14 nonland cards).
  * `docs/design/cast-order-ideal-with-ranges.md:351` still lists Dragons as a "remaining review gate".
* **What the USER ruled, and what the agent filled in.** The USER ruled the skeleton: land, rocks, Tempest, reducers (Incubator with Shaman), Lathliss, Scourge, no ties. The agent filled in five details with no quoted sign-off:
  * Sol Ring > Mind Stone > Fire Diamond;
  * Shaman before Incubator;
  * dragons cheapest-first;
  * Greaves 14th;
  * Bolt last.

## 2. What plays today (report = current tree)

| pos | card | main (report) | rule |
|---|---|---|---|
| land | land drop | m1 | no cast-draw in the deck, so the land goes first |
| 1 | Sol Ring | m1 | ROCK, mv 1 |
| 2 | Mind Stone | m1 | ROCK, untapped |
| 3 | Fire Diamond | m1 | ROCK, `enters_tapped` |
| 4 | Dragon Tempest | m1 | watcher (noncreature `dragon_ping_on_enter` / `haste_on_flying_enter`) |
| 5 | Dragonspeaker Shaman | m2* | `reduces_spell_subtype` |
| 6 | Urza's Incubator | m1 | `chooses_creature_type` + `_creature_only` |
| 7 | Lathliss, Dragon Queen | m1 | `etb_other_subtype_creates_tokens` |
| 8 | Scourge of Valkas | m1 | creature `dragon_ping_on_enter` |
| 9-12 | Atsushi (4), Glorybringer (5), Inferno (6), Utvara (8) | m2*/m1/m1/m2* | 9 + (mv - 4) |
| 13 | Lightning Greaves | m1 | `is_equipment` |
| 14 | Lightning Bolt | both* | `DirectDamage` |

**No ties.** Only identical copies share a rank.

**\* The m2 and both labels are inert.** The report stamps `uses_second_main=no`, so the phase filter never runs and every cast happens in main 1. The Utvara m2 rule was retired 2026-09-04 (`src/runner/GoldFishRunner.cpp:293-329`): single-main measured 5 faster / 0 slower per 1000, once the m2 "wins" were traced to an illegal firebreathing mana double-spend.

**Ranks govern play.** No card is order-opaque (no `draw`, staging, cascade, or the like), so the rank sort decides every cast set.

## 3. Proposed order: unchanged, with the main column corrected to all m1

Land drop **first**. The deck's only draw is Mind Stone's `{1},{T},sac` activation. It sits on the searched dig axis and is applied after the plan's casts, so it is not a cast slot. **No ranges.** Every affordability card (the rocks and reducers) already sits at both its ideal and its cost-efficient end.

| pos | card | main | range | why (card text) |
|---|---|---|---|---|
| land | land drop | m1 | - | Nothing to draw first. A Gruul Turf's bounce is deferred until after the casts (float-equivalent, discussed 2026-09-04). |
| 1 | Sol Ring | m1 | - | `{1}`, "{T}: Add {C}{C}". Nets +1 the turn it lands and funds everything after, including Mind Stone off one land (probe R1). |
| 2 | Mind Stone | m1 | - | `{2}`, untapped, `{C}` this turn. |
| 3 | Fire Diamond | m1 | - | "enters tapped": no mana this turn. Its slot only affects payment, and probes P1/P8 strand nothing. Kept with the rocks per the ruling (Q2). |
| 4 | Dragon Tempest | m1 | - | "Whenever a creature you control with flying enters, it gains haste" and "Whenever a Dragon you control enters, ... X damage". Goes before every flier and every Dragon. |
| 5 | Dragonspeaker Shaman | m1 | - | "Dragon spells you cast cost {2} less": before every Dragon. It has no effect interaction with Incubator, and paying its `{R}{R}` before Incubator's all-generic `{3}` is the robust payment order (P9). |
| 6 | Urza's Incubator | m1 | - | "Creature spells of the chosen type cost {2} less" (chooses Dragon): before every Dragon. |
| 7 | Lathliss, Dragon Queen | m1 | - | "Whenever another nontoken Dragon you control enters, create a 5/5 ... Dragon". Goes before every other Dragon, and each token is another Dragon entry for Tempest and Scourge. |
| 8 | Scourge of Valkas | m1 | - | "Whenever this or another Dragon you control enters, X damage". After Lathliss, so its own entry mints a token and pings again; before every other Dragon. |
| 9-12 | Atsushi > Glorybringer > Inferno > Utvara | m1 | - | No watcher among them, so order provably cannot matter. Ping totals and Lathliss tokens are the same for every permutation, and Utvara triggers on attack. Cheapest-first is just a deterministic convention. |
| 13 | Lightning Greaves | m1 | - | The equip is a separate action that trails the casts (probe G1: Atsushi, then Greaves, then equip, lethal), so the slot only affects payment; an all-generic `{2}` goes late. |
| 14 | Lightning Bolt | m1 | - | "3 damage to any target" (always face). The deck has no prowess or spectacle, so it goes last. |

Utvara must be m1 even if a second main ever returns. Its trigger fires for every attacking Dragon, including itself when Tempest gives it haste.

## 4. What changes and why it matters

**No change to the order.** The draft fills gaps rather than amending the order. Each claim below was checked on a hand-built board (`build/scratch/castorder/probe_Dragons/sc*/`, human line forced, `MTG_DRAGONS_ORDER=0` = generic):

1. **Tempest before the dragon (T1).** Board: 6 Mountains, opponent at 5, hand Atsushi then Tempest.
   * USER order: 1 ping, then a 4-damage haste attack, win.
   * Generic (Atsushi 10 before Tempest 20): no ping and no haste; the opponent stays at 5.
2. **Lathliss before Scourge (L1).** Board: 11 Mountains, Scourge first in hand.
   * USER order: 3 + 3 = 6 damage plus a 5/5.
   * Generic (both at rank 10, so hand order decides): 1 + 2 = 3 damage and no token.
3. **Sol Ring before Mind Stone (R1).** Board: one Mountain, Mind Stone first in hand.
   * USER order: both rocks land.
   * Generic (both at rank 5, no cheapest-first among rocks): the line is "rules-legal ... but the search never enumerated this line". This is the gi94 class: a whole turn slips.
4. **Payment.** Fire Diamond early (P1, P8), Bolt last (P3) and Shaman before Incubator (P9): every forced line resolved in full. On these turns the prepay declines because a rock is in the plan, so the per-cast greedy pays in rank order. The agent fill-ins are therefore safe.

## 5. Questions for the USER

1. **Confirm the five agent fill-ins inside your total order?** Sol Ring > Mind Stone > Fire Diamond, Shaman before Incubator, dragons cheapest-first, then Greaves, then Bolt. Only Sol Ring first is load-bearing (R1); the others are order-neutral for effect, and the probes show no stranding. Default: **yes**.
2. **Fire Diamond: keep it with the rocks, or move it to the very end?** It makes no mana the turn it lands. Default: **keep** (your ruling; no measurable line lost).
3. **Confirm Dragons stays single-main**, with every cast in m1? Default: **yes** (5 faster / 0 slower per 1000; the old m2 gain was a double-spend artifact).
4. **Write your 2026-09-04 rulings and this table into `cast-order-rankings.md` §Dragons**, and clear Dragons from the review-gate list in `cast-order-ideal-with-ranges.md`? Default: **yes**.

## 6. Generic-order findings (from this deck)

* **G1. Enter-trigger watchers are cast after the creatures they watch.**
  * Tempest (noncreature) sits at 20; Scourge sits at 10, tied with the dragons.
  * `MTG_WATCHER_ORDER` (tier 9, `DecisionProviders.cpp:3180`) exists but is default OFF.
  * Probe T1 shows a lost lethal turn.
* **G2. Two different watcher types tie at creature tier 10, so hand order decides between them.**
  * The two types: the token-on-entry maker (`etb_other_subtype_creates_tokens`, Lathliss) and the ping watcher (`dragon_ping_on_enter`, Scourge).
  * The right order is token-maker, then ping watcher, then other same-subtype creatures. Probe L1 shows 3 damage vs 6 + 5/5.
  * This is the same defect as the Dragonstorm gi310 note at `ManaPayment.cpp:2656`.
* **G3. Rocks tie at tier 5.** `CastOrderLessRanked`'s cheapest-first tiebreak applies only to `IsManaRitual`, which excludes rocks.
  * Order rocks cheapest-first, or by net mana this turn.
  * An `enters_tapped` rock is not a same-turn accelerant and should sit after the untapped ones (R1).
* **G4. Choose-a-type reducers miss the generic reducer tier.** Urza's Incubator has an empty `reduces_spell_subtype`, so it misses tier 8 and lands at 20, after the creatures it discounts. `IsSubtypeCostReducer` (`DecisionProviders.cpp:20725`) is the right predicate.

## 7. Implementation notes

* **No code change is needed for the order.** It is already param-keyed in `DragonsFullOrderSlot` (:21400):
  * `mana_rock` (split on mv and `enters_tapped`);
  * noncreature `dragon_ping_on_enter` / `haste_on_flying_enter`;
  * `reduces_spell_subtype`;
  * `chooses_creature_type && reduces_spell_subtype_creature_only`;
  * `etb_other_subtype_creates_tokens`;
  * creature `dragon_ping_on_enter`;
  * `IsCreature` (9 + mv - 4);
  * `is_equipment`;
  * `CardTemplate::DirectDamage`.
* **Latent edges if the list changes:**
  * The "dragon" slot keys on `IsCreature`, not subtype Dragon, so a future non-Dragon creature would land in the dragon block.
  * A creature with mv >= 9 maps to slot >= 14 and collides with Greaves/Bolt.
  * Creatures with mv <= 4 share slot 9 and are separated only by name hash.
* **Report cosmetics.** `--cast-order-report` could print the main column as m1 when `uses_second_main=no`; the m2 labels mislead a reviewer.
* **Scenario harness gap.** A staged Urza's Incubator gets no `chosen_subtype_id`: the `RunScenario` staging (`src/main.cpp:7213-7229`) sets `chosen_color` and keywords only. A fixture Incubator therefore discounts nothing, which made my Incubator probes (P5, P7, P11, P12) unpayable for that reason alone.
