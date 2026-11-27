# Dragonstorm APPLY-DROP round 2 (ds-apply2) handoff 1 (2026-11-27 ~00:50 UTC)

Worktree `.claude/worktrees/ds-apply2`, branch `wip/ds-apply2` (from origin/phase-1-2-deck-analyzer f5f442801). Push ONLY
wip/ds-apply2; the parent lands through the push queue. Never touch references/. Scratch
`/workspaces/MagicDeckTester/build/scratch/ds-apply2/` ($S2). The previous round's scratch
`/workspaces/MagicDeckTester/build/scratch/ds-apply/` ($S) is read-only: delete nothing there.
Background: `logs/ds-apply/handoff.md` (round 1 handoff 2), `git show 596363bf1:logs/ds-apply/handoff.md` (item 4 lists the
mechanisms), `docs/design/dragonstorm-apply-drop-2026-11-26.md` (design + measurements).

## Commit (this handoff)
- The WIP commit carrying this file: two levers, DEFAULT ON, both NOT YET VALIDATED (no gate, no A/B). heurarm slots
  CCO_RESERVE_JOINT, LEFTOVER_SWAP_EXACT at the END of both lists (enum + Name table, same order); readers in
  src/ai/EngineFlags.h (CcoReserveJointOn, LeftoverSwapExactOn); code in src/ai/ManaPayment.cpp; tests in
  test/unit/test_dragonstorm_line_keep.cpp. NOT landable yet.

## Mechanism (a): two Unclaimed Territories -- ROOT CAUSE FOUND, lever MTG_CCO_RESERVE_JOINT
- It is NOT the crit line-keep rung. It is the greedy ladder's CCO LINE RESERVE (ManaPayment.cpp, `cco_hold`) and its
  MTG_CCO_RESERVE_OTHER refinement: a Territory is released when, PER COLOUR, the OTHER untapped creature-only lands make
  that colour. Each other land is counted toward every colour it makes, but one tap makes one unit. With Karrthus owing
  {B} and {G} and two Territories, each read the other as covering both, so neither was held. The CCO HAND DEMAND rung
  (a noncreature card with a coloured pip in hand: Song, Dragonstorm) then puts the unheld Territory on the narrow rung 9,
  and Desperate Ritual's generic takes its {C}.
- Lever: the release needs the others to cover the shortfall JOINTLY (Hall's condition over every subset of the held
  colours; at most 16 other lands, otherwise the per-colour test stands). It only ADDS holds. Ordering only (the
  backtracker still runs on any greedy failure).
- Traced in game (temporary print, `logs/ds-apply2/ds2pay-debug.snippet.cpp`; env MTG_DS2_PAYDBG=<turn>): with the lever
  off, the engine (hook=0) pays Desperate with Territory#57 + Mountain#34 and Karrthus drops. s30096797 T5: base binary
  2 APPLY-DROP flags, lever on 0, lever off on the new binary 2.
- Unit test `s30096797 T5 -- two Territories are held jointly` PASSES on/off (on pays, off fails) once the fixture has the
  game's HAND (the hand-demand rung is what tapped the Territory early; an empty hand does not reproduce it). Control
  test (Kolaghan owes only {B}): PASSES, the arms are identical.
- The other four repros (s30112157 T6, s30142602 T6, s30155973 T5, s30089733 T5) flagged on the base binary (4 / 6 / 2 /
  4 flags; `$S2/rep_base_<seed>.err`) and have NOT been re-run on the new binary.

## Mechanism (c): exact swap test -- implemented, but the repro is NOT fixed (diagnosis incomplete)
- Lever MTG_LEFTOVER_SWAP_EXACT in SwapLeftoverForLine: prefilter (a colour the rest owes went to this payment's
  generic, and a leftover unit of another colour floats), then the rest's PIPS are judged with LumpAwareBillPayable (new
  `for_creature` parameter, default true; other callers unchanged) twice, LOOSE (Territory any colour) and STRICT
  (Territory {C}). The brief's "rest_creature as in LineReducerKeepHold" does NOT work here: g_line_noncreature_mv is the
  line's TOTAL noncreature MV (Ruby 2 + Song 3 = 5), so at Song's payment (MV 2) rest_creature is false and Karrthus
  reads as unpayable both ways. The loose/strict bracket: no swap when strict-payable as left; swap when it proves more
  (loose-payable where it was loose-short, or strict-payable where it was not); never makes a payable judgement
  unpayable.
- Side effect, found by the units: the EXACT path also fixes the old s30087280 board (it credits the ritual's float
  even when WIDE is off), so that WIDE test now pins EXACT off to keep isolating WIDE.
