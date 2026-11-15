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

On a fresh 5,000-game paired PGO sample (seeds 9,200,001–9,205,000), both
arms won 4,894 games. The flag-on arm changed four play digests: three kept
their win turn and one moved from turn seven to turn eight; none improved.
Loss-penalized average moved from 5.5748 to 5.5750, and search units from
259,571,921 to 259,707,511 (+0.052%). The summed per-job runtime rose from
32,444,377 to 33,094,097 ms (+2.0%); these sequential runs do not isolate
CPU contention. In the slower game (seed 9,202,050, index 2,049), the shipped
depth-six/budget-20 search chose Careful Study before Life from the Loam on
turn four with the mask, delaying the win. At depth eight with unlimited budget,
both arms make the same turn-four Loam play and win on turn seven. This is a
bounded-search sensitivity, not a lost legal line. The held-out acceptance bar
requires every affected deck's net turn change to be non-positive, so this
version remains default off. The next step is to narrow its trigger further
without losing the four focused oracle repairs, then repeat a fresh held-out
comparison.

A follow-up shortcut skipped a held solve when the payer's total-mana upper
bound proved that reserving the scarce sources could not pay the current cast.
It preserved the legal-payment set, but the slower seed still chose Study first
and won on turn eight. The shortcut was discarded; the source remains the
default-off trial measured above.
