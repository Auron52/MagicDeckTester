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

## STATUS 2026-10-06: the fix above is PARKED (branch `viewer-combat-swap-engine-parked`)

Split out of the human-play swap-timing change at the user's request (it moves Bruna GT). The parked
branch also carries `HoldForCombatAuraSwap` (`DecisionProvider::AttackWith`, `MTG_COMBAT_SWAP_ATTACK_HOLD`):
with a combat swap pinned, the mana creatures the {2}{U} needs stay home instead of swinging.

Measured with both (Bruna only moves; every other config byte-identical):
* smoke: bruna d0 +21 (11 slower, 4 -> unwon), d3 +8, d5 +2, bruna2hg d3 +2; 0 faster.
* regression: d0 s2002 +11, d3 s2002 +6, d3 s3003 +2 (2 faster), d5 s2002 +4, d5 s3003 0 (1 faster);
  ref gate 0 play-drift / 0 board-diverged.
* Attribution (smoke, 23 slower games): both levers `=0` reproduces GT on all 23; 21 had a REAL combat
  swap paid by an attacking Birds / Pilgrim (`MTG_TRACE=swapatkpay`) -- illegal wins removed; d3/d5 gi1
  = budget churn (recovers T4 at d8 b0). Regression attribution was started but not finished.
* Bruna d5 b20, 1200 paired games: CR fix alone 2 better / 52 worse; CR fix + hold 0 / 35 (+0.032
  turns/game); sampled worse games gi42/198/1110 all had the attacking host pay its own swap. Search
  hit rate on the automatic-win state with the parked branch: 158/158 + 18/18.
Decision needed (user): accept the GT-worse-on-purpose correctness fix (Karoo precedent) or not.
