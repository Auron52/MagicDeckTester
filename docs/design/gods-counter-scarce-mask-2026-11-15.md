# Counter-land scarce-colour payment trial

## Reachability

Gods' remaining focused `APPLY-DROP` events were three payment shapes across
seeds 30033371, 30034180, and 30035713. The broad
`MTG_SCARCE_COLOR_MASK=1` held-first payment clears all four events, but it
also makes Gods seed 8900431 lose a turn-seven win even at depth eight and
unlimited budget. The loss occurs through coupled lookahead: a future branch
draws Gemstone Mine, the broad hold changes that branch's value, and the root
waits on turn five. A trial that requires the held source itself to carry a
counter cleared only seed 30035713. Requiring a live countered land on the
board still left the other three events.

`MTG_SCARCE_COUNTER_MASK=1` now uses the same held-first attempt only when a
counter-gated land is on the board and fewer than two Reflecting Pools are
present. The two Pools share their underlying colour providers, so the simple
scarce-provider count treats them as more independent than they are. On the
known losing game, the two-Pool exclusion restores the exact old turn-seven
digest. On the six focused Gods oracle repros, all four `APPLY-DROP` events
clear, the old sibling issue remains clear, and the previously capped game is
clean with the new 200,000-node retry. No focused game's play digest changes.
The trial is a payment order with an unrestricted retry; it does not remove a
searched action.

The card database defines only three lands with `enters_with_mana_counters`:
Gemstone Mine, Tendo Ice Bridge, and Vivid Creek. A sweep of shipping deck
files found them only in Gods. The trial's precheck therefore has no current
play effect on other shipping decks.

## Paired play and gates

On 1,000 fresh paired Gods games (seeds 9,100,001–9,101,000), both arms win
978 games and average 5.584 loss-penalized turns. Every per-game play digest is
identical. Search work changes from 54,972,642 to 54,974,118 units (+0.003%).
The first implementation ran 3.9% slower in summed job time; moving the cheap
counter-land/two-Pool precheck before the full scarce-colour calculation reduced
the difference to 1.8%. Those are sequential Release runs, not a controlled
CPU-time measurement. The optimized implementation matches all 1,000 earlier
trial digests and all 40 job unit counts exactly. Raw paired data are in
`build/scratch/gods_counter_mask_ab/`.

With the flag on, combined Gods/Gods2HG smoke and regression passed all eight
cells with exact ground-truth play digests, all 561 scenarios, and the strict
viewer replay (zero play drift, board divergence, or enum gaps among 864
references). The per-game audit reports zero changed games in either tier.

The flag remains default off while a 5,000-game PGO paired sample checks for
rarer quality losses. If adopted, the default-on change and a fresh Gods
mana-clean gate require a new PGO build stamped with the adopted source.
