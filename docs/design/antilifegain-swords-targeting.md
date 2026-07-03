# Anti-Lifegain: Swords-to-Plowshares targeting (shipped) + pump-then-Swords (deferred)

Self-contained record (2026-07-03). How the Anti-Lifegain deck uses Swords to Plowshares as a
life-loss tool, what shipped, and the larger combo left for later.

## The interaction

Swords to Plowshares exiles a creature and its **controller gains life equal to its power**
(`controller_lifegain_equals_power`). A Tainted Remedy / Plague Drone (`lifegain_to_loss`, i.e.
`RemedyActive`) replaces an *opponent's* lifegain with an equal life **loss**. So Swords on an
**opponent** creature, with an enabler in play, makes the opponent lose `power` life. Resolution
(`EffectHandler::ResolveRemoval`) and the search rollout (`ApplyPlanDirect` Removal branch) both model
this via `OpponentGainsLife(state, 1 - tgt_controller, tgt_power)`, so the search already simulates and
values the life-loss (and, without an enabler, correctly sees the opponent *gain* life).

## Shipped (2026-07-03): largest-target + enabler gate

Against a **passive goldfish opponent**, exiling a creature has no defensive value — the only payoff is
the life-loss. So:

- **Enabler gate:** the Swords cast is only offered / cast while a `lifegain_to_loss` enabler is in play
  (else it just hands the passive opponent life). Enforced in the enumeration guard (CollectActions),
  the executor (`CastSpellFromHand`), and the rollout — all via the one helper.
- **Largest target:** among the opponent's creatures, hit the **highest `EffectivePower()`** (max life
  loss). `EffectivePower()` includes temporary buffs, so a pumped creature is picked up automatically.

One shared helper keeps all three sites in lockstep: `FindLifegainRemovalTarget(state, active)` in
`SpellEffects.h` — returns the largest opponent creature's index, or **-1 = "do not cast"** (no enabler
in play, or no opponent creature). Only Swords carries `controller_lifegain_equals_power`, so every
other deck is byte-identical; verified: smoke/regression show all non-antilife decks unchanged, antilife
searched depth improves overall, d0 net strongly positive. Behaviourally confirmed in game logs: Swords
exiles the max-power opponent creature (e.g. the 6/6, or the power-2 among [1,1,2]) and drops opponent
life by that power.

### The enumeration gate uses "enabler IN PLAY" (not "in hand") — and why

The gate that decides whether to *offer* the Swords cast checks `RemedyActive` (an enabler on the
**battlefield**), not merely an enabler in hand. Loosening it to "in hand" was tried and **tanked the
greedy d0 path** (antilife d0 −33 wins): the deck holds ~8 enablers, so "enabler in hand" is true almost
every turn, and the myopic d0 greedy then considers/misfires Swords constantly. The apply-time gate
(`FindLifegainRemovalTarget` → `RemedyActive` at resolution) still prevents a no-enabler cast, but the
enumeration breadth alone destabilised d0. Keeping the gate at "in play" keeps d0 healthy.

### >> TRACKED REGRESSION TO FIX: the same-turn "Tainted Remedy → Swords" combo

The in-play gate forgoes the same-turn combo — on the turn the enabler lands it isn't on the
battlefield at enumeration (turn start), so Swords isn't offered that turn and waits until the next.
This is a **known searched-depth regression accepted into ground truth on 2026-07-03** (net change is
strongly positive: ~11 searched games faster, 3 slower). It must be fixed; the fix should make these
recover to their pre-change win turns WITHOUT the d0 blow-up the "enabler in hand" gate caused (−33 d0
wins).

Exact regressed games (repro `--seed <base+gi> --game-index <gi>`; base = the config's seed):

| config | game | now | target | repro |
|---|---|---|---|---|
| antilife d3 s2002 | gi87  | T5 | **T4** | `--seed 2089 --game-index 87 --depth 3 --budget-ms 200` |
| antilife d5 s2002 | gi87  | T5 | **T4** | `--seed 2089 --game-index 87 --depth 5 --budget-ms 200` |
| antilife d3 s2002 | gi235 | T7 | **T6** | `--seed 2237 --game-index 235 --depth 3 --budget-ms 200` |

(Confirmed a real capability gap, not budget churn: gi87 is T5 at both d3 AND d5 with the in-play gate,
but T4 with the looser "enabler in hand" gate. gi87's old T4 line = T3 cast Tainted Remedy **and** Swords
the 4/4 in one turn.)

Fix directions (pick one, must not regress d0):
- Enumeration condition "an enabler is **castable this turn** (in hand AND affordable)" instead of
  "enabler in hand" — narrow enough to spare the d0 greedy, wide enough for the combo.
- Keep the enumeration gate loose but gate the **d0 greedy selection** of Swords strictly (enabler in
  play) — search enumeration stays wide, d0 stays clean. Needs the greedy removal-selection locus.
- After a fix, re-verify with the repros above (all → target turn) AND full smoke+regression (d0 wins
  must not drop; other decks byte-identical), then re-accept.

### GOLDFISHING ASSUMPTION (revisit for Phase 2)

Both rules above assume a **passive** opponent (goldfish): a real opponent's creature is worth exiling
on its own even without an enabler (it removes a blocker/attacker), and you might prefer removing a
specific threat over the largest. When a real opponent is added (Phase 2), gate these behind the
goldfish assumption / make them opponent-model-aware rather than unconditional.

## Deferred: pump-then-Swords (grow their creature, then exile it)

Because the life-loss equals the exiled creature's power, you can **pump the opponent's creature** and
then Swords it for more loss. Invigorate ({2}{G}, +4/+4) hard-cast on an opponent creature, then Swords
it, turns +4 power into +4 life loss. `FindLifegainRemovalTarget` already targets by `EffectivePower()`,
so the *Swords* half needs nothing — the missing piece is **enumeration**: Invigorate is
`target_own_creature: true` (its pump only ever targets our own attacker), so the search never considers
casting a pump on an *opponent* creature. Supporting the full combo means enumerating that unusual line
(offer Invigorate-on-opponent-creature when a Swords + enabler are also available this turn) and letting
the existing simulation value it. It's niche (spends a pump + a Swords to convert +4 into +4 damage,
only worth it with an enabler out), so it's deferred until it proves worthwhile.

Touch points: `Invigorate` `target_own_creature` handling in the Creature-targeting enumeration
(`TurnSolver::CollectActions`) and cast (`AIEngine::CastSpellFromHand` / `ApplyPlanDirect`); the pump
target would be the same `FindLifegainRemovalTarget` creature so pump and Swords agree.
