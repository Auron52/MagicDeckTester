# DM-2: "deeper search loses lines", cases 1 + 2 (Maze's End gi73, Martyr gi149) -- DONE, ready for batch 2

Worktree `.claude/worktrees/maze-dm1`, branch `wip/maze-dm1` (pushed), based on origin/phase-1-2-deck-analyzer 8fbbc5023.
Scratch: `build/scratch/maze/dm2/`; summaries copied to `logs/maze/dm2/`. Case 3 (Gods gi16) not touched: still T9 at d3 b0.

## Commits (on top of DM-1's 7ec94d53b / c475494cd / a398da039)
- `5fb2556fc` restore `gamelc::t_hit_key` (c475494cd removed the wrong namespace's member; Windows CI was red on it).
- `32061a166` **MTG_LEAF_GAME_OVER** (heurarm LEAF_GAME_OVER, default ON) + keep **MTG_INWIN_EXIT_GUARD** ON with corrected
  evidence in its comment; seam unit test `test/unit/test_leaf_game_over.cpp` (replaces DM-1's disabled game test,
  `TurnSolver::LeaflessFrameWinForTest`).

## Root causes
- **Case 1, Maze's End s3076 gi73 (d5 T5, d6/d8 T6).** `SimulateEndAndStartNextTurn` leaves the turn counter on the
  ending turn when the game ends at the boundary (Maze's End's off-main win at our end step / their turn,
  `won_by_effect`; also deck-outs). A depth-1 node's depth-0 child is then a state already won at T. FSLineWin's depth-0
  frame scored it as a horizon leaf: the no-leaf constant said "no win" (the value model, an estimate), so a LEAFLESS
  node at T5 with depth 1 missed T5's end-step win that the same node at depth 2 finds. The answer depended on remaining
  depth (hypothesis (b)), and the depth-free inwin map stored the depth-1 "no win by 5" and served it to the depth-2
  node. Corrected verify provenance (pre-fix): `DISAGREE turn=5 depth=2 cutoff=5 cached=9 fresh=5 leaf=2 | stored:
  turn=5 depth=1 cutoff=5 leaf=2 sup=0 twop=1 hexits=0` (chained T4 d3, T3 d4, T2 d5). Not hypothesis (a): the store was
  leafless. Fix: a depth-0 frame whose game is already won returns `state.turn_number` (constant and value leaves; the
  rollout already plays the won state out to T). After: d5/d6/d8 b0 all T5, guard on or off; verify 390 hits, 0 DISAGREE.
- **Case 2, Martyr s2151 gi149.** Does reproduce on 8fbbc5023 with the guard OFF (DM-1's note said otherwise): d6 b0 = T6
  guard off, T5 guard on (both with and without LEAF_GAME_OVER). Verify, guard off: 2 MATERIAL, stored by nodes whose
  subtree took horizon exits (`stored: turn=3 depth=4 cutoff=9 ... hexits=4`, `turn=2 depth=5 ... hexits=5`). So
  **MTG_INWIN_EXIT_GUARD stays ON on evidence**; it is the fix for case 2. d8 b0 = T5.

## Measurement (binary = 32061a166's code; `mtg.v2`)
- Units: 3775/3775 test cases pass. Windows CI run 44716351938 on 32061a166: **success**.
- Smoke + regression (`--both`, VPC_SKIP=1): 16 moved keys, searched slower=0 in both tiers (faster=2 each).
  Exact keys in `logs/maze/dm2/tier_moved_keys.txt`:
  smoke: midrangeburn_smoke_d5_s1001, creature_giving_smoke_d3_s1001, creature_giving_smoke_d5_s1001,
  creature_giving2hg_smoke_d5_s1001, alchemist_smoke_d5_s1001 (4.9400 -> 4.9333), alchemist2hg_smoke_d5_s1001
  (5.1400 -> 5.1200);
  regression: mazesend_regression_d3_s30121, mazesend_regression_d5_s30121, mazesend_regression_d5_s30081 (5 -> 4),
  creature_giving_regression_d3_s30001, creature_giving_regression_d5_s30001, creature_giving_regression_d3_s2002,
  creature_giving_regression_d5_s2002 (4.6024 -> 4.6000), creature_giving_regression_d5_s3003,
  midrangeburn_regression_d5_s2002, midrangeburn2hg_regression_d5_s2002 (the rest are digest-only).
  **GT NOT accepted** (left to the batch-2 integrator). `test/check_gt_logs.py`: 1345 consistent, 0 stale.
- Held-out A/B, ONE pooled batch, arms innermost (overnight seeds of the moved decks, d0 dropped, + 2HG held-out
  s4004-7007): base = both levers off (= 8fbbc5023), guard = guard only, ship = both on. Then a second pooled batch
  widening creature_giving (s8008-14014) + creature_giving2hg (s8008-12012), base vs ship. Ship vs base, pooled:

  | deck | games | net turns | better | worse | units | lever |
  |---|---|---|---|---|---|---|
  | alchemist | 2535 | -6 | 7 | 1 | 1.006x | guard |
  | alchemist2hg | 600 | -1 | 1 | 0 | 1.007x | guard |
  | creature_giving | 6065 + 8550 | +1 / -8 = **-7** | 9 | 2 | 0.98x | leaf-game-over |
  | creature_giving2hg | 600 + 1200 | 0 / -1 = **-1** | 2 | 1 | 0.98x | leaf-game-over |
  | mazesend | 1286 | -1 | 1 | 0 | 1.000x | leaf-game-over |
  | midrangeburn | 6006 | 0 | 0 | 0 | 1.002x | - |
  | midrangeburn2hg | 680 | 0 | 0 | 0 | 1.001x | - |

  NOTE: creature_giving's FIRST sample alone was net +1 (1 better / 2 worse); the widening batch was run after seeing it,
  so both are reported. Mechanism of its worse games: the value model scored a state whose opponent was already dead
  at the T6 upkeep as "no win" (9); with the fix the d5 b30 T1 pass 5 sees that exact T6 and commits it, where base
  (seeing nothing) happened onto a T5 line by value ordering. A horizon effect, not a defect.
- d8 b0 on every ship-slower game -- ALL recover: creature_giving s4004 gi290 (seed 4294) 6 -> 5, s5005 gi419 (5424)
  6 -> 5, creature_giving2hg s8008 gi212 (8220, life 30 heads 2) 6 -> 5, alchemist s7007 gi325 (7332) 6 -> 5.
- VPC: full corpus held (Galadriel s10 / F5R, owned elsewhere); Maze_s_End, Midrange_Burn, Creature_Giving,
  Accomplished_Alchemist strict = 42 refs, 15 ok, 27 repaired, 0 play-drift / board-diverged / enum-gap (pass).

## What remains (for the integrator)
1. Rebase wip/maze-dm1 onto the shared branch in the push-queue turn, rebuild, smoke, accept smoke + regression GT for
   the 16 keys above (they are this branch's whole move), `test/check_gt_logs.py`, `scripts/check_references_safe.py`.
2. Possible follow-up, not done: the rollout leaf on a won state replays a turn before noticing (SimulateToEndImpl only
   short-circuits `opponent_decked`); returning `state.turn_number` there too is a wasted-work collapse that moves
   rollout-deck budgets, so it needs its own A/B.

## USER rulings carried in the brief (verbatim)
- "everything should be findable at unlimited or large budgets"
- Bar: per-deck net <= 0 on held-out; every slower game recovers at --depth 8 --budget-ms 0. A soundness fix is default ON.
- Coordinator hold: run every regression tier with VPC_SKIP=1; run the reference gate separately for the moved decks.
