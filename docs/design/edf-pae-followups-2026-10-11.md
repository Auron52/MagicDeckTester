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
| `MTG_TAPAHEAD_PENDING_PIP_AUTO` (follow-up) | the ETB-untap tap-ahead committed the only {U} land's unit to {G} in autonomous play, stranding the very Cloud of Faeries it banked for (the human-play fix existed); now the pending cast's own pip is committed when no other source covers it | held-out gi 462: 6 -> 5 |
| `MTG_SOLVE_AURA_PAYER_AS_EXECUTED` (follow-up) | `MTG_SOLVE_AURA_PAYER_CHECK` priced the subset with the per-cast walk alone, stricter than execution, whose joint prepay pays lines the walk cannot | held-out gi 1011 t2: 5 -> 4 |

### Held-out measurement (EDF; seeds 5300001 d0 / 5310001 d3 b10 / 5320001 shipped d5 b20; paired per game, lever ON vs =0 with every other lever at default; net = sum(ON - OFF), loss = 9)

| lever | d0 (1500) net / faster / slower | d3 b10 (80) | shipped (40) |
|---|---|---|---|
| `MTG_SOLVE_UNTAP_CREDIT` | -143 / 166 / 50 (t -7.8) | -3 / 3 / 0 | 0 |
| `MTG_PREPAY_ENERGY_RESTORE` | -25 / 22 / 11 (t -2.6) | 0 | 0 |
| `MTG_SOLVE_AURA_PAYER_CHECK` (before AS_EXECUTED) | **+5** / 2 / 6 (t +1.5) | 0 | 0 |
| `MTG_EDF_ROUTE_BOARD_DEPLOY` | -57 / 50 / 0 (t -6.7) | 0 | -1 / 1 / 0 |
| `MTG_EDF_GOFF_VERIFY_UNPROJECTED` | -12 / 12 / 0 (t -3.5) | 0 | 0 |
| all five vs all off | -207 / 207 / 52 (t -9.4) | | |

Bruna and Fungus (the other land-Aura decks; only `MTG_SOLVE_AURA_PAYER_CHECK` can reach them): d0 2000 and
d3 b10 100 games each, 0 games moved. burn, Dragonstorm, Hinata2: digest-identical to the pre-fix tree.

Slower games, classified:
* `MTG_SOLVE_UNTAP_CREDIT` (50): 48 are the untap credit opening a strictly larger legal turn (Cloud of
  Faeries plus a Wish / Call / Displacer / second Cloud) that d0's value ranking takes and that turns out
  slower; 2 (gi 147, 462) are the executor dropping the Cloud on the tap-ahead's {G} commit (fixed by
  `MTG_TAPAHEAD_PENDING_PIP_AUTO`: gi 462 6 -> 5; gi 147's drop is gone but Cloud then becomes castable a
  turn earlier, the later go-off is the Open-2 shape, and the game stays 7 against the leaking arm's 5).
* `MTG_PREPAY_ENERGY_RESTORE` (11): every one is an Aether Hub board where the restored {E} buys a legal
  extra cast (Eladamri's Call, Cloud, Displacer) the leaking arm could not pay; d0 ranking, not payment.
* `MTG_SOLVE_AURA_PAYER_CHECK` (6): gi 1011 and 147 -- the check was stricter than execution (the joint
  prepay pays them), fixed by `MTG_SOLVE_AURA_PAYER_AS_EXECUTED`; gi 59 and 1225 -- the lever-off arm's
  plan held a phantom cast whose failure happened to leave better mana (gi 59 is the Open-1 face choice);
  gi 836, 1390 -- the turn's COMBO OFF route trial verdict differs between the arms on an identical board,
  not root-caused.

### Follow-up levers (2026-10-12): same seeds, d0 1500 + d3 b10 80, every other lever at default

| lever | d0 net / faster / slower | d3 b10 | Bruna d0 2000 / Fungus d0 2000 |
|---|---|---|---|
| `MTG_TAPAHEAD_PENDING_PIP_AUTO` | -4 / 9 / 6 (t -0.7) | 0 | (unreachable) |
| `MTG_SOLVE_AURA_PAYER_AS_EXECUTED` | -4 / 4 / 1 (t -1.4) | 0 | 0 / 0 |
| both vs both =0 | -8 / 12 / 6 | 0 | |

Both =0 reproduces the previous tree on the same seeds (d0 digest 95e7f050e282d406, d3 47a3df16feed4902,
identical units). As first built the tap-ahead lever was unconditional and measured +1 (10 / 11); gating it
on ReservedColorsStillCovered (only when no other source covers the pip) is what took it to -4.

Slower games, classified. `MTG_TAPAHEAD_PENDING_PIP_AUTO` (6): gi 45 and 271 -- the off arm's executor
dropped the Drake / Cloud on the {G} commit and the on arm casts them legally, which d0's ranking then
spends worse; gi 147, 1291 -- {Cloud, Displacer} becomes castable on turn 3 and outranks the line the off
arm took; gi 957 -- the now-payable Drake beside a second Trace reshapes turn 4; gi 19 -- identical casts,
but the Drake's tap-ahead banks Kitchen as {U} and keeps the Aether Hub's {E}, and the post-Drake re-solve
then lacks the one floating mana the off arm used for Cloud (not further root-caused).
`MTG_SOLVE_AURA_PAYER_AS_EXECUTED` (1): gi 35 t6 -- {Overgrowth, Emiel}, now admitted because the prepay
pays it, outranks the off arm's line; d0 ranking.

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
