# EldraziDisplacerFlicker -- the USER-approved cast-order package (2026-10-14)

USER, 2026-10-14, on the draft `docs/design/cast-order-drafts-2026-10-14/EldraziDisplacerFlicker.md`:
*"Your EDF changes look reasonable to me. You can proceed with those."* Cross-deck ruling that governs
part 1: *"Let's fully order the cards in each list inasmuch as possible. I don't like the randomness of
choosing the order."* Single main phase stays the USER's 2026-09-08 ruling (`MTG_EDF_M2` off).

Five parts, each behind its own `EnvOn` lever and heurarm slot (appended at the end of the enum), so one
pooled batch measures every arm.

| part | lever (default) | what it does | where |
|---|---|---|---|
| 1 | `MTG_EDF_CAST_ORDER` | the TOTAL order, by params; the card name the last tie-break | `EldraziFlickerProvider::CastOrderRank`, `CastOrderTotalByName` (+ `CastOrderLessRanked`, the walk's sort) |
| 2 | `MTG_EDF_AURA_RANGE` | Fertile Ground / Trace / Overgrowth move behind the untappers only when the order is otherwise unpayable | `EldraziFlickerProvider::ArrangeCastOrder`, `TurnSolver::OrderPaysSequentially` / `OrderPrepaysWholeTurn`, Solve's rescue |
| 3 | `MTG_EDF_SHROUD_STACK` | Trace goes behind every other land Aura sharing its HOST | `EldraziFlickerProvider::ArrangeCastOrder` |
| 4 | `MTG_EDF_ROUTE_AURA_ORDER` | the COMBO OFF route deploys land Auras in the cast order | `comboroute::PickDeployAura` (DeployPieces step 2) |
| 5 | `MTG_EDF_ROUTE_LOOP_UNTAPPER` | the route casts a second untapper whose LOOP nets more, though its cast refund is short | `comboroute::DeployPieces` step 3, `Run` attempts C/D |

(Defaults as shipped and the measurement: see "Measurement" below.)

## The order (part 1)

All main 1. Slots by params; names only as the final deterministic tie-break.

| pos | card | rank | param rule |
|---|---|---|---|
| 0 | land drop | - | first at depth 0, searched at depth > 0 (unchanged) |
| 1 | Wild Growth | -990 | `is_land_aura`, 8*mv + 2*strict + hybrid - 1000 (USER 09-08, unchanged) |
| 2 | Fertile Ground | -982 | same |
| 3 | Trace of Abundance | -981 | same |
| 4 | Overgrowth | -974 | same |
| 5 | Cloud of Faeries | 3 | `etb_untap_lands > 0`, 3 + clamp(mv - 2, 0, 2) |
| 6 | Peregrine Drake | 5 | same |
| 7 | Training Grounds | 6 | `reduces_creature_activation` (2 under `MTG_EDF_PAYLOAD_FIRST=0`, reducer first -- unchanged meaning) |
| 8 | Eldrazi Displacer | 7 | `blink_cost`, 7 + clamp(mv - 3, 0, 1) |
| 9 | Emiel the Blessed | 8 | same |
| 10 | Dimensional Infiltrator | 10 | creature with its own sink (`drain_cost` / `exile_opponent_top_cost`), 10 + clamp(mv - 2, 0, 2) |
| 11 | Essence Depleter | 11 | same |
| 12 | Vexing Shusher | 13 | creature with no combo role |
| 13 | Living Wish | 20 | `tutor_to_hand`, 20 + clamp(strict pips - 1, 0, 1) |
| 14 | Eladamri's Call | 21 | same |

TOTAL: `DecisionProvider::CastOrderTotalByName()` (new hook, default false) makes `CastOrderLessRanked`
break a key tie by the card NAME (byte-wise `std::string` compare -- platform-independent, unlike
`std::hash`), and the enumeration's sequential walk (which sorts on the bare rank) uses the same
tie-break (`CastOrderNameTieLess`). Copies of one card keep plan order. Unit test
`test_edf_cast_order.cpp`: 28 hand permutations, one realised order.

## The range (part 2) -- the engine change, and why this shape

Q2 offered two routes: credit untappers and land Auras in the payable-order projection
(`FirstUnpayablePos` / `ApplyPayableCastOrder[At]`), or make the untapper hoist autonomous through an EDF
`ArrangeCastOrder`. Taken: the second, with the projection being the enumeration's own sequential walk.

* `ArrangeCastOrder` is the one hook every site that realises or projects a cast order calls -- both
  apply worlds (AIEngine clean branch / ApplyPlanDirect, main plan and breakpoint continuation), the
  enumeration's `SubsetPayableSequential`, `MergeBreakpointLeftovers`, the canonical report -- so the
  executor and the rollout agree by construction, and a subset the walk admits is the subset the apply
  realises.
* The projection is exact rather than aggregate: `TurnSolver::OrderPaysSequentially` walks a GIVEN order
  with `SubsetPayableSequential`'s own body (lifted into `SeqWalkFixedOrder`, byte-identical): live
  costs, the ETB-untap tap-ahead and the untap, a land Aura's bonus on its declared host. "Otherwise
  unpayable" is read the way the apply worlds pay: the whole-turn prepay first
  (`OrderPrepaysWholeTurn`), then cast by cast.
* The move: a land Aura whose mana value exceeds `land_aura_extra_mana` and that sits before the last
  untapper is movable; when the order fails, the nearest order that pays is taken (fewest pairwise
  inversions; each moved Aura right behind the last untapper, original relative order; the host stack
  re-applied). None pays -> the order as given. Wild Growth (adds what it costs) never moves.
* The aggregate route was not taken: crediting untappers in `FirstUnpayablePos` touches every deck's
  payable-order domain, and its colour model is the aggregate pool's -- the walk already prices colour.
* Solve (the d0 runner and the horizon playouts): its filter payer pays every land Aura FIRST, so it
  could never admit the moved order; a subset holding an untapper and a land Aura whose arrangement
  MOVED a cast is now asked of `SubsetPayableSequential` (same lever).
* Inert for every other deck: only EDF holds an `etb_untap_lands` card.

## The host stack (part 3)

Shroud (CR 702.18a) stops an Aura spell targeting its host (CR 303.4a), so Trace-then-Overgrowth on ONE
land was an illegal second cast that `ResolveEnchantTarget` quietly re-homed. A shroud land Aura now
moves to just behind the last non-shroud land Aura declaring the same `enchant_target` (stable). Trace
stays ahead of Overgrowth on different hosts. `LandAuraHostCandidates` reads the pre-plan board, so both
Auras are offered the same two hosts and the shared-host plan exists to be realised.

## The route (parts 4 and 5)

* Part 4: `DeployPieces` step 2 casts the land Aura with the lowest `CastOrderRank` (name tie-break),
  replacing "any-colour first, then hand order".
* Part 5, THE RULE: with an untapper already on the battlefield, a second untapper whose cast refund is
  below its cost is still cast when an outlet is on the battlefield and the candidate's LOOP net -- the
  yield of the lands its ETB untaps minus the outlet's blink cost -- is positive and above the current
  payload's. It runs as two extra deploy attempts (C/D = A/B with the rule armed), tried only after A/B
  fail to kill and only when the rule changed the deploy, so it can add a kill and never displace one
  (a DEVELOP from A/B is held and restored if C/D do not kill). Probe t4: Cloud's loop nets 0, the
  Drake's +1.
* Both route levers are folded into the trial memo key when ON (the mixed-arm pooled batch rule).

## Probes

Base (`ff3e205b`, `build/scratch/edf-order/bin/mtg.base`), `logs/edf-order/probes.py <bin> <tag>`:

| probe | base `ff3e205b` | new (defaults) | attribution (new with one part OFF) |
|---|---|---|---|
| t4_cloud_first (hand [Cloud, Drake]) | T4 win (search: Cloud, Drake) | T4 win (the route, "Combo Off") | part 5 OFF: T4 win by the search's Cloud, Drake |
| t4_drake_first (hand [Drake, Cloud]) | **no win** | **T4 win** (the route) | part 1 OFF: T4 (the route, part 5); part 5 OFF: T4 (the search, Cloud then Drake, part 1) -- either part alone fixes it |
| aura_drake (search, max_turns 5) | casts nothing on T4 | casts nothing on T4 | no win either way, so the search has no reason to prefer the pair; findability is the CheckLine rows |
| aura_drake `validate_line cast=Peregrine Drake;cast=Fertile Ground` | `legal_not_enumerated` | **`accept`** (one variant, Fertile Ground on Brushland) | part 2 OFF: `legal_not_enumerated` |
| aura_drake `validate_line cast=Fertile Ground;cast=Peregrine Drake` | `illegal` (that literal order cannot pay) | `accept` -- the same enumerated plan, realised Drake-first by the range | |

Pinned as fixtures: `test/scenarios/edf_untappers_cheapest_first_t4.json` (expect_win_turn 4) and
`test/scenarios/edf_aura_range_drake_first.json` (validate_line accept). Unit 1067/1067, scenarios 162/162.

## References (all 24, base `mtg.base` vs new `mtg.new`, 2026-10-15)

`logs/edf-order/refs.sh` (box lock), outputs `build/scratch/edf-order/refs/{base,new}/`. No reference file
was modified.

| check | base | new |
|---|---|---|
| `ref_bench.py` (search from turn 1, shipped d5/b20) | 4.208 avg, 0/24 short, 4 faster than human | **identical, game for game** (4.208, 0/24 short) |
| `viewer_protocol_check.py --strict` | 3 ok, 21 repaired, 0 drift / enum-gap / board-diverged | **same counts**; 5 refs repair different menu indices (the menu order moved), every replay ends on the same win turn |
| `ref_handoff_sweep.py` (637 frames) | 8 short | **7 short**: 636 frames identical, 1 FASTER (`claude_s5_gi4` T4 post_main #0, 5 -> 4, no longer short), 0 slower |

## Identity (2026-10-15)

`logs/edf-order/ident.sh`: every suite deck (33 lists) x d0 40 / d3b10 20 / shipped 20 games, seed 11500001,
plus FiveColour 2HG d3 40, plus EDF with ALL FIVE parts OFF on the A/B's own seeds (shipped 40, d3 60, d0 200).
**114 of 114 jobs digest-identical** between the base binary and the new one. Units equal except kitty d3 /
kitty shipped / kittyv3 shipped / snow shipped (within 0.4%, digests identical): those decks never reach this
code (no EDF provider, no untapper), so that is the batch's job-to-thread scheduling, not this change.
Bruna and Fungus (the other land-Aura decks) are byte-identical: parts 2 and 3 need an untapper or live in
the EDF provider.

## Measurement

One pooled `mtg --batch` per slice, every arm in it (box lock, `--threads 20`), paired on held-out seeds:
shipped (d5/b20, EDF has no value_play) 11300001 + gi, d3/b10 11310001 + gi, d0 11320001 + gi; loss = 9
(max_turns 8); `MTG_DUMP_WINS=1`. Arms: `old` = all five parts OFF (identity-checked against the base
binary), `new` = defaults, `noK` = part K OFF. Sizes (the USER has the question; this is the default the
coordinator confirmed): old/new 1000 shipped / 1500 d3 / 10000 d0; part-OFF arms on the same seeds' prefix,
300 / 500 / 10000. Three slices of a third each. Analysis `logs/edf-order/analyze_ab.py`; per-game
attribution `logs/edf-order/attrib.sh` (each changed game re-played with each part OFF, single-threaded).

### Slice 1 (binary `mtg.new` = commit f6b9263c's src; 2026-10-15 17:06-18:34 UTC, 26,332 games)

| cell | n | old | new | faster | slower | net | units new/old |
|---|---|---|---|---|---|---|---|
| shipped | 333 | 4.2432 | 4.2492 | 1 | 3 | **+2** | 1.043 |
| d3/b10 | 500 | 4.3080 | 4.3060 | 5 | 4 | -1 | 1.019 |
| d0 | 3333 | 5.5869 | 5.5236 | 240 | 69 | **-211** | (no search) |

Part marginals (part K OFF vs new; + = the part helps), slice 1: d0 (3333 each) part 1 **-10**, part 2
**+197**, part 3 +2, part 4 +11, part 5 +7; d3 prefix (167) part 2 -2, part 3 -1, the rest 0; shipped
prefix (100) all 0. Every one of the 76 slower games was re-played with each part OFF:

| slower games | restored by part 1 OFF | part 2 OFF | part 3 OFF | part 4 OFF | both 2 and 3 / 2 and 4 / 1 and 2 |
|---|---|---|---|---|---|
| shipped 3 | | 240, 318 | | 163 | |
| d3 4 | | 60, 201 | | | 4 (2 or 3), 488 (1 or 2) |
| d0 69 | 11 | 51 | 3 | 3 | 1 (2 and 4) |

### Root causes (slice 1) and the amendments built (commit 305cab78)

* **Part 2 (the range), searched cells.** The range makes {untapper, ranged Aura} findable where the Aura
  alone was payable but the untapper was not after it: shipped gi 240 / 318, d3 gi 4 / 201 cast Cloud of
  Faeries a turn early (T2 / T3, "Cloud; Fertile Ground" or "Cloud; Trace"), its ETB untap paying for an Aura
  the old line cast alone, and the next turn's go-off -- which needed Cloud's untap -- comes a turn late.
  The line is legal and the USER's rule admits it ("moved behind the untappers only when the turn is otherwise
  unpayable"); the search ranks it above the old line at the shipped and d3 budgets. Stage 1 (d5/b100)
  recovers gi 240, not 163 or 318 (stage 2 pending). At d0 the same capability is part 2's +197. Split for
  attribution: part 2b `MTG_EDF_AURA_RANGE_SOLVE` (Solve's rescue: the d0 runner and the horizon playouts)
  vs the in-window range.
* **Part 3 (same-land stack), d0 gi 1199 / 2003 / 2173.** The legal stack (Overgrowth then Trace) is not
  always payable: Overgrowth's {G}{G} cannot pay Trace's {R/W} (the USER's own reason for Trace first), so
  where Overgrowth's cost took the only {R}/{W} source the stack cast Overgrowth alone; the old order cast
  both (Overgrowth silently re-homed). **Amendment 3b `MTG_EDF_SHROUD_STACK_PAYS` (default ON): the move is
  taken only when the moved order pays the way the apply worlds pay it (the whole-turn prepay, or cast by
  cast); otherwise the order stays as given -- what played before part 3.**
* **Part 4 (route Aura order), shipped gi 163, d0 gi 1010 / 1780 / 2333.** In the USER's order the route
  casts Wild Growth first: it takes the best host and Yavimaya Coast pays for it, Overgrowth then has to
  go on the Hub, and the Coast -- the only blue land the untap set refreshes -- is spent: the Drake finds
  no {U}. Overgrowth first (the hand order the old route happened to use) lands on the Coast and the loop
  runs (reconstructed T3 board `logs/edf-order/ship163_t3.json`: part 4 ON no T3 kill, OFF T3 kill).
  **Amendment 4b `MTG_EDF_ROUTE_AURA_PERMS` (default ON): when the deploy in the USER's Aura order kills
  nothing, the route tries the other orders of the distinct Auras in hand -- the USER's always first, each
  other order only after every earlier attempt failed to kill and only when it changes the deploy -- so it
  can add a kill and never displace one.** Unit test on the gi 163 board.
* **Part 1 (total order), d0, two mechanisms.** (a) The tutors: the draft called Living Wish vs Eladamri's
  Call payment-only, but at d0 the second tutor's fetch is chosen after the first resolves, so Wish-first
  spends the only route to the sideboard finishers on the piece the Call could fetch from the library
  (gi 12: Wish -> Displacer, Call -> Cloud, no finisher, T6; old Call -> Displacer, Wish -> Infiltrator, T5;
  likewise gi 1024 / 2295). **Proposed, default OFF (it reverses the USER's approved pair, so it is the USER's
  call): `MTG_EDF_TUTOR_LIBRARY_FIRST` -- the Call before the Wish.** (b) Cloud before Drake makes {Cloud,
  Drake} payable on smaller boards (the t4 probe's fix), and the d0 runner then sometimes casts the Drake
  instead of the outlet (gi 220 / 993 / 1022 / 1283 / 1308 / 1899 / 3038 / 3282). That is the greedy's choice
  with the capability the USER asked for; no amendment.

### Slices 2 and 3, and the whole A/B on the amended binary (`mtg.new2` = commit 305cab78)

Slices 2 (2026-10-15 20:27-22:14 UTC) and 3 (22:59-01:00) pooled every v2 arm: the seven above plus
`no3b`, `no4b`, `no2b` (MTG_EDF_AURA_RANGE_SOLVE=0) and `tutor` (MTG_EDF_TUTOR_LIBRARY_FIRST=1). `s1p`
(22:20-22:48) replayed slice 1's games with `new` on mtg.new2, so the whole A/B is old vs the amended
binary. `old` is the same in both binaries (all parts OFF; identity-checked). Merged:
`build/scratch/edf-order/ab/final.{out,err,analysis.txt}`.

| cell | n | old | new | faster | slower | net | units new/old |
|---|---|---|---|---|---|---|---|
| shipped (d5/b20) | 1000 | 4.2360 | 4.2320 | 7 | 3 | **-4** | 1.018 |
| d3/b10 | 1500 | 4.3093 | 4.3027 | 18 | 8 | **-10** | 1.018 |
| d0 | 10000 | 5.5809 | 5.5147 | 757 | 189 | **-662** | (no search) |

Per-lever marginal, lever OFF vs `new` (+ = the lever helps; the turns the lever saves). Shipped and d3:
the v2 prefixes (200 / 333 games) PLUS the census (every game the package changed outside the prefix --
9 shipped, 18 d3 -- replayed with each lever OFF, `logs/edf-order/census_eval.py`). d0: the v2 prefix
(6667 games).

| lever | shipped | d3 | d0 |
|---|---|---|---|
| 1 `MTG_EDF_CAST_ORDER` | +1 | +2 | **-10** |
| 2 `MTG_EDF_AURA_RANGE` (all of it) | 0 | +1 | +380 |
| 2b `MTG_EDF_AURA_RANGE_SOLVE` | -1 | -1 | +379 |
| 3 `MTG_EDF_SHROUD_STACK` | -1 | -1 | +4 |
| 3b `MTG_EDF_SHROUD_STACK_PAYS` | 0 | 0 | +5 |
| 4 `MTG_EDF_ROUTE_AURA_ORDER` | +2 | +5 | +51 |
| 4b `MTG_EDF_ROUTE_AURA_PERMS` | +3 | +4 | +38 |
| 5 `MTG_EDF_ROUTE_LOOP_UNTAPPER` | 0 | 0 | +7 |
| tutor proposal (ON vs OFF) | 0 | 0 | +3 |

### Recovery of every slower game (v2)

200 slower games (shipped 3, d3 8, d0 189), one pooled batch per stage (box lock):
`build/scratch/edf-order/rec/final_stage{1,2}.*`, `logs/edf-order/rec_eval.py`. Stage 1 (`--depth W
--budget-ms 100`, W = max(5, old win turn)) recovers **196**; stage 2 (`--depth 8 --budget-ms 0`)
recovers d0 gi 6739. **Three d3 games recovered at neither**: gi 4 (old 4, new 5), gi 201 (4, 5),
gi 1457 (3, 4). At d8/b0 each is restored by part 2 OFF (gi 4 also by part 3 OFF).

### Root cause of the three, and amendment 2c (`MTG_EDF_AURA_RANGE_HOSTS`, default ON)

The plan dedup folds land-Aura HOST variants into one class -- `MTG_EDF_AURA_HOST_SIG_KAROO` puts the host
in the signature only on the karoo-drop branch -- and keeps the class's first member. That assumes the
host variants are copies of one line. On a plan that casts an UNTAPPER they are not:

* **gi 1457, T3** (Conservatory, Brushland + Trace; hand Displacer, Cloud, Eladamri's Call, Overgrowth,
  Conservatory, Yavimaya Coast). The kill drops the Coast and puts Overgrowth ON THE COAST, paid by
  Conservatory + Brushland/Trace; the Coast taps {U}{G}{G} for Cloud with one to spare, Cloud untaps
  Brushland and the Coast, and the Displacer resolves with three floating for the first blink. With
  Overgrowth on Conservatory the plan pays only Cloud-first and leaves nothing for the blink. The range
  made that Conservatory variant payable (it moves Overgrowth behind Cloud), it sorted first (host 1 <
  host 9), it won the class -- and the Coast variant, the only kill, was gone from the menu at every
  budget. Found with `MTG_DBG_MULTI` (the Coast variant is OFFERED by the gate) plus `MTG_EDF_RANGE_TRACE`
  (it never reaches an apply) and confirmed by `MTG_EDF_AURA_HOST_SIG_KAROO=0` (every host distinct: T3
  win in every arm). gi 201 is restored by the same lever; at d8/b0 its arms already part at the opening
  (the bottoming playouts search the same folded menus: new bottomed Overgrowth, part 2 OFF bottomed
  Fertile Ground).
* **gi 4, T3/T4** (two Kitchens; T3 hand Fertile Ground, Cloud, Overgrowth, Trace, 2 Drakes, 2 Displacers;
  the T4 draw is Aether Hub). The range adds {Cloud, Fertile Ground} at T3 (FG behind Cloud), and the
  search took it; the old line cast FG alone, kept Cloud and killed T4 off the Hub: Overgrowth on the Hub,
  Cloud, Drake, Displacer, then the blink loop. Holding Cloud is weakly better on every T4 draw (casting it
  at T3 leaves no T4 kill even with the land), so this is not a clairvoyant loss: the two T3 lines TIE in
  the search because the T4 kill is UNFINDABLE from the FG-only board. The earliest-win census
  (`MTG_DUMP_EWINS`, through a `--claude-play --choices-then-auto` hand-off at T3) lists {Overgrowth,
  Cloud, Drake, Displacer} at T4 with win 5 only; part 3 OFF lists a member that wins 4. The kill is lost
  INSIDE the T4 go-off continuation (the blink loop draws the library and re-solves), whose plans deploy
  five or more land Auras: their host variants fold to one representative, and with part 3 ON (the legal
  same-host stack) the representative is not the one that kills. With part 3 OFF the old behaviour -- the
  illegal second Aura on a shrouded host, silently re-homed at resolution -- happens to land the kill.

**2c (`MTG_EDF_AURA_RANGE_HOSTS`, default ON), as shipped -- two tiers**, armed only when the candidates
hold a host-declared land Aura and an untapper (only EDF holds an untapper, `etb_untap_lands`; every other
deck skips the block):

* a ONE-Aura plan that casts an untapper keeps every host variant (the provider proposes two hosts, so at
  most two members per class) -- this is what recovers gi 1457 and gi 201;
* a plan with two or more host-declared Auras keeps its first rank-order member AND its first member the
  arrangement MOVES (computed lazily, only for a plan colliding with a kept one).

Additive: it only ever keeps a plan the fold would have dropped. gi 1457 and gi 201 then win on their OLD
turn at the d3 cell itself (3 and 4) and at d8/b0. Fixture `test/scenarios/edf_aura_range_keeps_rank_order_host.json`
(gi 1457's T3 board, d3/b0: T3 win; `MTG_EDF_AURA_RANGE_HOSTS=0` no win; `MTG_EDF_AURA_RANGE=0` win), and
`edf_aura_range_drake_first.json` now reads `choose` (two hosts offered) where it read `accept`.

**Measured and REJECTED: keeping every host variant of every untapper plan.** It recovers gi 4 (4 at the d3
cell and at d8/b0), but the 2^k host variants of the go-off's many-Aura plans blew the pooled batch's
resident set from ~4.5 GB (v2) to the 35 GB cap: `build/scratch/edf-order/ab3/v4.*` (`mtg.new4`, commit
ab2c4699) was ABORTED by the RSS guard after 45 min (2439 of 22680 games; shipped 995 games: net -3 vs
old, 2c's own marginal -1). A ceiling sweep on gi 4 (full hosts for plans of up to N Auras, lazy above):
N = 1, 2, 3, 4 all leave it at 5; only "every class" finds T4. **gi 4 stays OPEN** -- the remedy needs a
cheaper way to keep the go-off continuation's host assignments distinct (a sound identity fold over the
continuation's Aura hosts, or a host rule for the continuation), not a wider fold exception.

A reconstruction gap worth knowing: `logs/edf-order/board_from_log.py` boards are NOT faithful for EDF host
choices. The scenario harness numbers cards battlefield-first, and `LandAuraHostCandidates` breaks a
host-score tie on the card number, so gi 4's reconstructed T4 offers the bare Kitchen where the game
offered the Aether Hub (no arm wins it as a scenario). The `--claude-play --choices-then-auto` hand-off
reproduces the real board.

### The re-measure on 2c (`mtg.new5`, the two-tier form)

IN FLIGHT (`build/scratch/edf-order/ab3/v5.*`, started 2026-10-16 04:16 UTC): `new5` on every A/B game
(1000 / 1500 / 10000, the same seeds), `no3v5` on the v2 prefixes (200 / 333 / 6667), and the identity
jobs (every suite deck + EDF all-off). 2c is inert with part 2 OFF, so `no2` (v2) stands; 2c OFF = v2 `new`.

## Verdicts (the bar: per cell net <= 0 against old, every slower game recovers at stage 1 or 2)

PENDING the v5 numbers; the v2 standing, lever by lever, is in the table above. Every part ships at the
default the USER approved (ON) until the USER rules: cast order, ranges and the route's order are USER
decisions (CLAUDE.md), so an agent does not switch one off on a measurement.

## Status (2026-10-16)

* Branch `worktree-agent-a2c348fc11430e668`, base `ff3e205b`. Nothing pushed.
* Binaries on the build volume: `build/scratch/edf-order/bin/mtg.{base,new,new2,new3,new4,new5}` (+ `mtg-test.*`).
* Units 1070/1070 and scenarios 163/163 on the two-tier 2c binary (`mtg.new5`).
* Diagnostics added (env-gated, print nothing unset): `MTG_EDF_RANGE_TRACE` (one line per range
  decision: the order in, whether the walk / the prepay pays it, the board, the order out) and the Aura
  host / land card numbers in `MTG_DBG_SEQ`.
