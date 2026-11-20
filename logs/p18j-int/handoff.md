# p18j-int handoff / resume file (2026-11-19 ~23:00 UTC)

Worktree `.claude/worktrees/p18j-int`, branch `wip/p18j-int` (pushed to origin; the shared branch is NOT pushed).
Scratch: `/workspaces/MagicDeckTester/build/scratch/p18j-int/` (tools/, chain logs, binaries).

## Done (commits on wip/p18j-int, rebased onto origin/phase-1-2-deck-analyzer fb9cb5e03)
- Rebase of origin/wip/p18j-claude (13 commits): CLEAN, no conflicts; src hunks byte-identical to the original diff.
- 1eed5ba08 strip MTG_P18_ISO / MTG_P18_ZX / _pfp / _pwc / _paw / _pip / _pxa / _pllt-under-_pllw / mid-ladder walk
  (all off in mode 4 -> mode 4 unchanged by construction).
- 547b6cb9d `value_play.ladder "sparse"` = sampled gate 4 + any learned model set aside (MulliganProfileIO.h
  AttachValueSidecar); explicit sampled_gate wins (Boars/Morph/Spirits mode 3 unchanged); unit test
  test/unit/test_sampled_gate.cpp.
- 3b5303e73 valueleaf.sh phase E.8 `phase_sparse` + subcommand `valueleaf.sh sparse [decks]`; scripts/sparse_ladder_stage.py
  (live vs sparse, 8 seeds x 500 from 7,705,000; adopt iff units < 1 and net paired turns <= 0).
- 02c7044e2 RESEARCH sampled gate mode 5 = mode 4, then mode 2's priced sampled gate on a miss (screen arm sg5).
- docs/design/sparse-heuristic-ladder.md (canonical doc; taxonomy + measurements sections still to fill).
- chain1 DONE 2026-11-20 05:34 UTC on binary build/scratch/p18j-int/mtg_c0 (= 02c7044e2 src): build ok; unit tests
  3718/3718 pass; smoke 231 passed / 0 failed, per-game audit 0 changed configs (default play byte-identical to
  origin GT: rebase + strip clean); census done -> taxonomy table in docs/design/sparse-heuristic-ladder.md.
- AB1 is GATED by the parent: tools/chain2gate.sh (PID in chain2.pid) waits for build/scratch/p18j-int/AB1.GO, then
  execs chain2.sh unchanged.

## Running (queued) jobs
- chain1 (PID file build/scratch/p18j-int/chain1.pid; flock-wrapped `tools/chain1.sh`): ./build.sh -> copies
  build/Release/mtg to `mtg_c0` -> mtg-test (`unit.log`) -> `MTG_PGO=0 bash test/regression.sh --smoke` in the
  worktree (`smoke.log`) -> census (`tools/run_census.sh`: every shipping deck x 30 games, MTG_P16_TRACE=1,
  `census/trace.err`). Status lines: `chain1.status`; DONE marker `chain1.DONE` ("EXIT ok").
- chain2 (PID file chain2.pid; `tools/chain2.sh`): waits for chain1.DONE, then flock -> AB1 pooled batch:
  15 decks x 6 blocks x 200 games, seeds 17,000,000 + 10,000*b, arms live/sparse/sg5 (+heur on mode-3 and emulated
  decks), MTG_P16_TRACE=1. Out: `AB1/out.log`, `AB1/wins/`, `AB1/trace.err`; DONE marker `AB1.DONE`.
  CHECK the `[batch] heartbeat` line in AB1/trace.err within 10 minutes of its start (`AB1.status` has the time).

## Remaining, in order
1. chain1: unit.log all pass; smoke.log: every mismatch must also fail on origin (default play must be byte-identical
   -- any move is a rebase defect). Census: `python3 tools/census.py census/trace.err` -> fill the taxonomy section.
2. AB1: `python3 tools/abreport.py AB1` (baseline heur where present, else live) and `... AB1 live` -> per deck quality
   (net, t, earlier/later), units, CPU, rollout ratio and % of the heur rollouts skipped. Fill the doc's tables.
3. Root-cause every deck where sparse loses (quality or cost); fix the mode, not the deck; every slower game must
   recover at --depth 8 --budget-ms 0.
4. Flip clear winners: `{"value_play":{"ladder":"sparse"}}` (keep the deck's other value_play keys); NOT Sea
   Creatures / Merfolk Turns / Boars until the parent says their adoptions landed. Smoke + regression, per-game
   audit, accept scoped with --deck=..., python3 test/check_gt_logs.py, commit per deck, push wip/p18j-int only.
5. (2)-type prunes (committing-pass leaves), each A/B'd in isolation: (1) vs (1)+prune, held out; adopt only if
   net <= 0 AND units/CPU measurably lower.

## Code-reading findings so far (to be confirmed by the census)
- The sampled gate acts only with NO value model attached (TurnSolver p18_on: !vl_active, !g_force_heuristic_leaf).
- `leaf: none` + `ladder: escalation` (Bruna, Creature Giving, Extra Turns, Kitty Equipment) and the learned hybrid
  with no ladder key run a leafless / value probe, then on an unverified line the escalation's PATH 2 = a full
  heuristic rollout ladder (TurnSolver.cpp ~124288 `FullSearchLine(... esc_budget ...)` under
  ForceHeuristicLeafGuard(true)) unless the deck is `single`. So those shapes DO run a heur ladder in their
  escalation, and the sparse gate cannot act there (g_force_heuristic_leaf). Census will size it.
- `leaf: none` + `ladder: single` (FIT): leafless probe, then ONE rollout pass (F) at the fitted depth, stepping
  shallower on overrun (L climb passes) -- not a ladder of superseded passes.

## USER rulings (verbatim) -- all in docs/design/sparse-heuristic-ladder.md "USER rulings"
