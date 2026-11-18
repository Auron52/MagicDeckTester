# Sac-for-mana fodder on the plan menu (`MTG_SAC_FODDER_MENU`), 2026-11-18

Status: **default OFF** (held-out A/B failed the adoption bar). Root-cause of the residual is OWED: a default-off lever
that gates a missing searched capability is not an end state (CLAUDE.md, "Delete, don't lever").

## The defect (engine miss #1 of docs/design/goblins-sac-mana-oracle-2026-11-18.md)

The plan-menu mana gates (`EnumeratePlansCore`'s odometer bound `ManaPruneBound` and the per-subset flat pool check)
credited sac-for-mana fodder (Skirk Prospector "Sacrifice a Goblin: Add {R}", Utopia Mycon) only through an outlet
already ON THE BATTLEFIELD when the menu was built (`AddSacPayFodderToPool`). A plan that casts the outlet and then pays
a later cast by eating bodies (Goblins s30125177 T2 m1: Lightning Bolt + Skirk Prospector, no land) was never on the
menu at any budget: the odometer bound read 1 mana against a cost of 2, and the flat check rejected it (`mana_ok=0`).
With the oracle's sac-fodder model (`MTG_ORACLE_SAC_FODDER`) these surfaced as ENUM-GAP: Goblins 706 of 2,000 games'
flags, Fungus 129 of 500.

## The change (commits 9ffb16151 + a75265ce0, all behind the lever)

* **Menu credit** (`EnumeratePlansCore`): per subset, the fodder an outlet cast IN that subset makes available (legal
  board bodies, the outlet's own body if it is legal fodder, the subset's matching creatures and their ETB tokens),
  less the smallest body count among the other actions (the last action cannot pay with its own bodies); the same
  credit feeds the odometer bound and the colour table. `SacFodderMenuPrefixOk` is a necessary per-action prefix test
  before the credit (a cast's own bodies never pay for it).
* **Apply order** (`PayableCastOrderCore`, `ExactOrderWalkOnce` via `PoSacFodderWalk`): orders are judged without
  fodder first; only when no order pays from the board does the walk use the fodder model (bodies eaten only once they
  and an outlet are in play, fewest bodies, others before the outlet; a later outlet re-opens eating; only an outlet
  whose own body is legal fodder is eatable -- Utopia Mycon eats Saprolings, not itself; a same-plan tap grant such as
  Brightcap Badger's loses one unit per board body eaten before it).
* Tests: test/unit/test_sac_fodder_menu.cpp (8 cases). Units 3657/3657.

## Measurement

Gate slices (oracle, distinct decisions; Goblins gi 125000-126999, Fungus gi 125000-125499), off -> on (a75265ce0):

| | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | PHANTOM |
|---|---|---|---|---|---|
| Goblins | 705 -> 57 | 6 -> 3 | 1 -> 1 | 3 -> 3 | 83 -> 319 |
| Fungus | 129 -> 10 | 0 -> 0 | 0 -> 0 | 0 -> 0 | 143 -> 148 |

(9ffb16151 alone had Goblins APPLY-DROP-ORDER 4 / PHANTOM 455 and Fungus ORDER 7; a75265ce0's walk fixes removed those.)

Held-out paired A/B, one pooled batch, play settings, seeds 30000000 + gi, game_index = gi, x5000 per deck (the only
two decks with a sac-for-mana outlet):

| deck | pairs | faster | slower | net | digest-only | units on/off | cpu on/off |
|---|---|---|---|---|---|---|---|
| Goblins (gi 620000-624999) | 5000 | 2 | 6 | **+4** | 378 | x1.070 | x1.176 |
| Fungus (gi 630000-634999) | 5000 | 0 | 5 | **+5** | 109 | x1.034 | x1.053 |

d8 b0 replays: all 6 Goblins slower games recover; Fungus 630658, 631367, 633295 recover; **630182 and 630865 do not**
(off 4, on 5). 630182: T2 m1, board Hickory Woodlot, hand Ancient Tomb, Wild Growth, Utopia Mycon, Undercellar Myconid,
Beastmaster Ascension; OFF casts Tomb + Wild Growth + Mycon + Myconid, ON leaves Mycon in hand to T4. No oracle drop flag:
the search CHOSE the other line.

Smoke at lever=0: ALL PASS, 0 configs changed. GT not accepted (default OFF: play byte-identical).

## Open (in order)

1. Root-cause Fungus 630182 / 630865 at d8 b0: dump the T2 m1 root candidates on both arms with their projected win
   turns (suspects: a fodder plan whose projection the realisation cannot reach; a tie broken differently).
2. The play-setting losses that DO recover at d8 b0 (Goblins 6, Fungus 3) are consistent with budget spent on the
   extra PHANTOM plans the wider menu admits (Goblins 83 -> 319): tighten the credit (sound: never refuse a payable
   plan) so over-admission falls, then re-measure.
3. Residual Goblins ENUM-GAP (57): `sac:Skirk Prospector` explicit-action + live-Skirk shapes, Muxus / Siege-Gang m2
   boards. Residual ORDER s30126737 T2 m2 {Muxus + Chieftain} beside a live Skirk (probably the payer's fodder pick).

Scratch: build/scratch/claude_sacfodder_menu/ (ab/, slow/, slice.sh, cls.py); handoff logs/sacfodder-menu/handoff.md.
