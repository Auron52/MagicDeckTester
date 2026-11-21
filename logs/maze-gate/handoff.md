# Maze's End oracle gate -- handoff (2026-11-20 ~15:30 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a02c2671f9e6fcd10`, branch `wip/maze-gate`
(made from origin/phase-1-2-deck-analyzer `c86a86333`, src tree `31070f52`). WIP commit: see `git log -1`
("WIP maze-gate: oracle non-mana tap choice ... UNBUILT/UNTESTED"). Not pushed. Do not push the shared branch.

## Mechanism found (UNMODELED, step 1)
139,000 of 139,380 UNMODELED = "engine tapped a mana source outside any payment". Cause: two NON-MANA Gate taps
whose WHICH-Gate pick is the provider's `MazesEndProvider::EtbTapSubtypeOrder` (mana-payment exemption, ledger
decisions 2/10/30), which the oracle never drove:
- Guild Summit ETB "tap any number of untapped Gates, draw per Gate" (SpellEffects.h, etb_tap_subtype_draw block).
  4,845 of the 5,000 whole UNMODELED lines in the gate stderr hold a Summit cast (also via breakpoint
  continuations, hence `{land=Cliffgate}` plans flagged).
- Heap Gate "{1},{T}, tap an untapped Gate: Treasure" cost (`PayActivationOtherTap`): the other 155.
The Gond Gate / Talon Gates names are simply the Gates the rank picked. Engine action is not wrong (standing
exemption); the oracle must drive the pick. Plan text `(xN)` on Summit = chosen_x = N+1 (x1 = tap none).
Useful colours in Maze: U, R, G (+C); W/B useless.

## Done (in the WIP commit, NOT yet compiled)
- `src/core/SpellEffects.h`: `g_oracle_tap_choice` function pointer (global, null-safe) called at both pick sites
  after the provider order (Summit kind 0, PayActivationOtherTap kind 1). Off the oracle's copy it returns false
  with no side effect -> play byte-identical by construction (still must smoke).
- `src/ai/ManaOracle.h`: Hook fields `f_tapchoice/f_tc_kind/f_tc_elig`; `kTapChoiceWhat="tap-choice"`;
  `OracleTapChoice` decl.
