# Shard Volley hold — "don't spend a land before it buys something"

**Status: ADOPTED (default ON), ~~pending ground-truth rebaseline~~** *(rebaselined 16 minutes
after adoption — 7a61ea43, 2026-08-04; noted 2026-09-03)*. Provider-owned prune in
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
| (c) | the card **expires this turn** | Light Up exiled it; past `m_staged_expiry` it is gone, not deferred |

**Flood is not a fourth exception.** Surplus lands make an early cast *harmless*, never *better* —
there is no upside to weigh, so the rule needs no flood branch. That also makes deferring free in
exactly those hands, which is what makes the rule safe to apply unconditionally.

**Neither is prowess** — see the retraction below. And (c) is deliberately narrow: it fires on the
LAST legal turn, not merely because a card is staged. "Any staged card" was measured and is *worse*
(−0.00018 vs −0.00044): there is no reason to play it earlier than required.

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
| fix + hold, `(a)+(b)` only | −0.00128, t = −11.45, 20/20 better | — |
| fix + hold with prowess (retracted) | −0.00147, t = −12.00, 20/20 better | −0.00176, t = −11.19, 20/20 better |
| **fix + hold with expiry (ADOPTED)** | — | **−0.00204**, t = −12.30, **20/20 better** |

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

### RETRACTED: the prowess exception, and what it was really detecting

An earlier version of this rule carried a third exception — "the cast pumps a prowess attacker that
connects this turn" — justified as a same-turn payoff of the same character as Spectacle. **That
reasoning was wrong, and the user refuted it directly:**

> "We don't kill our prowess creatures, so you can always play shard volley on the final turn and
> use it to enable prowess. In fact, holding it for later prowess is always as good if not better,
> since there might be multiple prowess creatures."

Exactly right. The goldfish opponent has no removal and no blockers, so a Monastery Swiftspear that
resolves stays on the board and attacks every turn. The prowess trigger is therefore **not**
use-it-or-lose-it: casting Shard Volley on turn N+k still pumps, and by then there may be *more*
prowess creatures, so the same spell is worth *more* later. Holding is weakly better on prowess too.

So why did the prowess exception measure as an improvement at all? Because it was a **proxy** for
something real that it only partly overlapped. Burn's Light Up the Stage exiles the top two cards
*playable only until the end of your next turn*; a Shard Volley exiled that way is **destroyed**, not
deferred, if held past expiry. A prowess attacker is usually on board in this deck, so exception (c)
incidentally rescued a slice of those expiring casts — the audit counts 7,632 staged casts among the
237k plans prowess rescued, out of 21,968 among the 728k the strict rule dropped. It was catching
roughly a third of the real cases, for the wrong reason.

Replacing the proxy with the actual mechanism is both cleaner and better (100k games per arm, paired,
against the same strict `(a)+(b)` baseline):

| rule | delta vs strict | t | seeds b/w/t |
|---|---|---|---|
| prowess (retracted) | −0.00019 | −3.05 | 12/2/6 |
| any staged card | −0.00018 | −3.45 | 15/3/2 |
| **expires this turn (adopted)** | **−0.00044** | **−6.68** | **19/0/1** |

Head-to-head, the expiry rule beats the prowess rule by **−0.00025, t = −5.00, 16/1/3**.

**The lesson is about how the wrong rule survived.** It was adopted because it measured better and I
supplied a plausible mechanism for it afterwards. The mechanism was never tested — and the number it
"explained" was real, so the story felt confirmed. A heuristic that measures well is evidence that
*something* is there, not that the reason you invented is that something. The way out was to ask
what, concretely, the engine destroys when a card is held: exactly one thing does, and it is not
prowess.

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
- Exception (c) generalises to **any** card that can leave the zone it is held in. Today only Light Up
  the Stage stages cards for this deck, but Apex-of-Power-style impulse exile has the same shape, and
  `Card::m_staged_expiry` already carries the turn, so the check is card-agnostic as written.
- `MTG_SV_LETHAL_AUDIT=1` also reports the staged/expiring population, which is how the proxy was
  caught. Re-run it after any change to Light Up's modelling.
