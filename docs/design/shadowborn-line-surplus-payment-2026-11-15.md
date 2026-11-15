# Shadowborn Lake and Lotus line surplus payment

Shadowborn seed 30075295, turn four, main two, offers Sire of Insanity followed
by Culling the Weak. Lake of the Dead and its sacrificed Swamp produce five
black; Lotus Bloom produces three red. Sire costs `{4}{B}{R}`. The old generic
drain spent all four black left after Sire's coloured pips before considering
red, leaving two red and no black for Culling. The mana oracle found the legal
payment that leaves one black and one red.

`SpendFloatingTowardCost` now excludes the spell's already-paid coloured pips
from the line demand and spends each colour's surplus before touching any
colour the rest of the line still owes. It remains an ordering choice with
fall-through: every generic pip still pays if total mana suffices.
`MTG_LINE_SURPLUS_KEEP_DEMAND=0` restores the previous order.

The focused test passes with the switch both on and off. All 3,353 Release unit
cases pass with it on. The ten-seed Shadowborn oracle replay fell from 15 to
13 `APPLY-DROP` events, removing both occurrences in seed 30075295; no other
issue class increased. A fresh 1,000-game paired Shadowborn run (indices
85000–85999) had identical win turns and play digest in every game, with four
more search units out of 18,567,213 (+0.00002%).

A pooled smoke plus regression run covered 48 cells and 13,112 games across
Shadowborn, Gods, Dragonstorm, Prevent Damage, and Eldrazi Displacer Flicker.
All smoke cells passed ground truth. One Prevent Damage regression cell differed
from ground truth by one turn and three same-turn play digests; an exact
same-binary, switch-off replay of its 150 games produced the **same** average,
digest and every per-game win turn as the switch-on run. This is pre-existing
ground-truth drift, not an effect of the payment change. The exploratory run
skipped the viewer sweep; the focused test and full unit suite cover the new
payment path.

Raw results are in `build/scratch/gods_counter_mask_ab/shadow_line_surplus_*`,
`pd_line_surplus_control_*`, and `line_surplus_regression_on.*`. The remaining
13 Shadowborn drops all follow tutor breakpoints, so they remain in the queue.
