# Free-cast targeted hits -- deferred lockstep gaps (2026-10-17)

Status: DEFERRED. No deck in the regression suite cascades (or reveal-casts) into a targeted spell, so neither gap
moves any game today. Found while fixing `FreeCastHasLegalTargets` (a hit with no legal target is not cast and goes
to the bottom, CR 702.85a / 601.2c -- landed on `colossus-analysis`).

## 1. A castable targeted free-cast hit resolves with NO target in the executor

`EffectHandler::PushFreeCast` pushes the hit with no chosen target, so in the real game a cascaded burn spell / pump /
Swords resolves to nothing, while the rollout's recursive cast picks a target and applies it. The two worlds diverge
on every such hit.

Fix shape: share the executor's hand-cast target selection (the same chooser + provider ranking a hand cast of that
card uses, human chooser included) with the free-cast path, so the free cast is targeted exactly as a hand cast would
be. Applies to all three free-cast paths (cascade, Creative Technique's reveal, Breaching Dragonstorm's ETB).
Test: a lockstep unit case (cascade into Lightning Bolt with an opponent creature / face available) comparing the
resulting life totals and zones in both worlds.

## 2. The rollout files a resolved instant/sorcery into the graveyard as the TEMPLATE card

The rollout puts the template `Card` into the graveyard, not the actual copy that was cast, so its card number differs
from the executor's. Anything that later reads graveyard identity by number (a recursion effect, a delve/escape count
that de-duplicates, reference replay) can disagree. Same class as the Green Sun's Zenith shuffle fix. Fix shape: move
the actual cast copy through the stack into the graveyard in the rollout, as the executor does. Test: cast an instant
in both worlds and compare graveyard card numbers.
