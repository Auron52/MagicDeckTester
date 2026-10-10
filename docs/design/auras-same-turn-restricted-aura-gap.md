# Auras: a restricted Aura on a creature cast the same turn is never enumerated (FIXED 2026-10-10)

Status: FIXED 2026-10-10, behind `MTG_AURA_RESTRICTED_NEW_CREATURE` (heurarm
`AURA_RESTRICTED_NEW_CREATURE`, default ON; `=0` never enumerates the line and is byte-identical to the
tree before the fix). **GT-affecting for the Auras deck only:** the candidates are injected only when
the hand holds a restricted Aura, a plain creature Aura and a creature. No other suite deck runs
Daybreak Coronet or Lion Umbra, so every other deck injects nothing. **Not yet measured** -- the suite
cells and the deck's references are owed at integration (see "Fix" below). Found 2026-10-09 during the
USER's review of the Auras cleanup-discard design (docs/design/auras-discard-policy-proposal.md). It
is a PLAY defect, not a discard one: the search could not express a rules-legal line, so no discard or
cast-order setting could recover it.

## Fix (2026-10-10)

The fix follows the sketch below, in `src/ai/TurnSolver.cpp`:

- **The new candidates.** `AppendSequencedAuraCandidates` gains arm (1b). Each restricted Aura in hand
  is offered onto each creature in HAND, using that creature's stable `m_number`.
  - It fires only while the hand also holds a plain creature Aura, the only enabler a creature cast
    this turn can have, and only with `AppendCreatureTargetAuraCandidates`' machinery live
    (`MTG_LEGACY_NO_AURA_NEW_CREATURE` unset).
  - Bound: restricted Auras x hand creatures.
- **Rejects and apply order (unchanged).** A subset is valid only if it casts that creature
  (`SubsetHasAuraOnUncastCreature`) and a plain Aura on the same number
  (`SubsetHasUnenabledRestrictedAura`). The two existing stable sorts order the plan creature -> plain
  enabler -> restricted Aura.
- **Cast-ordering search guard (new).** `OrderingPlacesRestrictedAuraBeforeEnabler` drops an ordering
  that casts a conditional restricted Aura before any Aura on its target. The apply does not re-check
  the Enchant restriction, so such an ordering would attach illegally. Only human play and claude-play
  run that search, and a creature in play had the same exposure. The guard is gated on the same flag,
  so `=0` stays byte-identical.
- **Wrong comment corrected.** The comment on `AppendCreatureTargetAuraCandidates` claimed the aura-aura
  sequencing already covered this case; it now points at arm (1b).

Tests: `test/unit/test_auras_restricted_new_creature.cpp`, plan level through
`TurnSolver::EnumerateMainPlans` / `ApplyPlan`:
- Bogle -> Ethereal Armor -> Daybreak Coronet is enumerated, in that order, and applies with both Auras
  on the Bogle.
- Scout -> Rancor -> Lion Umbra ("enchant modified creature") likewise.
- `=0` never offers the line.
- With two hand creatures, every Coronet plan casts its host and puts the Armor on the same host first.
- With no plain Aura in hand, nothing is injected.

The first two fail on the pre-fix tree.

Owed at integration (not run here; the brief forbids `mtg` runs on the shared box):
- the Auras suite cells A/B'd `=0` vs default;
- the deck's references;
- a plan-count check on the Auras cells (`MTG_BRANCH_STATS`) confirming the injection stays bounded.

The discard design keeps a Coronet with a creature in hand as a useful Aura. That now matches play.

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
