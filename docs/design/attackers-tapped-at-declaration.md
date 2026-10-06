# Attackers are tapped at combat DAMAGE, not at declaration (CR 508.1f) -- deferred residue

Status: PARTLY FIXED 2026-10-06 (the Arcanum Wings combat swap); the general case is DEFERRED.

## The rule

CR 508.1f: declaring attackers taps every chosen creature without vigilance. That happens BEFORE
attack triggers resolve and before any player gets priority in the declare-attackers step, so an
attacking creature's `{T}: Add ...` mana ability is unavailable for anything paid during combat.

## What the engine does

`ResolveCombatDamage` (src/ai/Combat.cpp, shared executor + rollout) is the single place attackers get
`tapped = true`. Everything that runs between the declaration and that point sees the attackers
UNTAPPED: `GameEngine::CombatPhase` / `TurnSolver::SimulateCombat` run the attack triggers (token
makers, Bruna's gather), then `ApplyCombatAuraSwap`, then `Firebreathe` (leftover-mana attacker
pumps), then damage.

## Fixed (2026-10-06)

`ApplyCombatAuraSwap` now taps the non-vigilant attackers before it probes or pays
(`MTG_COMBAT_SWAP_TAPPED_ATTACKERS`, default ON, `=0` restores). Found while building the human-play
Aura-swap timing: an attacking Birds of Paradise / Avacyn's Pilgrim paid the {2}{U} of an in-combat
Arcanum Wings swap -- an illegal swap the viewer's `dig` prompt offered to the human (its affordability
probe saw the attacker's mana) and the search's combat pin scored. Unit:
`test/unit/test_bruna_sweep.cpp` "CR 508.1f: an attacking mana creature cannot pay the in-combat Aura
swap". Only runs when an attached Aura-swap permanent opens the window, so every deck without an
`aura_swap_cost` card is byte-identical -- in autonomous AND human play (the human `dig` chooser is
installed for every deck, so the gate is the attached Wings, not the chooser).

## Deferred: the rest of the combat window

* **Firebreathing** (`AIEngine::Firebreathe` / `ApplyFirebreathing`, `AvailableManaPool(state)`): the
  leftover-mana pump can still be paid by an attacking mana creature. Affects any deck with a
  `firebreathing` param AND a creature mana source that attacks (Dragons lists with mana dorks).
  Fix shape: the same tap-attackers-first step at the top of the firebreathe pass (both worlds share
  `ApplyFirebreathing`), measured with a paired A/B on the firebreathing decks -- it can only make
  them slower (it removes mana that did not exist), so the verdict is "correctness, GT worse on
  purpose" per the Karoo-payment precedent.
* **The general fix** -- tap at declaration in `DeclareAttackers` and keep `ResolveCombatDamage`'s tap
  as an idempotent backstop -- would cover every present and future combat-time spend at once, but
  every reader of `tapped` between declaration and damage (attack triggers that count untapped
  creatures, Jorn's untap, convoke-like effects) has to be audited first. Not started.
