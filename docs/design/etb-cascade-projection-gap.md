# The ETB-cascade projection gap

**Status:** open, deliberately. Found 2026-08-30 while re-applying the Mirrorwing candidate-card
engine support. Not urgent for any shipped deck; written down because the fix is a one-liner that
looks obviously correct, has already been written once, and cannot currently be **validated**.

## The gap

Two code paths bring a permanent onto the battlefield:

| path | fires the param-gated ETB cascade (`OnGoblinEnters`)? |
|---|---|
| the executor, `EffectHandler::EnterBattlefield` | **yes**, on every entry, creature or not |
| the search's projection, `ApplyPlanDirect` | **no** |

`OnGoblinEnters` is the shared ETB cascade. Despite the name it is not goblin-specific — it reads
`etb_damage_any`, `etb_damage_each_opponent`, `etb_damage_devotion_color`, `etb_reveal_count`,
`etb_self_creates_tokens`, `etb_opp_creates_tokens`, `etb_team_pump_per_creature`, `etb_life_floor`,
`etb_destroy_own_noncreature_max`, `etb_opp_creatures_debuff`, `tutor_to_hand`, `tutor_to_top`,
`tutor_types`.

So the search **under-projects** any permanent whose ETB is expressed through one of those params:
it plans as though the ETB does nothing, then the executor fires it for real. That is an fd-diverge
in the same family as the Puresteel note in `TurnSolver.cpp`.

The fix is to add `OnGoblinEnters(state, state.active_player_index, bi);` beside the existing
`OnDragonEnters` call in `ApplyPlanDirect`'s permanent-enter branch.

## The surface is much wider than a stale comment claimed

The version of this fix that arrived with the Mirrorwing stash carried the justification:

> *"Byte-identical for every existing deck: all 8 cards carrying a cascade param are instants or
> sorceries, which never reach this permanent-enter branch."*

That is **false**, and it is why the change looked inert. Counted against `cards.json` on
2026-08-30: **18 permanents** carry a cascade param against **5** instants/sorceries.

```
Craterhoof Behemoth   Fanatic of Mogis     Goblin Matron       Mogg War Marshal
Elderscale Wurm       Frontline Heroism    Hornet Queen        Muxus, Goblin Grandee
Goblin Chainwhirler   Goblin Instigator    Hunted Phantasm     Nest Invader
Massacre Wurm         Siege-Gang Commander Stoneforge Mystic   Terastodon
Twinshot Sniper       Undercellar Myconid
```

Lesson worth generalising: **a byte-identity claim in a comment is a measurement with an expiry
date.** This one may well have been true when written and was still being trusted long after
`cards.json` moved underneath it.

## Why it is blocked: the tier cannot currently validate it

The `minotaur_regression_d5_s2002` / `_s3003` cells are **nondeterministic in the full tier**,
independently of this line and of any local change — the control commit flakes at the same rate.
Full details and the isolation ladder: `minotaur-d5-regression-flake.md`.

A search that stops under-projecting a real ETB *should* move some lines, and that would be a
perfectly legitimate rebaseline. But with those exact cells flipping run to run, a genuine movement
cannot be told apart from the flake. **Fix the flake first**; then this becomes an ordinary GT
change, accepted on its merits.

## Retracted claims — recorded so they are not re-derived

Two conclusions were published during this investigation and are both **wrong**:

1. *"The `OnGoblinEnters` line causes the minotaur failures."* It does not. The cells flake with the
   line absent, and on the unmodified control commit.
2. *"`ProfileCache` eviction is the root cause."* It is not. Asserted from ONE clean run at a raised
   cache cap; repeated five times, 3/5 still fail.

Both came from concluding on a **single run of a flaky thing**. On this suite, treat one run as one
sample of a distribution until proven otherwise, and repeat every arm of a comparison before
believing the contrast.

## What would close it

1. Fix the nondeterminism (`minotaur-d5-regression-flake.md`).
2. Re-add the call.
3. Re-run the full tier and accept the resulting GT movement on its merits.

Until step 1 lands, this projection gap is the lesser of the two problems, and the safer place to
sit is with the gap open.
