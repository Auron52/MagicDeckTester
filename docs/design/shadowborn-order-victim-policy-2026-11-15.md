# Shadowborn order audit and fixed sacrifice victim

The latest Shadowborn mana-clean block reported five `APPLY-DROP-ORDER` events
in seed 30075090, turn four. The oracle reordered Culling the Weak before the
hand Shadowborn Apostle, sacrificed the battlefield Reaper from the Abyss, and
then cast the Apostles with Culling's mana. This is a rules-legal cast sequence,
but it changes the deck's fixed sacrifice choice: when an Apostle is in hand,
Shadowborn's user-approved order casts and sacrifices it before consuming a
Demon. The searched Culling action records that hand victim in
`trick_hand_target`; the alternate-order audit had ignored that dependency.

The oracle now excludes a reorder that puts a creature-sacrifice spell before
its required hand victim when the deck has a fixed victim policy. All other
payment and order alternatives are still checked. The five events now report
quiet `PHANTOM` with an explicit policy reason, and the exact seed has zero
mana misses, no oracle gap, no cap, and no unmodeled case. The 10-seed focused
Shadowborn replay has zero order flags, 15 remaining `APPLY-DROP` payment
events, one hidden-information case, and no oracle gap/cap/unmodeled case.
The engine's play path is untouched.

A broader trial that counted every unavailable activation as an oracle drop
misclassified speculative Apostle activations as phantom plans; it was
reverted. A trial to fold impossible pre-combat Jetmir declarations merely
moved these order reports to main two and increased them from five to fifteen;
it was reverted too. The fix above models the actual deck policy at the
oracle's alternate-order check. The focused unit test and full Release suite
passed. Raw replays: `build/scratch/gods_counter_mask_ab/shadow_order_*` and
`shadow_repros_policy.*`.

The remaining Shadowborn queue is the 15 payment drops, followed by a fresh
5,000-game mana-clean block and scoped viewer/regression checks.
