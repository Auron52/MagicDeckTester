# Boars: drafted cast order (2026-11-14, awaiting the USER's review)

## Status

**APPROVED by the USER 2026-11-14** ("That order seems reasonable to me.", after the full list below). The USER
asked for a recommended order to review ("If you want a ruling on the cast order I recommend you come up with a
recommended order and I review it"). Adoption = `MTG_BOARS_CAST_ORDER` DEFAULT ON, landing after the in-flight
fetch-width tiers (chain1) so the two play changes are accepted separately.

The order applies to **the casts a plan has already chosen for this main phase**. Whether a card is cast at all,
and which creature Ilharg puts into play, stays with the plan search. Card text below is taken from
`src/cards/data/cards.json`.

## What the generic order does now

`mtg decks/Boars/Boars.cod --cast-order-report`:

| Rank | Cards |
|---:|---|
| land | the land drop first |
| 10 | every creature, cheapest first: Birds, Hierarch, Flinthoof, Zhur-Taa, Sage, Herald of Ilharg, Raggadragga, Ilharg, End-Raze, Decimator |
| 20 | Lightning Greaves, Herald's Horn |

Two problems follow from the card text:

1. **Herald's Horn comes after the Boars it discounts.** A Horn cast in the same main as a Boar saves nothing.
2. **Herald of Ilharg comes after the cheap creatures.** It gets counters only from creature spells cast *after*
   it is on the battlefield, so the dorks and Flinthoof cast before it add nothing.

## Proposed order -- the full list

Every card in the main deck, one position each. Ties within a tier are broken by mana value, then by name, so the
order is total. A second copy of a card goes directly after the first.

| # | Card | MV |
|---:|---|---:|
| 1 | Land drop (a fetchland resolves on play) | -- |
| 2 | Herald's Horn | 3 |
| 3 | Lightning Greaves | 2 |
| 4 | Herald of Ilharg | 4 |
| 5 | Birds of Paradise | 1 |
| 6 | Ignoble Hierarch | 1 |
| 7 | Flinthoof Boar | 2 |
| 8 | Zhur-Taa Druid | 2 |
| 9 | Somberwald Sage | 3 |
| 10 | Raggadragga, Goreguts Boss | 4 |
| 11 | Ilharg, the Raze-Boar | 5 |
| 12 | End-Raze Forerunners | 8 |
| 13 | Decimator of the Provinces (hard cast or emerge) | 10 |

Positions 5-10 have no card-text reason to differ from each other. Their order is cheapest first, then by name.

## Why each position (tiers)

Earlier rows execute first.

| Slot | Cards | Reason |
|---:|---|---|
| 0 | Land drop (a fetchland resolves on play) | No cantrip in the list, so nothing gains from preceding it. Horn's top-card look happens in upkeep, before the main. |
| 1 | Herald's Horn (a second copy follows the first) | Creature spells of the chosen type cost {1} less. Every Boar in the list (Herald of Ilharg, Flinthoof, Raggadragga, Ilharg, End-Raze, Decimator, including emerge) is cheaper after it. It is not a creature, so it costs Herald of Ilharg no counters. |
| 2 | Lightning Greaves | Order-neutral: Equip {0} can move to any creature once both are out. It goes ahead of the creatures so its {2} is settled from lands before Somberwald Sage's creature-only mana is spent, and so it is on the battlefield for the haste-a-fresh-dork line (exception E1). |
| 3 | Herald of Ilharg (a second copy follows the first) | "Whenever you cast a creature spell, put two +1/+1 counters". Every creature after it adds two counters. A second Herald is itself a creature cast, so the first one counts it. |
| 4 | Creatures of mana value 4 or less: Birds of Paradise, Ignoble Hierarch, Flinthoof Boar, Zhur-Taa Druid, Somberwald Sage, Raggadragga. Cheapest first, then by name. | They build Herald's counters before the big spells read them. Raggadragga must be on the battlefield before a spell that costs 7 or more ("untap target creature, +7/+7"), so it sits ahead of End-Raze and Decimator. Its +2/+2 for mana creatures is static, so its place within the block is free. |
| 5 | Ilharg, the Raze-Boar | Mana value 5: Herald deals damage equal to all its counters. Casting it after the cheap creatures means more counters when that damage is dealt. It goes before End-Raze and Decimator so it gets both pumps. |
| 6 | End-Raze Forerunners | Its enter trigger gives the **other** creatures you control +2/+2, vigilance and trample when it resolves. A creature cast later gets nothing, so End-Raze follows every other creature except Decimator. Raggadragga's untap fires on it (8 mana, or 7 with a Horn). |
| 7 | Decimator of the Provinces (hard cast or emerge) | Its cast trigger pumps every creature you control as it resolves (not Decimator). An emerge victim cast this turn must already be on the battlefield. With End-Raze first, End-Raze is available as the victim (a {G}{G}{G} emerge). |

**End-Raze before Decimator** is a tie on pumps. Either way each other creature gets +4/+4, and whichever of the
two comes first gets +2/+2 from the other. The tie is broken by the emerge victim: End-Raze-first allows the
End-Raze victim, and the reverse order allows nothing extra.

**Herald's damage favours ending with the big spells.** On each creature cast of mana value 5 or more, Herald deals
damage equal to all its counters, and the counters rise by two with every creature cast. Total damage is
greatest when every cheap creature precedes every 5-plus spell. The order among the 5-plus spells does not change
that total.

## Exceptions that outrank the slots

* **E1. A fresh mana creature hasted by Greaves to pay for this line.** Birds, Hierarch, Zhur-Taa and Sage are
  summoning-sick, so their mana is usable this turn only with Greaves' haste. If the chosen line pays a later spell
  with that mana, the dork goes before the spell it pays for. It can even precede Herald of Ilharg or Herald's
  Horn, and Herald then gets no counters off that dork. This is the order's fallback when the ideal order cannot
  pay for the chosen casts. It is never a reason to drop a cast.
* **E2. Raggadragga's untap is new mana mid-main.** A 7-plus spell that untaps a tapped mana creature (Sage: three
  more creature-only mana) re-plans the rest of the phase from the new board (the existing breakpoint). Order
  after that point comes from the new plan.
* **E3. Creatures kept for Ilharg's attack put are not cast.** Putting one onto the battlefield is not a cast, so
  it triggers neither Herald nor Decimator. Whether a creature is cast or saved for the put is the plan search's
  choice.
* **E4. Second main (D6).** When a line uses the second main (End-Raze vigilance or a Raggadragga untap keeping
  mana after combat, or an Ilharg put body as an emerge victim), the same order applies within that main.
* **E5. Activations are not casts.** Flinthoof's {R} haste, Greaves' equip and Zhur-Taa's taps are activations.
  They fit around these casts as the search finds them.

## Questions for the USER

1. **Greaves at slot 2.** It is order-neutral by its text. Its only effect is the Sage creature-only-mana payment and
   E1. Keep it there, or put it after the creatures as the generic order does?
2. **Herald of Ilharg before every creature, even a dork.** A dork cast ahead of Herald gives no counters, and its
   mana is not usable this turn without Greaves. So the only cost is the E1 fallback.
3. **End-Raze before Decimator** (the emerge-victim tie-break above). Agreed?

## Measurement (to follow)

Implementation will be `BoarsProvider::CastOrderRank`, behind `MTG_BOARS_CAST_ORDER`, **DEFAULT OFF** until the
USER reviews. A held-out paired A/B (Boars and Boars 2HG, play setting and d3) will be reported here and to the
USER. The USER decides adoption, whichever way the measurement points.
