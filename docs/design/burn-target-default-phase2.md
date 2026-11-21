# Burn target default against a real opponent (Phase 2, deferred)

## Status
Deferred until Phase 2 (a real opponent). Nothing to do while play is goldfish.

## What exists today
In human play, a fixed-damage burn spell that hits a player and a creature (Searing Blaze) shows a default target
pin. `HumanPairedBurnDefault` in `src/` (SpellEffects.h) ranks the pins:
1. the opponent's face, then an opponent's planeswalker, with our own face last;
2. then the creature with the most toughness left.

This was added in `c5f3b7610`. It changes only the default and the menu label. The option order and the searched
alternatives are unchanged.

## USER ruling (2026-11-21, verbatim)
- *"The opponent's toughest creature is good for searing blaze."*
- *"(in Goldfishing. In a real game you would probably target the best creature you can kill)"*

## What Phase 2 must change
The goldfish opponent never blocks and never uses its creatures, so the creature half of the burn only matters
through its own triggers. "Toughest" is a fine tie-break there.

Against a real opponent, the default should be the most valuable creature the damage actually kills: lethal damage
given its toughness and the damage already marked on it. Prefer that over a larger creature that survives.

This is a default and label rule only. The search still branches over every legal target, so it is not a
search-window pick.

## Where to look
- `HumanPairedBurnDefault`, `PrepareFixedBurnPin`, `DisplayedFixedBurnPin`.
- `PrunesPassiveBurnTargets`: the provider prune that keeps the toughest creature against the passive opponent.
  It would also need revisiting.
- Scenario `test/scenarios/creatureburn_human_blaze_default_opponent.json`.
