# DM-1 handoff: deeper/unlimited search finds a worse line (IDG-2's three d8 b0 non-recoveries)

Worktree `.claude/worktrees/maze-dm1`, branch `wip/maze-dm1` off wip/maze-shared aa16513e7 (src tree 11ecf180).
Scratch: `build/scratch/maze/dm1/` (`rep.sh <bin> <tag> maze|martyr|gods <depth> <budget> [ENV=V...]` is the
single-game repro; `mtg.base` = the maze-shared binary, built 02:08 from src a70d02184; `acts.py <logdir>` prints a
game log's actions).

## USER rulings carried in the brief (verbatim)
- "everything should be findable at unlimited or large budgets"
- NO GREEDY in the search window; no unbudgeted-only gates.
- Bar: per-deck net <= 0 on a held-out A/B pooled into ONE `mtg --batch`, and every slower game recovers at
  `--depth 8 --budget-ms 0`. Then scoped smoke + regression GT accept on the branch, `test/check_gt_logs.py`,
  `scripts/check_references_safe.py`.
- Heavy commands through `scripts/heavy_run.sh`, `MTG_RSS_CAP_GB=20`. Push wip/maze-dm1 only, never the shared
  branch. CI watch with `--interval 60`. No timeouts. Do not kill runs you did not start. Hand off at ~250K.

## Reproduces on aa16513e7 (base binary, single game, --ignore-play-profile, b0)
| case | d3 | d4 | d5 | d6 | d8 | notes |
|---|---|---|---|---|---|---|
| mazesend s3076 gi73 | | | 5 | 6 | 6 (IDG) | |
| martyr s2151 gi149 | | | 5 | 6 | 6 (IDG) | |
| gods s2018 gi16 | 9 (b10: 6) | 7 | 7 | | 7 | d3 b20 7, d4 b10 7, d5 b10 7 |

## Case 1 + 2 (maze gi73, martyr gi149): ONE mechanism -- the in-window proof cache stores horizon-exit answers
Lever flips at d6 b0 (base binary): MTG_LAZY_LEAF=0 -> maze 5; MTG_INWIN_CACHE=0 -> maze 5, martyr 5 (and both at d8);
MTG_FS_HORIZON_EXIT=0 -> maze 5 (d6 and d8), martyr 5. No effect: GAME_LINE_CACHE, FS_NOWIN_CACHE, ID_HINT_GAME,
ENUM_MEMO, LEAF_CACHE, EQUIV_CACHE (all still 6).
MTG_INWIN_CACHE_VERIFY=1, martyr d6 b0: 4 MATERIAL disagreements, e.g. `turn=5 depth=2 cutoff=5 cached=9 fresh=5`
(chained up T4/T3/T2). gods d3: 2 MATERIAL. With MTG_FS_HORIZON_EXIT=0 the same verify reads DISAGREE=0 on all three.
Trace: maze d6 b0 T1 ladder `pass=4 win=5`, then `pass=5 win=9 cost=5`, `pass=6 win=9 cost=5` (answered from the map);
d5's pass 5 searched (cost 4754) and kept win=5.
Mechanism: the FIRST-VERIFIED-WIN horizon exit (FsHorizonExitOn) returns a node's first in-window win, which is the
earliest only under the ID premise of the pass it ran in. The inwin map (MTG_INWIN_CACHE, depth- and leaf-free, per
game) stored that answer -- a non-minimal win, and the no-win bound "nothing before it" -- and served it to other
passes and decisions where the premise does not hold. Its `complete` test checked truncation and budget binds but not
exits (DvsTailGate already excludes `g_fs_hexits` changes for exactly this reason).
Fix (MTG_INWIN_EXIT_GUARD, heurarm slot INWIN_EXIT_GUARD, default ON): a node whose subtree took an exit stores no
no-win bound, and stores its win only when an existing proof already refutes every earlier turn.

## Case 3 (gods gi16): a DIFFERENT mechanism, not fixed here
- d3 b0 LOSS vs b10 6: the value-leaf / heuristic escalation arbitration. MTG_VALUE_MIN_DEPTH=0 (pure value leaf) gives
  6 at d3 b0 and b10. At b0 the heuristic escalation completes pass 3 and `value_fallback_crossover`
  take_heuristic_at_hdepth[3]=3 takes it; at b10 it stops at pass 2 and the leaf line stands. The heuristic ladder's
  deeper passes are WORSE than its shallower ones every turn (T2: 5,6,6; T3: 6,6,8; T4: 7,8,8).
- d4..d8 b0 give 7, and so do d3 b20, d4 b10, d5 b10, and d8 b0 pure value leaf. None of HORIZON_EXIT, INWIN, LAZY_LEAF,
  GAME_LINE_CACHE, BP_DEV_SEQ, ID hints, ROLLOUT_GAME_CACHE moves it (all 7).
- The games split at T2: b10 d3 plays Gemstone Mine + Magus of the Bazaar (wins T6); every other setting plays Vivid
  Creek (d8 b0: T7). d8 from T1 covers every turn to max_turns, so a T6 line that real play reached is outside the
  search's tree or mis-scored: a MENU / coverage gap (Magus draw-2-discard-3, dredge, Replenish choices), not a cache.
- Next probe: replay the b10 game's T2 choice (Gemstone Mine + Magus) and let d8 b0 search from T3; bisect the turn
  whose search fails to find the T6 continuation, then compare that node's interior menu with the root menu.

## Status / next steps
(filled in below as work proceeds)
