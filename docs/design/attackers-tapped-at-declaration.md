# Attackers are tapped at combat DAMAGE, not at declaration (CR 508.1f) -- deferred residue

Status: FIXED for the Arcanum Wings combat swap -- LANDED 2026-10-07 with the combat-swap attack hold, USER-approved
(*"We should allocate the creatures beforehand to be the sources of mana for the swap and not attack with
them."*). Firebreathing and the general case are DEFERRED (below).

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

## Fixed (landed 2026-10-07)

`ApplyCombatAuraSwap` now taps the non-vigilant attackers before it probes or pays
(`MTG_COMBAT_SWAP_TAPPED_ATTACKERS`, default ON, `=0` restores). Found while building the human-play
Aura-swap timing: an attacking Birds of Paradise / Avacyn's Pilgrim paid the {2}{U} of an in-combat
Arcanum Wings swap -- an illegal swap the viewer's `dig` prompt offered to the human (its affordability
probe saw the attacker's mana) and the search's combat pin scored. Unit:
`test/unit/test_bruna_sweep.cpp` "CR 508.1f: an attacking mana creature cannot pay the in-combat Aura
swap". Only runs when an attached Aura-swap permanent opens the window, so every deck without an
`aura_swap_cost` card is byte-identical -- in autonomous AND human play (the human `dig` chooser is
installed for every deck, so the gate is the attached Wings, not the chooser).

**The attack hold that goes with it** (`HoldForCombatAuraSwap` in `DecisionProvider::AttackWith`,
`MTG_COMBAT_SWAP_ATTACK_HOLD`, default ON): with a combat swap pinned, the mana creatures the {2}{U}
needs (lowest combat power first) stay home instead of attacking, so their mana can legally pay.

**Measured (Bruna, paired, every former suite row: smoke + regression + overnight, 16,725 games,
`logs/cr508_bruna_ab/`):** 270 games slower, 9 faster, net +0.011..+0.053 turns per row. 251 of the
270 had a REAL combat swap paid by an attacking Birds / Pilgrim at or before the old win turn
(`MTG_TRACE=swapatkpay` on the pre-fix binary): illegal wins, removed on purpose (Karoo precedent).
The other 19: 12 searched games where only the search's rollouts had credited illegal swaps -- all 12
recover the old turn at `--depth 8 --budget-ms 0`; 7 d0 games caused by the attack hold (below).
Every other suite deck is byte-identical (smoke 114/114, regression and overnight differ only by the
known batch run-to-run nondeterminism, each moved game reproducing GT-equal on both binaries in
isolation).

**Known weakness of the hold (open, proposal to the user):** it is blind to what the held creature
would have hit for. Bruna d0 s4004 gi360: a Birds of Paradise wearing Colossification + Eldrazi
Conscription + Mythic Proportions was held home to pay a swap onto a 1/1 Mother of Runes -- 1 damage
instead of 40. All 7 hold-caused d0 slowdowns are this shape. Proposed amendment (branch
`cr508-hold-gain`): hold only when the swap's power gain on the host exceeds the held creatures'
combat power. On the d0 rows it recovers those 7 plus 2 more and makes nothing worse.

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
