# Cavern of Souls: the chosen creature type is simplified to "any creature"

**Status: DEFERRED (USER 2026-11-21: "That should be fixed, but only when there is time.").**
Found during the Beasts analysis (`docs/design/analysis-Beasts.md`).

## The gap

Cavern of Souls reads: "As Cavern of Souls enters, choose a creature type. {T}: Add {C}. {T}: Add one
mana of any color. Spend this mana only to cast a creature spell of the chosen type, and that spell
can't be countered." The engine models the restricted coloured ability as payable for **any creature
spell**. Its bracket note says that is exact for a tribal list whose creatures all share the named
type. That holds for Beasts (every creature is a Beast) but not for every deck that runs the card.

## Where it is wrong today

**Merfolk Turns** (`decks/Merfolk Turns/Merfolk Turns.cod`) runs 1 Cavern of Souls and 4 Seahunter.
Seahunter is a Human Mercenary, not a Merfolk, so a Cavern that named Merfolk cannot pay for it.
Today Cavern's coloured mana can pay for it. The error is in the deck's favour: it over-credits the
deck.

## Fix shape

* Give the land a chosen type. For a deck whose creatures share a type, the choice is forced. For a
  mixed deck it is a real decision when the land enters: search it, or let the provider fix it to
  the deck's tribe (that narrowing must be a tested one-option prune).
* The coloured ability's spend restriction checks the spell's subtypes against the chosen type.
  This needs an audit of every payer site: ManaPayment, affordability, fed-float, and the rollout's
  payer.
* The fix moves Merfolk Turns ground truth. Accept its smoke and regression rows through the normal
  flow, with a held-out A/B on Merfolk Turns.
