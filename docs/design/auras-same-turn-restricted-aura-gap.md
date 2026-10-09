# Auras: a restricted Aura on a creature cast the same turn is never enumerated (DEFERRED)

Status: DEFERRED -- found 2026-10-09 during the USER's review of the Auras cleanup-discard design
(docs/design/auras-discard-policy-proposal.md). It is a PLAY defect, not a discard one: the
search cannot express a rules-legal line, so no discard or cast-order setting can recover it.

## The line that cannot be played

Turn N, no creature in play, hand: a creature (e.g. Slippery Bogle), a plain Aura (e.g. Ethereal
Armor) and a restricted Aura -- Daybreak Coronet ("Enchant creature with another Aura attached to
it", `aura_enchant_requires = "another_aura"`) or Lion Umbra (`"modified"`). With the mana for all
three, the legal sequence is: cast the creature, cast the plain Aura on it, then cast the
restricted Aura on it (the plain Aura makes the creature a legal target, CR 303.4a / 700.9).
The engine offers the first two casts but never the third.

## Why

Two candidate injectors in src/ai/TurnSolver.cpp each cover half of the line and neither covers
the join:

1. `AppendSequencedAuraCandidates` (around line 42060) adds a restricted Aura onto a creature that
   another same-turn Aura will enable -- but it iterates **battlefield** creatures only
   (`for (const Permanent& p : state.battlefield)`), so a creature still in hand is never a target.
2. `AppendCreatureTargetAuraCandidates` (around line 42169) adds an Aura onto a creature cast in
   the same plan -- but it skips every restricted Aura
   (`if (!def->params.aura_enchant_requires.empty()) { continue; }`). Its comment says restricted
   Auras are "OUT of scope here -- a fresh creature satisfies those only via other same-turn casts,
   which the aura-aura sequencing already models". That is wrong: the aura-aura sequencing (item 1)
   never looks at hand creatures, so the combined case is modelled by neither.

`SubsetHasUnenabledRestrictedAura` (the reject that keeps the sequencing sound) already checks for
an enabler Aura on the SAME target number, and card numbers are stable from setup, so it would
accept the combined line unchanged once a candidate for it exists.

## Fix sketch (not implemented)

In `AppendSequencedAuraCandidates`, also offer each restricted Aura onto each creature in HAND (by
its stable `m_number`, the way `AppendCreatureTargetAuraCandidates` does), gated on the hand holding
a plain Aura that could enable it. The subset is then valid only if it also casts that creature
(`SubsetHasAuraOnUncastCreature`) and an enabler Aura on it (`SubsetHasUnenabledRestrictedAura`);
the apply order is creature (CastOrderRank 10) -> plain Aura -> restricted Aura (the existing
enabler-first sort). Fix the wrong comment at the same time.

This is a capability EXPANSION, so it is GT-affecting for the Auras deck (and only for decks holding
a restricted Aura -- no restricted Aura in hand means nothing is injected, byte-identical
elsewhere). Measure with the Auras suite cases and the deck's references, and check that the plan
count stays bounded (restricted Auras x hand creatures, only with a plain Aura in hand).

## Why it matters to the discard design

The settled Auras discard design keeps Daybreak Coronet / Lion Umbra as useful Auras only when a
creature is in play or in hand ("stranded" otherwise). Until this gap is fixed, a Coronet with only
a hand creature is in practice uncastable on the turn that creature lands, so the engine
undervalues exactly the hands the design keeps.
