# KittyEquipment v2: one reference replay serialises the regression's reference stage

**Status:** diagnosed 2026-10-14; an exact (output-identical) fix is on branch `kitty-replay`, not
merged. See "Diagnosis" and "Fix" below.

## Symptom

`test/regression.sh` ends with the reference stage, `test/viewer_protocol_check.py --strict --threads 24`.
The checker's own usage note calls the full sweep a matter of seconds. On 2026-10-14 the stage had been
running for 3h40m and was still going, for commit 69e479bf. Almost all of that time went to ONE reference:

    references/KittyEquipment/v2-puresteel-hammer/claude_s8_gi7.json   (20 decisions; last changed 9d86a8f2, 2026-10-03)

The checker replays a reference one step at a time, with one `mtg` invocation per step
(`--claude-play --depth 0`, and a `--choices` prefix that grows by one per step). A single reference
therefore runs on one thread, and the other 23 sit idle. The run for the rebased tip 03d32461 hit the
same reference within minutes of starting the stage.

## What was observed

* **The slowness is in particular steps.** In the 03d32461 run the first five steps took about a
  minute in total. The step with `--choices 16,-1,-1,25,-1,-1` then ran for more than 10 minutes at
  99% CPU and ~23 MB RSS. In the 69e479bf run, steps near choice 13-14 took 3-15 minutes each.
* **It is depth 0,** so the turn search should not be running at all. Whatever costs minutes is done
  by the depth-0 `--claude-play` path at a decision point. One hypothesis, unverified: the
  enumeration of legal options that the viewer protocol dumps at each decision, which would grow with
  the number of equipment-host pairs.
* **This is separate from** the documented d3 search slowness in
  `analysis-KittyEquipment-v2-puresteel-hammer.md`.

## Repro

Run from the repo root with a Release build:

    build/Release/mtg decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod --claude-play --seed 8 \
      --game-index 7 --max-turns 8 --depth 0 \
      --profile decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.profile.json \
      --choices 16,-1,-1,25,-1,-1 --force-mulligan 0: \
      --cast-order '10:*|Cid, Freeflier Pilot|Golem-Skin Gauntlets;11:*|Shadowspear|Shadowspear|Dragonfire Blade|Colossus Hammer;15:*|Puresteel Paladin|Colossus Hammer|Skateboard|Accorder'"'"'s Shield|Cathar'"'"'s Shield' \
      --force-attackers '3:Sram, Senior Edificer' \
      --tap-pref '2:pre:3:32,33;3:pre:10:32,33,34;4:pre:15:32,33' \
      --legacy-main-swap --legacy-karoo-float --legacy-pain-pay --legacy-gsz-shuffle

## Diagnosis (2026-10-14)

**It is the equip route's lethal detector, run by the viewer's menu enumeration, and inside it the
enumerator's odometer walking millions of positions it can never pay for.** Not the dump, not the
turn search.

* **The call path.** Every main-phase frame the replay passes through (and the step re-plays every
  earlier frame, so the prefix pays again) enumerates its menu with `TurnSolver::EnumerateMainPlans`.
  `CollectActions` (TurnSolver.cpp, "THE MECHANICAL EQUIP-CHAIN GO-OFF") calls `EquipRouteTrial` on
  any board that passes `EquipRoutePiecesInPlace` (a draw watcher, a creature, an artifact in hand --
  Sram on turn 3 here). The trial's greedy certificate fails (no kill exists on turn 3), so
  `equiproute::TrialPolicy` falls through to the DFS, `RunSearch`, in its `viewer` regime: no budget,
  a fixed cap of `MTG_EQUIP_ROUTE_VIEWER_NODES` = 16,384 links. Each link is one
  `EnumerateMainPlansExhaustive` + one `ApplyPlan`, and the links run under `HumanPlayActive()`, so
  they enumerate the unpruned human menu. The DFS never finds a kill and stops only at the cap:
  `t3 SEARCH CUT (viewer; 16384 links applied ...)`. Each land option (play the Plains or not) is a
  separate board, so a gated frame typically pays two such DFS walks.
* **Why each link cost 0.3-0.85 s.** perf on the `--choices 16,-1,-1,25,-1,-1` step (Profile build,
  dwarf call graphs): 97.5% under `equiproute::RunSearchRec -> EnumerateMainPlansExhaustive ->
  EnumeratePlans`; the hot frames were `eval_and_push_body` (51% self), the odometer walk itself (16%),
  `AddCostCarryingHybrids` (8%), `SubsetHasStrandedEquip` (8%), `FungibleEquipCopyAdvance` (4%).
  `MTG_BRANCH_SHAPE` over a 20-link run: 17,325,113 `eval_and_push_body` entries for 617 plans across
  48 enumerations. The chain draws Puresteel Paladin into hand ({W}{W}, uncastable off 0-1 untapped
  lands), which arms `any_metalcraft`, which adds `metalcraft_bound` -- the generic of EVERY Equip
  candidate -- to the walk's scalar mana bound: `[chain-bound] mana_bound=75..98` on a 0-1 mana pool.
  So every canonical equip arrangement passed the walk and died at the body's flat-mana gate. And on
  the boards without Puresteel (bound 0-1) the walk still stepped through every position one at a
  time, since a refused position is not a jump.
