# Hinata's full cast order (MTG_HINATA_ORDER_FULL)

**Status (2026-10-17):** `MTG_HINATA_ORDER_FULL` and `MTG_HINATA_GAMBLE_SEARCHED` are both **default ON in code since aff2b02f (2026-10-15, "integ: Hinata's settled order ON with Gamble's slot searched (pending the USER)")**, `src/ai/DecisionProviders.cpp` `EnvOn(..., true)`; `=0` on either restores the old OFF state. This is pending the USER's ruling on the amended order (0.6) -- no USER approval of it is recorded; the two non-recoveries (gi 82, gi 1538) stay open for the USER. The "both flags stay default OFF" clause in the 10-15 status below is SUPERSEDED by aff2b02f.

**Status (2026-10-15, second round; flag-default clause superseded above): the order is SETTLED by the USER and BUILT; as built it misses the bar (section 0.4). The AMENDED order -- Gamble's slot relative to the cantrips SEARCHED (`MTG_HINATA_GAMBLE_SEARCHED`, section 0.6) -- is net <= 0 on all six cells and recovers 1,331 of 1,335 slower games at +2-7% work; the two that do not recover trace to the Preordain-before-Ponder tie-break and to the USER's Soulfire-above-Opus ruling. Both flags stay default OFF pending the USER's call (recommendation in 0.6).**
Self-contained. Everything needed to resume is here or in git. Sections 1-8 below are the HISTORY (the 08-26
order, its 08-28 revision and the August measurements, which predate the 09-01 Irencrag waste gate, the 09-02
breakpoint node and the 10-10 restrictor / Crackle fixes); section 0 is the order as it is now.

## 0. The settled order (USER, 2026-10-14)

TOTAL, all main 1 (`MTG_HINATA_ALL_MAIN2` stays off: all-main-2 measured worse):

| slot | card | rank (x64) | why |
|---|---|---|---|
| 0 | land drop | `LandDropCastOrderRank` 0 | still SEARCHED at depth > 0; slot 0 is for condemnation only |
| 1 | Sol Ring | 4 | {1} -> untapped {C}{C} this turn (Izzet Signet 5, sideboard) |
| 2 | Hinata, Dawn-Crowned | 6 | her per-target discount reaches every TARGETED spell after her |
| 3 | Reality Spasm | 7 | once the land, rocks and Hinata are down |
| 4 | Gamble | 8 | before the cantrips (agent tie-break, below) |
| 5 | Preordain | 10 | before Ponder (agent tie-break, below) |
| 6 | Ponder | 11 | |
| 7 | Expressive Iteration | 12 | its exile is use-or-lose THIS turn |
| 8 | Ornithopter of Paradise | 14 | sick; one more Soulfire / Crackle target |
| 9 | **Irencrag Feat** | 18 + SET-LEVEL placement | always SECOND LAST (below) |
| 10 | Soulfire Eruption | 20 | USER: above Opus |
| 11 | Magma Opus | 21 | |
| 12 | Crackle with Power | 23 | X takes all remaining mana |
| 13+ | Icy Blast, Memory Lapse, Remand, Distorting Wake | 30 + MV + name hash | never cast; ordered only for totality |

**Totality.** USER: *"Let's fully order the cards in each list inasmuch as possible. I don't like the randomness
of choosing the order."* Every live card has its own rank, and `DecisionProvider::CastOrderTotalByName` (new root
hook, Hinata opts in under the order) makes the shared comparator `CastOrderLessRanked` break a rank tie between
two DIFFERENT cards by NAME, never by plan / hand order. Two copies of one card keep the root rule (cheapest
first among rituals, else plan order). Unit-pinned: every one of the 8! hand orders of an 8-card set realises the
same sequence, and a tie arm (`MTG_HINATA_DORK_TIE`) breaks by name.

**Two agent tie-breaks, put to the USER and not yet contradicted:**
* **Gamble before the cantrips** -- Gamble's search shuffles the library, so a Gamble after Ponder / Preordain
  throws away what they set up on top.
* **Preordain before Ponder** -- each cantrip re-plans after its draw (a breakpoint truncates the plan), so this
  decides only which goes first; Preordain-first sees more distinct cards (Ponder-first leaves Preordain
  re-scrying the two Ponder already saw). `MTG_HINATA_PP_STRICT` is now the REVERSE arm (Ponder first). The
  Ponder -> Preordain -> Ponder interleave stays reachable as single-cantrip subsets, each its own breakpoint;
  the dedicated interleave is still deferred (`hinata-ponder-preordain-interleave.md`).

### 0.1 Irencrag Feat: always second last

USER, verbatim: *"Your change is fine, but Irencrag needs to be castable after Soulfire and after Magma Opus,
since it needs to go second last. There has to be flexibility around that."* / *"So, in reality the Irencrag can
go in any of three spots depending on what other spells are cast, but it is always second last."* / *"So we can
restrict it using that."*

`max_casts_after: 1` is exactly one more spell of ANY kind, so "second last" means: immediately before whichever
payoff (Soulfire Eruption, Magma Opus, Crackle with Power) is the plan's LAST cast. Irencrag -> Soulfire,
Irencrag -> Opus, Irencrag -> Crackle, Soulfire -> Irencrag -> Crackle, Opus -> Irencrag -> Crackle and Soulfire
-> Opus -> Irencrag -> Crackle are all lines. A scalar rank cannot say that (22 forecloses Irencrag -> Soulfire;
18 forecloses Soulfire -> Irencrag -> Crackle), so it is built SET-LEVEL, with the Dragonstorm Irencrag-closer
(`MTG_DRAGONSTORM_IRENCRAG_CLOSER`) as the precedent:

* **Placement** -- `HinataProvider::ArrangeCastOrder` (`MTG_HINATA_IREN_SECOND_LAST`, default ON under the order;
  =0 -> the scalar 18 alone). The comparator already puts every payoff after every set-up cast, so the plan's
  last cast is its last payoff whenever it has one; Irencrag moves to just before it. With no payoff it stays
  at its scalar slot (last, nothing to fund).
* **The walks count in the same order** -- new hook `DecisionProvider::RestrictorFollowsArrangement`. When it
  holds, both subset walks (`Solve::consider`, `EnumeratePlans`) count Irencrag's followers in `PlanCastOrder`
  (comparator + `ArrangeCastOrder`, exactly the order both apply worlds realise), and `RestrictorPrePayable`'s
  pre-set and `RestrictorSubsetWasted`'s follower read that same order (`RestrictorSubsetWasted` now takes the
  `arranged` order). Legality and realisation cannot split. (By rank, {Soulfire, Irencrag, Opus} had two
  followers and was illegal; arranged it has one -- unit-pinned, with the apply casting all three.)
* **The prune, reconciled with the 09-01 waste gate** -- the gate (`MTG_IRENCRAG_WASTE`, lethal-exempt) now reads
  the arranged follower. A plan whose Irencrag precedes no payoff leaves it last with no follower and is pruned:
  that is the position rule's "restrict it using that". Rule 3 ("the payoff NEEDED the float", USER 09-01: "we'll
  go with your rule, but just those 3 cards") is KEPT, because it is a second fact the position cannot see
  (Irencrag -> Soulfire on a board that pays the Soulfire anyway). One accounting fix was needed for the new
  lines: in arranged mode "what the board has left" charges every cast placed before the follower, not just
  Irencrag's own cost -- otherwise Soulfire -> Irencrag -> Opus, where Soulfire ate the board, reads the float as
  unneeded and is deleted. Producers' output is still not credited (the keep-the-line direction).

