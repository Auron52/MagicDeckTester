# Hinata d4 vs d5 -- handoff 3 (2026-11-22 ~10:30 UTC, agent at ~255K)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/hinata-d4-d5`, branch `wip/hinata-d4-d5`, rebased onto
origin/phase-1-2-deck-analyzer `4a7f65983`. Scratch (build volume): `/workspaces/MagicDeckTester/build/scratch/claude_hinata_d4d5/` (= `$D`).
Design doc: `docs/design/hinata-d4-vs-d5-2026-11-21.md`. Do NOT push phase-1-2-deck-analyzer (the coordinator lands it).

## USER rulings (verbatim)
1. 2026-11-21: "This doesn't make much sense. We shouldn't be getting better results with Depth 4 than Depth 5 unless perhaps
   this is just variance. If they are similarily constrained below 4 turns lookahead they should act the same and going
   further should not make the results worse. We need to dig into this rather than just wave it off."
2. 2026-11-22: "if it is that much better then we should work on it. Can we not try an escalating budget to see if it
   recovers rather than d8 b0 if that is unreachable? I don't like leaving gaps and this is the alternative."
   (Coordinator: so d5E0 -- emulated ladder, beam off -- is the candidate to ship.)
Standing (from the briefs): Hinata play depth must be >= 5. Bar for a play change: every deck nets <= 0, searched and d0,
and every slower game recovers at --depth 8 --budget-ms 0 (or via an escalating budget where d8 b0 cannot reach it).
Cut CPU; never justify a CPU increase by headroom. Subagents do not push the shared branch.

## Commits (wip/hinata-d4-d5, all after 4a7f65983)
- fcaec9629 handoff 1; 8a724a30d handoff 2 (coordinator)
- b635c0230 design doc draft; 0356ee7d2 doc: held-out candidates + d8 b0 bar + USER decisions
- **0c993e9a9 SHIP: `decks/Hinata/Hinata.value.json` value_play: `ladder: "emulated"`, `emul_r [1096.28, 256.6, 100.58,
  105.9]`, `beam_width 3 -> 0`, target_depth 5 (note appended).**
- WIP commit after this file: doc escalating-budget table + this handoff.

## Key findings (detail in the doc)
- The depth-5-only W3 value beam (`TurnSolver.cpp` DEPTH-ADAPTIVE BEAM, `depth == 5`) was the whole d5-vs-d4 residual:
  beam off, d5E0 == d4E on 2398/2399 held-out games (1 faster).
- Held-out b30 n=2399 (`$D/cand/`): d5E0 5.4848 vs shipped d5N 5.5581 = -0.0734 (t -8.7, 208/70) at 0.74x CPU.
  d5N0 (hybrid, beam off) -0.0267 at 1.17x CPU.
- Held-out slower games of d5E0 (70): 67 recover at d8 b0; s62002 gi74 (50M-unit cap), s62002 gi212 (runaway) and
  s63003 gi548 recover by escalating budget (`$D/esc/`): gi212 at b60, gi74 at b120, gi548 at b120.
- gi33 (s61001 gi33 = seed 61034): unpriced plan enumeration (~72.5K lazy clones per list, ~270x underpriced units);
  perf in `$D/gi33p/`. Axis not yet attributed. Perf item, not d4/d5.

## Gate on the rebased tree + ship (`$D/gate/`, box lock, MTG_PGO=0)
Release build OK; unit (`build/Release/mtg-test`) exit 0; `test/regression.sh --both`: 384 passed, 5+5 FAIL, ALL Hinata,
every aggregate equal or better:

| case | GT | got |
|---|---|---|
| hinata_smoke_d3_s1001 | 5.5400 | 5.5200 |
| hinata_smoke_d5_s1001 | 5.5000 | 5.4300 |
| hinata2hg_smoke_d5_s1001 | 5.3200 | 5.2800 |
| hinata_regression_d5_s30001 | 5.2000 | 5.2000 (play differs) |
| hinata_regression_d3_s2002 | 5.6000 | 5.6000 (play differs) |
| hinata_regression_d5_s2002 | 5.4133 | 5.4067 |
| hinata_regression_d5_s3003 | 5.7400 | 5.6867 |
| hinata2hg_regression_d5_s2002 | 5.4133 | 5.3867 |

No other deck moved (so every other deck nets 0). d0 rows: none failed.

## Slower games (23 = 5 smoke + 18 regression), `$D/cls/{smoke,regression}.log` + `$D/pers/batch.out.final`
- 19 churn at 4x/16x (test/classify_turn_later.sh): smoke d5 s1001 gi35/gi63/gi89; regression 2hg d5 s2002 gi17/gi58,
  d3 s2002 gi17/gi25, d5 s2002 gi3/gi17/gi38/gi65/gi94, d5 s3003 gi13/gi58/gi59/gi95/gi111/gi125/gi129.
- 4 PERSISTED at 16x, all recover at a higher budget (escalating ladder, same seed/game_index):
  - hinata_regression_d5_s3003 gi20: T5->T6; recovers to T5 at **32x** (b960); also T5 at d8 b0.
  - hinata_regression_d5_s3003 gi128: T4->T5; recovers to T4 at **64x** (b1920); also T4 at d8 b0.
  - hinata_smoke_d5_s1001 gi46: T5->T6; recovers to T5 at **64x** (b1920) (32x = T6).
  - hinata_smoke_d3_s1001 gi46: T5->T6; recovers to T5 at **128x** (b1280) (32x/64x = T6).
  (I stopped the batch's remaining jobs -- x256 and the gi46 d8 b0 rows -- once all four had recovered.)
So the clairvoyance test is not needed: no game fails to recover.

## Remaining, in order (next agent)
1. GT accept (the last run's results are in test/results; no re-run):
   `cd /workspaces/MagicDeckTester/.claude/worktrees/hinata-d4-d5 && bash test/regression.sh --both --accept-with-regressions="Hinata d5E0 ship (0c993e9a9: emulated ladder + escalation beam off at d5, USER 2026-11-22): only Hinata cases move, every aggregate equal or better; 23 searched slower games: 19 churn at 4x/16x, 4 recover by escalating budget (reg d5 s3003 gi20 at 32x, gi128 at 64x, smoke d5 s1001 gi46 at 64x, smoke d3 s1001 gi46 at 128x); held-out b30 n=2399 -0.0734 at 0.74x CPU"`
   Check first that test/results/{smoke,regression}.env are still from the gate run (07:16 UTC 11-22) and that nothing
   else ran a tier in this worktree since.
2. `python3 test/check_gt_logs.py` (must be clean) and `python3 scripts/check_references_safe.py`.
3. Update the doc's section 5 ("What the USER decides") to record the USER's ruling and the ship + gate + slower-game
   result; commit (GT + doc) and push `wip/hinata-d4-d5` (`git push origin wip/hinata-d4-d5`). Never the shared branch.
4. Optional follow-ups (report, do not adopt): the three other decks with a d5 W3 ld2 value beam (Anti-Lifegain,
   Creature Giving, Dragonstorm) have the same depth-5-only beam; gi33 axis attribution (unpriced enumeration).

## Running jobs
None of mine. (cand, d8, esc, g548, g212, gate, cls, pers all finished or stopped; the box lock is free of my work.)
