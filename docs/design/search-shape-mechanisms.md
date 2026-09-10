# Why each cell of the shape table reads what it reads

Companion to `per-deck-search-shape.md`, which says WHICH shapes survive. This one says WHY, per deck, from
per-decision instrumentation rather than from the aggregate ratios. Method: 196 single-worker runs
(`logs/shape_why/`, 60 games, seed 860000, `MTG_ROLLOUT_STATS=1`), one per deck x shape x configuration, so
each process's counters describe exactly one cell. Aggregate with `logs/shape_why/agg.py`.

The units, committed depths and node counts below are DETERMINISTIC structural facts and hold at 60 games.
Quality claims do NOT — they come from the 8 x 500-game screen, because 60 games cannot separate a 0.005
win-turn difference (see the sign-test note in `per-deck-search-shape.md`).

## Four mechanisms explain the whole table

**1. The rollout ladder converts its budget into SIMULATION instead of DEPTH.** It is not merely expensive,
it is expensive AND shallower: on 15 of 18 modelled decks it commits 1.0-1.9 plies below the shipped shape
while costing 1.5-20x. The cause is visible in where its units go -- the search tree is 2-6% of them
(`units.fs_pre` + `units.fs_main2`), the rest is rollouts to game end. Examples at d5b20, mean committed
depth, shipped vs rollout ladder: auras 4.08 -> 2.32 (7.6x cost), stompy 4.22 -> 2.29 (4.0x), slivers
4.18 -> 2.46 (11.1x), fivecolour 4.08 -> 2.36 (1.5x), critter 4.75 -> 3.29 (9.5x). A shape that pays more
and sees less is dominated on both axes at once, which is why it leaves the menu.

**2. A MODEL leaf widens the search tree 1.1-1.8x at the SAME committed depth.** Measured with the shape
held fixed -- the escalation ladder, leaf the only variable, same deck, same seeds:

| deck | model nodes / constant nodes | deck | ratio |
|---|---|---|---|
| melira | 1.73x | dragonstorm | 1.50x |
| dragons | 1.63x | knights | 1.49x |
| fivecolour | 1.49x | stompy | 1.43x |
| critter | 1.41x | hinata | 1.35x |
| slivers | 1.25x | minotaur | 1.24x |
| kitty | 1.17x | burn | 1.12x |
| breaching | 1.05x | **antilife** | **0.96x** |

Committed depth is unchanged to within 0.05 plies in every one of those pairs, and memo hit rates are
identical to within a few points (checked on 7 decks: solve-memo 14-63%, same both ways), so this is neither
a depth effect nor a reuse effect -- the model probe simply expands more nodes per pass. The best-supported
reading is that a constant leaf turns each pass into a PROOF search, where a subtree that cannot contain a
proven win is cut, while a model leaf turns it into an OPTIMISATION search, where every last-ply plan must be
expanded to compare its estimate against the incumbent. That explanation is consistent with all 14 decks but
has not been confirmed at the code site; antilife being the lone inversion is the case to look at first.

**3. How often the probe PROVES its win decides escalation-vs-final-depth.** The final-depth single pass
fires only on decisions where the probe committed nothing provable. Count of single passes per ladder
decision at d5b20: slivers 1/61, knights 2/62, auras 2/62, goblins 5/65, fluct 6/66, critter 10/70, th
14/154 -- versus fivecolour 311/646, hinata 77/136, dragons 28/88, kitty 22/82. Where the probe nearly always
proves, BOTH the escalation and the final-depth pass are nearly free, and the leafless escalation wins simply
by not adding a pass at all (this is Breaching: identical committed depth 3.13 and identical 188 passes under
every shape, yet the final-depth arms cost 5.6x because their rare rollout is enormous on a cascade deck).
Where the probe frequently cannot prove, one pass at the end beats escalating at every depth.

**4. CAVEAT -- part of the leafless advantage is an asymmetry this work introduced.** The constant-leaf
exhaustion stop (2026-09-10c) cuts a LEAFLESS pass at `used >= budget`; a MODEL pass has no such stop and
runs to the 25x proportional overrun ceiling. Setting `MTG_CONSTANT_EXHAUST_MULT=25` makes the rules match:

| deck | leafless tree @1x | @25x | model tree | share of the gap explained |
|---|---|---|---|---|
| dragons | 112,862 | 158,788 | 190,817 | 59% |
| hinata | 861,945 | 2,223,897 | 1,176,536 | more than all of it |
| knights | 58,589 | 58,589 | 85,925 | none (no pass overruns) |
| critter | 66,570 | 66,570 | 93,723 | none |

Confirmed again for the ESCALATION arm (not just the final-depth arm): on slivers, knights and critter,
`esc_nl` at stop-1x and stop-25x are IDENTICAL to the unit (79,413 / 58,054 / 73,320 nodes), so the stop never
fires there at all, while ship still expands 1.25x / 1.49x / 1.41x more nodes. On auras the stop DOES fire and
hides waste: 112,174 nodes at 1x against 152,010 at 25x for identical play (4.22 both). So mechanism 2 is real
and independent on decks whose passes fit the budget, and partly confounded on decks whose passes overrun. **And the comparison points somewhere useful:** play is IDENTICAL at 1x and 25x on all
four decks (avg 5.30 / 5.70 / 4.32 / 4.93 unchanged), so the extra work a model pass is permitted to do buys
nothing there. Applying the exhaustion stop to MODEL passes is therefore an untested candidate lever, not a
conclusion -- it needs a screen.

