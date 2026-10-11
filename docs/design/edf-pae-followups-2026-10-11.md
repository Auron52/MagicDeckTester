# EDF: what MTG_SOLVE_PENDING_AURA_EXACT exposed, what was fixed, what is still open (2026-10-11)

`MTG_SOLVE_PENDING_AURA_EXACT` (PAE, docs/design/discard-round-play-defects-2026-10-10.md s4) stopped
Solve crediting an in-hand land Aura with colour for a subset that does not cast it. The ab3 A/B then
showed EldraziDisplacerFlicker games slower than the pre-round arm; a root-cause pass reproduced each
one (repro kit: `logs/discard_impl/rc3/README.md` on the primary machine, slower list
`rc3/slower_edf.txt`). This doc records the fixes that landed, behind one default-ON lever each, and
the two defects that are still open.

## Fixed (each lever =0 restores the previous behaviour byte-identically)

| lever | defect | game |
|---|---|---|
| (none) | five reads of the go-off projection's `casting` list dereferenced the nullptr a non-cast pick puts there (the MTG_EDF_PROSPECTIVE_DEBUG print segfaulted every traced game) | trace runs |
| `MTG_SOLVE_UNTAP_CREDIT` | no payer credited Cloud of Faeries' / Peregrine Drake's same-turn land untap, so {Cloud, Training Grounds} off one {U} land was rejected once PAE removed the widening that had admitted it | gi 1408 t4 board |
| `MTG_PREPAY_ENERGY_RESTORE` | `BatchPrepayMainCasts` rolled back a declined joint solve without giving back the {E} its coloured Aether Hub tap spent; the per-cast payer then found a spent-out Hub | gi 1985 t3: 7 -> 4 |
| `MTG_SOLVE_AURA_PAYER_CHECK` | a land-Aura subset that PASSES the board-only gates was never priced by the real payer; the gates cannot see which land carries the Aura or that an energy-less Hub makes only {C} | gi 1475 t7: 8 -> 7 |
| `MTG_EDF_ROUTE_BOARD_DEPLOY` | the COMBO OFF route skipped its deploy when the loop was already in play, so Training Grounds / land Auras / a land drop were never paid off the lands and a mana-neutral loop stalled | gi 547: 6 -> 5 |
| `MTG_EDF_GOFF_VERIFY_UNPROJECTED` | Solve's go-off cut verified the maximal blink only when the projection called it lethal; the blink apply's dig-and-wish finisher is outside the projection | gi 1676 t4: 5 -> 4 |

Root cause worth recording for gi 1985: it was NOT the executor holding the Hub's single energy for the
line's later {W}. The joint prepay tapped the Hub coloured, produced `wild`, declined, and its rollback
restored the battlefield, the float and both lives but not `energy_counters`. With the prepay disabled
the same game casts Trace + Displacer on turn 3 and wins turn 4.

## Open 1 -- gi 985: a legal line no payer realises (simulation and execution now AGREE on "no")

Board turn 5: Yavimaya Coast + Wild Growth, Mariposa Military Base + Fertile Ground; hand Trace of
Abundance, Dimensional Infiltrator. {Trace -> Mariposa, Infiltrator} is legal: tap the Coast for {U}
(plus the Wild Growth {G}) and Mariposa for {C} (plus the Fertile Ground's any colour as {R}); Trace takes
{R}{G}, Infiltrator {U} + {C}. Both payers (Solve's `SubsetPayableWithFilters` -> `TapForCostDirect`, and
the executor's `TapForCost` under its line scope and plan traits) pay Trace by tapping the Coast for its
painless {C} face, leaving {C}{C}, and Infiltrator fails. Probe: a unit-test reconstruction of the board
gives `trace ok float c2 / infiltrator FAIL` on BOTH payers. Before `MTG_SOLVE_AURA_PAYER_CHECK` the
board-only gates admitted the pair and the executor dropped Infiltrator; with it, Solve no longer
offers the pair (agreement), and the turn casts Infiltrator alone. The game is 7 either way (old arm 6).

What would make it castable: a per-cast payment that chooses a surplus land's face for the rest of the
LINE (`g_line_unpaid_cost`, bound in both apply worlds) -- today that steering is human-play-gated -- or
a prepay that accepts a joint solve producing `wild` by pinning the wild unit to the deficit colour (it
declines as "ambiguous"). Both touch every deck with multi-face lands and need their own A/B.

## Open 2 -- gi 1408: the turn-4 kill after an earlier Cloud is invisible to Solve

With `MTG_SOLVE_UNTAP_CREDIT`, turn 3 casts Cloud of Faeries beside Eldrazi Displacer (legal: Cloud's
untap refunds the lands). On turn 4 the kill is "Training Grounds alone, then the post-cast go-off digs
through Mariposa into Living Wish -> Essence Depleter". Solve picks {Training Grounds, Fertile Ground}
(more value, all lands tapped, no go-off) because no go-off candidate exists before the reducer resolves
(the loop is net-negative without it, so no chosen_x > 3 blink) and the projection does not see the dig.
The COMBO OFF route (now deploying Training Grounds) reaches Living Wish but stalls on COLOUR: its
`ChooseUntaps` refunds the {C} lands every iteration and never the Coast, the only {U} source the wished
Dimensional Infiltrator needs, while Depleter's {B} needs the Fertile Ground deployed first. Game 5
(old arm 4, tip 5). Directions: an untap set that keeps the finisher's coloured pips reachable, and a
hand go-off candidate for "cast the reducer, then blink".
