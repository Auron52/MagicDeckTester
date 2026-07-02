# Deferred: pump an ENEMY creature + Swords to Plowshares (anti-lifegain burn line)

Self-contained deferred idea (2026-07-02, from the user). **Not** being worked on — parked here.

## The line

In an anti-lifegain shell (Tainted Remedy: "if an opponent would gain life, they lose that much
instead"):

1. Pump an **opponent's** creature (e.g. Invigorate → +4/+4) to inflate its power.
2. **Swords to Plowshares** it: Swords exiles the creature and its *controller* (the opponent) "gains
   life equal to its power" → Tainted Remedy flips that to **loss**.

Net: the opponent loses (creature power + pump) life — a burn line that doesn't need an attacker, so
it's useful **when you can't afford / don't have your own attacker** but the opponent has a creature.

## Why it isn't modeled today

- **Invigorate is modeled as own-target only** — `parameters.target_own_creature: true`
  (`PumpSpell`). The engine's pump enumeration (`FindBestOwnAttacker`, the `target_own_creature`
  branch in TurnSolver/AIEngine) only ever pumps one of the *controller's* creatures. Pumping an
  **opponent's** creature is not a candidate.
- **Swords' lifegain-to-controller isn't tied to a pump.** Swords is modeled as removal; its
  "controller gains life = power" rider (the thing Tainted Remedy weaponises) would need to be
  represented AND combined with an enemy-target pump in one searched line.

## What implementing it would take

- Let a pump spell optionally target an **opponent** creature when a payoff makes that profitable
  (a controlled Tainted-Remedy + a way to convert the pumped power to opponent life loss, i.e.
  Swords' controller-lifegain). Gate it so it only fires with that payoff — a random +4/+4 on an
  enemy creature is otherwise a gift.
- Model Swords' "controller gains life equal to the exiled creature's power" and run it through the
  lifegain→loss replacement (same `lifegain_to_loss` path Aria uses).
- Enumerate the two-card line (enemy-pump → Swords-same-target) as a searched plan; value it by the
  resulting opponent life loss. A good `test/scenarios/*.json` fixture (opponent creature on board,
  Tainted Remedy in play, Invigorate + Swords in hand) would drive and guard it — see
  `docs/design/scenario-harness.md`.

Related: the own-creature reservation work (`docs/design/mana-source-reservation.md`) is about keeping
your *own* attacker untapped; this is the opposite target-side idea and is a modeling gap, not a
reservation one.
