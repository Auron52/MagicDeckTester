# d0 greedy scorer: no same-turn lord credit, no enter-drain credit (DEFERRED)

**Status:** QUEUED 2026-09-27 (USER: "The d0 scorer can be queued, but is not top priority quite
yet") -- behind the Giants value-leaf follow-up and the Pirates value leaf. Originally deferred 2026-09-27. Found while adopting `MTG_PAYABLE_ORDER` on Pirates; the lever is correct
and exposed these PRE-EXISTING gaps in `TurnSolver::Solve` (the depth-0 greedy scorer, also the rollout
policy). Searched play is unaffected (it chooses by rollout); only d0 and rollout policy move.

## Evidence (Pirates, d0, `MTG_PAYABLE_ORDER=1` vs `0`, root-caused by trace)

`SameSubsetRevealSurcharge` used to keep the LARGER surcharge of the selection order and the rank order,
so a subset whose selection order could not pay was rejected even when the order the apply realises
pays. The lever routes both through the payable order, so these subsets become admissible -- correctly.
What the scorer then does with them is the defect:

* **No lord credit in the win projection (s1931 gi930, T4 -> T5).** {Adaptive Automaton, Daring
  Buccaneer} is lethal (16 damage vs 14 life) but scored `wins=0` -- `SolveUncached` has no
  `power_bonus`/lord term in `projected_atk` -- so {Kitesail Larcenist, Buccaneer} (value 1199-1200 vs
  1000) wins the comparison.
* **No enter-drain credit (s2070 gi68, T5 -> T6).** `own_creature_enters_opp_life_loss` (Forerunner of the
  Coalition) never appears in Solve. Two subsets tie at 1400; the lowest-mask tie-break picks the one
  that casts Forerunner LAST (Buccaneer must reveal it first), so it drains nothing. A puts-last line
  draining 2 exists, but Solve has no puts-last Vial variants (only `EnumeratePlans` does).

## Proposed fixes

1. Lord credit: for a cast with `power_bonus` (chosen-type or `subtypes_affected`), add bonus x attackers
   of that subtype to `projected_atk` (a `ComputeLordBonus`-style pass over the subset).
2. Drain credit: count Pirates (the watched subtype) entering after a drain source in the realised order,
   plus every entry while one is already on board; add to `direct_dmg` and a small score term (at
   least as the tie-break instead of lowest mask).
3. Optional: puts-last Vial variants in Solve for subsets holding a Vial put and a reveal-or-pay cast.

Both touch every deck with lords/drains at d0 (Slivers, Knights, Goblins, ...), so they need their own
suite measurement and GT re-accept.