## Where the HEURISTIC actually gets used (and why "skip it" has nothing to skip on some decks)

A shape can only win by avoiding the heuristic where the heuristic is being used. Under the shipped shape at
d5b20, `interior_esc` (interior nodes re-walked by the heuristic escalation) as a share of all interior nodes:

| deck | escalated share | rollout share of units | decisions touching the heuristic ladder |
|---|---|---|---|
| slivers | 0.008% (8 of 99,081) | 0.95% | 2 of 61 |
| auras | 0.00% | 1.2% | - |
| breaching | 0.00% | 0.0% | - |
| critter | 0.00% | 1.3% | - |
| burn | 0.1% | 1.5% | - |
| melira | (see below) | - | - |

So on Slivers the heuristic is ALREADY effectively skipped: the search proves or refutes nearly everything
inside the horizon and the rollout leaf is 1% of the work. That is why no shape can win quality there and why
every shape reaches the same 4000 outcomes -- there is no heuristic use left to remove, and the value leaf's
TRUST is not load-bearing either, because proof arrives first. It is not that trust is "unnecessary" in
general; it is that this deck wins on turn ~4.2 inside a 5-ply horizon, so the horizon is rarely the binding
constraint. The decks where the heuristic IS load-bearing are the ones where the probe often cannot prove
(fivecolour 311 single passes / 646 decisions, hinata 77/136) -- and there, removing it costs quality.

**RETRACTED:** an earlier reading of this took `[leaf-eval] published=0` as "the value leaf is never
evaluated". That counter measures TIEBREAK exposure (the life term at a horizon leaf), not value-model
evaluations, and its line is simply absent when nothing published. There is no counter for value-leaf
evaluations; do not infer one from that line.

## Per deck, at d5b20

`heur` depth loss = plies below the shipped shape. `widen` = mechanism 2's ratio. `proves` = single passes
per ladder decision (low = the probe nearly always proves its win).

| deck | heur: cost / depth loss | widen | proves | what the row is saying |
|---|---|---|---|---|
| antilife | 2.91x / 1.00 | 0.96x | 18/78 | the ONE deck where the model does not widen the tree, so fit_v is the cheap final-depth arm (0.91x fit_nl) on fewer single passes |
| auras | 7.59x / 1.76 | 1.31x | 2/62 | probe nearly always proves; leafless escalation is the only arm under ship (0.78x) |
| breaching | 19.88x / 0.00 | 1.05x | ~0/60 | every shape commits at 3.13 in 188 passes; the rollout is so costly that merely running one is 5.6x |
| burn | 4.13x / 1.46 | 1.12x | 16/76 | low widening, so fit_v edges fit_nl (0.98x) on 12 vs 16 single passes |
| creature_giving | 3.16x / 1.61 | 1.17x | 17/77 | leafless looks good on the stand-in route and INVERTS on the sidecar route (see finding 12) |
| critter | 9.46x / 1.46 | 1.41x | 10/70 | widening dominates; fit_nl 1.12x cheaper than fit_v at identical depth and single count |
| dragons | 2.41x / 1.31 | 1.63x | 28/88 | the strongest final-depth case at d5b20 (fit_nl 0.56x) -- deep commits, heavy widening |
| dragonstorm | 3.01x / 0.85 | 1.50x | 11/71 | the staged candidate; cheap at d3b10 for the same reason |
| fivecolour | 1.52x / 1.72 | 1.49x | 311/646 | the probe fails to prove half the time, so the final-depth pass pays (0.72x) -- and it locks d6, unscreened |
| fluct | 1.00x / 0.00 | -- | 6/66 | no model at all; the relaxed leafless gate is the whole adoption |
| goblins | 3.74x / 1.08 | 1.14x | 5/65 | proves often; nothing beats ship, locks d6b40 (unscreened) |
| hinata | 1.13x / 1.14 | 1.35x | 77/136 | heavy widening AND frequent unproven decisions; its passes overrun, so mechanism 4 applies |
| kitty | 3.04x / 1.62 | 1.17x | 22/82 | ship wins on both axes at both configurations |
| knights | 8.70x / 1.41 | 1.49x | 2/62 | widening with no overrun: the cleanest isolation of mechanism 2 |
| melira | 0.71x / 0.75 | 1.73x | -- | the inversion: ship is the DEEPEST (3.25) and the WIDEST (3.08M nodes), so every alternative is cheaper and every one is worse |
| minotaur | 5.68x / 1.75 | 1.24x | 11/71 | fit_v cheaper despite widening, on 4 vs 11 single passes -- the two mechanisms competing |
| slivers | 11.11x / 1.72 | 1.25x | 1/61 | 0 of 4000 games change win turn under any shape; decisions differ, outcomes do not |
| stompy | 4.02x / 1.93 | 1.43x | 10/70 | biggest depth loss for the rollout ladder in the suite |
| th | 3.77x / 0.44 | 1.17x | 14/154 | proves often (154 decisions, 14 passes); shapes are near-ties on cost |
