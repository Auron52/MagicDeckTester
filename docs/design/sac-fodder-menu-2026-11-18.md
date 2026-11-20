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

## Fix-up 3 (2026-11-19/20, wip/sacfodder-fix 7fac7389e) -- does NOT clear the bar; line parked

Two changes, both default ON under the (still default-OFF) lever, TEMP `MTG_SFM_DUMP` removed:

1. `MTG_SAC_FODDER_INERT_PRUNE` -- exact dominance. A body the plan CASTS from hand (no alt cost) whose card is
   `fodder_inert` (every raw param key combat-only / static P/T lord / echo: Goblin Lackey, Piledriver, King,
   Stingscourger) is not credited as fodder for a SINGLE-colour outlet when its cost holds >= `per` plain pips of
   that colour; and a plan-cast `fodder_inert_outlet` (Skirk) whose own body is the only creditable one gets no
   credit. Premises are audited from raw cards.json keys (CardDatabase.cpp): every battlefield permanent and every
   candidate's card `fodder_board_audited`, every other card of the active player (hand, library, graveyard) and the
   exile zone `fodder_deck_audited` (presence-only watchers such as Krenko, Pashalik, Three Tree City, Cavern are
   fine off the battlefield). Proof at TurnSolver.cpp `FodderPipsCover`: P without B's cast reaches the same board
   with B in hand, and the source that paid B's colour pip is held instead of eaten-for back. Never fires for Fungus
   (Utopia Mycon makes any colour). Not covered: opponents' non-battlefield zones (no opponent acts in our main).
2. `MTG_SAC_FODDER_WAVE` -- admission order. In MTG_BP_DEV_SEQ's FIRST pass a fodder-credited plan is dropped after
   every gate and recorded as a dev-seq latent occurrence (so the node is dirty and the second pass runs, with root
   cut = pass-1 win - 1); only where the two passes govern (`g_rollout_nest == 0 && g_bp_enum_depth == 0`).

Dev seeds (gi 620000-624999 / 630000-634999, x5000, play settings, vs lever off, one pooled batch;
build/scratch/claude_sacfodder_fix3/ab1):

| arm | Goblins f/s net | units | Fungus f/s net | units |
|---|---|---|---|---|
| on (prune + wave) | 1/3 **+2** | x1.157 | 1/2 **+1** | x1.046 |
| prune off, wave on | 1/3 +2 | x1.162 | 1/2 +1 | x1.046 |
| prune on, wave off | 1/4 +3 | x1.092 | 0/3 +3 | x1.037 |

Slower (on): Goblins 620594 (4->5), 622286 (3->4), 622411 (3->4); Fungus 630865, 633295 (4->5). d8 b0: Fungus both
recover (4/4); Goblins 620594 4/4, 622286 3/3, 622411 3/3 -- every slower game recovers at d8 b0.

**Residue (why it does not clear):** in every slower game the dev-seq second pass NEVER took a line
(`MTG_DEV_SEQ_STATS`: pass2_better=0), and only 2-4 decisions per game route through FullSearchLine's two passes at
all. So the losses come from lever effects the wave does not stage: (a) decisions that do not go through the two-pass
root, (b) beyond-horizon playouts and breakpoint enumerations (`g_rollout_nest > 0` / `g_bp_enum_depth > 0`), which
still see credited plans and so move leaf values, and (c) the apply's fodder order walk (`BuildPoSacFodder`).
622286 diverges at T1 m1 (off casts Goblin Lackey, on casts Aether Vial) with no second pass taken. The wave also
costs units (pass 2 spends what pass 1 leaves): x1.092 -> x1.157 on Goblins. The prune is sound but moves nothing
measurable once the wave is on (credited plans are pass-2 only). Next would be a variant that stages (a)-(c), which
the brief ruled out; the line is parked for the USER.
