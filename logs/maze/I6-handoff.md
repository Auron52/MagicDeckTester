# I6 (Maze's End batch-2 integrator) -- 2026-11-20

Worktree `.claude/worktrees/maze-batch2`, branch `wip/maze-batch2`, built on origin/phase-1-2-deck-analyzer 1f3c23fab
by cherry-pick only (never merge). Scratch `build/scratch/maze/i6/`. Base binary for d8: `.claude/worktrees/maze-i6-base`
(detached at 1f3c23fab, built; remove with scripts/safe_worktree_remove.sh when done).

## Components (new hashes on wip/maze-batch2)
1. wip/maze-shared: 85721a2d7 (ea314cc1a), 379e75755 (90560457f), dc15b6d4b (39bc4a308, the 13 mazesend_overnight keys +
   .wins; only the header line conflicted, origin's kept; the rest of overnight is origin's).
2. DM: 116548531, 5ed94f92e, fb56e8675, 59d2b5f75 (5fb2556fc compile fix), 47cd6f0a0 (32061a166 MTG_LEAF_GAME_OVER ON;
   MTG_INWIN_EXIT_GUARD ON), 78a07e3b2 (handoff).
3. RC: 56df8846f (e201e33a9 bind guards + tests), ecf0ab0cf, bbc0e358e, 67052f2c1 (b01b594fd INCLUDED: print-only,
   gated on MTG_LEAF_VERIFY + MTG_LEAF_VERIFY_TRACE, and RC-3's 0-moved regression ran on it), 0c01bf729, cc556eff0,
   474222571. wip/maze-rc1-p5b NOT included.
4. P7: 040c37b46 .. 3aaa5edd2 (all 28 commits of wip/maze-p7; code tip 9558dfa51 = 3ca5e9f1e).
5. F6: 72ed66e7f (ac4b294d0; MTG_SCALED_FED_FLOAT + MTG_MAZE_TAP_BALDUR_LAST ON).
6. P5d (added on the USER's scope ruling): 7aca1bdd9 (fd6e382fd) .. 3abaf26fb, every commit except 070ff8061 (its GT);
   all doc commits rebased (doc conflicts unioned). a6b56e4fa flips MTG_MAZE_IDLE_FETCH_REVEAL to DEFAULT ON (USER quote in
   the comment). FETCH_TIER_FULL / FETCH_RANK_COND / MANABOND_DISCARD stay OFF.
7. aea784ee0: GT accept (25 keys) + refreshed test/suite_cost.json.
Conflicts: append lists (HeuristicArm.h enum + names, checked positionally against origin: 0 new mismatches; TurnSolver.h
test seams) unioned; DecisionProviders.cpp/.h: P7d+F6 blocks vs P5d blocks were pure insertions, unioned; P5d replaced the
P1b TapTutorPutEval comment (kept P5d's); CastDominanceKeep keeps P7's `int` signature.

## Validation (tiers on a6b56e4fa, MTG_PGO=0, VPC ON)
* build 0; units 3803/3803; scenarios 594/594.
* VPC 930 refs: 358 ok / 571 repaired / 1 beaten / 0 play-drift / 0 shuffle-dead / 0 board-diverged / 0 enum-gap /
  0 mull-drift / 0 contract-fail. REQUIRE-BEAT 2/2.
* Moved keys (accepted set == FAIL set, 25): smoke mazesend d0 6.8650->6.8500, d3 5.20->5.10, d5 5.6742->5.5758,
  mazesend2hg d5 5.3333->5.1667, hinata d5 5.49->5.50 (gi83, RC), alchemist d5 4.9400->4.9333, alchemist2hg d5
  5.14->5.12, midrangeburn d5 / creature_giving d3,d5 / creature_giving2hg d5 digest-only; regression mazesend d0
  6.864->6.828, d3 s2002 5.7333->5.6667, d5 s2002 5.5598->5.4946, d5 s3003 5.5867->5.5467, d5 s30081 5->4, d3/d5 s30121
  digest-only, creature_giving d5 s2002 4.6024->4.6000, other creature_giving / midrangeburn(2hg) digest-only.
  Decks moved: exactly DM-2's list + RC's hinata + Maze's End.
* Searched slower 12 (smoke 2, regression 10); d0 slower 254 (d0 net better: smoke 146/120, reg 158/134).
  Reused verdicts: hinata smoke gi83 (RC-2 d8 T5=T5); mazesend reg d5 s3003 gi14 (P7-9: d8 T7 in every arm incl. base
  reconstruction); mazesend smoke d5 g105, reg d5 s2002 g79 g113, reg d5 s3003 g58 g68 g108 (P5d-7 tier list, recovered).
  Fresh d8 b0 (`logs/maze/i6/new.out`, `base.out`, abandon 50M), combined vs origin 1f3c23fab:
  | game | GT | combined d8 | origin d8 |
  |---|---|---|---|
  | reg d5 s2002 g11 (seed 2013) | 5 | 5 | 6 |
  | reg d5 s2002 g42 (2044) | 5 | 5 | 6 |
  | reg d5 s2002 g86 (2088) | 5 | 5 | 5 |
  | reg d5 s3003 g112 (3115) | 6 | ABANDONED 50M | ABANDONED 50M | -> UNRESOLVED (both arms hit the ceiling)
* check_gt_logs 1358 consistent / 0 stale / 0 missing; check_references_safe OK.
* ref_bench maze_s_end: 0/11 short (human 5.818, search 5.818).
* Cost (suite_gate --measure-all --no-run, same regression run): mazesend worst d5 b20 4976.9 ms/game vs Hinata 5680.6
  = 0.88x (limit 3x). mazesend2hg has no regression rows; smoke d5 4229.9 ms/game vs Hinata smoke d5 4729.6 = 0.89x.

## USER rulings (verbatim, from the coordinator)
* "Yes, at this point we should only do high priority items. Ideally you would be wrapped up in a few hours so I can start
  a new deck here. And yes, I realize we won't be able to do the mulligan profile yet."
* "But I don't really like leaving the deck fully inoperable. Anything crucial or important should go in. We should check
  our regression test times for the deck to make sure they aren't too slow, but the deck should be in there."
