# Shard Volley hold — "don't spend a land before it buys something"

**Status: ADOPTED (default ON), pending ground-truth rebaseline.** Provider-owned prune in
`BurnProvider`; `MTG_UNPRUNE=saclandhold` / `MTG_UNPRUNED` reopen it, `MTG_SV_HOLD=0` is the
legacy hatch. Code: `HoldSacLandBurn` in `src/ai/TurnSolver.cpp`, called from both plan
enumerators (`Solve::consider` and `EnumeratePlans::eval_and_push`).

## The rule

Shard Volley is `{R}`, *additional cost: sacrifice a land*, 3 damage to any target. It trades a
**permanent** mana source for a **fixed** lump of damage. Goldfishing there is no race and the
opponent never gains life, so 3 damage now is worth exactly what 3 damage later is worth — while
the sacrificed land costs a mana on every intervening turn. The cast is therefore only right when
it buys something **this** turn. Three exceptions, all board-readable (nothing clairvoyant):

| | exception | why deferring cannot recover it |
|---|---|---|
| (a) | the plan **wins this turn** | there is no later |
| (b) | it turns **Spectacle** on for a spell in the same plan | Light Up the Stage `{2}{R}` → `{R}` is a same-turn discount |
| (c) | it pumps a **prowess** attacker that connects this turn | the +1/+1 is until end of turn |

**Flood is not a fourth exception.** Surplus lands make an early cast *harmless*, never *better* —
there is no upside to weigh, so the rule needs no flood branch. That also makes deferring free in
exactly those hands, which is what makes the rule safe to apply unconditionally.

## Why it exists

At d5 the search cast its first Shard Volley by T3 in **37/100k games vs 8 at d6**, and those games
were ~60% of d5's quality deficit against d6. Worked case, seed 203265: d5 sacrifices its **only**
land on T2, sits on zero lands through T5 unable to cast four spells it draws, and loses with the
opponent on 1 life; d6 holds the card and wins T7.

## The projection bug underneath it

The first version of this heuristic **lost** at the shipped config (+0.00510 turns, 20/20 seeds
worse) and only came right once two things were added. The user's read was that the second of them
was really a bug, not an exception — and that was correct.

`projected_atk` (both enumerators) counted this turn's attack damage as
`pending_atk + vial_haste_atk + noncreature_count * prowess_attackers`. Haste power was counted
**only for Vial activations**, so a *hard-cast* Goblin Guide or Monastery Swiftspear contributed
nothing — neither its power nor, for Swiftspear, the prowess triggers this plan's own spells give it.
A turn whose lethality depends on casting one therefore did not read as `wins_this_turn`.

That under-count is nearly harmless where it lived: it only mis-*ranks* a plan the search
re-simulates anyway. It becomes serious the moment anything uses `wins` as a **gate** — this
heuristic's exception (a) — because then it silently drops the killing blow of a "Goblin Guide +
Shard Volley = exactly lethal" turn. The fix is `Action::haste_attack_power` / `haste_prowess`,
stamped once by `CollectActions` (the shared action builder) and folded into both projections:

```
projected_atk = pending_atk + vial_haste_atk + haste_cast_atk
              + noncreature_count * (prowess_attackers + haste_cast_prowess)
```

Gated on `power > 0`, which is exactly when every provider's `ShouldAttackWith` returns true (the
AntiLifegain/Hinata overrides hold back only 0-power no-trigger dorks), so the projection cannot
claim an attack the real `DeclareAttackers` won't make.

**The general lesson: when you reuse an existing estimate as a gate, re-derive its error direction
from scratch.** "Conservative" is a property of a use, not of a number.

## Measurement

burn at its shipped config (`value_play` d6 / budget 20 / `value_trust_depth` 5), 20 paired seeds ×
5000 games per arm, seeds spaced 10000 apart so no two jobs share a game id. Metric = avg
turn-to-win, unwon = `max_turns+1`; **negative = better**. Control arm is a **branch-tip build in a
worktree**, so the projection fix and the heuristic are attributed separately. Harness:
`test/sv_hold_ab.sh` (`BIN_A`/`BIN_B` select per-arm binaries).

