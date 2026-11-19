# F6-2 handoff (Maze's End: F6 verdict + Baldur's Gate fed float), 2026-11-19 ~14:45 UTC

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f6`, branch `wip/maze-f6`. Scratch
`/workspaces/MagicDeckTester/build/scratch/maze/f62/` (F6's own scratch: `.../maze/f6/`). Read `logs/maze/F6-handoff.md` too.

## USER rulings, VERBATIM (carried from F6-handoff)
- "Mana difficulties: logs/play/rejections/Maze_s_End_cod_s6_gi5_t5.json In theory this is workable due to lands coming into
  play untapped."
- On F5's planned prune: "For simple decks with no rituals or equivalent (cards that add more mana then they cost) like this
  one it makes sense to drop plans that cannot pay a subset"

## Done (commits)
- dd0421f20 (F6): MazesEndProvider::ManaSourceRank -- Baldur's Gate 61 (MAZE_TAP_BALDUR_LAST), Maze's End 69 (MAZE_TAP_MAZE_LAST).
- WIP commit on top (this handoff's commit): F6-2 Baldur's Gate fed float, UNMEASURED (see below).

## Task 1 verdict on dd0421f20: FAILS the bar
F6 A/B (`build/scratch/maze/f6/ab/summary.txt`, held-out, MTG_PGO=0 all arms, net = sum of win-turn deltas vs base, loss=9):
| shape | both (ranks on) | baldur only |
|---|---|---|
| d5 b20 (1000) | -16 (59 better/44 worse) | -1 (16/15) |
| d3 b10 (300) | -8 (23/15) | -10 (12/2) |
| d0 (600) | +1 (26/24) | +4 (10/12) |
| 2HG d5 b20 (200) | **+2** (8/10) | **+1** (4/5) |
mazesend net: both -23, baldur -7; mazesend2hg net: both +2, baldur +1 (fails "net <= 0").
d8 b0 (`f6/ab/d8.out`): `both` 93 slower games -> 77 equal at d8, **16 NOT recovered** (d5 g31 g823 g194 g68 g954 g73 g975,
d3 g8 g95 g199, hg g28 g55, d0 g71 g421 g569 g471); `baldur` 34 slower -> **3 NOT recovered** (d5 g121 g19, hg g125).
(Coordinator's figures from old F6: both net -21 / 16 unrecovered, Baldur-only -6 / 3 unrecovered -- same picture.)
Root cause so far (d8 b0 replays with logs, `build/scratch/maze/f62/cmp.sh <tag> <seed> <gi> [both|baldur]`; seed = base+gi):
- d5 g823 (seed 211824): base T3, both T4. T2 base casts Guild Summit paying with Maze's End's {C}; `both` holds Maze's End
  (rank 69) although its {3} activation is unaffordable that turn, and the search instead activates Maze's End; Summit slips.
- d3 g95 (seed 212096): base T4, both T5. T3 base pays Guild Summit with Maze's End {C} and also plays Basilisk Gate; `both`
  keeps Maze's End untapped and the land drop slips a turn.
Mechanism: an UNCONDITIONAL hold of a {C}-only source spends a coloured/other source on generic and changes what the next
node can do, when the held source's activation is not in the line. PROPOSED AMENDMENT (coordinator agrees: conditional hold):
hold Maze's End only while the plan/line names its activation (and Baldur's Gate only when its fed mode will be reached),
else rank 5 as before. Not yet implemented.

## Task 2: Baldur's Gate fed float -- implemented, building, not measured
Finding (unit test diag, exact s6 T5 board): the second {3} ALREADY reaches the fed mode on that board (Thran Portal +
fetched Azorius Gate feed Baldur's Gate, X=5), but the backtracker's fed branch tries colours in WUBRG order -> floats
**two WHITE**, so Sylvan Scrying's {G} fails. Where the payer takes the plain {C} instead, nothing floats at all.
Changes (uncommitted until this WIP commit):
- `src/core/SpellEffects.cpp`: `ScaledFedColourOrder` -- the fed branch tries colours in demand order (bill pips, then
  g_line_unpaid_cost pips, then hand pips, ties WUBRG) under MTG_SCALED_FED_FLOAT. Order only.
- `src/ai/ManaPayment.cpp`: `ScaledFedFloatAfter` + `BoardHasGainfulScaledLand`, hooked in TapForCostSharedCore after
  PayRefineAfter/ExactLineFloatAfter (outermost payment only): a plain-{C} tap of a scaled land in a bill with
  generic >= feeder+1 is re-read as the fed tap (same taps, float + X-feeder-1 of the LumpColourOrder colour). Dominance;
  conservative refusals listed in its comment. Does NOT touch ManaOracle RefinePayment (F5R's area).
- `src/ai/EngineFlags.h` ScaledFedFloatOn (MTG_SCALED_FED_FLOAT, DEFAULT ON); `src/ai/HeuristicArm.h` slot SCALED_FED_FLOAT.
- `test/unit/test_mazes_end_f6.cpp`: F6-2 tests (acts then Scrying pay with flag on; fail with it off; small bill untouched)
  + a DIAG test ("ranks off, fed on") and DumpS6 MESSAGEs -- delete the DIAG test/dumps once the tests are settled.
  Before the colour fix both on/off tests failed (W float). Expected after build3: test 1 passes; test 2 (flag off) may
  still pay if the colour fix alone... NO: the colour fix is also gated on the flag, so off -> W float -> fails. Check.
Scope: Three Tree City decks (Goblins, Merfolk Turns) move too -- included in the A/B.

## RUNNING
- build3 of the F6-2 tree: `heavy_run.sh ... ./build.sh` PID **1589489**, log `build/scratch/maze/f62/build3.log`
  (DONE = a `build rc=N` line). A waiter (harness task bjz9kl1j5) runs `mtg-test -tc="*F6*" -s` after it.
- NOT yet launched: `build/scratch/maze/f62/chain.sh` (copies build/Release/mtg -> f62/mtg.f62, runs the pooled A/B
  `f62/ab/manifest.json` (10,800 jobs, from `mkab3.py`), writes `f62/ab/run.out`, `f62/ab/summary.txt`, then d8 b0 of every
  slower game of arms fboth/fbal/fnone -> `f62/ab/d8.out`; status lines `f62/chain.status`; DONE marker `f62/chain.DONE`).
  Arms (Maze, 4): fboth = F6 ranks + fed; fbal = Baldur rank + fed; fnone = fed only; base = nothing. Goblins/Merfolk
  Turns: fnone vs base. Shapes: Maze d5 b20 x600 (231001), d3 b10 x300 (232001), 2HG d5 b20 x600 (234001), d0 x600
  (233001); Goblins d5 b60 x300 (235001) + d0 x300 (236001); Merfolk Turns d5 b20 x300 (237001) + d0 x300 (238001).

## Remaining, in order
1. Read build3.log + the bjz9kl1j5 test output (`mtg-test -tc="*F6*" -s`). Fix tests to match (expect: fed on pays,
   float 2; fed off fails). Read the DIAG line: does the s6 line pay with the F6 ranks OFF and fed on? (If yes, F6's ranks
   may be unnecessary for the s6 line.) Remove DIAG test/dumps, rebuild under heavy_run.
2. Launch `bash build/scratch/maze/f62/chain.sh` run_in_background; wait on `f62/chain.DONE` (one waiter, no polling).
3. Verdict per arm: per-deck net <= 0 (mazesend = d5+d3+d0, mazesend2hg = hg, goblins, merfolkturns) and every slower
   game recovers at d8 b0 (`anab.py`-style compare of `f62/ab/d8.out`, arm vs base). Prefer the arm that passes with the
   fewest holds; if a hold is still needed, implement the CONDITIONAL hold (activation named in the line) and re-A/B.
   If an arm loses games, root-cause with `cmp.sh` (lever flips; "draws diverge" is not an explanation).
4. On a pass: delete the losing levers' branches (no default-off levers), accept smoke + regression GT for moved keys
   (mazesend*, goblins*, merfolkturns*) per `.claude/skills/regression-testing.md`, `python3 test/check_gt_logs.py`,
   commit (drop WIP), push wip/maze-f6 ONLY. Add an "F6/F6-2" section to `docs/design/analysis-Maze's End.md`.
5. Open (report): CheckLine prices TapTutorPut at 0 on this branch (V3's viewer-maze2 fixes it); the {Scrying, ME} second
   link needs F5's owed-acts joint prepay.
