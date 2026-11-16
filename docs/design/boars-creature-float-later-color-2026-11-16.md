# Boars double-Decimator mana: protect the later green pips

Boars seed 30,000,049 T4 offered `Forest, Decimator, Decimator`. The first
Decimator emerged from Somberwald Sage for `{3}{G}{G}{G}`; the second emerged
from the first for `{G}{G}{G}`. The oracle paid the first with Gruul Turf,
Forest, and Sage, then paid the second with Ignoble Hierarch, Stomping Ground,
and Birds of Paradise. The engine dropped the second cast.

The engine's whole-line prepay had already floated all available mana. Its
creature reserve held six green plus two flexible units, and its general
reserve held one red. The first spell's restricted-reserve-first generic drain
spent green that the second spell visibly needed while leaving the red unit
unspent. The resulting two green and one red could not pay the second spell's
three green pips. This was a payment allocation defect, not an extra mana
source or a new plan.

`TapForCostSharedOnceImpl` now holds up to the spare typed general mana back
from the creature reserve's generic drain when a known later cast needs a
colour the creature reserve can supply for both casts. The held generic pips
then drain from general mana. It excludes hidden-reveal lines, big-spell
restricted mana, and general colours with a current or later pip. The
`MTG_CREATURE_FLOAT_LATER_COLOR=0` hatch restores the old order. The first
trial used `LineOwedAfterOwnPips` too early, before `PayOwnPipsScope` was
active; subtracting this cost's pips directly from `g_line_unpaid_cost` fixed
that local accounting.

Evidence:

- Exact seed 30,000,049 T4: `APPLY-DROP=1` to no flags.
- First 500 Boars games: `APPLY-DROP` 139 to 36, `APPLY-DROP-ORDER` stayed 69,
  `DOMINATED-CREATURE` 191 to 183. The double-Decimator drop cluster clears.
- Fresh 2,000 paired Boars solo games (seeds 9,920,001–9,922,000): seven play
  digests change, zero win-turn changes; both arms average 4.8190 turns.
- 3,363 unit cases and 3,614,434 assertions pass. Combined smoke and regression
  over Boars, Goblins, Soldiers, Bruna, Angels, Merfolk Turns, Minotaur and
  their selected 2HG cases finds 23 faster and one slower searched game. The
  slower Soldiers game recovers its old turn at 4× budget. Depth zero has 46
  faster and ten slower games; Boars d0 gi913 casts the second Decimator early
  and wins one turn later. Viewer protocol has zero drift across 864 refs;
  both required search wins pass. Ground truth was accepted with the budget
  churn noted.

Boars remains unclean. The remaining first-500 drops are chiefly Horn,
Flinthoof, and Herald ordering or payment lines; the new rule only addresses
the creature-reserve generic allocation. A fresh 5,000-game gate is still due
after this source change.