| change | train (block 200000) | held-out (block 800000) |
|---|---|---|
| projection fix alone | −0.00004, t = −2.18, 4 better / 0 worse / 16 tied | — |
| **fix + hold (adopted)** | **−0.00147**, t = −12.00, **20/20 better** | **−0.00176**, t = −11.19, **20/20 better** |
| fix + hold, no prowess exception | −0.00128, t = −11.45, 20/20 better | — |
| prowess exception's residual | −0.00019, t = −3.05, 12/2/6 | — |

### Cost: the heuristic does ~15% LESS search work

**Corrected 2026-08-04.** This was first reported as "+0.14% instructions", i.e. a slight cost. That
number was wrong, and the way it was wrong is worth keeping.

It came from callgrind Ir over **60 games on one seed**. burn's per-game search cost is heavily
tailed, so a 60-game total is dominated by a couple of outlier games and is not a sample of anything:
the *same* comparison gives **+0.12% at seed 200000 and −16.6% at seed 800000**, and −12% at seed
200000 with 40 games instead of 60. Ir was the right *kind* of instrument (deterministic, load-immune,
same binary) — the sample was far too small, which no amount of determinism fixes.

The right instrument here is the built-in deterministic counter build (`-DMTG_PROFILE=ON`), which
costs nothing per game and so can run a real sample. Over **20,000 games** (seed 200000, shipped
config):

| counter | hold OFF | hold ON | delta |
|---|---|---|---|
| Search nodes (the budget unit) | 82,151,072 | 69,828,291 | **−15.0%** |
| ApplyPlanDirect calls | 84,910,430 | 72,419,445 | −14.7% |
| EnumeratePlans calls | 32,939,831 | 29,789,389 | −9.6% |
| GameState deep copies | 9,966,701 | 9,220,850 | −7.5% |

So the heuristic is a ~15% reduction in search work *as well as* a quality gain — pruning a card out
of the hand shrinks the plan powerset at every later decision. **Lesson: for a heavy-tailed deck,
prefer `MTG_PROFILE` counters over callgrind — determinism is not the same as adequacy, and a
60-game Ir total can flip sign on the next seed.**

### What the prowess exception is worth, before and after the bug fix

This is the part worth remembering, because the same numbers mean opposite things either side of it:

| | rule without exception (c) |
|---|---|
| with the projection bug | **+0.00292**, 0/20 seeds better — *the rule loses* |
| with the projection fixed | **−0.00128**, 20/20 seeds better — *the rule wins* |

So the bug was most of the story, exactly as suspected: fixing it swung the no-prowess variant by
0.0042 turns. What remains for exception (c) is small but real, and it is not a fudge — it marks the
rule's **domain of validity**. The rule's premise is "the spell's damage is worth the same whenever
you cast it." With a prowess attacker that premise is simply false: the spell is worth 3 + N now and
3 later. Where the premise fails the prune has no licence, so the decision goes back to the search —
which is the search-primary contract, not an exception to it.

**Is (c) masking a lethal-calculation bug?** The sharpest challenge, because the prune site only
runs when the projection says `!wins` — so exception (c) can *only ever* fire on plans already judged
non-lethal. Either those turns really are non-lethal (and (c) is about accelerating damage), or the
projection is still under-counting and (c) is papering over a second instance of the bug above. That
is decidable: `MTG_SV_LETHAL_AUDIT=1` applies every plan the prune touches on a copy, runs the real
combat, and counts how many actually kill the opponent.

| 2000 games | plans touched | ACTUALLY lethal |
|---|---|---|
| strict-pruned, seed 200000 | 728,274 | **0** |
| prowess-rescued (c), seed 200000 | 237,486 | **0** |
| strict-pruned, seed 800000 | 912,768 | **0** |
| prowess-rescued (c), seed 800000 | 256,321 | **0** |

