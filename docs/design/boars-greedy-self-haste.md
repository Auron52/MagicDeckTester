# Greedy self-haste rider (MTG_GREEDY_SELF_HASTE) -- Boars, 2026-11-14

## What changed

Flinthoof Boar: "{R}: This creature gains haste until end of turn" (`CardParams::self_haste_cost`, ActivatePump
mode 6). The search already reaches "cast Flinthoof, then haste it" (breakpoint site 9, ba5d932b0). The GREEDY
policy did not. That policy is `TurnSolver::Solve`, used by the depth-0 runner and by every playout turn past the
search horizon. CollectActions offers the activation only for a creature already on the battlefield, and Solve
applies its plan once per main phase. So a freshly cast Flinthoof was never hasted in a rollout, and the d0 runner
hasted one only when its second pass happened to find mana left over.

`AppendGreedySelfHasteRiders` (TurnSolver.cpp, Solve's candidate list only) adds a RIDER for each
self_haste_cost creature Solve may cast pre-combat. The rider is the mode-6 activation of that card
(`sac_source_id` = the hand card's number, `Action::haste_rider_hand` = its hand slot):
* its cost is billed as a NONCREATURE use, so Somberwald Sage's mana cannot pay it;
* its attack is projected through `haste_attack_power`;
* its eval is one attack of the card's power (`power x 100`, EvalCard's unit).

`SubsetHasUncastHasteRider` refuses a rider unless the subset also casts that card. The trailing pass of both
apply worlds (ApplyPlanDirect, AIEngine::TakeTurn) activates the rider after the casts, checking liveness first.

The rider is offered only where it can matter: pre-combat; when the body would land unable to attack without
haste and able to with it (judged on the definition's card, because a real game's hand copy is a name-only
DeckLoader placeholder); and when the body has power. It is never offered in human play. Only Flinthoof holds
the param, so every other deck is byte-identical. Checked on 6 decks x 40 games at d0 and d3 b10: Minotaur,
Goblins, Stompy Surprise, Vampires, Angels and Hinata, every digest equal.

## Measurement

Held-out seed 99001. 400 games per cell, paired per game, one pooled batch, arms set by the heurarm slot.

| cell | avg off | avg on | net turns | better | worse |
|---|---|---|---|---|---|
| Boars d0 | 5.4775 | 5.4175 | -24 | 24 | 0 |
| Boars d3 b10 | 4.8350 | 4.8350 | 0 | 0 | 0 |
| Boars play (d5 b20) | 4.8200 | 4.8200 | 0 | 0 | 0 |
| Boars 2HG d0 | 6.0625 | 6.0125 | -20 | 22 | 2 |
| Boars 2HG d3 b10 | 5.1650 | 5.1650 | 0 | 3 | 3 |
| Boars 2HG play (d5 b20) | 5.1550 | 5.1525 | -1 | 1 | 0 |

Every cell is <= 0, so the rider ships DEFAULT ON. The d3 b10 and play cells have new digests (the rollouts
differ) but the same outcomes, apart from the 2HG cells listed above.

## The slower games

* **2HG d0 gi39 (T7 -> T8) and gi388 (T7 -> T8).** These come from an older greedy scoring gap that the rider
  now exposes. Near the end of the game, EvalCard values a summoning-sick creature at
  `power x (ExpectedAttacks - 1) = 0`. Lightning Greaves' haste equip onto a fresh creature earns a flat 100
  with no attack projection. The rider's `power x 100` is then the only positive term on the board:
  * gi39 T7: {Flinthoof + haste} (300) beat {Ilharg + Greaves on Ilharg} (100), and Ilharg's attack was the
    kill.
  * gi388 T6: {Flinthoof + haste} and {Raggadragga + Flinthoof + haste} tied at 200, and the smaller mask won,
    so Raggadragga was not cast.

  Possible follow-ups, neither attempted here because both move other decks:
  1. Credit a haste equip by the host's power and project its attack.
  2. Give a late creature cast some floor value.
* **2HG d3 b10 gi24 (5 -> 6), gi39 (6 -> 7).** A searched choice moved at a tight budget, because the playout
  policy changed under it. Both recover at d3 b40, at d3 b0, and at the play setting d5 b20.
* **2HG d3 b10 gi399 (6 -> 7).** This one stays slower at d3 b0 (7 vs 6) and recovers at d4 b20 and d5 b20. It
  depends on the top-1 land-drop fetch width (`DecisionProvider::OnPlayFetchWidth`). With the rider on, the d3
  search played Stomping Ground on T1 and Windswept Heath on T2. Pinned to the top of the order, the Heath
  fetched a Forest, and T2 became Flinthoof instead of Ignoble Hierarch. With `MTG_UNPRUNED=fetch`, both arms win
  T6 at d3 b0. The fetch prune was left unchanged. It is an agent default awaiting the USER's ruling, and this is
  one more data point for that ruling.