### 0.2 Binding inside cantrip plans

Ponder, Preordain and Expressive Iteration make a set `OrderingOpaque`, which both apply worlds cast in PLAN
order unless the deck opts in: at depth > 0 the cantrip-first partition ([may-precede] -> [cantrips] -> [rest]),
which HOISTS Irencrag over a cantrip because it has `ritual_float` (handing the cantrip Irencrag's one cast);
at depth 0 raw hand order. Hinata now opts into `OrderOpaqueCastsByRank` under the order, so the executor
(`TakeTurn`) and the rollout (`ApplyPlanDirect`) rank-sort and arrange those sets too -- the order the walks
count. Unit-pinned on a searched {Ponder, Irencrag, Crackle} plan whose own order is Irencrag-first.

### 0.3 Housekeeping

* `MTG_HINATA_IREN_EARLY` (Irencrag back to 18) is folded into the order and deleted (slot + reader).
* `CastOrderTierName` renumbered to the current tiers (the 08-28 revision had left Hinata labelled "TUTOR", Spasm
  "TOP-OF-LIBRARY CANTRIPS", the cantrips "MANA DORK" and Sol Ring unlabelled).
* The false rank comment "a cantrip cast before her ... paid full price" is fixed: her discount is per target,
  and Ponder, Preordain, Gamble and EI have none.

### 0.4 Measurement (2026-10-14/15)

Held-out seeds from 11100001 (hinata shipped 11100001, d3 11110001, d0 11120001; hinata2hg 11150001 / 11160001 /
11170001), paired per game, loss = turn 9, `MTG_DUMP_WINS=1`. One binary per batch with the arms as per-job heurarm
flags: old = `MTG_HINATA_ORDER_FULL` off (byte-identical to the `ff3e205b` binary, below), new = on. Shipped =
the profile's play settings (d5 b30, value leaf + keep table); d3 b10 and d0 pin the depth. Scripts:
`test/tools/hinata_order/`.

**Identity.** Every other suite deck at d0 50 / d3 b10 20 / shipped 20 games (Dragonstorm and Dragonstorm 2HG,
the other Irencrag deck, at 200 / 50 / 50), the new binary with the order ON, plus hinata and hinata2hg with the
order OFF on both sides (d0 400 / d3 40 / shipped 20): **104 of 105 jobs, 4,290 games, every game byte-identical
to `ff3e205b`** (win turn and per-game play digest). The 105th job, `kittyv3_ship`, holds a KittyEquipment v3
game (gi 14) that ran over 18 minutes on BOTH binaries and was not waited for.

**gi 664 (seed 6600665, the draft's case).** Generic T5; the 08-28 order (Irencrag at 22) unwon; the settled
order T5 at shipped and at d3 b10, playing the generic order's exact game.

**The settled order as built (Gamble before the cantrips, Preordain before Ponder)** -- 34,500 paired games:

| cell | games | old avg | new avg | faster | slower | net | units new/old |
|---|---|---|---|---|---|---|---|
| hinata shipped | 1,000 | 5.6240 | 5.6370 | 22 | 27 | **+13** | 1.011 |
| hinata d3 b10 | 2,000 | 5.5870 | 5.5840 | 42 | 32 | -6 | 0.995 |
| hinata d0 | 20,000 | 6.7572 | 6.7562 | 846 | 839 | -20 | -- |
| hinata2hg shipped | 500 | 5.4220 | 5.4180 | 7 | 6 | -2 | 0.969 |
| hinata2hg d3 b10 | 1,000 | 5.4920 | 5.4920 | 15 | 14 | 0 | 0.955 |
| hinata2hg d0 | 10,000 | 6.6376 | 6.6347 | 430 | 401 | -29 | -- |

**It misses the bar on one cell: hinata shipped, net +13.** Every other cell is net <= 0.

**Root cause of the shipped-cell loss: the agent's "Gamble before the cantrips" tie-break, once the order binds
inside cantrip plans.** Read single-game, old vs new, on nine slower games (hinata shipped gi 13, 153, 184, 390,
414, 726; 2HG shipped gi 98, 290, 440). In six of them (gi 13, 184, 390, 726; 2HG 98, 440) the turning point is a
turn where the new arm casts Gamble AHEAD of a cantrip in the same main phase, where the old arm cast the cantrip
first and Gamble later or not at all:

* gi 13 T3: old Ponder, Ponder, land, Gamble; new Gamble (fetches Hinata; the random discard throws that Hinata
  away), Ponder, Ponder -- T5 -> T7.
* gi 726 T3: old Preordain, land, Gamble (discards Sol Ring); new Gamble (fetches Hinata, discards Hinata),
  Preordain, Preordain -- T6 -> T8.
* gi 390 T3: old Ponder, Gamble; new Gamble, Ponder (a different fetch) -- T6 -> T8. gi 184 T3: Gamble, its
  discard hits the Crackle, Ponder -- T5 -> T8.

Two things go wrong together: Gamble fetches BEFORE the cantrips show what is missing (TutorCandidates reads the
state at resolution), and its random discard lands on a different card. With the binding, a plan holding Gamble
and a cantrip always realises Gamble first, so the cantrip-then-Gamble line survives only through a breakpoint
continuation. The decisive single-game check is 2HG gi 98 (T4 -> T5): still T5 at **d8 b0** with the order as
built, but **T4 at d8 b0 with Gamble after the cantrips (`MTG_HINATA_GAMBLE_LATE`) or with the binding off
(`MTG_HINATA_ORDER_BINDING=0`)** -- and still T5 with only the proposed Reality Spasm placement
(`MTG_HINATA_SPASM_LATE`). So the line the order loses there is the agent's tie-break, not a USER slot. (gi 414
and 153 moved for other reasons: a T1 Ponder keep-vs-shuffle choice and an early cantrip choice changed on
different evaluations; no order inversion is involved.)

**Recovery of the 2HG shipped slower games (single-game, new arm):** stage 1 (d5 b100): gi 349, 425, 484 recover;
gi 98, 290, 440 do not. Stage 2 (d8 b0): gi 290 (T5) and gi 440 (T4) recover -- gi 440 by casting Ornithopter in
main 1 and Gamble in main 2, so the discard lands on Distorting Wake as in the old line; it is a recovering game,
not a clairvoyant loss. **gi 98 does not recover (T5 at d8 b0)** -- root-caused above.

**Recovery of every slower game (new arm):** stage 1 = `--depth max(5, old win turn) --budget-ms 100`, stage 2 =
`--depth 8 --budget-ms 0`.

| cell | slower | recover at stage 1 | recover at stage 2 | do NOT recover |
|---|---|---|---|---|
| hinata shipped | 27 | 19 | 7 | 1 (gi 82) |
| hinata d3 b10 | 32 | 22 | 7 | 3 (gi 253, 1538, 1632) |
| hinata d0 | 839 | 829 | 9 | 1 (gi 2029) |
| hinata2hg shipped | 6 | 3 | 2 (gi 290, 440) | 1 (gi 98) |
| hinata2hg d3 b10 | 14 | 11 | 3 | 0 |
| hinata2hg d0 | 401 | 399 | 2 | 0 |

The six games that do not recover, each root-caused single-game against the arms below (all at d8 b0):

| game | old -> new (d8 b0) | recovers with | mechanism |
|---|---|---|---|
| 2HG shipped gi 98 | T4 -> T5 | Gamble after the cantrips (T4); binding off (T4) | main 2 casts Gamble ahead of the staged Ponder; the old kill needed Ponder first |
| hinata d3 gi 253 | T6 -> T7 | Gamble after the cantrips (T6); binding off (T6) | T3 Gamble ahead of Preordain, its discard takes the Preordain |
| hinata shipped gi 82 | T5 -> T6 | binding off (T5); NOT Gamble-late | a cantrip plan realised in rank order instead of the partition's |
| hinata d3 gi 1538 | T5 -> T6 | payoffs tied (`MTG_HINATA_PAY_TIE`, T5); not Gamble-late / binding / Irencrag / PP | the T3 choice (Ponder first, then the drawn Mystic Monastery for T4's Hinata) changes with the payoff block's order |
| hinata d3 gi 1632 | T4 -> T5 | -- | **not an order loss**: the OLD arm is T5 at d8 b0 too; its d3 T4 was budget luck |
| hinata d0 gi 2029 | T7 -> T8 | -- | **not an order loss**: the OLD arm is T8 at d8 b0 too; its d0 T7 was the greedy line's luck |

**Decomposition and amendment arms** (same seeds, same batch shape; each arm = the order + one change; `chk`, the
order re-run on the rebuilt binary, is byte-identical to `new` on 5,750 games):

| arm | hinata ship | hinata d3 | hinata d0 | 2HG ship | 2HG d3 | 2HG d0 |
|---|---|---|---|---|---|---|
| **as built** (`new`) | **+13** | -6 | -20 | -2 | 0 | -29 |
| Gamble after the cantrips (`MTG_HINATA_GAMBLE_LATE`) | +9 | +2 | **+71** | -2 | -6 | -13 |
| binding off (`MTG_HINATA_ORDER_BINDING=0`) | +3 | +10 | +57 | +1 | +7 | -7 |
| Irencrag scalar 18 only (`MTG_HINATA_IREN_SECOND_LAST=0`) | +13 | -6 | -14 | -2 | 0 | -32 |
| Reality Spasm latest-payable (`MTG_HINATA_SPASM_LATE`, proposed) | +16 | +2 | -21 | +1 | +5 | -25 |

(net game-turns, loss = 9; units new/old 0.95-1.07 throughout, the order as built 0.95-1.01.)

**Verdict: the settled order does NOT meet the bar, so `MTG_HINATA_ORDER_FULL` stays default OFF.** It misses on
one cell (hinata shipped, net +13 of 1,000 games, 22 faster / 27 slower -- about 1.3 standard errors) and on four
slower games that do not recover even at d8 b0 (two more, gi 1632 and gi 2029, are the old arm's own luck: it does
no better at d8 b0). 26 of the shipped cell's 27 slower games DO recover at stage 1 or 2, so most of that cell's
deficit is the shipped budget (d5 b30) not reaching lines the order still allows. No single
measured change fixes it: Gamble-after-the-cantrips repairs gi 98 / 253 and the 2HG cells but costs +71 at d0;
binding off is the USER's Q2 reversed and costs +57 at d0; the Irencrag placement is near-inert against the scalar 18
(2 games differ); the proposed Spasm placement makes the shipped cell worse (+16) and stays OFF.
* The Irencrag rule is NOT the cost: `sc` and `new` differ on two games only, and gi 664 is the line it was built for.
* The largest single mechanism is Gamble's position relative to the cantrips inside a BOUND cantrip plan. Neither
  fixed position wins: before the cantrips loses gi 98 / 253 (and it is the turning point in six of the nine slower
  games read single-game), after them loses heavily at depth 0. That is the shape of a decision the ORDER should not settle at all: the
  amendment to propose is to make Gamble's slot relative to the cantrips SEARCHED (both orders offered as
  candidate orderings of a bound cantrip plan, the rank order first), not a third fixed slot. Not built: it is a
  change to the deck's cast-ordering machinery, not a slot, and it is the USER's call whether the order should leave
  that one relation open.

### 0.5 Flags, defaults as shipped, and how to re-measure

| flag | default | meaning |
|---|---|---|
| `MTG_HINATA_ORDER_FULL` | **OFF** | the settled order (ranks, totality, binding, Irencrag placement); =1 to measure |
| `MTG_HINATA_IREN_SECOND_LAST` | ON (under the order) | Irencrag set-level second last; =0 -> the scalar 18 |
| `MTG_HINATA_ORDER_BINDING` | ON (under the order) | the `OrderOpaqueCastsByRank` opt-in; =0 -> clean sets only (decomposition arm) |
| `MTG_HINATA_SPASM_LATE` | OFF | PROPOSED Reality Spasm latest-payable placement; measured worse (+16 shipped), not adopted |
| `MTG_HINATA_PP_STRICT` | OFF | the REVERSE Preordain/Ponder tie-break (Ponder first) |
| `MTG_HINATA_GAMBLE_LATE` | OFF | Gamble after the cantrips (13); measured above |
| `MTG_HINATA_GAMBLE_SEARCHED` | OFF (recommended ON with the order) | Gamble before AND after the cantrips, searched (0.6) |
| `MTG_HINATA_PP_SEARCHED` | OFF | the same fan for Preordain / Ponder; measured worse (0.6) |

Re-measure: `test/tools/hinata_order/gen_ab.py` (seeds above; `--arm NAME=FLAG=0/1` adds an arm, `--deck-dir` runs off
a copy of the deck folder on the build volume), `run_batch_lock2.sh` (one pooled batch through the box lock,
nothing on 9p), `analyze_ab.py`, `gen_recovery.py` / `stage_serial.py` / `recovery_verdict.py` for the two stages,
`probe_pair_cell.sh` for old-vs-new single-game diffs.

### 0.6 The amended order: Gamble's slot SEARCHED (2026-10-15)

The coordinator's ruling on 0.4: "Gamble before the cantrips" and "Preordain before Ponder" were the
coordinator's tie-breaks, not the USER's, so changing Gamble's slot stays inside the approved design; and the
fix should search the relation, not fix a third slot.

**Built** (`MTG_HINATA_GAMBLE_SEARCHED`, `ee149b33`): inside a bound plan, a set holding Gamble and a Preordain /
Ponder / Expressive Iteration ranked after it is offered in TWO orders -- the arranged order first (Gamble before
them), then Gamble moved to just after the last of them. Nothing else moves; Irencrag keeps its second-last slot in
both. It reuses the StompySurprise dual-slot shape (`e1f1bba1`): the engine's cast-ordering search
(`WantsCastOrderingSearch` + `CastOrderingCandidates`), canonical first, variants deduplicated, an empty list for a
set with nothing to search. Each candidate is the full realised order, and `Plan::searched_order` makes the
executor and the rollout replay it in vector order (lockstep). Search window only: the rollouts and the d0 runner
keep the arranged order, so the d0 cells are byte-identical to the order without the fan (30,000 games, 0 differ).
One engine fix came with it: the Irencrag tests (`RestrictorPrePayable`, the waste gate) were scoped OFF for any
ordering-search deck; they now stay ON when the deck places its restrictor set-level (`RestrictorTestsInScope`),
Dragonstorm unchanged.

Two more arms were measured beside it, because the remaining non-recovering game gi 82 turned on the other
tie-break (Preordain before Ponder): the same fan for Preordain / Ponder (`MTG_HINATA_PP_SEARCHED`, `4b8d204f`,
default OFF) and the fixed reverse (`MTG_HINATA_PP_STRICT`, Ponder first). The name tie-break was switched to the
canonical `CastOrderTotalByName` + `CastOrderNameTieLess` (integration `2ac8db0b`, `c498e1f4`); the rebuilt binary
plays the order byte-identically to the one measured in 0.4 (34,500 shared games, 0 differ).

**Held-out A/B** (one pooled batch through the box lock, 145,500 games; hinata shipped now 2,000 paired games and
2HG shipped 1,000 -- the 0.4 seeds plus new ones from the same ranges; loss = 9):

| arm (the order + ...) | hinata ship (2,000) | hinata d3 (2,000) | hinata d0 (20,000) | 2HG ship (1,000) | 2HG d3 (1,000) | 2HG d0 (10,000) |
|---|---|---|---|---|---|---|
| as built (`n7`) | +3 (52 / 51) | -6 (42 / 32) | -20 | -3 (20 / 15) | 0 (15 / 14) | -29 |
| **Gamble searched** (`MTG_HINATA_GAMBLE_SEARCHED`) | **-3** (44 / 40) | **-6** (39 / 32) | **-20** | **0** (15 / 14) | **-5** (13 / 9) | **-29** |
| Gamble searched + Preordain/Ponder searched | -13 (51 / 40) | -6 (39 / 32) | -20 | +3 (16 / 17) | -4 (14 / 11) | -29 |
| Gamble searched + Ponder first (`MTG_HINATA_PP_STRICT`) | -13 (46 / 37) | -10 (43 / 32) | -6 | +5 (13 / 16) | -4 (15 / 11) | -78 |

(net game-turns vs the generic order, loss = 9; faster / slower in brackets; d0 counts as in 0.4.)

**Work units, new / old** (the cost of the fan, the USER's concern):

| arm | hinata ship | hinata d3 | 2HG ship | 2HG d3 |
|---|---|---|---|---|
| as built | 0.993 | 0.995 | 0.971 | 0.955 |
| Gamble searched | 1.024 | 1.067 | 1.014 | 1.018 |
| + Preordain/Ponder searched | 1.056 | 1.121 | 1.052 | 1.076 |
| + Ponder first | 1.004 | 1.033 | 1.010 | 1.005 |

So the Gamble fan costs about +3% / +7% work at shipped / d3 against the order without it (+2% / +7% against the
generic order). The d0 cells carry no search, and the fans are inert there (byte-identical to the order without
them).

With 2,000 shipped games the order AS BUILT is +3 on hinata shipped (the first 1,000 were +13, the next 1,000 -10):
still not <= 0, and the earlier verdict stands. **Gamble searched is net <= 0 on all six cells** (-3, -6, -20, 0,
-5, -29); both Preordain/Ponder variants buy more on hinata shipped (-13) but lose 2HG shipped (+3 / +5), and the
searched one costs +6-12% work.

**Recovery, Gamble searched** (every slower game, the arm's own flags; stage 1 `--depth max(5, old) --budget-ms
100`, stage 2 `--depth 8 --budget-ms 0`):

| cell | slower | stage 1 | stage 2 | do NOT recover |
|---|---|---|---|---|
| hinata shipped | 40 | 22 | 17 | 1 -- gi 82 |
| hinata d3 b10 | 32 | 23 | 7 | 2 -- gi 1538; gi 1632 (old arm's luck) |
| hinata d0 | 839 | 830 | 8 | 1 -- gi 2029 (old arm's luck) |
| 2HG shipped | 14 | 10 | 4 | 0 |
| 2HG d3 b10 | 9 | 7 | 2 | 0 |
| 2HG d0 | 401 | 397 | 4 | 0 |

The fan recovers the two games 0.4 traced to Gamble's slot (2HG gi 98 is T4 at its own shipped settings, d3 gi 253
recovers at d8 b0) and introduces no new non-recovering game: of 1,335 slower games, 1,331 recover. The four that do
not are exactly 0.4's leftovers. gi 1632 and gi 2029 are the OLD arm's luck: it does no better at d8 b0. That
leaves two real ones:

* **hinata shipped gi 82** (seed 11100083, T5 -> T6): the OTHER coordinator tie-break, Preordain before Ponder. Its
  T5 kill plays the Soulfire exile as Ponder (shuffle) -> Reality Spasm -> Ponder -> Spasm -> Preordain -> Ponder ->
  Crackle; with Ponder ranked first (`MTG_HINATA_PP_STRICT`) it is T5 at the shipped settings and at d8 b0. The
  SEARCHED Preordain / Ponder fan does not recover it (T6 at d8 b0): the line runs through breakpoint continuations,
  which a plan-level ordering candidate does not reach.
* **hinata d3 gi 1538** (seed 11111539, T5 -> T6): the payoff block's order. Only tying the payoffs
  (`MTG_HINATA_PAY_TIE`) recovers it -- the T3 choice (Ponder first, then the drawn Mystic Monastery for T4's Hinata)
  turns on how the payoffs after it sequence. Soulfire above Opus is the USER's own ruling ("Soulfire is typically
  better than Opus ... I would still put Soulfire higher"), so nothing is proposed against it.

For comparison, the order **as built** on the same 34,500 + 1,500 new shipped seeds: 0.4's six non-recovering games
plus two more on the new 2HG shipped seeds (gi 767 and gi 907: T5 -> T6 at d8 b0) -- eight, of which the Gamble
fan recovers four (gi 98, 253, 767, 907 are not slower under it or recover) and leaves gi 82 and gi 1538.

The two Preordain / Ponder arms, both stages (the arm's own flags, same protocol):

| arm | slower | stage 1 | stage 2 | do NOT recover |
|---|---|---|---|---|
| Gamble searched + Preordain/Ponder searched | 1,340 | 1,298 | 38 | 4 -- gi 82, gi 1538, gi 1632, gi 2029 |
| Gamble searched + Ponder first | 1,356 | 1,308 | 45 | 3 -- gi 1538, gi 2029, **2HG shipped gi 626** |

The searched Preordain / Ponder fan leaves exactly the Gamble arm's four (it does not reach gi 82, as above). Ponder
first recovers gi 82 but introduces a new one: **2HG shipped gi 626** (seed 11150627, T5 -> T6, still T6 at d8 b0).
Every other arm -- the generic order, the order as built, Gamble searched, and the searched fan -- wins it T5 with
the identical game (same digest), so Ponder first trades gi 82 for its mirror image: a game whose T5 line needs
Preordain RANKED before Ponder (inferred from the arms; not traced turn by turn). Neither fixed tie-break recovers both, and the plan-level fan reaches neither.

**Verdict.** The amended order (the settled order + `MTG_HINATA_GAMBLE_SEARCHED`) is net <= 0 on all six cells and
recovers 1,331 of its 1,335 slower games, at +2-7% work. It does NOT strictly meet the bar: two slower games do not
recover even at d8 b0, one from the coordinator's Preordain-before-Ponder tie-break (gi 82) and one from the USER's
Soulfire-above-Opus ruling (gi 1538). So both flags stay **default OFF** and the call is the USER's:

1. **Recommended:** ship the order ON with the Gamble fan ON, accepting gi 82 and gi 1538 as the two known losses
   (both trace to a ranking decision, not to the engine, and neither fan nor fix inside the USER's slots recovers
   them without losing a cell elsewhere).
2. **Alternative:** additionally flip the Preordain / Ponder tie-break to Ponder first (`MTG_HINATA_PP_STRICT`):
   recovers gi 82 and is the best hinata cell (-13 shipped, -10 d3), but loses 2HG shipped (+5 of 1,000) and swaps
   gi 82 for a new non-recovering game (2HG gi 626, which wants Preordain first).
3. Not recommended: the searched Preordain / Ponder fan -- +6-12% work, loses 2HG shipped (+3), and does not reach
   gi 82 (same four non-recovering games as the Gamble fan alone).

A Preordain / Ponder fix that recovers BOTH gi 82 and gi 626 would have to reach inside the breakpoint
continuations (the Soulfire-exile chain), not the plan's cast order -- the place this would be built if the USER
wants the tie-break searched rather than ruled.

### 0.7 PROPOSED amendment: Gamble can also go after Ornithopter (2026-10-20, pending the USER)

**Status: built behind default-OFF levers, measured, NOT adopted -- the USER decides.** Root-caused from the
2026-10-19 overnight rebaseline, where `hinata_overnight_d3_s6006` / `_d5_s6006` gi 256 (seed 6262) went T5 -> T6
and stays T6 at `--depth 8 --budget-ms 0`; `MTG_HINATA_ORDER_FULL=0` restores T5.

**The turn.** T3, hand Gamble, Hinata, Reality Spasm, Soulfire Eruption, Island x2, Ornithopter (just drawn); board
Izzet Boilerworks. The old (generic-tier) line: Island, **Ornithopter, then Gamble** -> Crackle with Power; Gamble's
random discard takes the dead Soulfire Eruption; T4 Island + Hinata; T5 Forbidden Orchard, Reality Spasm X=5,
Crackle X=4 (20) + her attack = T5. The settled order realises the same set **Gamble first** (slot 4 before slot 8):
with the Ornithopter still in hand the discard draws from six cards instead of five and takes the **Island** that
T4's Hinata needs; whichever card Gamble fetches, T5 is out of reach (the search's T3 Gamble fetched Sol Ring).

**The clause.** "Gamble (4) ... Ornithopter of Paradise (8)" -- the TOTAL order puts every Gamble ahead of every
Ornithopter in a plan, and the Gamble fan (0.6) is written to stop short of it ("Gamble never moves past
Ornithopter"). The 08-28 reasoning for Ornithopter's slot ("it only matters as a target for Soulfire and Crackle")
misses its one other same-turn role: Gamble's "discard a card at random" is drawn from the hand AS IT STANDS when
Gamble resolves (SpellEffects.cpp: the victim is mixed from the hand size and indexes the stored hand), so whether
the Ornithopter is still in hand changes which card Gamble discards.

**Why depth / budget cannot get it back.** (1) Inside main 1 the plan {Ornithopter, Gamble} has exactly one
realisation, the order's, at every depth and budget; the cast-ordering fan offers no Ornithopter-first candidate.
(2) The split route -- Ornithopter in main 1, Gamble in main 2 -- is a different game: the Ornithopter's {2} is
paid from Boilerworks' {U}{R} bundle, so the {R} either pays the Ornithopter or floats and empties at the end of
main 1. `MTG_M2T_TRACE=3` at d8 b0 shows every main 2 after {Island, Ornithopter} holding Gamble with one {U} (or
nothing) and only the pass plan. `MTG_DISCARD_TRACE=1` confirms the search never resolved a T3 Gamble on a
five-card hand, at the case settings or at d8 b0.

**Proposed amendment, in the order's own terms** (`MTG_HINATA_GAMBLE_PAST_DORK`, default OFF):

> *Gamble's slot is searched past Ornithopter too: a plan with Gamble and Ornithopter is also tried with Gamble
> after the Ornithopter (and after the cantrips), still before Irencrag and the payoffs -- Gamble's random discard
> comes out of what is still in hand, so casting Ornithopter first changes what it can hit.*

`HinataProvider::CastOrderingCandidates` adds one candidate (Gamble moved to just after the last Ornithopter /
cantrip / Expressive Iteration that follows it), deduplicated; nothing is pruned, so every candidate is findable at
every budget. Measured beside it, two FIXED alternatives (default OFF):
* `MTG_HINATA_DORK_BEFORE_GAMBLE` -- Ornithopter ranked between Reality Spasm and Gamble (still inside the USER's
  08-28 "after Hinata, but before Soulfire Eruption"), so ahead of the cantrips too;
* `MTG_HINATA_DORK_THEN_GAMBLE` -- the narrow one: in a plan holding Gamble, every Ornithopter after it moves to
  immediately before it (`ArrangeCastOrder` step 3); an Ornithopter in a plan with no Gamble keeps its slot.

**Target game** (seed 6262, gi 256; d3 b10 / d5 b20 / d8 b0): base T6 / T6 / T6; each of the three levers T5 / T5 /
T5; `MTG_HINATA_ORDER_FULL=0` T5 / T5 / T5 -- every T5 arm plays the identical game (digest dc2b64c851fba30e). All
levers =0 are byte-identical to HEAD (target digests; hinata / hinata2hg smoke and regression rows; 6,500 held-out
games of the base arm re-run on the final binary, 0 changed).

**Held-out A/B** (2026-10-20; one pooled `--batch` per binary, arms as per-job heurarm flags, seeds paired
`seed = BASE + gi`, `game_index = gi`; hinata BASE 12100001 + 1000 x block, hinata2hg 12150001 + 1000 x block, d0
cell +50000; loss = 9; ship = the profile's own play settings, d5 b30 value_play):

| arm | hinata ship (12 x 500) | hinata2hg ship (6 x 250) | hinata d0 (12 x 500) | units (ship) |
|---|---|---|---|---|
| `_GAMBLE_PAST_DORK` (searched) | **+3** (2 faster / 5 slower; 46 changed) | **+3** (0 / 3) | 0 (inert: the fan is search-window only) | 1.003 / 1.002 |
| `_DORK_THEN_GAMBLE` (narrow fixed) | +13 (5 / 16) | -1 (2 / 1) | +29 (21 / 30) | 1.002 / 0.998 |
| `_DORK_BEFORE_GAMBLE` (fixed) | +84 (25 / 107) | +14 (5 / 17) | +62 (35 / 68) | 1.004 / 0.981 |

Both fixed placements lose: on the average hand the order's Gamble-first IS right (an Ornithopter still in hand
is a cheap buffer for the random discard -- the coordinator's "safest on the fullest hand"). Only the searched slot
is close. Its two faster games are exactly this mechanism (s12109356 gi 355 T8 -> T7 and s12111048 gi 47 T7 -> T6:
Ornithopter, then Gamble, whose discard takes a Soulfire Eruption instead of the Ornithopter / a needed card).

**Recovery of the searched slot's 8 slower games** (the arm's own flags): stage 1 (depth max(5, old), b100) recovers
6; stage 2 (d8 b0) recovers 7 (2HG gi 91 even goes T5 -> T4). **One does not recover: hinata ship s12111487 gi 486**
(T5 -> T6, still T6 at d8 b0, where base is T5). Its T4 picks the Ornithopter -> Gamble -> Soulfire line, which the
line search scores as a T5 kill (`MTG_FSW_TRACE` T4 d2 `tail=5`), and at T5 the line search again finds a kill on
the REAL board (`T5 d1 p=land=Izzet Boilerworks ... tail=5`) -- but the executed T5 (Soulfire, Reality Spasm x2,
Preordain, Ponder, Sol Ring, Ornithopter, Irencrag Feat, then Remand on its own Irencrag) ends with Crackle in hand.
That is a search / execution divergence on the T5 board, downstream of the order: the amendment's T4 choice is the
search's own best, and the line the search believes in is not the one TakeTurn realises. Not root-caused further
(owed: a lockstep check of that T5 chain). Repro: `--seed 12111487 --game-index 486 --games 1`, hinata shipped
profile, `MTG_HINATA_GAMBLE_PAST_DORK=1`, `--depth 8 --budget-ms 0`.

**Verdict (agent; the USER decides).** No variant meets the bar (every cell net <= 0 AND every slower game recovers
at d8 b0). The searched slot is the right SHAPE (nothing pruned; inert at d0; +0.2-0.3% work) and is the only one near
neutral, but it is +3 / +3 on the two shipped cells and leaves gi 486. Recommended: **keep the settled order as is**
and record s6006 gi 256 as a known loss of the Gamble-before-Ornithopter slot; if the USER wants the Ornithopter ->
Gamble line findable regardless, `MTG_HINATA_GAMBLE_PAST_DORK` is the amendment to take (not either fixed placement),
and gi 486's T5 divergence should be fixed first.

---

# HISTORY -- the August order and its measurements (sections 1-8, kept as written)

**August status (SUPERSEDED by section 0): BUILT, default OFF, and MEASURED NEGATIVE in every configuration --
do not adopt the order. Its lasting output is two ENGINE bugs it root-caused in the condemnation filter (bugs 4
and 5 in `breakpoint-condemnation-status.md`), which are general and worth adopting on their own.** Those
numbers predate the 09-01 waste gate, the 09-02 breakpoint node and the 10-10 restrictor / Crackle fixes, and
measured an order that did not play inside cantrip plans (no `OrderOpaqueCastsByRank`).

## 1. Why this deck needed an order at all

Hinata had **no user-authored cast order**. `HinataProvider` did not override `CastOrderRank`; it
inherited the generic card-parameter tiering, and that tiering puts **seven live cards on rank 20**:

| rank | cards |
|---|---|
| 5 | Sol Ring |
| 10 | Ornithopter of Paradise, Hinata |
| 15 | Reality Spasm |
| 18 | Irencrag Feat |
| **20** | **Gamble, Ponder, Preordain, Expressive Iteration, Crackle with Power, Magma Opus, Soulfire Eruption** (+ 4 never-cast) |

Only 7 of 13 providers override `CastOrderRank` (AntiLifegain, Vial, CreatureGiving, FiveColour,
Mirrorwing, Equipment, Stompy). Hinata, Dragonstorm, Goblins, TreasureHunt and Burn run on generic
tiers.

Two consequences, and the second is what prompted this work:

1. **Condemnation cannot work.** A tie is not a decline, so the `>=` tie-exemption fires and exempts
   the whole class. Hinata does not even opt into `CondemnsConsideredAtBreakpoint` -- correctly, per
   `TurnSolver.cpp`: soundness "is CONDITIONAL on the deck's cast order encoding draws-before-
   deploys", which the generic order does not establish here.
2. **The canonical continuation is undefined.** `MTG_BP_NO_GREEDY_CONT` replaces the greedy
   `TurnSolver::Solve` at a breakpoint with `cands[0]` -- the CANONICAL continuation. Ties fall
   through `std::stable_sort(CastOrderLess)` to plan order, so on Hinata "canonical" meant
   *enumeration order*, not judgement. Hinata is the ONE deck that leans negative under that lever
   (0 better : 4 worse, n=400, t=1.42 -- open item 1 of `bp-greedy-continuation-deletion.md`).

## 2. The order

Reviewed with the USER 2026-08-26. Param-derived, no card names (house style). Implemented in
`DecisionProviders.cpp` (`HinataFullOrderRank`); ranks are scaled by 64 with a mana-value + name-hash
totality guard on the never-cast tier.

| pos | card | why |
|---|---|---|
| — | **land drop** | NOT ranked -- searched at depth > 0 |
| 5 | Sol Ring | untapped {C}{C} the same turn -- real acceleration, unlike the sick dork |
| 6 | Gamble | most information; its random discard is safest on the FULLEST hand (`1/(n+1)` to hit the card just fetched), which also argues early |
| **7** | **Ponder** *and* **Preordain** | **peers -- see §3** |
| 9 | Expressive Iteration | its exiled card is playable THIS TURN only, so a cost-efficient-end placement throws away a third of the card |
| 10 | Ornithopter of Paradise | summoning-sick (no mana this turn) but one more creature for her per-target discount |
| 11 | Hinata, Dawn-Crowned | the engine, before every payoff |
| 15 | Reality Spasm | ritual float before the payoff |
| 20 | Soulfire Eruption | USER: *"Soulfire is typically better than Opus ... I would still put Soulfire higher."* |
| 21 | Magma Opus | payoff that draws 2 |
| 22 | Irencrag Feat | USER: *"must be second last as it can only be cast before Crackle"* |
| 23 | Crackle with Power | last; X consumes all remaining mana |
| 30+ | Icy Blast, Memory Lapse, Remand, Distorting Wake | never cast; positions exist only so the order is TOTAL |

### Two things this order deliberately gives up

* **Irencrag -> Soulfire is no longer expressible.** `max_casts_after: 1` means exactly one spell
  may follow Irencrag, and the USER reserves that slot for Crackle. With Soulfire at 20 and Irencrag
  at 22, a plan holding both casts Soulfire first. This is a narrowing accepted on the USER's
  ruling, not a no-op.
* **Reality Spasm's rank 15 does not bind today.** It is a DEAD CARD in the current model -- the
  static-pool planner does not offer X-untap nor chain the freed mana into a same-turn Crackle. That
  is 4 copies. Rank 15 is a position waiting for the deferred Layer-2 item.

### The land drop is searched, not ordered

USER: *"We also need to list the land drop, which can be before everything or after drawing. The
start is actually fine with clairvoyant as it can play any land currently in hand or draw and then
play a new type of drawn land."*

It is left unranked because the drop is **searched at depth > 0** (folded into the plan), and
measurement says it uses that freedom. Over 120 games at play settings, of **233 turns with both a
land drop and a draw spell, 51 (22%) play the land AFTER a draw spell**, including
`DRAWSPELL, LAND, DRAWSPELL`. Pinning it to "first" would delete those.

> **This is also a concrete reason to expect the greedy deletion to HELP this deck.**
> `TurnSolver.cpp` (`bp_play_searched_land`): *"Play a continuation's SEARCHED land drop ... **Inert
> for a greedy continuation (Solve never sets land_decided)**."* A greedy continuation structurally
> cannot play a land it just drew. On a deck whose early game is cantrip-into-land, every greedy
> continuation is one that cannot use its own draw.

## 3. Ponder / Preordain: measured, then A/B'd both ways

USER: *"Ponder and Preordain have a bit of a messy relationship (interacting with the top of the
deck) that might cause us some issues. It's possible you might sometimes want to do Ponder ->
Preordain -> Ponder"*, narrowed to: *"Only the 'I know what is on the top of the library' effects
like Ponder and Preordain are troublesome here."*

Expressive Iteration looks at three but CONSUMES all three (hand / exile / bottom), so it leaves no
known top and is not entangled. The pair is exactly {Ponder, Preordain}.

**Measured before choosing** (120 games, play settings): 12 turns cast more than one
top-manipulator, and one is exactly **`Ponder, Preordain, Ponder, Preordain`**. That line is
reachable *only because* the rank-20 tie lets `stable_sort` keep plan order. A strict
Ponder-before-Preordain forces `Ponder, Ponder, Preordain, Preordain` and deletes it.

This is the Bonesplitter / Lightning-Greaves case from `TurnSolver.cpp`'s condemnation rule -- peers
at one rank have no enumerated order between them, and the `>=` exemption stops them condemning each
other. So the shipped default is **peers**, and `MTG_HINATA_PP_STRICT` exists to measure the split
the USER asked for ("let's try it both ways"). Frequency is not the axis the no-lossy-truncation bar
uses, so a neutral result still favours `peer`.

**Round 1 could not answer this** -- with condemnation off the split is byte-identical, and the pair
only arbitrate anything once condemnation is on. See §6.

## 4. What was deliberately NOT measured into an order

USER: *"sometimes the order of other draw spells can be better done after or before, but a lot of
this is, I believe, just clairvoyance."*

Agreed, and it is why no position here was fitted to a sweep. An order tuned against a clairvoyant
search would be tuned to information a real player does not have, and would not transfer to the
non-clairvoyant path. Every position above is **structural** (cost, use-or-lose staging, the
Irencrag restriction) or an explicit USER call.

## 5. DEFERRED -- cast order vs the non-clairvoyant path

USER, 2026-08-26, on Reality Spasm's placement: *"(at some point we should look at how this
interacts with the non-clairvoyant path)"*.

Open question, not scheduled. The reviewed orders in this repo are all authored against a
**clairvoyant** search: the engine knows the library, so "cast A before B" is frequently a
formality -- the search would find the right sequence either way, and the order is doing canonical-
isation rather than judgement. On the non-clairvoyant path (the NC play policy) the same order is
load-bearing in a different way: it IS the judgement, because there is no lookahead to correct it.

Worth checking, when it comes up:

* Which positions here are inert under clairvoyance but decisive without it? Reality Spasm's is the
  USER's own example -- a human casts it after tapping out, the clairvoyant search does not care.
* Does an order authored for the clairvoyant engine *mislead* the NC policy anywhere?
* Should a deck be allowed to carry two orders, or is one order with the NC path as the tie-break
  the right shape?

## 6. Round 1, and the methodology error in it

Round 1 (`gen_hinata_order_manifest.py`, n=3000/cell, two blocks, 36,000 games) crossed the order
arms with `MTG_BP_NO_GREEDY_CONT`. Three results:

| finding | numbers |
|---|---|
| **`MTG_BP_NO_GREEDY_CONT` alone is NEUTRAL here** | hold +0.0007 (t=0.41, 10 better : 11 worse); train +0.0013 (t=1.41, 2 : 6) |
| **the full order alone REGRESSED** | hold +0.0210 (t=4.86, 21 : 67); train +0.0270 (t=6.06, 15 : 75) |
| **`MTG_HINATA_PP_STRICT` was BYTE-IDENTICAL to peer** | all 12,000 games, both blocks, with and without the greedy lever |

The first closes open item 1 of `bp-greedy-continuation-deletion.md`: the 0-better:4-worse lean that
made Hinata the one deck blocking that lever was an n=400 artifact and does not reproduce at n=3000.

**The error: condemnation was OFF in every round-1 arm.** `BpClassifyActive` is
`BpClassifyEnabled() || provider.CondemnsConsideredAtBreakpoint()`; `MTG_BP_CLASSIFY` defaults OFF
and Hinata does not opt in (only AntiLifegain and Equipment do). So round 1 priced the order purely
as a re-sequencing of casts -- never as the enabler it was authored to be. USER, catching it: *"Oh,
are we not using condemnation yet?"* Round 2 (`gen_hinata_cond_manifest.py`) crosses the order arms
with `MTG_BP_CLASSIFY` and `MTG_BP_CONDEMN_ORDER_AWARE`.

**A regression measured in the wrong configuration is not evidence about the right one** -- the same
shape as the mirrorwing finding that evaporated across the mana-overhaul rebase.

### Why the Ponder/Preordain split was inert, and when it stops being

An `MTG_CONDEMN_WHO` preflight (6 games, order on, `MTG_BP_CLASSIFY=1`) shows the pair **condemning
each other**: `Preordain @ Ponder` 340, `Ponder @ Preordain` 304. With condemnation OFF the two ranks
never arbitrate anything, hence byte-identical. With it ON, the `>=` tie-exemption is precisely what
decides whether the measured `Ponder, Preordain, Ponder, Preordain` line survives:

* **peer** (both rank 7) -- each is `>=` the other, both exempt, interleave reachable.
* **strict** (7 / 8) -- at a Preordain site, Ponder is strictly earlier and gets condemned, so
  `Preordain -> Ponder` is deleted.

So the USER's "try it both ways" is only answerable in the order-aware condemnation arms
(`peeroa` vs `strictoa`), and round 1's byte-identity is not an answer to it.

The same preflight shows condemnation dropping **Sol Ring 705 times** without order-awareness -- the
accelerant-nailed-to-its-rank failure the mana exemption exists for. `BpSlotIsAfterSite` only
consults that exemption when `MTG_BP_CONDEMN_ORDER_AWARE` is on, which is a second reason the
non-order-aware arm is expected to be the bad one.

## 7. Verdict (2026-08-26)

Every arm is worse than the shipped generic tiering, on both blocks:

| arm | hold | train |
|---|---|---|
| full order | +0.0210 (t=4.86) | +0.0270 (t=6.06) |
| best order arm (Irencrag back to 18) | +0.0110 (t=3.26) | +0.0113 (t=3.72) |
| + condemnation + both exemptions | +0.0117 | +0.0140 |
| + the greedy deletion too | +0.0113 | +0.0163 |

Leave-one-out attributes about half the damage to ONE position: the USER's ruling that Irencrag Feat
is "second last ... it can only be cast before Crackle". `max_casts_after: 1` permits one more spell
of ANY kind, so pinning it there forecloses Irencrag -> Soulfire and Irencrag -> Opus, and Soulfire
is {6}{R}{R}{R} against Irencrag's seven {R}. Reverting it recovers 0.0100 hold / 0.0157 train. The
remaining ~half is unattributed; the untested suspect is that the generic order TIES Ornithopter and
Hinata at 10 and this order splits them 10/11.

**The order's motivation also evaporated.** It was proposed to fix Hinata's negative lean under
`MTG_BP_NO_GREEDY_CONT`; at n=3000 that lever is NEUTRAL here (+0.0007 t=0.41 hold, +0.0013 t=1.41
train) and the 0-better:4-worse lean at n=400 does not reproduce.

**Condemnation cannot be the justification either.** Once bugs 4 and 5 are fixed it is ~inert on this
deck (30-52 of 3,000 games differ), because Hinata's breakpoint sites are its CANTRIPS at rank 7 --
near the front of the order -- so almost nothing is ever "already considered". That is structural,
not a defect in the order.

#### CORRECTED 2026-08-28: it is INERT UNDER THE GENERIC ORDER ONLY. Under the USER's order it FIRES.

An earlier revision of this section claimed condemnation was "proven inert on Hinata **in both
orders**", on the strength of the four digests below. **That generalisation was wrong**, and the
error is worth naming because it is a repeat: every one of those arms was measured under the GENERIC
tiering, so the table says nothing whatever about the full order.

| arm (all under the GENERIC tiering) | avg | digest |
|---|---|---|
| base | 5.7475 | `426a9d78...` |
| + condemnation | 5.7475 | `426a9d78...` **identical** |
| + site 3 eager | 5.7650 | `85d70bab...` |
| + site 3 eager + condemnation | 5.7650 | `85d70bab...` **identical** |

Under the generic tiering the null is real and the mechanism is as described: `BpSlotIsAfterSite`
exempts any candidate whose rank is `>=` the site's (the peer rule), and ELEVEN cards sit tied at
rank 20 (both cantrips, Gamble, Expressive Iteration, all three payoffs, the four never-cast
spells), so at a cantrip site nothing is strictly earlier and the condemnable set is empty by
construction.

**Re-measured with `MTG_HINATA_ORDER_FULL` and `MTG_BP_SITE3` armed TOGETHER, condemnation fires 113
times in 8 games and changes the digest.** Both flags are required: the order supplies the strict
ranks, and site 3 opens the class the rule acts in. The real cases, from `MTG_CONDEMN_WHO`:

| dropped | rank | site | site rank | n |
|---|---|---|---|---|
| Preordain | 448 (r7) | Soulfire Eruption | 1280 (r20) | 58 |
| Ponder | 448 (r7) | Soulfire Eruption | 1280 (r20) | 47 |
| Expressive Iteration | 576 (r9) | Soulfire Eruption | 1280 (r20) | 8 |

That is exactly the draws-before-deploys rule the order was authored to encode: the plan cast the
payoff without first casting Ponder, so Ponder was considered and declined, and the continuation off
Soulfire's dig must not re-offer it having seen what was revealed. **This is the USER's order giving
condemnation a surface it provably did not have before** -- which is the point of authoring one.

#### THE SURFACE IS RANK-SHAPED, which is why it still cannot fund the eager class

Condemnation's condemnable set at a site of rank R is "cards ranked strictly < R", so **its power
scales with how LATE in the order the breakpoint site sits.** That single fact explains both the
success on other decks and the shortfall here. Measured, n=40, paired, deterministic work units,
every arm relative to the full-order base:

| arm | units | avg |
|---|---|---|
| site 3 EAGER | **1.2419** | 5.7000 (base 5.6500) |
| + condemnation | 1.2413 -- **repays 0.04%** | 5.7000 |
| + `MTG_BP_NO_GREEDY_CONT` | 1.2669 -- **repays nothing, 2.5% dearer** | 5.6750 |

Hinata's *cost* is at the CANTRIP sites (rank 7, the earliest non-mana slot, cast constantly), where
the only earlier cards are Sol Ring (320) and Gamble (384) -- both soundly exempt under the
mana-source (bug 4) and tutor (bug 5) exemptions. So the surface is **empty exactly where the work
is**, while the rich surface sits on Soulfire Eruption, a rare and expensive card. Mirrorwing and
KittyEquipment work because their sites are LATE (Fists of Flame r16, the equipment ETB draw).

One further measured result, and it constrains any combined design: **with `MTG_BP_NO_GREEDY_CONT`
on, condemnation is SUBSUMED** -- `f_s3_ng` and `f_s3_ng_cond` are byte-identical (`814b5a34`). The
canonical continuation already takes the order's own answer, so there is nothing left for the rule
to remove. The two mechanisms are not additive.

**What this leaves for future work.** Making condemnation bite at Hinata's *frequent* sites needs a
premise other than "earlier in the order", because at a rank-7 site the later cards genuinely have
not been considered yet and banning them is lossy truncation. It is not a matter of narrowing the
exemptions: both are sound, and the order's front is made entirely of cards they protect.

#### The Irencrag pin is a RULES point, not a tuning one

Verified against `cards.json` rather than recalled: **Irencrag Feat {1}{R}{R}{R} Sorcery -- "Add
seven {R}. You can cast only one more spell this turn."** (`ritual_floating_mana: 7`,
`max_casts_after: 1`). "One more spell" is ANY one spell, so the ruling *"it can only be cast before
Crackle"* forecloses Irencrag -> Soulfire Eruption ({6}{R}{R}{R}) and Irencrag -> Magma Opus
({6}{U}{R}). With seven red floating, Crackle reaches only X=1 (5 damage), while Soulfire is
castable off the seven plus two lands -- so the pin deletes the strictly better follow-up. That is
why reverting this ONE position recovers about half the regression.

### CORRECTION -- Reality Spasm is NOT a dead card

§2 above repeats the claim in `cards.json` that Reality Spasm is never cast in the current model.
The game logs refute it: it is cast throughout, and it is the single most common cast that
condemnation deletes (33 of the 75 regressions). The `[PARTIAL: ...]` note in its `oracle_text` is
STALE on that point. The X-untap -> same-turn-Crackle CHAIN may still be missing, but the card is
live and its rank-15 position binds.

## 8. Files

* `src/ai/DecisionProviders.cpp` -- `HinataFullOrderRank`, `HinataProvider::CastOrderRank`,
  `CastOrderTierName`, and the flag readers.
* `src/ai/HeuristicArm.h` -- slots `HINATA_ORDER_FULL`, `HINATA_PP_STRICT` (per-job, so every arm
  pools into ONE `mtg --batch`).
* `test/tools/kitty_ab/gen_hinata_order_manifest.py` -- the 2x3 cross (order arm x greedy lever),
  n=3000/cell, two blocks.
* Inspect with `MTG_HINATA_ORDER_FULL=1 mtg decks/Hinata2/Hinata2.cod --cast-order-report`.
