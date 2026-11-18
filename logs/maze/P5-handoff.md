# P5 handoff (Maze's End provider prunes) -- 2026-11-18

Worktree `.claude/worktrees/maze-p5`, branch `wip/maze-p5`, base `wip/maze-end` c9caae1c8.
Brief: the coordinator's P5 brief (items 1-13, below). The coordinator asked for this handoff at ~290K context.
Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/p5/` (build1.log, units log, mkab.py, anab.py).

## Commits
- `589cf5847` P5 WIP: every code change below, the new unit file, profile max_lands 6, and the suite rows.
  It was committed BEFORE the build finished. See "Unit result" for the first units run.

## Built, item by item (all in 589cf5847)
Levers are heurarm slots, readable as `flags` in a batch manifest. They replace the three MTG_MAZE_WSEL_* slots
at the end of `HeuristicArm.h`. Each is DEFAULT ON unless it is marked as a variant.
1. **Find nothing** `MTG_MAZE_FIND_NOTHING`. `MazesEndProvider::TapTutorPutCandidates` returns true ("narrowed",
   so no fail-to-find) whenever the name list is non-empty after the Plaza filter. Premise (in the code comment):
   the cost and the shuffle are paid either way, a found Gate only adds, and nothing in the list rewards a smaller
   board or a bigger library. If only an unpayable Plaza remains, the list empties and the caller re-offers the decline.
2. **Burgeoning menu = land-drop menu: NO CODE NEEDED, verified by reading.** Both menus are distinct land names x
   `LandEntryPinCandidates` (P1 colour, entry pins), plus none. The new `LandPlayOffered` restriction (item 4) is
   applied to BOTH: the TurnSolver land menu and `ResolveBurgeoningTriggers`. No unit test yet compares the two menus directly.
3. **Archdruid's Charm "to hand": NO CODE NEEDED, there is nothing to prune.** By the card's oracle text and the engine
   (`tutor_land_to_battlefield`, `PerformLandTutorToBattlefield`), a land always goes onto the battlefield tapped and
   no to-hand variant exists. The P1d item-8 "x2" is probably the tutor axis's c==0 duplicate: the base plan
   (unpinned) plus the pinned (index 0, pins.front()) variant, TurnSolver ~93525. That is an ENGINE identity-fold
   candidate (check that the unpinned default == pins.front() for colours: LockEtbChosenColor asks EtbChosenColor
   first). Not built; report it to the coordinator.
4. **Gateway Plaza** `MTG_MAZE_PLAZA_PAY`. (a) `LandEntryPinCandidates`: Plaza -> {1} (pay). (b) New hook
   `DecisionProvider::LandPlayOffered` (appended at the END of the class) is consulted in TurnSolver's land_names
   loops (both branches) and in `ResolveBurgeoningTriggers`. The Maze override returns false for an unpayable Plaza.
   (c) Archdruid's Charm (`TutorCandidates`, only for `tutor_land_to_battlefield`; Sylvan Scrying goes to hand and is
   unaffected) drops an unpayable Plaza but never empties the list. (d) Maze's End's fetch drops it with the
   activation's {3} owed first, and may empty the list. Payable (`MazePlazaPayable`) = an Amulet (untapper count > 0),
   or a Gond Gate controlled (the Plaza is a Gate), or a probe `TapForCostDirect` of owed+{1} on a state copy.
   Optimistic by design. Manabond cannot leave a land out, so item 9 keeps "no" for that case.
   CAVEAT (stated in code): with Guild Summit out, a sacrificed Plaza still draws a card on entry.
5. **Heap Gate Treasure** `MTG_MAZE_HEAP_TREASURE`. New hooks `PlanListFilterLive`/`PlanListAdmit` (+ struct
   `PlanView`, appended at the END of DecisionProvider), called in `EnumeratePlansWithLandUncached` right after the
   sac-cost filter. They run in every enumeration, root and continuation, materialise the PB lazy list, never empty
   the list, and are skipped in human/unpruned play.
   Definition of "otherwise unused": a plan P with the Treasure is REMOVED when the list also holds
   "P minus the Treasure, plus one more mana-spending action c" with the same land. In other words, the board could
   have paid for c instead.
   Action identity = kind|name|mode|tutor_target|chosen_x|sac_source_id|hand_index.
   Kept regardless when `MazeTreasureBinds` (next turn's mana or a colour binds): next = lands + Treasures + 1 if a
   land is in hand. A spend binds when it needs exactly one more: a hand card's mana value, its G or U pips against
   the sources of that colour, Talon's {4}, or Maze's End's {3} (next-1).
6. **Exploration land-order fold: NOT BUILT.** The fold point is the site-10 breakpoint CONTINUATION enumeration. That
   enumeration has no record of the previous section, i.e. "the last section was land-only, land X". It would need
   either a state field folded into the continuation's cache key or a thread-local set at the continuation
   (TurnSolver ~78282 `LandDropNewlyOpen` arming / the deferred re-solve). Rule to build:
   - when the previous section played only land X, drop land Y from the continuation's land menu if Y < X
     (canonical order) and the two commute;
   - "commute" = neither land has an entry pin choice (colour/Plaza), a fastland condition (Thran Portal), Gond's
     gates_enter_untapped, The Black Gate's pay-life entry, or a Karoo bounce.
   Check at d8 b0. Possibly hand it to P5b (the engine duplicate-copy fold agent).
7. **Colour single (disclosure 1).** P1 (`MTG_MAZE_ONE_COLOUR`, shipped) ALREADY gives one colour per entry at every
   route (land drop, Burgeoning/Manabond pins, Charm pin, Maze fetch pin). The only remaining width is the Charm
   c==0 duplicate of item 3. Built: the variant `MTG_MAZE_COLOUR_ALT` (default OFF), which alternates by source count
   (G while green sources <= blue sources, else U) instead of P1's "no G -> G, no U -> U, else G".
8. **Basilisk Gate pump: NO CODE NEEDED.** It is never offered autonomously: TapTargetPump needs a creature target and
   the list has none and makes none (card note; ledger decision 3). Not verified by a census run.
9. **Manabond "no", all-land hand** `MTG_MAZE_MANABOND_ALL_LANDS`. New hook `CrossingDumpAllLandsDominatesNo`
   (END of class). `PerformEndStepManabond` forces yes without consuming a rank, and the crossing-pending predicate
   (TurnSolver ~110458) mirrors it: it returns false when Manabond alone is live (no pending Burgeoning put), the
   hand is all lands, and no land has 2+ pins. "No" is KEPT when a Plaza in hand is unpayable at the end step.
   Premise in the code comment: a land on the battlefield dominates the same land in hand, nothing rewards holding
   lands, and Summit draws come sooner.
10. **A1 -> v5** `MTG_MAZE_FETCH_EVAL_V5` (fetch eval 100; =0 -> 200). `MazeWeights()` selector deleted.
11. **Talon variant** `MTG_MAZE_TALON_LACKDROPS` (default OFF): Talon is offered only when no Exploration, Burgeoning
    or Manabond is controlled or in hand (`MazeLacksExtraDrops`). Ship whichever measures better.
12. **max_lands 5 -> 6** in `decks/Maze's End/Maze's End.profile.json`. For the A/B, `mkab.py` writes
    `build/scratch/maze/p5/profile_ml5.json` (max_lands 5; the deck has no sibling models).
13. **Suite rows** (`test/regression_cases.sh`; GT NOT accepted).
    - smoke: d3 1001 50 -> 10, d5 1001 100 -> 132;
    - regression: d3 2002 75 -> 15, d5 2002 150 -> 184;
    - overnight: d3 4004 125 -> 25, d5 7007 300 -> 357.
    Equal cost by S4 units/game (d3 b10 184K/145K, d5 b20 230K/255K). `mazesend2hg` (d3 50 b10 smoke canary) is
    left unchanged: an open question for the coordinator (it is a separate key).

## Unit result
Build: `./build.sh` hit the 9p `Cannot allocate memory` read error on test objects three times (build1-3 logs,
no compile errors). The fourth try succeeded (`build4.log` exit=0). `build/Release/mtg` (07:35) and `mtg-test` are
the 589cf5847 tree. Units (`build/scratch/maze/p5/units1.log`): **3437/3444 test cases pass, 7 fail (10
assertions)**.
- EXPECTED, from the new defaults (pin the lever off in each test, as P1d did):
  - `test_mazes_end.cpp:219,228` (MAZE_FIND_NOTHING);
  - `test_mazes_end_p1d.cpp:183` (MAZE_PLAZA_PAY), `:240,251` (MAZE_FIND_NOTHING);
  - `test_mazes_end_i2.cpp:368,477,587` (MAZE_PLAZA_PAY: the pay+sacrifice menus and the sacrifice variant).
- REAL, to investigate: `test_mazes_end_p5.cpp:161-162`. Maze's End's fetch with ONE untapped Forest and
  names {Gateway Plaza}: expected the Plaza dropped (it cannot pay {3}+{1}) and false/empty. Got true and non-empty.
  So either `MazePlazaPayable`'s probe `TapForCostDirect(probe, {4})` returns true on one Forest (does it pay
  partially, or count something else?), or the drop is skipped. Line 163-165 (Plaza + Simic) passed, so check
  whether `MazeDropUnpayablePlaza` with a single name... note the first build of the provider had a `names.size() <= 1`
  early return, later replaced by `names.empty()`. CHECK that the compiled object is the edited one: DecisionProviders.cpp
  was edited during build1 and rebuilt in build2-4, so it should be. Debug with a print of the probe.

Existing tests that P5 defaults may break (pin the lever off in each, like P1d did):
- `test_mazes_end.cpp` ~210 (P1dOff struct: add MAZE_FIND_NOTHING = 0);
- `test_mazes_end_p1d.cpp` 183 (Plaza {0,1}: pin MAZE_PLAZA_PAY 0);
- `test_mazes_end_i2.cpp` 368/462/572 (Plaza pay+sac menus);
- `test_mazes_end_i3.cpp` Manabond tests;
- any test expecting a fetch eval of 200.

## Remaining, in order
1. Fix the build and units (pin levers off in the old tests; fix test_mazes_end_p5.cpp if its fixtures are wrong).
2. Smoke (Release, MTG_PGO=0) vs `build/scratch/maze/f2/smoke-f2/wins` (same src as wip/maze-end). Only mazesend
   rows may move. Note the suite row counts changed, so the mazesend keys differ in count anyway.
3. Base equivalence: the `base` arm (all P5 levers off + profile_ml5) must equal the old engine
   `build/scratch/maze/f2/mtg_f2b` (= wip/maze-end's final engine per F2-results) on a d5 b20 sample, digest for digest.
4. The pooled A/B (below), then d8 b0 on every slower game, the starved-games table, and cost vs 3x Hinata (15,510 ms;
   re-time uncontended if the box is busy).
5. Item 6, if the coordinator still wants it from P5.
6. `logs/maze/P5-results.md`, the ledger section "P5", push `wip/maze-p5` only (no shared push, no GT accept).

## Intended A/B (`build/scratch/maze/p5/mkab.py <manifest>`; `anab.py <run.out> [ref]` tabulates)
- ONE pooled batch, arms innermost, 8 workers, `MTG_RSS_CAP_GB=16`, profile attached.
- Arms:
  - `base` = all P5 levers off + ml5;
  - `all` = defaults + ml6;
  - leave-one-out: `nofind`, `noplaza`, `noheap`, `nomb`, `nov5`, `ml5`;
  - variants on top of all: `colalt`, `talonlack`.
- Shapes and seeds (unused by the suite, P1d, V1, F2 or the sweep; 131001/132001 were V1's dropped 5g scout and
  were avoided):
  - d5 b20: 141001+gi x200
  - d3 b10: 142001+gi x150
  - d1 b1: 143001+gi x100
  - d1 b3: 144001+gi x100
- Plus the starved re-check (base + all, d5 b20): 111001+gi for gi 32, 33, 100, 124, 142, 145, 147, and the sweep
  61001+gi for gi 0, 7, 13.
- ~5,500 jobs. Rough estimate ~30-40 min at 8 workers.
- Check the loss encoding in run.out (anab.py assumes avg 0 = no win, scored as 9).
- d8 b0 recovery: every game slower in `all` vs `base` (and in each LOO arm's adopted direction), at d8 b0 with an
  abandon ceiling of 2M units. Report every game that cannot finish.

## Design decisions / premises settled (agent defaults, to be reported to the USER)
- "Otherwise unused" for the Treasure = the plan-list superset test above (plus the binds exception).
- Plaza payability is optimistic: an over-admitted Plaza is a wasted branch, never a lost line.
- Charm keeps an unpayable Plaza if it is the only candidate (no whiff). Maze's fetch may empty to the decline.
- Talon "lacking extra land drops" = no Exploration, Burgeoning or Manabond controlled or in hand.
- The colour variant = alternation by source count.
- Items 2, 3 and 8 need no code (reasons above).
- Item 6 is not built.

## Running jobs
None. The builds and units are finished. No A/B was started (the coordinator's instruction).

## USER rulings (VERBATIM, copied from docs/design/mazes-end-work-queue.md)
1. Tie fix: "For 1 I'm not too worried about d0, but the searched is a little more troubling. Have we looked at the results and determined it was just ties breaking one way or the other? I would analyze them and 100% make sure they are recoverable. If alll of those things are try then we can accept."
2. Greedy scoring ties: "Those sound reasonable to fix. For Reveillark evoking can be worse or better depending on the context."
3. C1-C4: "Yikes we should fix all of these problems and A/B them."
4. Defaults table + P4 prunes: "2-4 Yes, Talon Gates is potentially a worthy candidate if we are lacking in extra land drops. The rest of the table. For d3 I would consider playing more d5 games instead of the majority of d3 if they are faster. No point in playing d3 if we can test play settings instead at a cheaper price. Maze's End "find nothing" should be heuristically pruned. I thought we already did the duplicate merge, but if not then yes, let's do it. Burgeoning choice is the same as playing a land. If we need to we can prune it, but otherwise just treat it as a land-drop. Archdruid's Charm, yes it should go to the Battlefield in our heuristic. 5. We should either play it and pay the 1 or not play it. Sacrificing is a dominated option. 6. It is also worth it when we have literally nothing else to use the mana for. In fact, this is the main use-case. 7. True, the order doesn't matter much here unless the second land is played after other things or we are paying for a cost like Gateway Plaza."
5. Fetch defer + max_lands: "2-5 I'm not entirely sure that is 100% the right solution given that there is value to banking extra lands even if you have a new-name gate in hand. That said, I don't have recommendations as to how to value this while still not breaking anything. 2-6 Not a huge deal either way because it is temporary. We just want to get the deck in a position that we can run the mulligan profile generation. However, if you would feel a lot better about it you can make the tweak."
6. Off-main activation, b0, b20: "2-7 I think we should allow something like this. It's especially easy if we can respond to a trigger from Manabond or Burgeoning and activate in response. I cannot think of a reason why that would not be good, so that could be heuristically pruned to always search and deploy. The searched decision would be whether it is worth getting Maze's end back in hand without being able to redeploy. Ideally we would support that as well and see what we can do to minimize the cost. 3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you. 3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other avenues."


### USER on the B disclosures (VERBATIM)
7. "4-10 1 We might want to prune further then that with an A/B. It would be nice if there was no extra branching given how easy the mana is in this list. 3. Yes, these should be heuristically pruned. 13. Yes, this should be pruned heuristically in the search. 16-21. That's fine. 25. I'm not sure what this means. 28. Yes, manabond yes with no land should be pruned. Similarily "no" with only lands should be pruned heuristically. 31-32 Okay, sounds good."


### Also from logs/maze/USER-rulings-P5.md (VERBATIM, same rulings with the Part 2 items)
"2-5 I'm not entirely sure that is 100% the right solution given that there is value to banking extra lands even if you have a new-name gate in hand. That said, I don't have recommendations as to how to value this while still not breaking anything. 2-6 Not a huge deal either way because it is temporary. We just want to get the deck in a position that we can run the mulligan profile generation. However, if you would feel a lot better about it you can make the tweak."

