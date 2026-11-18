# P7b handoff (Maze's End performance) -- 2026-11-18

Worktree `.claude/worktrees/maze-p7`, branch `wip/maze-p7` @ `82eeffb80` (pushed). Results: `logs/maze/P7-results.md`
sections 7-9. Scratch: `build/scratch/maze/p7/`.

## Done (commits on wip/maze-p7, on top of 7093daceb)
- `1ce7f76d5` fold1, `8f59d7441` fold2 (previous agent).
- `cc32d32a3` = cherry-pick of P6 `5ac60ceba` (additive conflicts; P6's heurarm slots placed before P7's).
- `ccf65938c` P6 guard: `DecisionProvider::CastDominanceKeep` + `MazesEndProvider::CastDominanceKeep` (keeps a plan
  whose unspent mana could pay an off-main Maze's End activation; premise at the definition). Units 3546/3546.
- `82eeffb80` smoke + regression GT accepted for mazesend* (on base 7093daceb + P6), results s7-9.
- Measured: ab3 held-out net -11 (17/6), d0 identical; d8 b0 identical digests fold-off vs fold3 on all 7 A/B-slower
  and all 21 smoke/regression-slower games; b0 d3 finish 17/20 -> 20/20, d5 b0 finished-units 2.0x cheaper.

## In flight (not waited on)
- `d8f/` -- UN-RUN: killed while still queued by P7c ("8-game batch would hold the machine-wide lock at 8/32 cores"); fold it into a larger pooled batch. Was: pre-P6 binary `mtg.p7b`, fold off, d8 b0 on the 8 games that persist vs the
  pre-P6 GT equally with the fold off. Read: `grep preP6 build/scratch/maze/p7/d8f/run.out | awk '{print $1,$3,$4}'`
  once `d8f/DONE` exists. Compare with the current-tree d8 b0 (`d8d/`, `d8e/` base rows): equal turn = GT budget luck;
  pre-P6 better = a P6 regression for the P6 owner (not the fold). Record the line in P7-results.md s8.

## Remaining, in order
1. Coordinator: rebase wip/maze-p7 onto the shared branch (now `e682d7942`, which already contains P6 -> drop
   `cc32d32a3` in the rebase), rebuild (`MTG_BUILD_JOBS=12` inside the flock), smoke + `--deck=mazesend` regression,
   re-accept mazesend*, `python3 test/check_gt_logs.py`.
2. Next prune candidates (s9 of the results; each = A/B on the ab3 seeds via `tools/mkab3.py`, then d8 b0 on slower
   games, abandon >= 50M): (a) P6-guard window tightening -- in `MazesEndProvider::CastDominanceKeep` return false
   when `dumps == 0 && (burg == 0 || !s.opponent_plays_lands)`; (b) replace the global Heap Gate / Gateway Plaza
   vetoes in `CastDominanceLive` with per-plan keeps of the same shape; (c) re-census 30042 gi1 / 30065 gi4 single
   threaded (`cen3/manifest.json`, env in `tools/cen3.sh`).

## USER rulings (verbatim, carried from the brief)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- Box rules (USER 2026-11-18): one heavy process machine-wide under `flock /workspaces/MagicDeckTester/build/heavy.lock`;
  `--threads $(nproc)`, default cache budget, `MTG_RSS_CAP_GB=20`; builds `MTG_BUILD_JOBS=12 ./build.sh` inside the flock.
