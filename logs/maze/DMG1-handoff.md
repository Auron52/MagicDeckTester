# DMG-1 handoff: gods s2018 gi16 (regression d3 s2002 gi16) -- deeper search loses the T6 line

Worktree `.claude/worktrees/maze-dmg`, branch `wip/maze-dmg` off origin/phase-1-2-deck-analyzer 8fbbc5023.
Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/dmg/` (build volume).
Handed off at ~275K context (coordinator request), 2026-11-20. Written before ANY build of this worktree
existed: **the fix is UNBUILT and UNTESTED.**

## USER rulings carried in the brief (verbatim)
- "everything should be findable at unlimited or large budgets"
- NO GREEDY in the search window (CLAUDE.md); a missing searched capability is a defect to fix, not a lever.
- Bar: per-deck net <= 0 on held-out (ONE pooled batch, decks that move), every slower game recovers at
  `--depth 8 --budget-ms 0`. Validate: units, smoke, held-out A/B, d8 b0 on slower games, check_gt_logs.
  **Do NOT accept GT on the shared branch -- report moved keys.**
- If it is the value file's crossover table, report it plainly; do NOT regenerate artifacts
  ("an artifact fitted to an older engine is fine", refitting is the USER's call).
- Heavy jobs via `bash scripts/heavy_run.sh`, one at a time, `MTG_RSS_CAP_GB=20`; no timeouts; waiters on
  `kill -0 PID` / DONE markers; push wip/* only; never touch references/; scratch in build/scratch/maze/dmg/.

## MECHANISM (diagnosed on DM-1's `build/scratch/maze/dm1/mtg.fix` = 8fbbc5023 + inwin exit guard)
It is a **PROVIDER PRUNE**, not the crossover table and not a cache:
`GodsDredgeCandidatesForRule` (src/ai/DecisionProviders.cpp, default `MTG_GODS_DREDGE_RULE=4`) answers a
dredge-eligible draw with ONE choice, `{best}` = the largest dredger, with NO deck-out guard (rule 5 has one:
`library <= best_mill + 2 -> {0}`). The search's turn-boundary dredge fan (BoundaryFan, TurnSolver.cpp
~117520 / Cross ~115315) only fans that menu. On the forced-Magus line, T5 Forest + Golgari Grave-Troll
(22 counters) wins with the T6 attack (d3 b0 plays exactly that and wins T6). But the T6 crossing's upkeep
Magus draw is offered only "dredge the Troll (6)" from a 6-card library -> next draw from an empty library ->
lost; EVERY crossing into T6 fails (`SimulateEndAndStartNextTurn` false), so FSLineWin never builds a T6
node (MTG_FSW_TRACE at turn 6 prints nothing), the T5 GGT plan scores tail=9, and at d>=4 from T5 the
horizon reaches max_turns so `proven_no_win` fires and the game just ends (d4 plays nothing at T5).
Autonomous play / rollouts have no dredge chooser -> natural draw -> that is why d3 (whose T5 root falls
back to SolveWithLookahead rollouts) finds it and d4+ cannot. Upstream, the whole Magus line is under-
valued at T2 for the same reason, so deep settings play Vivid Creek (T7).

### Probes and results (all `--ignore-play-profile`, b0 unless noted)
- Repro on mtg.fix: d3 b0 = 9 (loss), d3 b10 = 6. (b10 wins by luck: T3-T5 searches say win=9.)
- Hand-off with T2 Magus FORCED (`P=1,0,3,1,0,-1,1,0`, `--choices-then-auto`): d3 = 6, **d4 = loss, d5 = loss**.
  Unforced T2 hand-off (`P=1,0,3,1,0,-1,1`): d3 b10 = 6, d3 b0 = loss (fidelity check matches the bench).
- d4 forced-Magus lever flips, ALL still loss: MTG_INWIN_CACHE=0, GAME_LINE_CACHE=0, FS_HORIZON_EXIT=0,
  LAZY_LEAF=0, ROLLOUT_GAME_CACHE=0, FS_NOWIN_CACHE=0, LEAF_CACHE=0, ENUM_MEMO=0, WINLESS_CERT=0,
  WINLESS_DEVELOP=0, WINLESS_SEED=0, DEV_SEQ_TAIL_SKIP=0, BP_DEV_SEQ=0.
- d4 forced-Magus by dredge rule: **rule 5 -> T5** (0.96 s), **rule 3 -> T4** (2.75 s), rule 4 (default) ->
  loss (55 s), rule 0 (full menu) -> loss, 617 s (probably the budget/size, not menu; not investigated).
  => the rule-4 singleton menu is what costs this game; rules 3/5 are USER-owned policy (surface, do not switch).
- FSW trace: `[fsw] T5 d4 ... p=land=Forest;Golgari Grave-Troll, ... after=20 tail=9`, board `GGT#21(22)`.

## THE FIX (uncommitted at handoff -> committed as WIP, see hashes below; UNBUILT)
1. `src/ai/DecisionProviders.cpp`: `GodsDredgeCandidatesForRule` now wraps the old body (renamed `...Impl`):
   if the menu has no natural draw and an offered dredge of N would leave `library < N + 3`
   (`kDredgeDeckoutMargin` = Magus 2 + draw step), append `0` AFTER the rule's pick. Lever
   `MTG_GODS_DREDGE_DECKOUT` (default ON, heurarm slot `GODS_DREDGE_DECKOUT`, `=0` = old menu, the A/B hatch).
   DredgeHumanDefault unchanged (takes front()).
