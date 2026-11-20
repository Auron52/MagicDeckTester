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

## STATUS AT HANDOFF (~225K context, 2026-11-20 ~07:00 UTC) -- READ THIS FIRST
Branch wip/maze-dm1 was REBASED onto 8fbbc5023 (origin/phase-1-2-deck-analyzer, batch 1) per the coordinator:
`git rebase --onto 8fbbc5023 aa16513e7`. Commits: 7ec94d53b (the guard + test + this doc, rebased) and the handoff
commit on top (diag-key fix + Windows skip of the unit test). build/Release in the worktree = 7ec94d53b's tree
(built 06:47; copy at build/scratch/maze/dm1/mtg.fix). The diag-key fix in the handoff commit is NOT built yet.

### What changed on the new base (single games, d6 b0, --ignore-play-profile)
- martyr gi149: 5 at d6 and d8 with the guard ON, and 5 at d6 with MTG_INWIN_EXIT_GUARD=0 too -> batch 1 already
  changed this game; it no longer reproduces on 8fbbc5023, so it cannot validate the guard.
- maze gi73: STILL 6 at d6 and d8 WITH the guard (5 at d5). So the exit guard is NOT the fix for case 1 on this base.
  Flips at d6 b0 on mtg.fix: MTG_INWIN_CACHE=0 -> 5, MTG_FS_HORIZON_EXIT=0 -> 5, MTG_LAZY_LEAF=0 -> 5,
  **MTG_LEAFLESS_READS_LEAFED=0 -> 5**, MTG_INWIN_CACHE_VERIFY=1 -> 5 (plays fresh), MTG_GAME_LINE_CACHE=0 -> 6,
  MTG_BP_DEV_SEQ=0 -> 6.
- VERIFY (guard on or off), maze d6: 4 MATERIAL, all one chain: `turn=5 depth=2 cutoff=5 cached=9 fresh=5 leaf=2`
  then T4 d3, T3 d4, T2 d5. A LEAFLESS T5 node with window T5-T6 is told "no win by 5" by the inwin map, and a fresh
  search finds the T5 win. The printed "stored:" provenance (turn=5 depth=1 leaf=2 twop=1 hexits=0) is UNRELIABLE:
  the built binary kept the hit key in a thread-local that children overwrite (fixed in the handoff commit, unbuilt).
- Working hypothesis for case 1 on this base: the no-win comes from a LEAFED (rollout) node via the inwin map's
  "any leaf" context (inwin::Ctx under MTG_LEAFLESS_READS_LEAFED), i.e. a rollout node's in-window search at T5 is NOT
  the leafless in-window search plus leaf estimates (some leaf-dependent in-window narrowing/ordering cut), OR the T5
  menu depends on the remaining depth (depth 1 vs 2). Either breaks the inwin soundness premise ("the menu inside the
  window does not depend on the remaining depth" / "a leafed node ... only ADDS leaf estimates").

### Next steps, in order
1. Build (heavy_run ./build.sh) and run `rep.sh <new bin> V maze 6 0 MTG_INWIN_CACHE_VERIFY=1` (guard on): read the
   CORRECT store provenance on the T5 DISAGREE line (leaf kind 0/1/2, depth, cutoff, devseq pass, hexits).
   If leaf != 2: the leafed->leafless direction of the inwin "any leaf" Ctx is unsound here; find which in-window cut
   in the rollout node dropped the T5 line (compare a depth-2 rollout FSLineWin vs leafless at that T5 state).
   If depth == 1 and leaf == 2: the T5 menu depends on remaining depth; find the depth-1 special case.
2. Fix soundly (store only what the reader's context can rely on); keep or drop MTG_INWIN_EXIT_GUARD on evidence
   (it is a real tightening -- exits are not minima -- but currently has NO repro that it changes; consider an A/B).
3. Then the brief's measurement: smoke+regression `--both` run (moved keys), held-out A/B on moved decks pooled into
   ONE batch (arms via the heurarm slot, logs/idgate/mkab.py pattern), d8 b0 on every slower game, accept smoke +
   regression GT, test/check_gt_logs.py, scripts/check_references_safe.py. Push wip/maze-dm1 only; report the tip to the
   coordinator for batch 2.

### Running / pending at handoff
- Unit tests of build 7ec94d53b running in place: output build/scratch/maze/dm1/unit1.log (ends with `rc=N`).
- CI run 44683038229 (6dea361e9): Windows FAILED only on test_inwin_exit_guard.cpp -- SIGSEGV stack overflow (a d6 b0
  game on a std::thread, 1 MB Windows default stack). The handoff commit skips that test on _WIN32; re-check CI.
- Case 3 (gods) unchanged on the new base: F3_gods d3 b0 = 9 with mtg.fix.

### Unit-test result (arrived after the report)
mtg-test on 7ec94d53b: 3774/3775 pass; the ONLY failure is the new test_inwin_exit_guard.cpp, and not for the
defect: in-process `GoldFishRunner::Run(deck,1,2151,8,prof,{},149,depth,0,1)` plays Martyr gi149 to **T7 at both d5
and d6** (the CLI repro gives 5 and 5 on this base). So the harness does not reproduce main.cpp's single-game setup
(suspects: ResolvePlaySettings / --ignore-play-profile handling, deck-trait stamping, sideboard/legacy flags). The
test is now `doctest::skip(true)` (commit after c475494cd). It also no longer has a defect to pin, since Martyr gi149
does not reproduce on 8fbbc5023. Replace it with a test that reproduces the real case-1 mechanism once step 1 names it
(Maze gi73; build the setup the way main.cpp does, or test the cache store rule on a crafted board via a seam like
LeaflessReadProbeForTest).
