# F6-3 handoff (Maze's End F6: conditional tap-order hold + Baldur's Gate fed float), 2026-11-19 ~17:15 UTC

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f6`, branch `wip/maze-f6`. Scratch (build volume)
`/workspaces/MagicDeckTester/build/scratch/maze/f63/`. Read `logs/maze/F6-handoff.md` and `logs/maze/F6-2-handoff.md` too.

## USER rulings, VERBATIM
- "Mana difficulties: logs/play/rejections/Maze_s_End_cod_s6_gi5_t5.json In theory this is workable due to lands coming into
  play untapped."
- On F5's planned prune: "For simple decks with no rituals or equivalent (cards that add more mana then they cost) like this
  one it makes sense to drop plans that cannot pay a subset"
- "the top priority are optimizations and fixes"
- "Naturally, I'm expecting that this means we are matching the references and all other bars. If not, we should focus on
  doing so first."
- (Parent's standing rules: the mana-payment tap order is a standing exemption from no-greedy, but a hold must still not make
  a line unfindable at d8 b0. Do not push the shared branch; push wip/maze-f6 only. Heavy jobs via
  `/workspaces/MagicDeckTester/scripts/heavy_run.sh` at full nproc, `MTG_RSS_CAP_GB=20`, `MTG_BUILD_JOBS=12`; `./build.sh`
  only; no timeouts; waiters on PID / DONE marker, never `pgrep -f`; hand off at ~250K context.)

## Commits (wip/maze-f6)
- dd0421f20 F6: unconditional ranks (Baldur 61, Maze's End 69) -- FAILED the bar (F6-2 handoff).
- 87f6c07ee F6-2: fed float (`ScaledFedFloatAfter`, demand-ordered fed colour in the backtracker), `MTG_SCALED_FED_FLOAT`.
- **c522bc6ca F6-3 (this work, WIP, unmeasured):**
  - `src/ai/DecisionProviders.cpp:69300` `LineDemandAfterPayment()` + `:69314` `MazesEndProvider::ManaSourceRank`:
    Baldur's Gate rank 61 ONLY while `ScaledManaNetYield > 0` AND the line still owes >= {feeder}+1 (= 3) mana after this
    payment = later casts (`g_line_unpaid_cost` minus `g_pay_full_mv` when a cast is being paid) + pending activations
    (`PlanTraits::act_mana_total - g_act_line_paid - g_act_line_current`). Otherwise the generic rank 5.
  - **Maze's End rank 69 DELETED** (`MAZE_TAP_MAZE_LAST` slot removed from `src/ai/HeuristicArm.h`): the "hold it only
    while the line names its activation" half is ALREADY the existing `MTG_ACT_LINE_HOLD` source hold
    (`PlanTraits::act_src_nums`, ActLineHoldMask), which base also has -- so conditional Maze = base behaviour.
  - `src/ai/ManaPayment.cpp:11235` `ScaledFedRecolourAfter` (called at `:11412`, before `ScaledFedFloatAfter`, under
    `MTG_SCALED_FED_FLOAT`): FOUND in the unit test -- Thran Portal's life cost triggers PayRefineAfter (ManaOracle
    RefinePayment), which re-makes the second {3} with Baldur's Gate FED in the oracle's WUBRG face order -> floats W2,
    Scrying's {G} fails (this, not the backtracker, was the "W float"). The recolour re-reads the fed surplus in
    LumpColourOrder's colour (line pips, then hand): same taps, same total, only colour moves; refuses unless the whole
    float gain is one colour with no pip of it in the bill and gain <= X.
  - `test/unit/test_mazes_end_f6.cpp`: rewritten around plan traits (PlanTraitsScope / ActLinePassScope / ActLinePayScope /
    LineUnpaidCostScope); DIAG test and dumps removed. Verified 11/11 F6 tests pass on a scratch-linked binary
    (`f63/mtg-test.diag`; the real build is queued).

## Trace evidence (d8 b0, single game, binary = c522bc6ca MINUS the recolour fix, `f63/mtg.f63`; script `f63/cmp2.sh`)
Arms: base = `MTG_MAZE_TAP_BALDUR_LAST=0 MTG_SCALED_FED_FLOAT=0`, both = defaults. ALL SIX previously-unrecovered games now
TIE base: d5 g823 (seed 211824) T3=T3, d3 g95 (212096) T4=T4, d5 g121 (211122) T5=T5, d5 g19 (211020) T5=T5, 2HG g125
(214126) T5=T5, 2HG g28 (214029) T5=T5. g823/g95/g121 play identical lines; g19/h125/h28 differ only by one fewer land
tapped (the fed float). Outputs `f63/n823.txt` etc. (Old-binary traces of the Baldur-only failures: `f63/g121.txt`,
`g19.txt`, `h125.txt` -- each diverged on a lone Maze's End activation turn, where the new condition gives rank 5.)

## RUNNING (launched ~17:00 UTC by F6-3's session -- may DIE with that session; check first)
`bash /workspaces/MagicDeckTester/build/scratch/maze/f63/go.sh` = PID **1664840** (children: `build.sh` 1664843 queued on
the heavy lock, `after_build.sh` 1664842). Stages:
1. `f63/build.sh`: heavy_run `./build.sh` -> `f63/build.log` (`build rc=N`), then `mtg-test -tc="*F6*"` -> `f63/f6tests.log`
   (`tests rc=N`); writes `f63/build.DONE`.
2. `f63/after_build.sh`: if build rc=0 AND tests rc=0 -> `f63/chain.sh`, else writes `chain.status` STOP + `chain.DONE`
   `rc=STOPPED`.
3. `f63/chain.sh` -> ONE heavy_run of `f63/chain_inner.sh` (log `f63/chain.heavy.log`), stage lines in `f63/chain.status`:
   copies binary to `f63/mtg.f63`; full units -> `f63/units.log`; ref_bench (run dir `f63/rb/`, symlinks to the worktree +
   a COPY of the 11 Maze refs from the primary checkout) -> `f63/refbench.out`; pooled A/B `f63/ab/manifest.json` (10,800
   jobs, from `f63/mkab4.py`) -> `f63/ab/run.out`; `f63/ab/summary.txt` (`anab.py run.out base`); `f63/ab/wall.txt`
   (`ab_wall.py`, from wip/ab-order a25cd46d6); d8 b0 (abandon 50M) of every slower game of arms fed/cond/both ->
   `f63/ab/d8.json`, `f63/ab/d8.out`.
   **FINAL MARKER `f63/chain.DONE`** (`rc=N`); last status line `done`.
**If PID 1664840 is gone and there is no `f63/chain.DONE`: relaunch `bash /workspaces/MagicDeckTester/build/scratch/maze/f63/go.sh`
with run_in_background** (it removes build.DONE/chain.DONE itself), then arm a waiter on `chain.DONE`.

A/B design: arms innermost, arm order ROTATED per seed, `same_seed_inflight: 0`, MTG_PGO=0 on every arm, held-out seeds.
Maze's End 4 arms: `base` (BALDUR_LAST=0, FED_FLOAT=0), `fed` (BALDUR_LAST=0), `cond` (FED_FLOAT=0), `both` (defaults).
Shapes: mzd5 d5 b20 x600 (seed 231001+gi), mzd3 d3 b10 x300 (232001), mzhg 2HG d5 b20 x600 (234001, heads 2 / life 30),
mzd0 d0 x600 (233001). Three Tree City decks, 2 arms (`base` = FED_FLOAT=0, `fed`): gbd5 Goblins d5 b60 x300 (235001),
gbd0 (236001), mtd5 Merfolk Turns d5 b20 x300 (237001), mtd0 (238001). Repro of a game: seed = base + gi, game_index = gi.

## How to read the results
- `ab/summary.txt`: per shape, per arm: avg win turn (loss = 9), units/game, ms/game, better/worse vs base, **net turns**.
  Per-deck net = sum over shapes: mazesend = mzd5 + mzd3 + mzd0; mazesend2hg = mzhg; goblins = gbd5 + gbd0; merfolkturns
  = mtd5 + mtd0. Units are primary for cost; wall from `ab/wall.txt` (order-balanced ratio).
- `ab/d8.out`: lines `<shape>_g<gi>_<arm>: played=1 avg=T ...` for each slower game, arm and base at d8 b0. A game
  RECOVERS when arm avg <= base avg (avg 0 = no win = 9). Abandoned at 50M units = unmeasurable (d0 g104 abandoned on all
  arms in F6) -- report it, do not count it as recovered.
- `refbench.out`: per reference, search win turn vs the USER's. s6 = `claude_s6_gi5`.
- `units.log`: doctest summary; any failure outside F6 -> check against origin first.

## Bar (per deck) and remaining steps, in order
1. Wait for `f63/chain.DONE`. If STOPPED, read build.log / f6tests.log, fix, rerun go.sh.
2. Per deck: net <= 0 AND every slower game recovers at d8 b0 (`ab/d8.out`). Choose the arm (prefer `both`; `cond`
   isolates the hold; `fed` isolates the float). A non-recovered game: attribute it (lever flips, cmp2.sh traces --
   "draws diverge" is not an explanation), amend, re-A/B. Delete the losing lever's branch (no default-off levers).
3. ref_bench: all 11 Maze refs must tie or beat the USER. Report s6. ALSO check whether s6's reordered USER line now pays:
   validate with the `--validate-line` repro (`build/scratch/maze/f6/repro.sh` shape; run from the WORKTREE, binary
   `f63/mtg.f63`), e.g. `VL="act=Maze's End;act=Maze's End;cast=Sylvan Scrying;seq=act=Maze's End#43|act=Maze's End#46|cast=Sylvan Scrying"`
   and the original queued order `seq=cast=Sylvan Scrying|act=Maze's End#46|act=Maze's End#46`; report the verdicts.
