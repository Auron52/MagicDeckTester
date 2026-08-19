# Upkeep instant window for tutors-to-top (Enlightened Tutor)

**STATUS: DEFERRED (recorded 2026-08-19, from the Creature Giving cast-order review). Not built.**

## The hole, in the USER's words (2026-08-19, verbatim)

> I should mention that we likely have a hole with Enlightened Tutor right now, since it can be
> cast in upkeep (as an instant) to ensure we draw a specific card. Sometimes this is really
> relevant, especially when you used your mana the previous turn but have say 5 mana to play
> Defense of the Heart in the same turn.

## What the engine does today

All casts happen in the main phases; there is no upkeep cast window. Enlightened Tutor
(`tutor_to_top`, an INSTANT) is therefore only ever cast in a main phase, which places its target
on top **after** that turn's draw — the target arrives next turn, and next turn's mana pays for
it. The line the card is actually built for — **cast at your own upkeep, before the draw step, so
this turn's draw IS the tutored card and this turn's full mana can cast it** — cannot be
expressed. The USER's example: five mana available, Defense of the Heart in library; upkeep
E-Tutor ({W}) → draw Defense → cast it ({3}{G}) the same turn. Today that line takes two turns.

## Scope if built

* A pre-draw decision point at the active player's upkeep, offered only when an instant-speed
  `tutor_to_top` card is in hand and payable — narrow on purpose, not a general instant system
  (the goldfish engine has no opponent-turn play; this is the one same-turn-relevant instant
  window a tutor-to-top creates).
* The searched question is cast-now-vs-hold (upkeep cast commits mana before the draw is seen);
  the greedy consumer likely needs a gate the same way the risky-wipe (c) term did — deliver to
  the search first (see the cast-time-scoping lesson in `cast-order-rankings.md`, Anti-Lifegain).
* Affects any deck holding an instant tutor-to-top; today that is Creature Giving and
  Anti-Lifegain (both run Enlightened Tutor).

## Trigger to build

USER asks for it, or a review/measurement shows Enlightened Tutor lines losing turns to the
missing window (the Creature Giving review predicts exactly this shape).
