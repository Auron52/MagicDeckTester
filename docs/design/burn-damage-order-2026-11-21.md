# Burn damage order, Lavamancer target prune, Fireblast copy pins (2026-11-21)

Branch `wip/burn-damage-order`; three agents in turn. Source: the mana-clean gate run `claude-queue-20261120b`
(1 Drop Burn 7,478 issues = ORACLE-GAP 4,881 + APPLY-DROP-ORDER 2,232 + ENUM-GAP 361; Midrange Burn 4,778 =
APPLY-DROP 2,594 + ORACLE-GAP 1,087 + ENUM-GAP 556 + APPLY-DROP-ORDER 541).

## What shipped (default ON, GT accepted)

| Flag (heurarm slot) | What it does | Kind |
|---|---|---|
| `MTG_LAVAMANCER_TARGET_PRUNE` | Grim Lavamancer's ping (gy-exile mode 3) targets only the opponent and opponent-controlled creatures / planeswalkers in autonomous play. Provider hook `DecisionProvider::GraveyardDamageTargetCandidates` (root: keep all; Midrange Burn + 1 Drop Burn: `PruneGraveyardDamageTargetsToOpponent`). Human play and `MTG_UNPRUNE=fixedburntarget` keep every target (the UnprunedGate masks are full at 64, so the ping shares the fixed direct-damage target gate). | provider RESTRICTION, USER-ordered |
| `MTG_FIREBLAST_COPY_PINS` | (a) `SubsetHasDuplicateSacSource` clause `kDupFireblast` (full + compact paths, precondition bit only when two hand slots carry Fireblast alternative casts): two Fireblast alternative casts whose pinned Mountains overlap are rejected. (b) Under a provider's one-pair fold, a later Fireblast copy is offered only the pairs disjoint from the earlier copies' pins (when any exist), so the two copies sacrifice the four best Mountains and the double cast stays findable. | sound: rejects only selections no order can pay |

Why (b) is needed: with the one-pair fold every copy got the SAME pin, so two Fireblasts on four Mountains
were never castable together in one plan (the second apply returned silently at
`if (IsFireblastAlternative && !PayFireblastAlternative) return;`). Why (a): with two Fireblasts and only two
Mountains both copies carried the one pin; the plan was a phantom (the mana oracle also believed it). Gate
20261120b had 573 flags whose plan held two `cast:Fireblast` (1DB 451, Midrange 122).

## What did not ship

`MTG_PO_DAMAGE_ORDER` (agent 1: the damage-order domain of the payable-order search, `DamagePayableOrder`, plus
the spectacle credit `before` under it) stays **default OFF**. The 1 Drop Burn update on origin (e357a05d8:
`MTG_FIREBLAST_FLOAT`, `MTG_ONEDROPBURN_FIREBLAST_EARLY`, `MTG_ONEDROPBURN_M2_HELD` default ON) superseded its
purpose: every repro it was built for (1DB s30000108 T2, s30000096 T2/T3, s30000167 T3, s30000000 T4; Midrange
s30105052 T3) is already clean with origin's defaults and the lever OFF. Measured over them it adds nothing
searched and is net worse at d0 (below). Candidate for deletion (the parent / USER's call).

The Family B reporting (`NoteGraveyardDamageDrop`, oracle-only, verified byte-identical with the oracle off) stays.

## Measurements

Held-out A/B, one pooled batch (`logs/burn-damage-order/mk_ab3.py`, seed 71001, 52 jobs, 80,800 games, 32/32
workers, 13 min wall). Base = origin defaults (float + Fireblast-early + M2-held ON), the three levers OFF.
Cells per deck: d0, d3/b10, d5/b20 x 2000, 2HG d5/b20 x 400 (Modern RW Burn d3 + d5 x 1000).

| arm | deck | better / worse | net | delta (turns) | units |
|---|---|---|---|---|---|
| pins | 1 Drop Burn (d0, d3, d5, 2HG) | 2 / 0 | -2 | -0.0005 d0, 0 d3/d5, -0.0025 2HG | 0.992-0.994 |
| pins | Midrange Burn | 2 / 0 | -2 | -0.0015 d0, 0 searched | 0.998-0.999 |
| pins | Creature Burn | 3 / 0 | -3 | -0.0010 d3, -0.0005 d5, 0 d0/2HG | 0.987-0.992 |
| lava | 1 Drop Burn | 3 / 0 | -3 | -0.0005 d3, -0.0005 d5, -0.0025 2HG, 0 d0 | 0.912-0.945 |
| lava | Midrange Burn | 1 / 0 | -1 | -0.0025 2HG, 0 elsewhere | 0.944-0.956 |
| dmg | Midrange Burn | 14 / 22 | **+8** | **+0.0065 d0** (13/21), d3 1/1 | 1.006-1.009 |
| dmg | 1 Drop Burn | 6 / 6 | 0 | d0 6/6, searched 0 | 1.002-1.003 |
| dmg | Modern RW Burn | 0 / 0 | 0 | identical | 1.003-1.004 |
| all | 1DB + Midrange | 28 / 28 | 0 | Midrange d0 +0.0050 (15/21) | 0.910-0.963 |

Totals: pins -0.0004 over 19,200 paired games (7 better / 0 worse, units 0.993); lava -0.0003 over 12,800 (4 / 0,
units 0.944, i.e. -5.6% search work on the two Lavamancer decks); dmg +0.0009 over 14,800 (20 / 28, units 1.005).
No slower game in either shipped arm, so the d8 b0 bar has nothing to recover.

GT (full `regression.sh --both`, Release, MTG_PGO=0): only burn keys failed, all with an unchanged average
(digest only); audit slower=0 faster=0 in both tiers; 18 searched + 5 d0 games play-changed at the same score
(the Mountains Fireblast sacrifices, the absent self-ping). Accepted `--deck`-scoped (onedropburn,
midrangeburn, creatureburn and their 2HG keys); `check_gt_logs`: 1387 consistent. Unit suite 3865/3865.

Gate slice (`mana_clean.py run --games 5000 --name bdo3-gate-20261121 1_drop_burn midrange_burn`, PGO binary of
src `fe6a1436eac5`, fresh blocks: 1DB games 5000-9999, Midrange 110000-114999; oracle-off play byte-identical).
Report: `logs/mana_clean/bdo3-gate-20261121/report.md`. The drop combines origin's float + Fireblast-early (the
other session) with this branch's two levers, on a different game block, so it is not attributable to either alone.