* **Cost of one frame, old binary:** 3,562 links in 19.4 CPU-min (trace run), i.e. ~90 CPU-min for the
  16,384-link cap -- per gated frame, per land option, per step that replays it.

## Fix (branch `kitty-replay`, src/ai/TurnSolver.cpp, EnumeratePlans' inline scalar walk)

Three changes, all to which odometer positions are VISITED; the emitted plans and their order are
unchanged.

1. **`MTG_MC_WALK_BOUND` (default ON).** Replace the frame-wide `metalcraft_bound` with the credit
   the body really grants THIS selection (`McWalkCredit` = `SameTurnMetalcraftEquipCredit`, else
   Kili's storied waiver, from precomputed tables): reject when `mcost - credit(sel) > mana_bound -
   metalcraft_bound`. `MTG_MC_WALK_BOUND_VERIFY=1` feeds every refused position to the body anyway
   and counts any that yields a plan.
2. **`MTG_MANA_WALK_CUT` (default ON).** A radix cut on the walk's own test `mcost <= mana_bound`:
   with every candidate costing >= 0, if the digits >= h already cost more than the bound, zero the
   digits below h and carry from h. Exact by inspection -- those positions never reached the body.
3. **`MTG_MC_WALK_CUT` (default ON).** The same jump for test 1, from a lower bound on the credited
   cost over every completion of the high part (credit OFF: Kili's single largest equip; credit ON:
   the high part's equips forgiven, plus the cheapest reducer cast still selectable below h when no
   reducer is in play). VERIFY checks the per-position test refuses every position of every block.

## Measurements (box shared with another investigation, so wall is inflated; CPU is the number)

* The `16,-1,-1,25,-1,-1` step: old **5,288 s user (88 CPU-min), 2h54m wall**; new **2.0 s user /
  3.1 s wall**. The two stdouts (99,097 bytes) are byte-identical.
* With a 20-link DFS cap on the same step: old 9.58 s user, new 0.24 s, stdout byte-identical;
  VERIFY: 17,309,325 positions refused, 0 violations, 0 cut violations.
* Whole reference, every prefix step timed (19 stateless spawns): **~417 s user in total** (vs hours
  for step 6 alone); per-step stdout hashes identical across the bound-only, bound+cut and final
  binaries. Per step: steps 1-5 0.1 s, 6-9 2-4 s, 10-14 12-14 s, 15-19 61-77 s.
* Checker, all 30 KittyEquipment references (`--only KittyEquipment/`): 10 ok, 20 repaired, 0 of every
  failure class; the 29 references the old binary can finish produce byte-identical result lines
  old vs new; `claude_s8_gi7` reads `repaired ... won=True win_turn=4 vs ref win_turn=4; 4 stale
  index/indices repaired (602->545)`, the same as the 2026-10-06 sweep's line.
* Unit tests (`mtg-test`): 596/596 pass.
* Autonomous A/B, old vs new binary, game logs identical (runId aside): kittyv2 d3/b10 8 games, d5/b20
  8 games, and 4 games each at d3/b10 of kitty, kittyv3, burn, knights, whiteknights, slivers, goblins,
  auras, stompy, angels, soldiers, critter, selesnya, giants, pirates, fivecolour, minotaur,
  creature_giving. VERIFY on kittyv2 d3: 95,695 refused, 0 violations; 109,081 cut-block positions, 0
  cut violations.

## What is left (not done; each is a decision, not a free collapse)

* The reference is still the stage's longest: the turn-4 steps cost 61-77 s user each. The cost is
  now general per-link engine work (ApplyPlan, state copies, payment, sim keys; perf is flat) in the
  DFS, which still walks its full 16,384-link cap twice per gated frame (once per land option)
  because no kill exists -- ~3-5 s per T3 walk and ~6-8 s per T4 walk -- plus the COMBO OFF gate's
  trial applies once the route proves a kill (`MTG_PLAY_STEP_TIMING`: trials=3, ~37-45 s of nested
  wall on a contended box).
* The checker replays statelessly, so step k re-runs every gated frame before it: O(n^2) frames. The
  viewer's own `--interactive` persistent child would make it O(n), at the price of not exercising
  the stateless spawn the checker exists to guard.
* The DFS links enumerate the HUMAN menu (unpruned + valve) because nothing suppresses
  `HumanPlayActive()` inside `RunSearch`. Running the viewer's trial under the search's own menu
  semantics would be far cheaper but can change the verdict, so whether the menu offers COMBO OFF:
  a viewer-semantics decision for the user, and references could move.

## Why it matters

Every regression run spends hours of wall clock on this one reference, with the box almost idle. That
delays the validation of every commit.