- s30129376 T4 still flags 1 with both levers on (base 1). The flagged plan is the `no land` variant {Ruby, Song,
  Karrthus} on Needle (2 counters), one Territory, Mountain#29, Lotus Bloom. The trace (`$S2/dbg2_30129376.err`, grep
  `ds2pay` + `lu g6 B1 R2 G1` + `hook=0`) shows the engine's Song payments have NO floating {B} before them (before
  B0 R1/R2). So the engine's Ruby payment did not leave the Bloom's {B} floating: it paid Ruby with the Needle /
  Mountain and kept or cracked the Bloom otherwise. The {B} is lost at RUBY's payment (decision 1; the oracle's ROLLBACK
  says "keep the engine's first 1 mana decision" and fails at #3, so check that reading), NOT at Song's generic.
  The swap lever never gets the shape it was written for. The unit test (fixture: {B} already floating) fails in BOTH
  arms: even with the {B} floating, the exact swap does not keep it. That needs a debug pass too (prefilter, loose_left,
  the trial's judgement).
- Decide next: (i) look at what the engine pays Ruby with on that board (`MTG_DS2_PAYDBG=4`, Ruby lines with
  `lu g` = Ruby+Song+Karrthus, hook=0) and whether a mechanism there is in scope; (ii) find why the exact swap does
  not fire in the unit fixture; (iii) if (c) cannot be shown to fix a real event, DROP the lever (delete it: an
  agent's own dead lever is deleted, never shipped default-off) before the gate.

## Remaining, in order
1. Fix or drop lever (c) (above). If dropped, remove its slot from BOTH heurarm lists, the reader, the code, the test,
   and the `for_creature` parameter if nothing else uses it.
2. Rebuild (HeuristicArm.h changes recompile ~400 objects: full width under
   `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock flock /workspaces/MagicDeckTester/build/scratch/claude_build.lock ./build.sh release -- --target mtg mtg-test`;
   a ManaPayment.cpp-only change: `MTG_BUILD_JOBS=2 bash scripts/box_lane.sh --slots 2 -- flock .../claude_build.lock ./build.sh release -- --target mtg mtg-test`).
   Wait ~200 s after a src edit (9p lag). Copy binaries to $S2/bin_<x>/ (bin_base = f5f442801 already there, with src/decks
   symlinks; bin_new = the first two-lever build; bin_dbg = the debug build).
3. Units: `UNIT_PAR_DIR=$S2/unit bash scripts/box_lane.sh --slots 2 --cap-gb 8 -- bash scripts/unit_par.sh build/Release/mtg-test 2`
   (full suite), plus `-tc="Dragonstorm line keep*"`.
4. Event mini-gate: `THREADS=32 bash logs/ds-apply2/run_turns.sh <bin> $S2/tn_base` with bin_base and the new binary
   (under the box lock at 32 threads, or the lane at THREADS=2), then `python3 logs/ds-apply2/cmp.py tn_base tn_new`
   (both dirs under $S2). Event manifests are copied to `$S2/miniT*.json`.
5. Gate: `$S2/gate.json` is BUILT (`logs/ds-apply2/mk_gate2.py`, arms off / on / no_cco_reserve_joint /
   no_leftover_swap_exact; 906 jobs, 180,218 games). If lever (c) is dropped, rebuild the manifest with only the
   joint lever (edit `L` in mk_gate2.py). Run: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env
   MTG_RSS_CAP_GB=40 MTG_PGO=0 bash logs/ds-apply2/run_gate.sh <bin> $S2/gate.json $S2/gate_out`; report
   `python3 logs/ds-apply2/gate_report.py $S2/gate_out [--drops]`.
6. Play A/B: the CCO reserve is a SHARED payer rule (Pirates, LandfallElementals and others run creature-only lands),
   so every deck: `python3 logs/ds-apply2/mk_ab.py $S2/ab.json --n 400 --hg 200 --wf 3000 --base 5220000` (L already
   names the two new levers; trim it if (c) is dropped), run with `logs/ds-apply2/run_ab.sh` under the box lock, report
   `python3 logs/ds-apply2/ab_report.py $S2/ab_out --list-slower`. Check the heartbeat in its first 10 min.
7. Replay every slower game at d8 b0 on both arms (`depth 8, budget_ms 0, ignore_play_profile true, abandon_units
   30000000`; 2HG adds `starting_life 30, opponent_heads 2`; template $S/slower.json). Root-cause any that persists
   (`--claude-play --choices ... --choices-then-auto` and log diffs). Units and CPU per deck.
8. Design doc section (docs/design/dragonstorm-apply-drop-2026-11-26.md), commit, push wip/ds-apply2, final report.
9. Low priority, only if cheap: d (Apex-line Kolaghan overkill-only), e (need-later past Apex).

## Running jobs
None. Every build and run of this session has finished.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm).
- Adoption bar: judge the average at play settings. Adopt if many decks gain, mitigate the few that get worse
  afterwards, ignore seed noise and clairvoyant losses; d0 barely counts.
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything must stay findable at unlimited budget.
- USER-owned decisions are surfaced in the report, never settled by you.

## Box rules (from the brief)
ONE big engine process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
`env MTG_RSS_CAP_GB=40 MTG_PGO=0` (a coordinator gate filler uses the box lock and yields to waiters). Small jobs via
`scripts/box_lane.sh --slots N`, N <= 2. No `timeout`; wait on `kill -0 <pid>` or a DONE marker, never `pgrep -f`.
Rebase, never merge. Mana payment is a standing exemption; these are payer choices in the shared payer (executor,
rollout, payability walks together, at every budget). Past ~250K context: hand off.

## Traps found this round
- A unit fixture without the game's HAND does not reproduce a CCO-land payment: MTG_CCO_HAND_DEMAND reads the hand.
- An oracle APPLY-DROP flag shows the oracle's witness payments, not the engine's. The engine's taps need the debug
  snippet (hook=0 lines are the engine's own payments; hook=1 lines are the oracle's probes).
- `./build.sh release -- --target mtg mtg-test` builds both; `--` without a mode fails.
