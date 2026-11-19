# P7-4 handoff (Maze's End performance) -- 2026-11-19

Worktree `.claude/worktrees/maze-p7`, branch `wip/maze-p7`. Results: `logs/maze/P7-results.md` s10. Scratch:
`build/scratch/maze/p7/` (binaries `mtg.p7e` = step 1, `mtg.p7f` = (a)+(b); tools in `tools/`).

## Done (on top of origin/phase-1-2-deck-analyzer e682d7942)
- `1b374186f` fold1, `37b86db2a` fold2, `78197a557` P6 guard, `795974991` + `947a3796f` handoff docs (rebased; P6
  cherry-pick dropped).
- `17d414e8f` (HEAD:src `c9f26082e`) STEP 1, the integrable fold2 + P6-guard state: smoke + regression GT re-accepted
  for mazesend*, every moved key equal to the P7b accepted aggregate + digest; all other keys unchanged; units 3658;
  Windows CI 44404519142 success.
- `fc204f2f8` (HEAD:src `c291bb996`) (a) `MTG_MAZE_CASTDOM_WINDOWS` + (b) `MTG_MAZE_CASTDOM_PERPLAN`, both default ON.
  Units 3660/3660. Held-out A/B `ab4/`: net 0 / 650 (3 better / 2 worse), d0 identical, both slower games identical
  at d8 b0 on both arms; b0 finished units -11% / -12%.
- `ca0a10718` results s10 (incl. d8f attribution: the 8 games persisting vs the pre-P6 GT are budget luck -- P6 on =
  P6 off at d8 b0).
- `ebbc388b0` smoke GT accepted for (a)+(b): only mazesend_smoke_d5 (5.7348 -> 5.7197) and mazesend2hg_smoke_d5
  (play only) move; 0 searched slower, 2 faster; 229 unchanged.

## Regression tier on fc204f2f8 -- DONE, NOT YET ACCEPTED (`build/scratch/maze/p7/reg8.log`)
365 unchanged; 3 changed, all mazesend*: d3 s2002 5.7333 (play only), d5 s2002 5.6685 -> 5.6522, d5 s3003 5.6933
(play only). Searched: 3 faster, **1 slower: `mazesend_regression_d5_s3003 gi14` 6 -> 7** (repro `--seed 3017
--game-index 14`, d5 b20). Its d8 b0 check (both arms) is owed before the accept.

## Remaining, in order
1. d8 b0 on s3003 gi14 (seed 3017), both arms. Then explain every searched-slower game
   (d8 b0, abandon >= 50M, `"ignore_play_profile": true`, repro seed = base + gi, `game_index` = gi; arms
   base = `MTG_MAZE_CASTDOM_WINDOWS=0` + `MTG_MAZE_CASTDOM_PERPLAN=0` vs default). Then
   `bash test/regression.sh --accept-with-regressions="<one line>" --deck=mazesend`, `python3 test/check_gt_logs.py`,
   commit, push wip/maze-p7.
2. Candidate (c), sound and not built: Heap Gate's / Gateway Plaza's keeps only need to block fold A (fold B's twin
   holds every source P holds plus the land). The keep currently answers for both folds; give it a per-fold answer
   (engine `CastDominanceFold` + `DecisionProvider::CastDominanceKeep`). A/B + d8 b0 as for (a)+(b).
3. Next branching target: 30042 gi1's turn 2 (d5 b0) -- 12.9M plans through interior breakpoint waves (fs_bp_wave 87%
   of 7.9M units, 31 us/unit) on a 2-land turn. Plan-dump that subtree and name the axis.
4. The kept 7s' turn-1 cost is 70-87% beyond-horizon playouts (greedy_walk ~10 subsets per rollout call, rollout
   cache 40% hits): a per-node cost -> profile the playout's greedy subset walk (`./build.sh profile`).

## USER rulings (verbatim)
- "I suppose P7's prune premise is fine." (the dominance-fold premise is SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- Tonight's priority (via coordinator): "optimizations and fixes".
- Box rules (USER 2026-11-18): one heavy process machine-wide under `flock /workspaces/MagicDeckTester/build/heavy.lock`;
  `--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; builds `MTG_BUILD_JOBS=12 ./build.sh` inside the flock.