| deck | 20261120b issues | this run | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP |
|---|---|---|---|---|---|---|
| 1 Drop Burn | 7,478 (OG 4,881, ADO 2,232, EG 361) | **720** | 0 | 8 | 4 | 708 |
| Midrange Burn | 4,778 (AD 2,594, OG 1,087, EG 556, ADO 541) | **798** | 392 | 25 | 250 | 131 |

What remains: (1) the ORACLE-GAP family `{land, Fireblast, <burn>, gy-exile Lavamancer}` T3 (1DB ~260 of 708,
Midrange `{Bolt, Lavamancer cast, Fireblast}` 84): the engine's Fireblast floats {R}{R} from the sacrificed
Mountains (MTG_FIREBLAST_FLOAT, default ON on origin since e357a05d8) and leaves the new land untapped; the
oracle's nearest outcome taps the new land instead -- the oracle does not reproduce the autonomous float, an
ORACLE completeness item, not an engine miss. (2) Midrange ENUM-GAP 392 `{Skewer, Chandra's Incinerator,
Fireblast}` in every order: the Incinerator's cost reduction from same-plan noncombat damage is not credited at
the enumeration gate (the open item below). (3) Midrange APPLY-DROP-ORDER 250.

## USER rulings (verbatim)

- 2026-11-21, on MTG_ONEDROPBURN_FIREBLAST_EARLY / the 1 Drop Burn cast order: "The agent that worked on this made
  some changes for this. Let's leave this item until we see their update on origin."
- 2026-11-21, on the Grim Lavamancer self-target ping: "This indeed should be pruned. In fact everything but the
  opponent should be pruned. That said, let's wait for the other 1-drop burn changes I mentioned above to land on
  origin."
- 2026-11-19 (in the code): "Casting it before Needle Drop and Skewer could be okay if we float the red mana
  automatically." (Fireblast-early; shipped default ON by the other session in e357a05d8, reported to the USER there.)

## Still open

- Midrange ENUM-GAP {Fireblast, Incinerator, Bolt}: Incinerator priced at full cost at plan start (not addressed).
- T1 PHANTOM {land, Needle Drop} with no damage source on the menu (noted only; not a gate class).
- Midrange s30105349 T3 ORACLE-GAP {suspend Rift Bolt x2, Fireblast} under the float (engine leaves Mountain#31
  untapped, oracle floats {R}); still present with origin's defaults.
- Creature Burn keeps every Fireblast Mountain pair (no one-pair fold in CreatureBurnProvider); the fold Midrange /
  1DB use would apply there too (identical basic Mountains), for the Creature Burn owner to consider.
