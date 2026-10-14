# Why the one-host equip rule lost KittyEquipment gi149 / gi738 -- root cause

**Status (2026-10-14):** ROOT-CAUSED. Two findings, in order:

1. **The loss the USER asked about is an engine defect, not the rule.** The one-host rule's double-strike
   gate had a dead half: it never saw a double striker the frame could CAST. Fixed in code:
   `MTG_EQUIP_DS_GATE_CASTS` (heurarm slot `EQUIP_DS_GATE_CASTS`). gi149 and gi738 then play byte-for-byte
   the rule-off game (T4) at every depth and budget tried.
2. **The fix exposes a second, older hole** -- in the USER's equip-consolidation doctrine (2026-08-14),
   not in the one-host rule -- that the dead gate had been masking: the doctrine sends every rider to a
   double striker that cannot attack this turn. A proposed amendment, `MTG_EQUIP_DS_SWING_FIRST`
   (heurarm slot `EQUIP_DS_SWING_FIRST`).

**Both ship default OFF, so the tree is byte-identical to 77e094a2 until the USER decides.** The gate fix
cannot ship alone: by itself it fails the adoption bar on kitty v1 (held-out shipped +2, d3 +2, d0 +16; four
searched games unfindable even at d8 b0), because un-masking the doctrine hole costs more games than the gate
gains. Together they are better on every kitty / kitty2hg cell (shipped -6, d3 -13, d0 -21; 2HG -2 / -3 / -9),
identical on v2 / v3, every slower game recovers at stage 1, and the committed tree has 27 game-cells it cannot
find at any budget that the pair plays. **Recommendation: adopt both together** (section 9).

Section 8 has the measurement, section 9 the open questions.

## 1. The rule and its premise

