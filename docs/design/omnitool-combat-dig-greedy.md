# Adaptive Omnitool: the combat dig takes the provider's top pick (in-window greedy)

**Status: DEFERRED, latent.** Found 2026-11-21 during the Beasts analysis. The USER: *"If choosing
the top is built into the search this is a notable problem, yes. However, if it is just that the
provider returns one item it is not an issue."*

## Finding

It is the first case: choosing the top is built in.

* Adaptive Omnitool's attack trigger (`attack_dig_hand_count` 6, types [Artifact]) fires at
  declare-attackers in `FireAttackEquipDigHand` (SpellEffects.cpp, near line 2666).
* That routes to `PerformLookTakeDig` under `ScriptedEtbDig(-1)`, so no searched pin applies.
* `EtbDigCandidates` returns the full legal set, unpruned, and the host takes the first entry.
* So with search depth remaining, the engine picks one of several provider candidates without
  branching. That is the "host takes the provider's top candidate" defect in CLAUDE.md's no-greedy
  rule.
* The card's own oracle bracket already discloses this ("provider-ranked ... NOT a searched axis").

## Reachability

None in shipped play. The Omnitool is sideboard on the shipped list, and the dig only fires from an
equipped attacker on the battlefield. It becomes live the moment a list maindecks it, for example
in a deck-screening arm.

## Fix shape

Garruk's Harbinger (Beasts) needs the same thing: a combat-damage dig whose card is a searched
choice. That analysis builds a searched card-number pin for a combat-time dig, in the Goblin Lackey
shape. Once it lands, point Omnitool's fire site at the same pin. The provider may still narrow the
set, and a one-item return is fine. The executor and the rollout must consume the pin in lockstep.