**Zero missed lethals in ~2.1M touched plans.** The lethal calculation is correct after the haste
fix, and (c) fires exclusively on genuinely non-lethal turns. So (c) is not hiding a bug — it is a
real heuristic about *acceleration*: the damage arrives earlier, which pulls the eventual win in.

That distinction matters for the rule as stated. "There is no reason to cast Shard Volley before the
winning turn" is exactly right about the **card's own damage** — 3 now is 3 later. It does not cover
the prowess trigger, which is worth 0 later. The quantity that justifies casting is therefore not
"is this turn lethal" but "is the spell worth MORE now than later", and lethality is only the most
common way for that to be true.

**A warning for anyone re-running this audit.** The first version of it reported 45 missed lethals,
all `Shard Volley + Searing Blaze`, all short by exactly 2. That was the *probe's* bug, not the
engine's: `EnumeratePlansWithLand` plays the land into a copy before calling `EnumeratePlans` and
stamps `land_decided` on every plan it returns, so a hand-built probe Plan that leaves
`land_decided` false makes `ApplyPlanDirect` fall back to greedy `SimulateLandPlay` — which fires
Searing Blaze's landfall (1 → 3 damage) and fabricates the missing 2. Set `probe.land_decided = true`.

**Is (c) just a budget artifact?** A second fair challenge: holding leaves an extra card in hand, which
enlarges the plan powerset at every later decision, so "allowing the cast is better" could be cheaper
enumeration rather than real damage. It is not, on two independent counts:

1. Holding *reduces* search work by ~15% (the counter table above), so it cannot be costing budget.
2. Re-running the (c) A/B at **unbounded budget** (`budget_ms: 0`, depth still 6) reproduces the
   effect exactly — **−0.00022, t = −3.69, 13/1/6** against −0.00019, t = −3.05, 12/2/6 budgeted.
   With budget removed entirely the benefit is if anything slightly larger.

The mechanism is concrete. Turn 3, Monastery Swiftspear (1/2, prowess, haste) attacking. Cast Shard
Volley pre-combat: 3 to the face plus a pumped 2/3 connecting = **5 damage this turn**. Hold it:
Swiftspear connects for 1, and the 3 arrives on some later turn = **4 damage total**. The prowess
trigger is use-it-or-lose-it, so casting early is worth one extra damage permanently — the same
*kind* of same-turn payoff as Spectacle's discount, which is why it passes the "does it buy
something this turn?" test. What it trades away is a land, which may or may not convert into more
than that later; the measurement says it narrowly does not.

## Measured dead end

Whether exception (b) requires the sac-land burn to be the plan's **only** face-damage source
(strict) or merely allows any plan holding a not-yet-active Spectacle spell (permissive) is
**byte-identical over 100k games**. The powerset never affords Light Up at full cost, so the only
`{Shard Volley, Light Up}` plans come from `EnumeratePlans`' Plan-B spectacle builder, where the
sac-land burn is the sole trigger by construction. The strict form is kept because it is the rule
as reasoned; the permissive branch was deleted.

## Notes / follow-ups

- **d0 moves a lot** (−0.031 to −0.099 depending on seed, vs −0.0015 at d6). The greedy rollout has
  no lookahead to discover the cost of the land, so the rule is worth far more there. That also
  means burn's value/eval label generation — which labels via a d0 rollout — now has a different
  baseline policy than the one its current sidecars were trained under. Not blocking (stale
  value-leaf tables measured near-free), but it is a reason burn is a candidate next time the
  regeneration queue runs.
- The prune is **not** applied to `EnumeratePlans`' Plan-B spectacle builder, which needs no gate:
  every plan it emits pairs the trigger with the Spectacle spell, i.e. exception (b) by construction.
- Generalisation, if another sacrifice-a-land spell is ever added: the hook
  (`DecisionProvider::HoldsSacLandBurnUntilLethal`) keys on the deck, and the scan keys on
  `Action::sacrifice_land`, so a second such card is covered without change — but exception (c)
  assumes the payoff is *prowess*, and a different same-turn payoff would need its own clause.
