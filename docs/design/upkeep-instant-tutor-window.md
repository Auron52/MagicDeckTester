# Upkeep instant window for tutors-to-top (Enlightened Tutor)

**STATUS: Human play implemented; autonomous search remains open.**

## The hole, in the USER's words (2026-08-19, verbatim)

> I should mention that we likely have a hole with Enlightened Tutor right now, since it can be
> cast in upkeep (as an instant) to ensure we draw a specific card. Sometimes this is really
> relevant, especially when you used your mana the previous turn but have say 5 mana to play
> Defense of the Heart in the same turn.

## What the engine did before the viewer fix

All casts happened in the main phases; there was no upkeep cast window. Enlightened Tutor
(`tutor_to_top`, an INSTANT) is therefore only ever cast in a main phase, which places its target
on top **after** that turn's draw — the target arrives next turn, and next turn's mana pays for
it. The line the card is actually built for — **cast at your own upkeep, before the draw step, so
this turn's draw IS the tutored card and this turn's full mana can cast it** — cannot be
expressed. The USER's example: five mana available, Defense of the Heart in library; upkeep
E-Tutor ({W}) → draw Defense → cast it ({3}{G}) the same turn. Today that line takes two turns.

## Current human-play behavior

The play viewer now offers a paid upkeep cast of each payable instant `tutor_to_top` card in hand,
before the draw. The target is chosen in the same prompt; choosing "hold" keeps the tutor. The
ordinary cast path pays mana and resolves the tutor, so Worldly Tutor and Enlightened Tutor both
replace that turn's draw with the chosen card. This is covered by a focused engine test.
The prompt defaults to hold, so references saved before the new prompt replay their original
main-phase tutor lines.

The autonomous search and rollout still do not generate an upkeep-cast branch. Their main-phase
casts and regression results remain unchanged. A full searched implementation needs a pre-draw
choice that survives the previous turn's plan and is consumed in both executor and rollout; see
the scope below. A viewer reference using the new window can therefore be faster than search.

## Scope of the remaining search work

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
