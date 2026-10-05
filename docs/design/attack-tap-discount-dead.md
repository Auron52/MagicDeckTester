# The greedy's attack-tap discount never fires (deferred)

Status: **DEFERRED** (found 2026-10-05 while giving fungusb smoke s1001 gi239 its verdict). It did
not cause that game. Fixing it changes d0 and rollout play for every deck whose creatures tap for
mana, so it needs its own fleet-wide measurement and must not ride on another change.

## What the code means to do

`TurnSolver.cpp` (`SolveUncached` / `consider` and the `EnumeratePlans` twin) works out this turn's
attack as

```
atk_tap_need = combined.ManaValue() - pool_noncreature.Total();
atk_tap_lost = AttackTapDiscount(atk_mana_srcs, atk_tap_need);
projected_atk = pending_atk - atk_tap_lost + ...;
wins = projected_atk + direct_dmg + ... >= opponent life;
```

The comment says `need` is "the mana this subset must draw from creature sources, i.e. its cost
beyond what the non-creature pool covers; zero whenever the lands alone pay". It was added in
92c7ce076 (the FiveColour Faeburrow Elder case: cast pre-combat, lose the dork's attack).

## Why it is dead

1. `BuildNonCreaturePool` is the pool for *noncreature spells*. It is not a pool of non-creature
   *sources*, so it already includes every untapped creature mana source (the `is_dork` branch,
   and Badger-granted bodies via `ManaDefOf`). For any payable subset `need <= 0`, so the
   discount is 0.
2. `CollectAttackingManaSources` scans only `ManaDork` templates and `mana_rock`. It skips
   definition-less tokens (`if (!def) continue;`) and defined creatures whose only mana is a grant
   (Brightcap Badger on Fungi/Saprolings), so for Fungus it returns 0 sources.

Measured with temporary instrumentation on fungusb s1001 gi239 T6 (opp 8 life): every subset
printed `srcs=0 lost=0 poolnc=8 proj=15`, so the projection credited all 15 power whatever the
plan tapped.

## A candidate fix (prototyped, not adopted)

* `need = cost - max(0, pool_nc_total - sum(yield of atk_mana_srcs))`: the cost beyond the lands,
  rocks and non-attacking sources.
* `CollectAttackingManaSources` resolves sources through `LiveManaGrant` + `ManaDefOf`, the same
  two populations `UntappedManaUpperBound` credits since b5b47d54. Keep the real definition for
  power, and add no animate/dynamic power for a token that has no definition.

With that prototype behind a default-off flag the T6 projections became honest (`lost=3`,
`proj=12` for one Slimefoot activation), but gi239 stayed at T8. The game turns on the next issue
instead.

## The related limitation that does decide gi239

Repeatable mana-sink activations (`PermAbilityMode::PayToken` and the other K-axis modes) are
emitted with `chosen_x = K` but **one activation's cost on the subset's books** ("the rest are
paid inside the apply loop out of whatever the turn actually produces"), with `eval = K`. In the
greedy, a K=2 block therefore costs the same as K=1 and scores higher, so the greedy always takes
the larger K. The apply then pays the extra activations by tapping attackers. The searched path
prices this through rollouts. Only the greedy (d0 and rollout leaves) cannot.

## What to measure before adopting either half

* The whole regression tier at d0 plus the rollouts (d3/d5), because the change touches every deck
  with creature mana. Use the adoption bar in `.claude/skills/regression-testing.md`: a verdict for
  each changed game and every deck improved or neutral.
* Booking K x cost for the greedy only (`!g_search_candidate_enum`) is the narrowest fix for the
  K-block half, and the gi239 shape is its natural unit test.