4. On a pass: smoke + regression in the worktree under heavy_run (`MTG_PGO=0 bash test/regression.sh --smoke`, then
   `--regression`); expected movers mazesend*, goblins*, merfolkturns* (fed float is engine-wide for Three Tree City).
   `--deck=`-scoped accept of the moved keys per tier, `python3 test/check_gt_logs.py`, final commit (drop WIP).
   Add an "F6" section to `docs/design/analysis-Maze's End.md`.
5. Push `wip/maze-f6` ONLY (never the shared branch; the parent integrates).
6. Open (report, do not fix here): CheckLine prices TapTutorPut at 0 on this branch (V3's viewer-maze2 fixes it); the
   `{ME#43}` single-activation root still pays #43 with Baldur's {C} + ME#46 (no later demand named) -- the s6 line is
   reached through the `{ME,ME}` root and the fed float; the {Scrying, ME} second link needs F5's owed-acts joint prepay.

## Notes
- PID 1589489 named in the F6-2 handoff's correction was ANOTHER agent's build (p5d4, output `p5d4/build10.log`); F6-2's own
  build3 (PID 1600482, still queued) was cancelled by F6-3 and replaced by go.sh.
- `f63/rb/references/Maze_s_End/` holds COPIES of the primary checkout's references (originals untouched).

## UPDATE 20:14 UTC: THE CHAIN FINISHED (`f63/chain.DONE` rc=0) -- results, not yet acted on
Net turns vs base (negative = better; from `f63/ab/summary.txt`):
| shape | fed | cond | both |
|---|---|---|---|
| mzd5 (600) | -15 (18/3) | +0 (2/2) | -16 (20/4) |
| mzd3 (300) | -11 (13/2) | +0 (2/2) | -12 (15/3) |
| mzd0 (600) | -19 (25/6) | -6 (8/2) | -25 (36/11) |
| mzhg (600) | -14 (17/3) | -1 (2/1) | -16 (18/2) |
| Goblins d5/d0, Merfolk Turns d5/d0 | 0 / 0 / 0 / 0 (0 better / 0 worse each) | - | - |
mazesend: fed -45, cond -6, both -53; mazesend2hg: fed -14, cond -1, both -16. Units/game at d5: base 130,880, both 129,397.
d8 b0 (`f63/ab/d8.out`): fed 14 slower -> 0 unrecovered; cond 7 -> 0; **both 20 -> 0 unrecovered**. `both` PASSES the A/B bar.
ref_bench (`f63/refbench.out`): 0/11 short, search 5.727 vs USER 5.818 avg, 1 faster than the USER; s6 = USER 6, search 6 (tie).
**units rc=1: ONE failure** -- `test/unit/test_mazes_end_f1.cpp:320/326`, "Maze's End F2 (gi1): Talon's {4} is paid so Maze's
End's {3} stays payable": the test expects the {3} UNPAYABLE (CHECK_FALSE) and it now pays, so the hold is empty. Probably the
fed float / recolour adding mana -- check whether the test's premise still holds (likely a test update, but verify it is not a
regression), then fix before the accept. Wall: read `f63/ab/wall.txt`. Still owed: the s6 `--validate-line` verdicts (step 3),
then step 4 (smoke + regression + scoped accept + check_gt_logs) and step 5.
