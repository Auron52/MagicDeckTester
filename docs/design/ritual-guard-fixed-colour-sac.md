# Solve's rituals-for-payoff guard misprices a fixed-colour SacForMana (DEFERRED)

Found 2026-10-22 while implementing the USER's Generator Servant rule for NivMizzetCombo
(`MTG_NIV_SERVANT_FIREMIND_ONLY`). Not fixed there: the fix moves BreachingDragonstorm's playout, so it
needs its own A/B and GT verdicts.

## The defect

`TurnSolver::SolveUncached`'s rituals-for-payoff guard (`MTG_NO_RITUAL_PAYOFF_GUARD` opts out) drops a
non-winning subset holding a SURPLUS ritual: one whose removal leaves everything else payable. It
tests each selected action with `ritual_float > 0` by taking its float back out of the credited pool's
`wild` slot:

```
ewr.wild = std::max(0, ewr.wild - (full_credit - cred_r));
```

Since the BreachingDragonstorm gi2 fix (2026-09-03), a FIXED-COLOUR SacForMana is not credited to
`wild` at all: `CreditFixedColorSac` puts it in its pinned colour (Generator Servant `{C}{C}` -> 2
colourless; Dwarven Ruins / Svyelunite Temple -> their colour, minus the land's own tap mana), and it is
excluded from `simul_ritual_credit`. The guard was not updated, so for such a source it:

- subtracts from the WRONG slot (wild, which holds lands' either-colour mana), and
- leaves the source's real credit in the pool.

It is wrong in both directions. With `eff.wild == 0` the removal changes nothing, so a sacrifice the
line NEEDS reads as surplus and the plan is dropped (the playout never ramps off it). With
`eff.wild >= 2` it takes two of the lands' flexible mana instead, so a sacrifice the line does NOT
need can read as load-bearing and survive. (Separately, the guard runs only on the credited-pool path:
a subset admitted by the filter-land fallback is never tested. A Niv d0 probe, `MTG_SOLVE_PROJ_TRACE`,
shows unneeded Servant sacrifices surviving Solve -- e.g. `{Generator Servant (cast); Generator Servant
(sac)}`, a 2-mana cast with 3 lands up -- by one route or the other. Harmless for the pick: such a
subset never beats its no-sacrifice sibling, equal value with a smaller mask. The harmful direction is
the first one, a NEEDED sacrifice dropped, so the playout under-uses the Servant as ramp.)

## Reach

Solve only: the d0 runner and the horizon playout. EnumeratePlans (the search's menu) has no ritual
guard. Decks holding a fixed-colour sac source: NivMizzetCombo (Generator Servant), BreachingDragonstorm
(Dwarven Ruins, Svyelunite Temple).

## The fix (when picked up)

In the guard, treat an `r` for which `CreditFixedColorSac` applies separately: remove its pinned credit
from its pinned colour in `ewr` / `encwr` (the same `amt` CreditFixedColorSac added), and leave it out of
`flat_sum` / `full_credit` (it never entered the wild total). A/B on BreachingDragonstorm and Niv, then
smoke/regression GT verdicts for the moved rows.