2. `src/core/HandEntry.h`: `g_dredge_singleton_multi` thread-local; `ScriptedDredge::Choose`'s singleton
   recording branch, on a menu with >1 answer, flags it, stops recording, returns 0 (replay discarded).
3. `src/ai/TurnSolver.cpp` (AppendSubdecisionAxes, ~102640): the general adaptive dredge tree is now the lambda
   `expand_adaptive`; the singleton fast path (DredgeSearchSingletonPolicy, Gods only) records each plan and
   sends a flagged plan (recorded prefix pinned) through `expand_adaptive` so the multi-answer draw is branched.
   The record scope is closed BEFORE expand_adaptive replays. Non-Gods dredge decks: same code path as before.
4. `src/ai/HeuristicArm.h`: slot GODS_DREDGE_DECKOUT appended after SUMMIT_TWIN_FOLD (enum + kNames).
5. `test/unit/test_gods_dredge.cpp`: new case "Gods dredge menu offers the natural draw when the dredge would
   deck the library" (9-card library -> {11}; 8 -> {11,0}; singleton recorder flags multi).
   Not reviewed against the mtg-rules skill yet (CR 702.52 dredge needs >= N cards; CR 704.5b empty-draw loss).

## BUILD UPDATE: the queued build FINISHED rc=0 (09:19 UTC) -- binary build/Release/mtg is fc04fac0e's tree; start at step 2.

## RUNNING AT HANDOFF -- do NOT kill (now finished, see above)
- Build of this worktree, queued on the heavy lock: heavy_run PID **2202227** (parent shell 2202225), command
  `MTG_BUILD_JOBS=12 bash scripts/heavy_run.sh ./build.sh`, log
  `/workspaces/MagicDeckTester/build/scratch/maze/dmg/build.log`; DONE marker = a line `rc=N` appended to
  that log. It compiles the tree as it is when the lock is granted (the WIP commit content). Binary:
  `.claude/worktrees/maze-dmg/build/Release/mtg` (symlinked onto the build volume by build.sh).
  If rc != 0 it is a compile error in the patch above: fix and rebuild.

## WHAT REMAINS, in order
1. Wait: `until grep -q rc= <build.log>; do sleep 30; done` (run_in_background).
2. Unit tests (the repo's unit target / test runner), incl. test_gods_dredge.
3. Repro with the new binary B=.claude/worktrees/maze-dmg/build/Release/mtg:
   `build/scratch/maze/dmg/rep.sh $B n_d3b0 3 0`, `... n_d8b0 8 0` (d8 ~2 min), `... n_d3b10 3 10`, d4/d5 b0;
   forced-Magus hand-offs at d4/d5/d8 (`cp.sh $B "1,0,3,1,0,-1,1,0" --choices-then-auto --depth 4 --budget-ms 0
   --ignore-play-profile | grep win_turn`), and the same with `MTG_GODS_DREDGE_DECKOUT=0` (must = old: loss).
   Expect the full game at d8 b0 <= 6. If the T2 root still plays Vivid Creek, trace T2 (MTG_TRACE=search) --
   a second mechanism would then remain.
4. Smoke (`test/regression.sh smoke`, via heavy_run) -> list moved keys (expect gods*/gods2hg only).
5. Held-out A/B, ONE pooled batch: manifest READY at `build/scratch/maze/dmg/ab/ab.json` (112 jobs, seed base
   7007, gods d0 x1000, d3 b10 x600, d5 b20 play profile x600, gods2hg d5 b20 x600; arms off/on of
   MTG_GODS_DREDGE_DECKOUT innermost, alternated, chunks of 50; regenerate with `mkab.py base.json ab.json
   --chunk 50`). Run: `MTG_RSS_CAP_GB=20 bash scripts/heavy_run.sh $B --batch ab.json --threads $(nproc)`
   (check the exact --batch output flags in .claude/skills/regression-testing.md). Bar: per-deck net <= 0.
6. d8 b0 (`--seed <base+gi> --game-index <gi> --games 1 --depth 8 --budget-ms 0`) on every slower game.
7. `python3 test/check_gt_logs.py`; commit; push wip/maze-dmg only. Do NOT accept GT on the shared branch.
8. Report: mechanism, fix commit, moved keys, A/B, d8 recoveries. SURFACE to the USER (their call): rule 3 gave
   T4 and rule 5 gave T5 on the forced-Magus hand-off vs rule 4's loss -- the rule-4 singleton policy itself
   may be costing Gods games beyond the deck-out case.

## Tools in build/scratch/maze/dmg/
- `rep.sh <bin> <tag> <depth> <budget> [ENV=V...]` single bench game s2018 gi16 (LOGDIR=... for a log).
- `cp.sh <bin> "<choices>" [args]` claude-play replay; pipe into `python3 dec.py` to print the decision.
  Choices: keep 1; T1 plan 0 (Gemstone + Careful Study); discards 3 (Stinkweed), 1 (Nylea); T1 continue 0;
  T1 post -1; T2 dredge 1 (Stinkweed); T2 plan 0 = Gemstone Mine + Magus.
- `acts.py <game.json>` action summary of a game log. Trace envs: MTG_TRACE=search,
  MTG_TRACE_SOLVE=1 MTG_TRACE_SOLVE_TURN=N MTG_TRACE_SOLVE_BOARD=1, MTG_FSW_TRACE=1 MTG_FSW_TURN=N MTG_FSW_BOARD=1.
