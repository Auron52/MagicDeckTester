# F5R-5 handoff (pay refine; Maze's End) -- STOOD DOWN 2026-11-20 21:30 UTC

Stopped on the parent's order. USER 2026-11-20 21:27 UTC: "At this point I want to wrap up Maze's End as soon as we
can." / "So having other nonsense in the way doesn't make sense." The box is reserved for the Maze's End batch-2
landing and its mulligan profile. Nothing of F5R-5's is running: chain3 was killed (twice: once by the parent, and the
relaunch by me) while it was still queued on the heavy lock. It never built, and it wrote no markers.

Worktree `.claude/worktrees/maze-f5r3`, branch `wip/maze-f5r3`. Read `logs/maze/F5R4-handoff.md` first, including its
CORRECTION section (9f9126a9b). Scratch: `build/scratch/maze/f5r3/`.

## Done (commits on wip/maze-f5r3)
- 9f9126a9b (F5R-4's correction): the trace's `float -` is the PRE-payment float, and the engine's payment does float.
- c7a62ec31 (F5R-5):
  * The subset unit test is fixed. Cascade Bluffs has a free {C} face, so both rules accept it, and the test now
    CHECKs that. A constructed Horizon Canopy + Island board (every Canopy face costs 1 life) pins where the plain and
    subset rules differ.
  * The MTG_RBT_DEBUG print is rewritten into ONE line per REFUSED plain-RBT call that releases >= 4 sources:
    T, released, combinations tried, e_float (the engine's payment), c_float, c_taps (`+name` = tapped by the
    candidate but not by the engine), and the released sources with their face counts. Diagnostic only; drop it or
    keep it env-gated before any landing.
  * The f5 binary (built from 7e9e9a280) is units-green apart from the two expected Bluffs CHECK_FALSEs; c7a62ec31
    fixes those but is NOT BUILT yet.

## Measurements so far
- Units f5: 3777/3778 test cases. The only failures are test_pay_refine.cpp:422-423, the wrong Bluffs pins, which
  c7a62ec31 fixes.
- run/rbtdbg (f5, wall.json, old per-combination print): 400 lines. Every sampled refusal has an engine float the
  re-tap cannot remake in colour, e.g. `released=4 ... float {W}{U}{B}{G} vs {W}{W}{W}{W}` (Baldur's Gate's
  X-of-one-colour mode) and `released=1 float {G} vs {R}`. There are 0 lines where the engine floated nothing, and 0
  apply FAILs or tapdiffs. This CONFIRMS the correction: the layer-5 refusals are genuine colour refusals, and
  RBT_SUBSET is unlikely to fix the 50 capped refines. The sample is not restricted to the capped refines; rbtdbg2
  (below) is.

## Remaining, in order (only when the USER/parent re-opens this work)
1. Build f6 from c7a62ec31, run units, then run rbtdbg2: the targeted print on wall.json, to confirm that the capped
   {9}{G}/{11}{U} refines are colour refusals. Then run versub: the RBT_SUBSET verify (BOUND_VERIFY + UNCAPPED +
   STATS); report the capped count.
2. The all-deck digest screen: `screen.json` = smoke+regression jobs (610 jobs, emitted by tools/emit_tier.sh,
   byte-identical to regression.sh's emit_jobs) x 6 arms (base/pre/cap/pri/sub/all), arms innermost and rotated per
   job, 3660 jobs, ~1.05M games. Read it with `python3 $S/tools/screen_read.py $S/run/screen/out.txt`.
3. The held-out A/B on the decks that move. `tools/mk_heldout.py <out> <arms csv> <tierjob>:<seedbase>:<games>...`
   writes per-game jobs (seed = base+g, game_index g, same_seed_inflight 0, arms innermost and rotated). Read it with
   `tools/ab_read.py out.txt err.txt`: per deck, net, slower list, units ratio, and ABANDONED games as UNRESOLVED.
   Then d8 b0 on every slower game.
4. The cap comparison: the screen's `cap` arm against `base`, where base = 08b13f602's fixed node cap. Also time the
   Galadriel claude_s10_gi9 replay with levers off against BUDGET_CAP alone (the levers-off timing was never taken).
   NOTE: DecisionBudget() is null at the executor's committed apply, so under BUDGET_CAP the executor walks to the
   CEILINGS (1<<20 probes, 64x nodes). Watch Galadriel's cost there. Do NOT change the shipped cap; report to the USER.
5. A candidate lever not yet built (coordinator's suggestion): a sound collapse of layer 5's face variants, or an RBT
   fold of released Gates by colour.
- Relaunch (one command, every heavy stage under its own heavy_run, harness-tracked via run_in_background):
  `bash /workspaces/MagicDeckTester/build/scratch/maze/f5r3/tools/chain3.sh > /workspaces/MagicDeckTester/build/scratch/maze/f5r3/chain3.log 2>&1`
  Markers: build.f6.DONE, stage4.log (STAGE4_DONE), run/screen/DONE, chain3.DONE. Wait on them, or on the chain's PID
  (never `pgrep -f`).

## USER rulings
All of them are verbatim in F5R4-handoff.md, plus the stand-down above. Adoption of any lever is the USER's. The node
cap (08b13f602) is another session's landed fix: propose the priced version, do not change it.
