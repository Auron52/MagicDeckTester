# RC-3 results / handoff (2026-11-20 ~19:40 UTC)

Continues `logs/maze/RC2-handoff.md`. Scratch: `build/scratch/maze/rc3/`. Nothing is running.

## Done
1. **P5b fold re-run** (RC-2's run, `rc2/p5b/run.out`, `p5b3/ana.py`): std net **-2** (4/2), 2HG net **-1** (2/1); units
   -0.29% / -0.56%. Before the fix (P5b-4) it was std -3 / 2HG +1.
2. **d8 b0 on the 3 P5b slower games** (`rc3/p5bd8/manifest.json`, `run.out`; rc2/mtg.p5b, depth 8, budget 0,
   ignore_play_profile, abandon 20M; fold off/on via MTG_FOLD_LAND_ORDER_SEEN):
   | game | budgeted off -> on | d8 off | d8 on | digest | units off -> on |
   |---|---|---|---|---|---|
   | std d1b3 g781 (seed 304782) | 5 -> 6 | T5 | T5 | 62a041442b606123 both | 788,413 -> 602,413 |
   | 2HG d1b1 g783 (seed 313784) | 6 -> 7 | T6 | T6 | f06069cfd67fe5dc both | 10,268,840 -> 8,562,236 |
   | std d1b3 g686 (seed 304687) | 5 -> 6 | T5 | T5 | c9c6f3693e20d728 both | 10,647,493 -> 9,847,240 |
   All three recover. With both bar halves met, the fold now passes the A/B bar. **NOT adopted; that is the USER's call.**
3. **Regression tier** on wip/maze-rc1 (b01b594fd build; VPC_SKIP=1; `rc3/reg.log`): 375 passed, 0 failed;
   per-game audit **0 configs changed, 0 slower, 0 faster, 0 play-changed**. No GT accepted (per coordinator).
   `test/check_gt_logs.py`: 1345 consistent, 0 stale. `scripts/check_references_safe.py`: OK.
4. **434023 / gi22 drift ROOT-CAUSED**: `docs/design/bp-node-wave-drop-in-lookahead-host.md`. The breakpoint node's
   wave stand-down (BpNodeWaveDropAt, keyed on g_condemn_root_turn) fires in SolveWithLookahead, which never
   hosts the node. So the rollout's answer depends on the root turn (not in the leaf key) AND node-site continuations
   are unreachable there on root/root+1 turns (a search hole). MTG_BP_NODE_ROOTTURN=0, MTG_BP_NODE_HOST2=0 and
   MTG_BP_NODE=0 each take the stale count 3 -> 0. A fix is proposed, not built (play change; USER decision).
   Diagnostic branch `wip/maze-rc3-diag` (print-only, never land); binaries `rc3/mtg.diag`, `rc3/mtg.diag2`.

## Remaining
- The USER decides on adopting the P5b fold and on the bp-node fix. If the fix goes ahead: implement the per-host gate,
  then A/B it held-out on the decks that move, and run d8 b0 on the slower games.
- Worktree `.claude/worktrees/maze-rc3` (branch wip/maze-rc3-diag, pushed) can be removed with
  scripts/safe_worktree_remove.sh once nobody needs the diagnostic.

## Rulings carried: see RC2-handoff.md (unchanged).
