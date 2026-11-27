# Dragonstorm APPLY-DROP round 2 (ds-apply2) -- FINAL (2026-11-27 ~05:40 UTC)

Worktree `.claude/worktrees/ds-apply2`, branch `wip/ds-apply2` (on origin/phase-1-2-deck-analyzer f5f442801). Push ONLY
wip/ds-apply2; the parent lands through the push queue. Never touch references/. Scratch
`/workspaces/MagicDeckTester/build/scratch/ds-apply2/` ($S2; tested binary `$S2/bin_new4/`).

## Status: DONE, validated, landable (see the two clean commits on wip/ds-apply2; the git log names them)
- Code commit: `MTG_CCO_RESERVE_JOINT` + `MTG_LINE_NC_LEFT`, both DEFAULT ON, `=0` byte-identical. heurarm slots at the
  END of both lists (enum + Name table, same order). Readers in src/ai/EngineFlags.h; code in src/ai/ManaPayment.cpp
  (+ ManaPayment.h scope/count, cast-site count-downs in AIEngine.cpp CastSpellFromHand and TurnSolver.cpp apply_one);
  tests in test/unit/test_dragonstorm_line_keep.cpp.
- Docs commit: docs/design/dragonstorm-apply-drop-2026-11-26.md "Round 2" section (mechanisms, repro table, gate table,
  A/B) and these logs.
- `MTG_LEFTOVER_SWAP_EXACT` was DELETED (its premise was wrong: the {B} of s30129376 is drained at Song's generic with
  nothing left floating to swap; the true mechanism is the line-keep rung's never-decremented noncreature total).

## Measurements
- Units: 4314 / 4314 (unit_par); line-keep 8 / 8, both new tests on/off.
- Event repros: every one fixed, each by exactly one lever (table in the design doc).
- Gate (180,218 games): declared events 59 -> 50 (10 fixed, 1 new in a moved block), need-later 48 -> 49 (1 new in a
  moved block), ORACLE-GAP 0 -> 1 (identical-digest block: oracle coverage, not play), turns 313,583 both.
- A/B (base 5220000, 87,600 games): 43,800 pairs, net 0, 0 slower, 0 faster in all 128 shapes; units -0.00%, cpu
  +0.09% (noise). No d8 b0 replay owed.

## Remaining (not this round)
- Kolaghan declared (144 flags) and the need-later Karrthus cluster (270-279 flags) are untouched.
- Round-1 items d (Apex-line Kolaghan overkill-only) and e (need-later past Apex).

## Running jobs
None.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm).
- Adoption bar: judge the average at play settings. Adopt if many decks gain, mitigate the few that get worse
  afterwards, ignore seed noise and clairvoyant losses; d0 barely counts.
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything must stay findable at unlimited budget.
- USER-owned decisions are surfaced in the report, never settled by you.

## Traps found
- A unit fixture without the game's HAND does not reproduce a CCO-land payment: MTG_CCO_HAND_DEMAND reads the hand.
- An oracle APPLY-DROP flag shows the oracle's witness payments, not the engine's; the engine's taps need the debug
  snippet (hook=0 lines are the engine's own payments). Read the `before`/`fl` float columns of the NEXT payment too:
  round 2's first diagnosis misplaced the loss by one payment.
- The LINE_FLOAT_TAP_FIRST rung never treats a depletion land (Sandstone Needle) as spare.
