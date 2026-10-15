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

### Slices 2 and 3 (binary `mtg.new2` = commit 305cab78; arms v2 = the seven above plus `no3b`, `no4b`,
### `no2b` and `tutor` = MTG_EDF_TUTOR_LIBRARY_FIRST=1) and `s1p` (slice 1's seeds, `new` only, on mtg.new2)

IN FLIGHT.

## RESUME (2026-10-14 night stop, coordinator: the box goes to a value-leaf generation)

* **Base commit** `ff3e205b` (tip of `work/integ-2026-10-14`). Branch `worktree-agent-a2c348fc11430e668`,
  worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a2c348fc11430e668`. Nothing pushed.
* **Built:** the BASE binary only -- `/workspaces/MagicDeckTester/build/scratch/edf-order/bin/mtg.base`
  (+ `mtg-test.base`). The NEW tree (this WIP commit) has **never finished compiling**: the build was stopped
  at 114/119 (`DecisionProviders.cpp` compiled clean; `TurnSolver.cpp` was mid-compile). So nothing below is
  compile-verified beyond DecisionProviders.cpp, and no unit test / scenario / probe has run on it.
* **Code state (all five parts written, all default ON pending measurement):** see the table at the top.
  Unit tests in `test/unit/test_edf_cast_order.cpp` (added to CMakeLists). Still TODO in code: extend
  `test/scenarios/edf_shroud_blocks_second_aura.json` (or a sibling fixture) with the Overgrowth-then-Trace
  same-land stack, and a `validate_line` fixture for aura_drake (`cast=Peregrine Drake;cast=Fertile Ground`,
  `expect_verdict accept`).
* **Measured so far (base binary only):** per-game cost probe `build/scratch/edf-order/cost.{json,out,err}`
  (NOTE: that probe's chunks reused one seed -- the chunk seed must be base + game_index; the generator is
  fixed): shipped = d5/b20 (EDF has no value_play; `source=default`), ~82k units/game, ~95 s wall/game on the
  loaded box; d3/b10 ~30k units/game, ~55 s/game; d0 ~0.7 s/game. Identity batch on base was STARTED and
  KILLED within minutes (`build/scratch/edf-order/ident/base.*` is PARTIAL -- discard, re-run).
* **What is left, in order:**
  1. `bash /workspaces/MagicDeckTester/build/scratch/build2.sh` from the worktree root; fix any compile error;
     `build/Release/mtg-test` (all unit tests incl. test_edf_cast_order); `MTG_BIN=build/Release/mtg bash
     test/scenarios.sh`; copy the binary to `build/scratch/edf-order/bin/mtg.new`.
  2. Probes on new: `python3 logs/edf-order/probes.py /workspaces/MagicDeckTester/build/scratch/edf-order/bin/mtg.new new`
     (expect t4_drake_first T4 win; aura_drake Drake;FG line `accept`).
  3. Identity: `python3 logs/edf-order/mkmanifest.py ident <dir>/base.json base` and `... new.json new`
     (34 suite decks x d0 40 / d3b10 20 / shipped 20, seed 11500001; plus EDF with all five parts OFF on
     seed 11600001, which must equal base game for game); run each with its own binary
     (`env -C <worktree> MTG_PGO=0 MTG_DUMP_WINS=1 <bin> --batch <manifest> --threads N`), compare digests by
     job name.
  4. The pooled A/B (ONE batch, new binary): `python3 logs/edf-order/mkmanifest.py ab <out> <ship> <d3> <d0>
     <chunk> <part_ship> <part_d3>`. Arms: old (all five OFF), new (defaults), no1..no5 (one part OFF).
     Seeds: shipped 11300001 + gi, d3/b10 11310001 + gi, d0 11320001 + gi; loss = 9 (max_turns 8);
     `MTG_DUMP_WINS=1`. Planned sizing at today's per-game cost: old/new shipped 1000, d3 1500, d0 10000;
     the part-OFF arms on the same seeds' prefix shipped 300, d3 500, d0 10000 (~12 h at 16 threads on the
     loaded box; the USER's aims of 2000 / 3000 / 10000 were ~55 h for seven arms at full size). Analysis:
     `python3 logs/edf-order/analyze_ab.py <batch.out> <batch.err> --slower-list <tsv>`.
  5. Every slower game: stage 1 `--depth W --budget-ms 100` (W = max(5, old win turn)), stage 2 `--depth 8
     --budget-ms 0`; root-cause the rest.
  6. References (24): `MTG_PGO=0 MTG_BIN=<bin> python3 scripts/ref_bench.py --deck EldraziDisplacerFlicker`,
     `python3 scripts/ref_handoff_sweep.py --deck EldraziDisplacerFlicker --log-root <dir>` and
     `MTG_BIN=<bin> python3 test/viewer_protocol_check.py --strict --only EldraziDisplacerFlicker`, on base and
     new; report every change; never modify a reference.
  7. Update the EDF section of `docs/design/cast-order-rankings.md` (stale: "No implemented non-land cards")
     and this doc; set each part's default from the bar.
* Helper scripts (host drive, gitignored): `logs/edf-order/{probes.py, mkmanifest.py, analyze_ab.py,
  refactor_walk.py, dumpcards.py}` in the worktree.
* **Findings so far the USER should hear:** none measured yet. One design call made and stated here: part 2
  takes Q2's second route (an EDF `ArrangeCastOrder` with the enumeration's own sequential walk as the
  projection) rather than crediting untappers in the shared `FirstUnpayablePos`, so no other deck's
  payable-order fallback moves; and part 5 is implemented as extra route attempts that can add a kill but
  never displace one (its stated rule is above).
