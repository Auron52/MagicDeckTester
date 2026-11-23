# Sac-for-mana fodder on the plan menu (`MTG_SAC_FODDER_MENU`), 2026-11-18

Status: **ADOPTED, default ON (USER 2026-11-23)** as a completeness fix: *"I think we could just adopt the sac-fodder
and put it to rest."* Shipped in f36e81469 (`MTG_SAC_FODDER_MENU` + `MTG_SAC_FODDER_STAGE_PLAYOUT` default ON, `=0`
disables; inert cuts, TEMP arms/diags and dead wave arms deleted). Before the ruling the held-out A/B had failed the
adoption bar (net -3/-1 on held-out, CPU x1.17/x1.07; the dev-seed residue is in the sections below); GT is accepted
with the batch-1123 landing.

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

## Fix-up 6 (2026-11-22, reopened by the USER) -- where the extra work went

USER (reopening): *"It still makes no sense to me that our results get slower. We should be searching less. We should
try any and all options to get it working better."* and *"My understanding of what we are doing with the sac-fodder
change is making it so that some of the work (the sacrifice ability) is not searched. To me that should tend to reduce
the amount of work, but somehow it keeps going up, which I don't like or agree with."*

**Is the lever a second route to the same mana? No.** The searched sacrifice was already retired by `MTG_SAC_OUTLET_PAY`
(default ON 2026-10-31): wherever the payer is live, Skirk's / Mycon's `SacForMana` action is not emitted
(`pay_suppress`, CollectActions) and the payment eats a body on demand. The one carve-out is a board where a death pays
(`SacOutletEatHasValue`: Pashalik Mons, Rundvelt Hordemaster, Mogg War Marshal -- on the battlefield OR IN HAND); there
the payer is OFF and the searched action stays. The lever's context (`BuildSacFodderMenuCtx` -> `LiveSacPayOutlet` /
`PlanSacOutletDef`) uses the same carve-out, so it is dead exactly where the searched action lives. No board has both.
What the lever adds is the one supply the payer could not reach: an outlet CAST this turn.

**Hypothesis tested first (it was WRONG): keys, not menus.** 7f322b6d3 folded the lever's admission mode (1 beyond the
horizon, 2 wave first pass, 3 admitting pass) into `BuildBreakpointKey` whenever a sac outlet was merely IN REACH
(battlefield, hand, graveyard, exile). That key feeds the m1/m2 enumeration memos, the solve memo, the searched-m2
memo, the breakpoint continuation lists and `MidTurnStateKeyOf`. It folded even where the context was DEAD -- e.g. all
of Goblins 622286, whose opening hand holds Rundvelt Hordemaster, so the lever can credit nothing from T1 to T3 -- and
there every list is byte-identical to the lever-off list, yet pass 1, pass 2 and the playouts each recomputed it under
their own key. The lever-off arm computes it once. On 622286 the lever-off T1 search finds the T3 kill at 51,126 of its
~54K units; a few percent of recomputation pushes it past the budget, which is the whole loss.

Three exact cuts (no plan list changes; every one is a re-keying or a necessary-condition bound):
1. `MTG_SFM_KEY_LIVE` -- the mode is folded only where the context CAN be live (`SacFodderCtxCanLive`: payer gates on and
   some outlet in reach whose eat is death-free). A dead context, beyond the horizon or not, keys as the lever-off list.
2. `MTG_SFM_GATE_KEEP` -- a live context used to BAIL the selection-exact mana gate to the scalar bound (more odometer
   visits, billed under the canon-wave charge). The gate is kept with its pool total widened by the context's credit
   bound (no subset is credited more), still a necessary condition at every reader.
3. `MTG_SFM_MODE_SHARE` -- a live-context enumeration body that applied NO fodder credit enumerated the same list in the
   wave's first pass as in the admitting pass (the wave drops only credited plans), so its memo entry
   (`Entry::sfm_touched` false) serves the other mode too, and the other mode's first-visit marker counts toward promotion.

**Measured: the three exact cuts are inert.** One pooled batch (ab1, build/scratch/claude_sacfodder_fix5): units x1.099 ->
x1.096 on the dev seeds, every game unchanged, and 622286's T1 pass 1 is 54,039 units / no win with or without them.

**Where the work really goes** (400 Goblins games, MTG_ROLLOUT_STATS + MTG_DEV_SEQ_STATS, off -> on: 3.725M -> 4.338M units,
+612K): dev-seq pass 1 +60K; pass 2 +385K (66 -> 117 runs); the deferred canon-drop pass 2 +168K (6 -> 17 runs); the
SolveWithLookahead root loop +65K. `pass2_better = 0` in both arms. So ~90% of the extra work is the second passes the
wave opens to search the credited plans, and in that slice they never took a better line. 622286 is the other shape:
860 of the 909 live contexts of its T1 search are BREAKPOINT enumerations inside pass 1 (Skirk on the battlefield,
Rundvelt eaten earlier in the line, Siege-Gang's three ETB tokens credited as fodder for the same plan), which the wave
does not stage; every variant (all cuts, wave without latents, wave off) misses the T3 kill there.

**Quality (held-out, one pooled batch, gi 640000+/650000+ x5000):** the lever (onP) is ALREADY net negative -- Goblins
4 faster / 1 slower (**-3**), Fungus 1 / 0 (**-1**); dev seeds +1 / 0. The held-out slower game 640933 recovers at d8 b0
(3 = 3). d0 is unchanged (the lever is off in the d0 runner). Cost: units x1.107 / x1.041, CPU x1.17 / x1.07. Wave off:
units x1.076 / x1.025 but Fungus +3 dev and +3 held-out. Pass-2 pricing arms (x2, x4, skip; ab2): all identical to on -- the
arm never fires, because every second pass also has a non-fodder (hard) latent: the lever does not OPEN second passes, it
ENLARGES them (dirty nodes 548 -> 1,880). Line parked for the USER; the CPU decision (held-out bar met at CPU x1.17 /
x1.07) is theirs.
