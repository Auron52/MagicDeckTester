# I5 (Maze's End fix-batch integrator) handoff -- 2026-11-20

Worktree `.claude/worktrees/maze-shared`, branch `wip/maze-shared` (pushed). Scratch `build/scratch/maze/i5/`.

## Batch (rebased onto origin c85d6e9e3, never merged)
Validated tip **5d0d26067** (code) + GT commit **426099b03** + this note. 69 commits over c85d6e9e3:
* parent's work-queue doc commits; P5c; P7 step 1 + 722fdd3dd (move_tier adapt).
* F5 d03420baa..45d476494 + 9d82aa3f1's ledger only; F5p 58a9fbeb5, 6f657e716, 146001c52 + 9a3f06c64's ledger
  only; composition test 1f3419be4, 77752b31e + a comment commit stating prune-before-fold at the call site.
* F5R ef45d809d, 0e0b59aa6, 5f9265e9c (parent's decision: better and cheaper; the USER has been told).
* F7 c7af5242a, 5470cc43d, b10a95ab3 (GT ce8ea2d4c skipped).
* SUMF 7241e1741, 4ef6638cd, 5ac113fe1, 4232b716c, ad2aaca68 (5ac113fe1 is the compile fix of 7241e1741's fold).
* SHUF 67dd82c14 + b6865852c (2ad8c3d41 already upstream). AB-ORDER a25cd46d6, b43a5bc06.
* viewer-maze2: 0b974260a, cf9c9b1ec, a3df399a6, 41cd55dc8, 4d0b3d090, 251e42c81, 8963f50c3, 37c5caf7c
  (patch-identical to the primary's 0323d157a / 492dc7033 / 4364aecc4 / 7b67edb17; R1 e62b38895 == b6865852c).
* heavy_run 6bb421f43; references 11e1fa904, f5e05fb17, 6dbf4dfc3 (parent's request; byte-identical to primary).
Conflicts: append lists only (HeuristicArm.h enum + names, CMakeLists units, ledger), unioned upstream-first;
TurnSolver plan-list filter: F5's ApplyProviderPlanListFilter THEN P7's CastDominanceFold (as wip/maze-f5p7).
One self-inflicted break (union of test_mana_oracle.cpp dropped a closing brace) fixed by autosquash into the F5R
commit on the same base; its rerere entry deleted. Backups: backup/maze-shared-pre-i5, backup/maze-shared-i5-prerebase.

## Validation on 5d0d26067 (chain3, plain heavy lock, MTG_PGO=0)
* build 0; units 3755/3755; scenarios 587/587.
* references 920: 364 ok / 555 repaired / 1 beaten / 0 play-drift / **0 shuffle-dead** / 0 enum-gap / 0 contract-fail
  (SHUF fixed Mill s2/s6 and Maze's End s2/s7); REQUIRE-BEAT 2/2.
* 51 moved keys (22 smoke + 29 regression), exactly the 18 expected decks; 37 digest-only. Aggregates:
  mazesend smoke d0 6.8920->6.8650, d3 5.30->5.20, d5 5.7500->5.6742; reg d0 6.90->6.864, d5 s2002 5.6902->5.5598,
  d5 s3003 5.6933->5.5867, d3 s30081 5.0->4.0; kithkin all slightly better; selesnyav2 smoke d0 5.074->5.073;
  martyr reg d5 s2002 5.1533->5.1600 (g149).
* d8 b0 (`i5/d8/`, abandon 50M, one pooled batch): all 22 slower games (12 searched + 10 d0) recover. 20 reach the
  GT turn or better on the integrated binary; martyr s2002 g149 T6 == base T6 (identical digest 6bb8504e293838cf),
  mazesend s2002 g120 T6 == base T6. Max units 26.8M (none abandoned).
* Accept: `--both --deck=<18 decks> --accept-with-regressions=<one line>`; wins set == GT set == FAIL set;
  check_gt_logs 1316 consistent, 0 stale, 0 missing.

## Remaining (at our turn, after 1dropburn lands)
1. `git fetch`; `git rebase origin/phase-1-2-deck-analyzer`; check_references_safe.py.
2. Rebuild, units, smoke (+ check_gt_logs) under `push_queue.py --id 550a08fdc1f3:wip/maze-shared hold -- ...`;
   only 1dropburn's own keys may move.
3. Report READY TO LAND. The parent runs `push_queue.py land` and the CI watch.
4. Afterwards: remove `.claude/worktrees/push2-base` with `scripts/safe_worktree_remove.sh` (it is at c85d6e9e3).

## USER rulings (verbatim)
* "I suppose P7's prune premise is fine."
* "It seems that it wasn't clear to other agents that we should still batch up changes. I still would like that."
* (F5R, from its handoff) "For 4 it seems like we need to have something like the refine, but make it cheaper? I'm not
  entirely sure, but we are looking for better results overall."
