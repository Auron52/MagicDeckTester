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

**Hold allocation fixed (2026-10-07, USER-approved as a mana-allocation fix, no new rule).** The 7 d0
games the hold made slower were not "the swap costs a big attacker": in every one there was NO Aura in
hand to swap in (empty hand, or only a wished Bruna). The rollout / d0 pin (`MaybePinRolloutAuraSwap`)
pins whenever a Wings is attached, and the hold then kept mana creatures home for a swap that could not
happen -- s4004 gi360 T7 held a Birds wearing Colossification + Eldrazi Conscription + Mythic
Proportions: 1 damage instead of 40. The hold now (1) holds nothing when no hand Aura could enchant the
host, and (2) holds the cheapest sufficient set of mana creatures (least combat power) instead of every
creature released lowest-power-first on the way -- non-attacking sources (lands, rocks, sick creatures)
already paid first. Units "Attack hold: ...". Measured: smoke 114/114 byte-identical; Bruna d0 rows
(10,000 games) 8 better / 0 worse vs the landed hold; searched rows (smoke + regression d3/d5, 725
games, and overnight d5 s4004/5005/6006, 1500 games) 0 better / 0 worse, one same-turn play change.
Where the swap genuinely needs the big attacker's mana the SEARCH already drops it: the in-tree pin is
a variant beside the unpinned plan, and the shared declaration (`DeclareAttackerIndices` ->
`AttackWith` -> the hold) makes the pinned line score the damage it really does (scenario: 20-power
Birds the only {U}, opp at 21 -- d3 attacks for the kill, d0 swaps and deals 9). Only the d0 greedy and
the beyond-horizon rollout take that swap; turning the rollout swap off entirely
(`MTG_ROLLOUT_AURA_SWAP=0`) measures worse on the searched rows (4 better / 11 worse, +7 turns), so it is
not distorting the search's evaluation.

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