- `src/ai/ManaOracle.cpp`: `TapChoiceOn` (MTG_ORACLE_TAP_CHOICE, default ON), `TapChoiceCap`
  (MTG_ORACLE_TAP_CHOICE_CAP 512), `TapChoiceClass` (ResClassKey+AttachedKey+plan#), `OracleTapChoice`
  (safe / engine-step / frontier / scripted; forced picks recorded in tapped_by_script), static registration,
  `TapChoiceOptions` (distinct class multisets of size n, provider's pick first); FrontierRec fields, FrontKey
  "k<n>/<kind>", frontier capture, expansion, `what` label, trace label, TapsText label, viewer to_pays text-only;
  guard in ScriptedTapAhead; `MTG_MANA_ORACLE_ONLY_TURN=<t>` diagnostic (audit one turn only); DECISION-COST now
  prints per-workctr-kind priced ms.
- `src/ai/ManaPayment.cpp`: ScriptedPay guard (a tap-choice entry is never a payment -> tap_error).
- `test/unit/test_mana_oracle.cpp`: two tests at the end (Summit N=2 on six Guildgates covered scripted-only, no
  UNMODELED/ORACLE-GAP; forced N=all is the line's own).
- `docs/design/maze-oracle-gate-2026-11-20.md`: draft (section 1 written; 2-4 pending).

## UPDATE: the `--target mtg` build below FINISHED with exit 0 (it compiled). Still rebuild once to pick up any
## ManaOracle.cpp edit it may have missed; unit tests not yet built.

## Running
- Build of `--target mtg` with the change: background task output
  `/tmp/claude-1000/-workspaces-MagicDeckTester/9ae25e19-181d-4e94-a402-0021f978d3aa/tasks/bmc9jxour.output`
  (box_lane 1 slot; done when it prints ">>> done: build/Release/"). Binary:
  `/workspaces/MagicDeckTester/build/wt/agent-a02c2671f9e6fcd10/Release/mtg` (build/ is a symlink there).
  It may have started before the last ManaOracle.cpp edits (DECISION-COST breakdown) -- rerun `./build.sh release
  -- --target mtg` through the lane afterwards; unit tests need the full `./build.sh` (mtg-test target).
- Baseline binary (current src, no change): `/workspaces/MagicDeckTester/build/scratch/claude_maze_gate/bin/mtg.base`.
- Repro runner: `MTG_MANA_ORACLE=2 bash logs/maze-gate/rep.sh <bin-tag> <out-tag> <slots> <capgb> <gi...>`
  (bin = build/scratch/claude_maze_gate/bin/mtg.<tag>; copy the new binary there as mtg.new first).

## Measurements so far (baseline, current src, oracle 2, one thread)
| gi | applies | UNMODELED | MISS | wall | RSS |
|---|---|---|---|---|---|
| 9 | 370 | 0 | 0 | 0.7 s | 42 MB |
| 21 | 10,390 | 10 | 0 | 3.1 s | 233 MB |
| 27 | 2,926 | 0 | 0 | 2.5 s | 166 MB |
| 167 | 578,290 | 170 | 0 | 81 s | 1.36 GB |
Gate (old src): worst games g4308 4.1 h, g2893, g950, g2014, g3144; 1,282 decisions spent the budget; the capped
decisions are over half the Maze oracle CPU. Most CAPPED-DECISION per game: g4604 (4,429), g4810, g4203, g4238, g454.
CAPPED-DECISION top repros: g30 T6, g14 T5, g4 T7, g15 T5, g90 T5 (each a full game: use MTG_MANA_ORACLE_ONLY_TURN).

## What remains, in order
1. Finish build; run unit tests (full ./build.sh in lane); fix compile errors if any.
2. Re-run g21, g167 (and g9/g27) with the new binary: expect UNMODELED ~0, no new ORACLE-GAP; compare applies/wall.
3. Smoke identity: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash test/regression.sh --smoke`
   (0 changed rows required). Background + PID waiter.
4. CAPPED-DECISION: run g30 T6 / g14 T5 / g4 T7 with ONLY_TURN + MTG_MANA_ORACLE_HEAVY=1 + COST_TRACE=1 (lane,
   cap-gb ~6) on the new binary; read DECISION-COST per-kind breakdown. Hypothesis: generic payments ({3} for
   Maze's End) over ~10 name-distinct Gates -> C(n,3) incomparable frontiers (Res is name-keyed), each apply
   re-solving a breakpoint continuation (Summit/Gate-entry draw). Candidate SOUND collapse: capability dominance of
   plain lands (same params except `produces`, same types; useful-face subset) in the oracle's Res/Pareto/Covers,
   SCOPED TO ORACLE CHECKS ONLY -- Res/Covers are also read by the engine payer's RefinePayment (play), so it must
   be gated like t_opp_keyed. Another session (claude_board_share / maze_oracle_mem) works on outcome-board
   sharing in Realize: avoid overlap.
5. Small classes (APPLY-DROP, ENUM-GAP, ORACLE-GAP): re-classify on current src after step 2 (Summit-driven
   outcomes may surface new APPLY-DROPs = provider Gate-order losses -> report to USER, ledger decision 2).
   Example ORACLE-GAP repro: gi115 T4 `{land=Maze's End}` (engine taps Gond Gate + Maze's End#43).
6. Fill the doc; final report.

## Rulings in my brief (verbatim essentials)
Keep changes analyzer-side; play defects -> document with repro, do not change Maze DecisionProvider (another
session owns Maze play on wip/maze-f5r3). Oracle-only change must keep oracle-off play byte-identical (smoke 0
changed rows). ONE big process under claude_box.lock; small work via scripts/box_lane.sh (<=4 slots). Never
timeout/pkill -f. 9p clock lag: delete touched objects before building if mtimes lag. No shared-branch push.
Never decide USER policy. Hand off at ~250K.

## UPDATE 2026-11-20 21:45 UTC (second agent)
Done: b97f2ff0a (test fix; builds; both tap-choice tests pass ON, FAIL with MTG_ORACLE_TAP_CHOICE=0), 3232c489c (doc:
UNMODELED g21 10->0, g167 170->0, no ORACLE-GAP/MISS; unit 3780/3780; smoke 235 pass, 0 configs changed),
b05457892 (exact Pareto-prune speedup: perf of g30 T6 showed ~75% of the capped decision in ParetoCovers' string-map
lookups; merge walk + sum/positive-mask screen; g21 identical counters, oracle 1624 -> 516 ms).
Capped repros (tap-choice binary, before the screen): g30 T6 priced 189 s / oracle 1728 s / WALL-BP-CAP; g4 T7 142 /
1453 / WALL-BP-CAP; g14 T5 165 / 577 completes (2 plans CAPPED by the 768 MB board budget, MISS=3 = DOMINATED-LIFE).
Queued in the lane (all on bin/mtg.ps = b05457892): ps_g167 (identity vs new_g167), capps_g{30t6,14t5,4t7} (chain pid
3979046, cap.sh), unit2.log, gap_g115 (ORACLE-GAP repro, level 3). DONE markers in build/scratch/claude_maze_gate/:
ps.DONE capps.DONE unit2.DONE gap_g115.DONE. Lane was held by another session's 4-slot regression --both.

## UPDATE 2026-11-21 ~00:30 UTC (second agent, stopping)
All queued jobs finished; nothing running. Pushed wip/maze-gate at 58a990bcd. Results are in
docs/design/maze-oracle-gate-2026-11-20.md sections 1-5. Screen: identical counters wherever uncapped (g21, g167,
g14 T5); g167 oracle 101.8 -> 25.4 s; capped g30 T6 / g4 T7 still WALL-BP-CAP but audit 61 / 27 plans (was 14 / 4);
unit 3780/3780. Next, in order: (1) gate re-run on this branch (box lock); (2) residual capped cost: Realize's bucket
scan (21% self) -> index live frontiers by sum, then the census of a capped decision's 6-8M applies for a sound
collapse; (3) ORACLE-GAP gi115 T4 (continuation re-decides land drops; doc section 3).
