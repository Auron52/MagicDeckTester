# Upkeep instant window for tutors-to-top

**STATUS: Human play and autonomous search implemented.**

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

The full-depth search now fans at each turn boundary over hold and the provider-ranked payable
instant tutor-to-top source/target pairs. The menu is built at the live upkeep after untap and
upkeep obligations, so it sees mana spent by Call of the Wild, echo, and other effects. Each
branch pays and resolves the tutor before the draw, then scores the following turn normally.
That includes Stompy's World War Hulk and Ghalta cheat lines, not just hard casts. The winning
rank rides on the crossing turn's plan and is consumed at the real next upkeep. The rollout and
executor use the same paid cast and stack-resolution path.

The deck provider's `TutorSearchWidth` limits target names, as it does for main-phase tutor
search. Search considers the hold branch too. At the horizon and at depth 0, holding remains
the existing playout policy; no unsearched in-window upkeep cast is made.

## Search implementation notes

* A pre-draw decision point at the active player's upkeep, offered only when an instant-speed
  `tutor_to_top` card is in hand and payable — narrow on purpose, not a general instant system
  (the goldfish engine has no opponent-turn play; this is the one same-turn-relevant instant
  window a tutor-to-top creates).
* The choice is made at the crossing, before the draw. It commits mana and a library target
  before the target is drawn. A next-turn main-phase search then scores deployment.
* The same path covers Worldly Tutor (Stompy) and Enlightened Tutor (Creature Giving,
  Anti-Lifegain, Fluctuator), plus any future instant tutor-to-top card with the shared param.
