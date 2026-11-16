# Spirits: same-main Souls flashback after Vialed Geist

The first fresh 5,000-game PGO mana gate for Spirits found 31 `ENUM-GAP`
flags. They cluster on a Lingering Souls hand cast, Patrician Geist put by
Aether Vial, and Lingering Souls flashed back in the same main. The oracle
could pay the line, but the plan's aggregate mana gate rejected it.

The pending flashback candidate starts in hand and carries a card name but no
`Action::def`. `SameTurnReducerDiscountFor` returned zero immediately for a
null definition, so it missed Geist's `{1}` reduction of the flashback's
`{1}{B}` cost. The live cast correctly cost `{B}`. The gate now resolves the
graveyard cast's definition by its interned name before pricing same-turn
generic reductions. `MTG_GY_REDUCER_DEF=0` restores the previous behavior.
This corrects a price for an existing candidate; it adds no searched choice.

Focused seed 30,000,106, turn four, clears its enum gap with the fix on and
restores it with the lever off. In the first 500 games of the old gate block,
the five enum gaps fall to zero; nine payment drops remain in both arms.
A new unit case starts with Souls in hand, Vial(3), Geist in hand, and four
mana across three lands. It checks that the three-action plan is offered and
both rollout and executor create four Spirit tokens and exile Souls. The
case passes 61 assertions with the fix on and fails with the lever off.

A fresh 5,000-game paired Release A/B, seeds 9,960,001–9,965,000, has the
same mean turn in every 250-game chunk. Nine chunks had different play
digests; per-game replay of those 2,250 games found 12 changed play digests
and zero changed win turns. The summed search work changed by -5,504 units
across all 5,000 games. The run is under `build/scratch/spirits_gy_def_ab*`.

The full unit suite passes 3,364 cases and 3,614,495 assertions. The pooled
Spirits/2HG smoke and Spirits regression run passed all four smoke rows and
changed three searched regression digests. One regression game became one
turn faster and one became one turn slower, leaving the mean unchanged; the
slower game recovers its old turn at both 4× and 16× budget. The other two
changed games keep their win turn and show the newly legal double-Souls line.
All 561 scenarios passed; strict viewer replay had zero play drift or enum
gaps in 864 references, and require-beat passed. The reviewed ground truth
was accepted with the budget-churn note; `test/check_gt_logs.py` reports zero
stale or missing logs. The remaining Spirits payment and oracle flags need
separate work; this fix does not claim a clean gate.