USER RULING 2026-10-11 (`MTG_EQUIP_ONE_HOST`, origin 79434805, default ON): *"There is no double strike so we
should always choose exactly one creature to equip (not summoning-sick preferred) and put everything on that
creature."* The implementation (`TurnSolver.cpp`, CollectActions' equip block, "ONE HOST FOR EVERY RIDER")
chooses one battlefield host per frame -- can attack now, then most Equipment already attached, then the
richer body -- and offers every rider equip only onto it. The premise ("no double strike") is enforced by
`FrameHasDoubleStrikeSource(state, actions)`, which is meant to disarm the rule when a double striker is on the
battlefield **or is a cast candidate of the frame** (its own comment: "a Balan cast in the same subset is
exactly the case ...").

KittyEquipment **v1** -- the suite deck -- runs 4 Kor Duelist (`double_strike_while_equipped`) and 2 Balan
(`double_strike_min_equipment` 2). **v2 and v3 have no double striker in the main deck** (every
double-strike param in `cards.json` checked: Kor Duelist, Balan, Thrumming Hivepool, Valiant Knight,
Kinsbaile Cavalier, Silverblade Paladin -- none is in either main deck). The rule was measured and adopted on v3.

## 2. Defect 1: the gate's cast half is dead inside CollectActions

`FrameHasDoubleStrikeSource` reads each `CastFromHand` candidate's `Action::def`. Inside CollectActions that
field is filled only by the closing pass, *"Resolve each action's card definition ONCE ..."*, which runs
**after** the equip block that calls the gate (~3,800 lines later in the same function). So at the gate every
cast candidate still has `def == nullptr`, `ds_def(nullptr)` is false, and **only a double striker already on
the battlefield disarms the rule**. A Kor Duelist or Balan the frame is about to cast never does. The rule then
picks the old Puresteel Paladin as the one host and drops every rider pair onto the double striker -- at both
the ranking loop and the emission gate; an in-hand host is never a one-host candidate, so the pair is simply
gone. The Greaves still reaches the double striker because haste equips are exempt, which is exactly the shape
of both lost games: the double striker gets the haste and nothing else.

`MetalcraftWillFreeEquips` has the same dead cast half at its one in-CollectActions call (the zero-power equip
rule, default OFF). Its other caller, `MetalcraftHoistArmed`, runs after the resolve pass and was always right.
The fix (`CastCandidateDef`) resolves a null `def` exactly the way the closing pass does; for every caller after
that pass it is an identity, and on any list with no double striker / metalcraft card in hand it is inert.

## 3. gi149 (seed 2151, regression s2002 gi149): d3/b10, shipped d5/b20, d8/b0

Opening (kept 7): Balan, Colossus Hammer, Plains x2, Umezawa's Jitte, Puresteel Paladin, Lightning Greaves;
draws Paladin, Armored Skyhunter, Kemba, Kemba, Plains, **Kor Duelist**, Bonesplitter. T1 Plains + Hammer, T2
Plains + Paladin (both arms). T3 has no land drop (two Plains).

**The faster line (rule off), every step:**

* **T3:** cast Umezawa's Jitte ({2}); the Paladin's Equipment-enters trigger draws Kemba. The Paladin attacks
  for 2 (20 -> 18).
* **T4:** draw Plains, play it (3 lands). "Combo Off" (the equip-chain route): cast Lightning Greaves ({2}) --
  the Paladin's trigger draws **Kor Duelist**, and Hammer + Jitte + Greaves = 3 artifacts turns metalcraft on,
  so every equip is {0}. Cast Kor Duelist ({W}). Equip Colossus Hammer -> Duelist, Jitte -> Duelist, Greaves ->
  Duelist (last: shroud; it gives haste). Equipped, the Duelist has double strike: 11 first-strike damage, the
  Jitte gets 2 counters, both spent on +2/+2, 15 regular damage = 26, plus the Paladin's 2 = **28 vs 18**.

**What the rule did with the dead gate:** on the frame after the Greaves draw the Duelist is in hand and
castable, but the gate cannot see it, so the one host is Paladin #48 and the Hammer / Jitte are offered only onto
the Paladin. The best line left is Hammer + Jitte on the Paladin (12) and the Greaves on the Duelist (1 power,
double strike: 2) -- not lethal. With no T4 kill in the enumerated space:

* the T3 search scores the Jitte line T5 like every other line, and its tie-break casts a second Paladin;
* T4 casts the Jitte (drawing Plains + Kor Duelist off two Paladins), plays the Plains and casts the Duelist
  with no mana left for the Greaves; T5 goes off. **T5.**

**Evidence.**

1. **Same board, three arms.** The T4 board handed to the search (`--claude-play --choices
   1,0,-1,-1,0,-1,-1,2,-1,-1 --choices-then-auto --depth 3 --budget-ms 10`): rule on **T5**, rule off **T4**,
   rule on + gate fix **T4**. The T3 board (`--choices 1,0,-1,-1,0,-1,-1`): on casts Paladin #2 (T5); off and
   fix cast Jitte (T4).
2. **The route.** `MTG_EQUIP_ROUTE_TRACE` on that T4 hand-off, rule on: the route DFS buys `Lightning Greaves`,
   then `Kor Duelist`, and reports `t4 SEARCH no win (... 4 links applied)`; rule off, the identical DFS reports
   `WON d127 branch 0/1 :: Kor Duelist` / `t4 SEARCH LETHAL`. (During the claude-play replay of T1-T3 the route
   reports LETHAL even with the rule on: human play opens every host, which is why `--claude-play` alone does
   not reproduce the loss.)
3. **The host choice.** `MTG_EQUIP_PICK_STATS` on the same hand-off: `Colossus Hammer -> rider host 19 (delta
   21)` -- the un-equipped Duelist, (1 + 10) x 2 - 1 -- appears **0 times** with the gate dead and 7 times with it
   fixed; with the gate dead the Hammer's rider host is Paladin #48 (delta 10).
4. **The projection.** `MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=3`: the same T2-root horizon rollout scores
   `spells[Umezawa's Jitte] win=4` with the rule off and `win=5` with it on.
5. **Full game.** Rule on / rule off / rule on + gate fix: d3 b10 **5 / 4 / 4**, shipped **5 / 4 / 4**, d8 b0
   **5 / 4 / 4**. The fixed arm's game log is identical to the rule-off arm's.
6. **Unit.** `test/unit/test_equip_one_host_ds_gate.cpp` pins the frame: with the gate dead no plan equips the
   Hammer or the Jitte onto the Duelist; fixed, a plan casts the Duelist and equips each onto it.

## 4. gi738 at depth 0 (seed 2740): the same mechanism, with Balan

T1 Plains + Bonesplitter, T2 Plains + Paladin, T3 Plains + Bonesplitter (draws Jitte) + Lightning Greaves (draws
Grafted Wargear), both Bonesplitters onto the Paladin, attack for 6 (20 -> 14). T4, four Plains, Balan in hand:

* **rule off (T4):** "Combo Off": cast Balan ({2}{W}{W}); metalcraft (2 Bonesplitter + Greaves) makes every
  equip {0}; move both Bonesplitters onto Balan and equip the Greaves to her. Three Equipment -> double strike:
  (3 + 4) x 2 = 14, plus the Paladin's 2 = **16 vs 14**.
* **rule on (T5):** the gate does not see the Balan cast, the one host is the Paladin (it already carries two
  Equipment), so the moves onto Balan are never offered; the Greaves alone goes to her (one Equipment, no double
  strike): 3 + 6 = 9. 14 -> 5, kill on T5.

Full game: on **5**, off **4**, fix **4**; the fixed arm's log is identical to the off arm's. The unit test's
second case is this frame.

## 5. Why the rule's argument fails here -- and why the rule itself is fine

The rule's argument is *"in a goldfish nothing blocks, a flat rider is worth the same on any attacker, and the
only multipliers -- double strike and Gauntlets-style scalers -- both favour one pile."* In both games the
multiplier the argument names, double strike, is present; the ruling says so itself ("There is no double
strike so ..."), and its implementation tries to stand down. It fails only because the gate cannot see a double
striker that is about to be cast. Nothing else in the chain is wrong for these two games: the host ranking, the
haste carve-out, the equip route and the T3 projection all behave as designed once the gate works.

What the gate deliberately still does NOT see is a double striker in the LIBRARY. In gi149 the Duelist is drawn
mid-turn, off the Greaves; the rule binds on the frame before that draw (the Hammer and Jitte are not free there
anyway -- metalcraft flips with the Greaves) and is disarmed on the continuation frame after it. A line that
needed the earlier frame to know the Duelist was coming would be a clairvoyant line (USER 2026-10-13: no fix
owed).

## 6. Defect 2, exposed by the fix: the consolidation doctrine's double-strike host may be unable to attack

The screen (section 8.1) showed the gate fix alone LOSING four kitty games at the shipped cell that the
committed tree wins on T4 -- and the rule-off arm loses them too, so they are older than the one-host rule.
All four are the same hole in the USER's consolidation doctrine (`ConsolidatesEquips`, 2026-08-14):

> the rider set collapses to {top double-strike-POTENTIAL host, Kemba}; with neither, the single best host
> (and by the USER's 2026-08-22 rule, one that can attack).

The "top double-strike-potential host" is chosen with no regard to whether it can attack THIS turn. A Kor
Duelist cast this turn with no haste source is double-strike-potential, so it becomes the only rider host, and
the non-sick Paladin beside it is never offered. On a frame where the equips are {0} the AUTO-EQUIP collapse
then forces that one variant into every plan.

* **gi66 (seed 10100067), shipped:** T4 with two Hammers on the battlefield, a third in hand, Kor Duelist drawn,
  two lands, no Greaves in the game. The committed tree: cast the Duelist and the Hammer (metalcraft), three
  Hammers on the Paladin, **32 vs 20**. Gate fix / rule off: all three Hammers onto the summoning-sick Duelist,
  the Paladin attacks for 2. T5. `MTG_EQUIP_ALL_HOSTS=1` -> T4 in every arm.
* **gi135 (10100136):** the same shape (Shadowspear + two Hammers onto the sick Duelist; 23 vs 20 on the
  Paladin in the committed tree).
* **gi190 (10100191):** T3 Kor Duelist cast; the committed tree equips the Jitte to the non-sick Stoneforge
  Mystic, attacks (2 counters) and parks the Jitte on the Duelist after combat -- T4 for 21. With the doctrine
  the only Jitte host on T3 is the sick Duelist, so the Jitte stays home and T4 deals 13 of 19.
* **gi332 (10100333):** T4 with the Duelist in hand and castable, a Greaves in hand the turn cannot pay for
  ({2} beside the Duelist's {W}, two lands): the committed tree casts Bonesplitter and Colossus Hammer
  (metalcraft) and puts both on the Paladin, **16 vs 16**; with the doctrine the rider host on those frames
  is the castable, unhasteable Duelist, so a subset that leaves the Duelist in hand has no rider host (the
  reading every probe agrees with: opening all hosts, or the amendment's swing test, restores the line). From the identical T4 board
  (`--choices 1,1,-1,-1,0,-1,-1,6,-1,-1 --choices-then-auto`): committed T4, gate fix / rule off T5,
  `MTG_EQUIP_ALL_HOSTS=1` T4.

All four fail stage 1 (`--depth 4 --budget-ms 100`) and stage 2 (`--depth 8 --budget-ms 0`) under the gate fix
alone: they are unfindable lines, not budget churn. **So the committed tree and the gate fix each make a
different set of lines unfindable.** The dead gate happened to run the one-host rule -- whose ranking is
"can attack this turn" first -- on exactly the frames where the doctrine's choice was wrong (a double striker
still in hand), and happened to run it on gi149 / gi738 too, where the double striker was the right host.

## 7. The proposed amendment: `MTG_EQUIP_DS_SWING_FIRST` (default OFF)

It amends the doctrine, not the one-host rule, and only for a FRESH rider equip (a move keeps the old
doctrine; staying put is always the alternative):

* the ds pick is the top double-strike-potential host **that can attack this turn**: battlefield and not
  sick, printed haste, a haste Equipment already on the battlefield (the gi149 / gi738 Greaves), or a haste
  Equipment in hand whose cost fits beside the host's own cast in the turn's spare mana (gi332's Greaves does
  not);
* when the only double striker cannot attack, the doctrine falls to the USER's 2026-08-22 rule -- the best
  host that can swing -- and **still offers the sick double striker for a PAID equip** (banking the equip pays
  this turn's mana instead of next turn's: a real trade, so it stays searched) and for Grafted Wargear (its
  re-equip sacrifices the old host);
* a **FREE** equip goes only to the host that swings. Under metalcraft the move onto the double striker next
  turn is also free, so the swinging line reaches the same next-turn board plus this turn's damage; offering
  both would be pointless anyway, because the AUTO-EQUIP collapse forces the higher-eval variant (the double
  striker's doubled delta) into every plan.

Why this keeps the USER's intent: both rulings put sickness first -- 2026-08-22 *"put it on one creature with no
summoning sickness"*, 2026-10-11 *"(not summoning-sick preferred)"* -- and the doctrine's own comment argues the
same thing for the no-ds case ("the rider delta is only banked if the body carrying it can attack THIS turn").
The amendment applies that argument to the double striker too, and widens (never narrows) wherever the
argument is a trade rather than a dominance.

Completeness: relative to the gate-fixed doctrine it only ADDS a host for paid equips; the one removal is the
sick double striker for a free equip, justified above by the free re-equip (a dominance that rests on metalcraft
persisting into next turn, which in a goldfish nothing removes). It is not inert at unlimited budget -- it
changes the menu -- which is why it is measured below, with every slower game checked at stage 1 and stage 2.

On all eight design games (gi149 d3 and shipped, gi738 d0, screen gi66 / 135 / 190 / 332 / 387 shipped) gate fix
+ amendment wins on the faster arm's turn. Those games were used to design it, so the measurement below is on
fresh seeds.

## 8. Measurement

### 8.1 The screen (seeds 10100001+, three arms, stopped early -- my own probe)

`on` (committed), `off` (rule off), `fix` (gate fix). Kitty shipped cell, 400 paired games: off-on 1 faster /
4 slower, **fix-on 1 faster / 4 slower (+3)**, fix == off on all 400. The four slower games are section 6's;
the faster one (gi387) is a gi149-shape game. kittyv2 shipped (148 games): off-on 1 faster (gi118, seed
10300119) -- the rule itself on a list with no double striker; it recovers at stage 1 (`--depth 4 --budget-ms
100` -> T4), so it is budget churn, not a removed line. The screen was stopped once the doctrine hole was
found; its seeds were then burned for design.

### 8.2 The held-out A/B (seeds 10500001+, one pooled batch)

One `mtg --batch` manifest, `--threads 6`, every arm a per-job heurarm flag set (so the binary's defaults do not
matter), arms interleaved per chunk, every cell of a deck on the same seeds (seed = 10500001 + 100000 x deck
index + gi; kitty 105xxxxx, kitty2hg 106xxxxx, kittyv2 107xxxxx, kittyv3 108xxxxx). Arms:

* `on` -- the committed tree: rule on, gate dead, doctrine as is;
* `off` -- `MTG_EQUIP_ONE_HOST=0`;
* `fix` -- rule on + `MTG_EQUIP_DS_GATE_CASTS=1`;
* `amend` -- rule on + `MTG_EQUIP_DS_GATE_CASTS=1` + `MTG_EQUIP_DS_SWING_FIRST=1` (the proposal).

On v2 / v3 (no double striker) `fix` and `amend` are the same code path as `on` by construction, so they ran
`on` / `off` / `amend` only and `amend` is the noise control. Cells: shipped (value_play depth 5, b20), d3 b10,
d0. Mean turn-to-win (loss = 9); f / s / net = games faster / slower / sum of turn deltas, row minus column;
ku/g = thousand search units per game (deterministic work; the ms column was taken on a box at load 25-95 and is
not a cost measure).

| deck | cell | n | on | off | fix | amend | off-on | fix-on | **amend-on** | amend-off | ku/g on / amend |
|---|---|---|---|---|---|---|---|---|---|---|---|
| kitty | shipped | 400 | 4.3325 | 4.3375 | 4.3375 | 4.3175 | 1/3/+2 | 1/3/+2 | **6/0/-6** | 8/0/-8 | 13.0 / 12.9 |
| kitty | d3 b10 | 600 | 4.3383 | 4.3417 | 4.3417 | 4.3167 | 2/4/+2 | 2/4/+2 | **13/0/-13** | 15/0/-15 | 13.0 / 12.9 |
| kitty | d0 | 2000 | 4.8335 | 4.8420 | 4.8415 | 4.8230 | 9/25/+17 | 9/24/+16 | **33/13/-21** | 37/0/-38 | - |
| kitty2hg | shipped | 100 | 4.7200 | 4.7100 | 4.7100 | 4.7000 | 1/0/-1 | 1/0/-1 | **2/0/-2** | 1/0/-1 | 30.0 / 29.0 |
| kitty2hg | d3 b10 | 150 | 4.8067 | 4.8000 | 4.8000 | 4.7867 | 1/0/-1 | 1/0/-1 | **3/0/-3** | 2/0/-2 | 21.4 / 21.7 |
| kitty2hg | d0 | 1000 | 5.2830 | 5.2900 | 5.2840 | 5.2740 | 5/12/+7 | 5/6/+1 | **14/5/-9** | 17/1/-16 | - |
| kittyv2 | shipped | 120 | 3.8250 | 3.8250 | = on | 3.8250 | 0/0/0 | - | **0/0/0** | 0/0/0 | 11.0 / 11.0 |
| kittyv2 | d3 b10 | 150 | 3.7933 | 3.7933 | = on | 3.7933 | 0/0/0 | - | **0/0/0** | 0/0/0 | 8.4 / 8.4 |
| kittyv2 | d0 | 1000 | 4.8360 | 4.8400 | = on | 4.8360 | 0/3/+4 | - | **0/0/0** | 3/0/-4 | - |
| kittyv3 | shipped | 80 | 4.7375 | 4.7375 | = on | 4.7375 | 0/0/0 | - | **0/0/0** | 0/0/0 | 42.4 / 42.5 |
| kittyv3 | d3 b10 | 80 | 4.7750 | 4.7750 | = on | 4.7750 | 0/0/0 | - | **0/0/0** | 0/0/0 | 33.2 / 33.2 |
| kittyv3 | d0 | 1000 | 5.6380 | 5.6370 | = on | 5.6380 | 1/0/-1 | - | **0/0/0** | 0/1/+1 | - |

Reading it:

* **The proposal (`amend`) is better than the committed tree on every cell of both double-striker keys and
  identical on v2 / v3** (2,430 v2 / v3 games, every one the same win turn as `on` -- which is also the noise
  floor: the known `--batch` nondeterminism, kittyv2-batch-nondeterminism-2026-10-04.md, moved no win turn
  here). Searched cells: 24 faster, 0 slower. Cost: units per game within -3% / +1.4% of the committed tree.
* **The gate fix alone (`fix`) fails the adoption bar on kitty v1** (shipped +2, d3 +2, d0 +16) and is the
  rule-off arm on almost every game: fixing the gate hands those frames to the doctrine, whose hole the dead gate
  had been hiding. That is why `MTG_EQUIP_DS_GATE_CASTS` ships default OFF and must be adopted together with
  the amendment.
* **The one-host rule itself, on lists with no double striker (v2 / v3):** off vs on 0 / 0 on the searched cells,
  and at d0 the rule is better on v2 (3 games) and one game worse on v3 (gi298, which recovers at stage 1). The
  screen's v2 gi118 (rule 1 game slower, shipped) recovered at stage 1 too. Nothing here argues against the rule.

Six held-out games were re-played single-process with all four arms (kitty shipped gi77 / 181 / 101 / 388, d3
gi405 / 432): every one reproduced its batch result exactly.

### 8.3 Recovery of every slower game

**Proposal (`amend`) vs the committed tree and vs rule-off:** 19 slower games, all in d0 cells (kitty 13,
kitty2hg 5, kittyv3 1 vs rule-off). Stage 1 (`--depth W --budget-ms 100`, W = the faster arm's turn), one pooled
batch: **19 / 19 recovered** (six of them a turn FASTER than the target). No stage 2 needed.

**For contrast -- the gate fix alone (`fix` vs committed):** 37 slower games; stage 1 recovers 30 (every d0 one)
and fails 7 searched game-cells (kitty gi101, 308, 320, 432); stage 2 (`--depth 8 --budget-ms 0`) recovers
**0 / 7** -- the doctrine hole: unfindable at any budget.

**And the committed tree vs the proposal:** of the 71 game-cells where `amend` is faster, the committed tree's
own search reaches the faster turn at stage 1 in 44; the other 27 (kitty shipped gi77 / 181 / 245 / 295 / 383 /
388, d3 those plus 405 / 430 / 439 / 528 / 562 / 586 / 594, d0 245 / 430; kitty2hg shipped and d3 gi25 / 91,
d3 112, d0 799) fail stage 2 as well: **27 lines the committed tree cannot find at any budget** and the proposal
plays.

Held-out examples of the two mechanisms:

* **kitty gi77 (seed 10500078), shipped and d3, 5 -> 4.** T4: a Kor Duelist already on the battlefield (not
  sick, carrying a Bonesplitter), a second Kor Duelist in hand, Jitte on the battlefield, opponent at 8. The
  doctrine's "top double-strike-potential host" is the IN-HAND Duelist (its rider delta 3 outranks the old one's
  2, because the old one already has double strike), so the Jitte is offered only to a body that cannot attack;
  every arm but `amend` casts the second Duelist and attacks for 6. `amend` picks the top double striker that can
  attack: Jitte onto the old Duelist, 3 first strike, 2 counters, +4, 7 regular = **10 vs 8**. Note `on` = `off` =
  `fix` here: this is a pure doctrine hole, older than the one-host rule.
* **kitty gi101 / gi308 / gi432 (d3, T3 -> T4 under `fix`)** are the screen's gi66 shape: the committed tree's
  dead gate lets the one-host rule put the riders on the Paladin; the gate fix hands the frame to the doctrine,
  which pins them to a double striker that cannot attack; `amend` restores the Paladin.

### 8.4 The shipped defaults are byte-identical to the committed tree

Both new slots default OFF. The final binary at its defaults against the 77e094a2 binary, no flags, seeds
10900001+ (kitty shipped 30, d3 60, d0 400; kitty2hg d3 20; kittyv2 d3 20; kittyv3 d0 200): every play digest
identical. Search units differed on two searched jobs at `--threads 2` (kitty d3 781,222 vs 781,769; kitty2hg
d3 500,366 vs 500,420); re-run at `--threads 1` both binaries give identical digests AND identical units
(781,179 and 500,505) -- the `--threads 2` unit drift is the shared result-neutral cache pools, which the
committed binary shows against itself (781,179 at one thread vs 781,222 at two).

## 9. Open questions for the USER (none blocks anything)

1. **Adopt `MTG_EQUIP_DS_SWING_FIRST` (the doctrine amendment) together with `MTG_EQUIP_DS_GATE_CASTS`?**
   Recommended: yes, both `=1`. Measured above: better on every kitty v1 / 2HG cell, identical on v2 / v3, every
   slower game recovers at stage 1, cost flat. Both ship default OFF; the gate fix cannot ship alone (it fails the
   bar by un-masking the doctrine hole), so the shipped tree is unchanged until you decide.
2. **Is "a double striker that cannot attack this turn is not the doctrine's host" what you meant?** Your
   2026-08-22 ruling reads *"if we have neither Kemba nor a doublestriker we should put it on one creature with no
   summoning sickness"*. The amendment reads "have a double striker" as "have one that can attack this turn"
   (battlefield and not sick, printed haste, or a haste Equipment the turn can use), keeps the sick double striker
   for a PAID equip (banking), and drops it for a FREE one (the free re-equip next turn makes the swinging line at
   least as good). If you would rather keep the sick double striker on free equips too, say so -- the AUTO-EQUIP
   collapse would then need to stop forcing the higher-eval variant there.
3. **An alternative shape, not built:** put the "can attack this turn" test into the one-host rule's GATE instead
   (a double striker that cannot attack this turn does not disarm the rule). The rule would then bind on exactly
   the frames where the doctrine is wrong, and the doctrine would stay untouched. It removes the paid banking line
   the amendment keeps, which is why the amendment was preferred; say if you want it measured.

## 10. Repro

* Root-cause game: `MTG_EQUIP_ONE_HOST=0|1 MTG_EQUIP_DS_GATE_CASTS=0|1 MTG_EQUIP_DS_SWING_FIRST=0|1
  build/Release/mtg decks/KittyEquipment/KittyEquipment.cod --profile
  decks/KittyEquipment/KittyEquipment.profile.json --games 1 --seed 2151 --game-index 149 --depth 3 --budget-ms 10
  --ignore-play-profile` (gi738: `--seed 2740 --game-index 738 --depth 0 --budget-ms 0`).
* The T4 board: add `--claude-play --max-turns 8 --choices 1,0,-1,-1,0,-1,-1,2,-1,-1 --choices-then-auto` (and
  `MTG_EQUIP_ROUTE_TRACE=1` / `MTG_EQUIP_PICK_STATS=1` for the traces in section 3).
* Unit: `build/Release/mtg-test -tc="one-host ds gate*,ds swing-first*"`.
* The A/B manifests, the stage-1 / stage-2 manifests and their analysers were scratch under
  `build/scratch/kitty_rc/` (ab2 = the held-out batch; st1 / st1fix / st2fix / st1on / st2on = the recovery
  checks).
