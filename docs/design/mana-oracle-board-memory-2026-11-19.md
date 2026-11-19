# Mana oracle: the board-memory runaway on Maze's End (2026-11-19)

Branch `wip/maze-oracle-mem`. Follows `mana-oracle-memory-time.md` (the 2026-11-05 layer-build fix and the
2026-11-06 time fix). The oracle is a checker: play never reads it, and every number below confirms play is
byte-identical.

## 1. Mechanism

The mana_clean gate's oracle arm (`MTG_MANA_ORACLE=2`) on Maze's End passed the 40 GB RSS cap in about 25 s at 32
workers, and a single game passed 8 GB on one thread. `Realize` (src/ai/ManaOracle.cpp) holds a full `GameState`
(about 21 KB on a Maze's End board) for every OUTCOME and every FRONTIER of the payment search. The apply ceiling
(`MTG_MANA_ORACLE_NODES`, CAPPED retry `_RETRY` = 400,000) and the priced decision budget bound TIME, not memory.
Under the 400,000-apply retry one plan's search held about 100k of each. For example, seed 30000000 T6 m1
{Heap Gate + Sylvan Scrying + Burgeoning + Manabond} held 97,402 outcomes and a 99,839-board frontier layer:
about 3.9 GB on one thread. The 2026-11-05 fix bounded the script layer, not the boards.

The boards are overwhelmingly duplicates. In the searches the new budget stops, a typical one holds 34,083
outcomes over 346 distinct core boards (3 canonical ones).

## 2. Fix

1. **Board memory budget, `MTG_MANA_ORACLE_BOARD_MB`** (default 768 per thread, 0 = unbounded). Every board a
   `Realize` holds is counted per thread (`BoardAcct`). This covers the outcomes, the frontiers and the capped
   pass's saved cursor. The count is a deterministic estimate from the board's contents, so verdicts depend on
   the game alone. A search whose thread passes the budget stops as **CAPPED**, with text naming the memory
   budget. Its escalation retry is skipped, because the same boards would stop it again. Nothing is dropped
   silently: the plan's complete side is reported as partial, exactly as at the apply ceiling. A summary line
   `=== MANA ORACLE board memory: ...` counts the stops.
2. **Online Pareto prune (exact).** The frontier key and resources are computed when a frontier is captured. A
   dominated frontier's board is released at once instead of after the whole layer. The kept set is exactly
   `ParetoKeep`'s (the argument is in the code comment). `MTG_ORACLE_PARETO_ONLINE_CHECK=1` recomputes
   `ParetoKeep` per layer and aborts on any difference.
3. **Hit-only release (exact).** In a hit-only search, a non-hit, non-Clean outcome's board is given back
   (`Outcome::released`; `Clean()` stays false). The board is kept whole under `MTG_ORACLE_ORDER_FOLD_CHECK`.
4. **`MTG_MANA_ORACLE_MEM_TRACE=<n>`** (diagnostic, off by default). It prints, for each large `Realize`:
   outcomes, peak frontier and kept counts, distinct outcome boards, held bytes and RSS.

## 3. Measurements

Single game, one thread, oracle 2. The old runs used the old code under an 8 GB cap; the fix runs used the fix
under a 3 GB cap with the online Pareto check ON. "killed" means the run passed its cap.

| gi | old RSS | fix RSS | digest |
|---|---|---|---|
| 0 | killed (> 8 GB) | 1.60 GB | (old never finished) |
| 1 | 1.97 GB | 0.56 GB | same |
| 2 | killed | 1.62 GB | |
| 3 | killed | 2.39 GB | |
| 4 | killed | 1.97 GB | |
| 5 | 0.89 GB | 0.69 GB | same |
| 7 | killed | 1.93 GB | |
| 10 | 2.84 GB | 1.19 GB | same |
| 11 | killed | killed at 3 GB (the engine plan cache was 2.3 GB of it; it completes in the pooled run) | |
| 12 | 3.92 GB | 1.72 GB | same |
| 13 | 6.49 GB | 1.29 GB | same |
| 33 | 8.88 GB | 1.58 GB | same |
| 35 | killed | 1.66 GB | |
| 41 | 2.33 GB | 1.46 GB | same |

No `PARETO-ONLINE MISMATCH` in any repro, nor in the full unit suite run with the check on.

**Verdicts are exact where the budget is not hit.** gi 1 and 5 print the same oracle lines on the old and the
fixed binary. gi 10 differs only by one CAPPED: an escalation retry the old code finished at 2.84 GB RSS is now
stopped by the budget. That is the budget's cost, and it is reported, not hidden.

**Pooled run**: Maze's End gi 0-199, one `mtg --batch`, 32 threads, `MTG_RSS_CAP_GB=40`, oracle 2. All 200
games finished in 38.6 min, with a heartbeat of 32 of 32 workers busy. Peak RSS was 27.4 GB. 560 searches were
stopped by the budget; 43.3M dominated frontier boards and 557k hit-only outcome boards were released early.
Before the fix this batch died at the 40 GB cap in about 25 s.

**Play is unchanged.** Per-game digests are identical between oracle 2 and oracle 0 on the fix (200 of 200).
Oracle 0 on the fix is also identical to oracle 0 on the base binary (PGO build of `HEAD:src` d49f1f0b), 201 of
201 games, Merfolk Turns gi 13 included.

**The 768 MB default fits.** 32 x 768 MB (24 GB) plus the engine's ~5 GB is under 40 GB; measured, 27.4 GB. The
estimate undercounts the real heap by roughly a third: a single game holding one capped search peaks at
1.1-1.6 GB RSS.

## 4. Not covered: Merfolk Turns seed 30000013 gi 13

This game still dies under oracle ON: 7.7 GB in 5 s on the fix and on the old binary alike. It also dies on the
merfolk-mem branch binary (a7d0ac85b, the legend-keep lazy-fan fix): 10.3 GB in 12 s. With the oracle OFF it
plays in 0.67 s and 51 MB. A backtrace at 3 GB shows the memory is NOT in `Realize`:
`manaoracle::AuditDecision -> TurnSolver::EnumerateMainPlans -> EnumeratePlansWithLand ->
EnumeratePlansWithLandUncached -> PbAxesAndTwins -> AppendSubdecisionAxes -> Plan(const Plan&)`. The oracle's
own call into the plan enumerator materialises a huge subdecision-axis product on the T6 main-1 board. A play
search does not, presumably because it goes through the lazy or pruned route. This is a separate defect, owned
by the plan-enumeration side.

## 5. Next lever (not built)

Outcome boards are duplicated about 100x in the capped searches (34,083 outcomes over 346 distinct core boards).
Sharing identical outcome boards would remove most of the 560 budget stops and the CAPPED verdicts they produce.
It must be exact: consumers read the full board and `o.script`, so the test has to be full-state equality, not
`CoreKey`.

## 6. Expected effect on the tiers and the gate

- **Smoke, regression, overnight:** the oracle is off in the tiers, and every change is under the oracle's
  checker path (play never reads it). Expected byte-identical GT; the base-vs-fix oracle-OFF identity above is
  the evidence.
- **mana_clean:** Maze's End now runs to completion instead of aborting at the RSS cap. Its plans that used to be
  uncomputable now report CAPPED (memory budget), so the deck fails the gate honestly on those plans rather than
  not finishing. Merfolk Turns still aborts on gi 13 (section 4) until the enumeration defect is fixed.

## 7. Integration with the Goblins goal fold (branch `wip/maze-oracle-mem2`)

The fix was validated on an old base. The shared branch then gained the goal fold (`MTG_ORACLE_GOAL_FOLD`, default
ON) and the `MTG_ORACLE_FOLD_STATS` diagnostic in the same `Realize`, so the fix was re-landed on top of them.

**How the two compose, exactly.**
- The online prune never erases a frontier. It marks it `pdrop` and gives back its board and script. Every
  index-parallel vector (`fs_fk`, `fs_fbad`, `fs_ftbad`) is pushed in the same capture block as `fronts`, so the
  indices stay aligned. `goal_cands_scripted` is a field of the frontier record, computed at capture before
  `admit`.
- The kept set is ParetoKeep's (`keep[i] == 0` exactly when `pdrop`). The goal fold's skip runs only on kept
  frontiers, after `keep`, as before. It reads only kept boards, and those are never released.
- The goal fold is off for a hit-only realisation (`goal_k` is null there). So its readers (`RealizationCovers`,
  the check's `on_key`) never see a released outcome board.
- The FOLD_STATS per-layer block now reads `pkey` and `pres` in place of the removed `keys` and `res`. It skips a
  released frontier before reading the board. That frontier's `fs_fbad` and `fs_ftbad` are never read, because
  only kept frontiers are expanded.
- The board budget's retry skip (`!r.mem_capped`) is inside the goal fold's `realize_main` escalation.
- **Memory hygiene added during the integration:** `realize_main` frees the previous pass's realisation (the folded
  one, before a redo) before it realises again. `MTG_ORACLE_GOAL_FOLD_CHECK` keeps only the folded pass's on-key
  scripts and its CAPPED bit, not the whole realisation. So a stale realisation's boards do not count against the
  budget of the pass that replaces it.

**Verification.** All runs were on fb9cb5e03 + this fix, under one box-lock hold: Maze 23 threads, Goblins 8,
units 1.
- Units: 3717/3717 pass, both plain and with `MTG_ORACLE_PARETO_ONLINE_CHECK=1` plus `MTG_ORACLE_GOAL_FOLD_CHECK=1`.
- **Maze's End gi 0-199**, oracle 2, Pareto check on:
  - 0 mismatches.
  - 200/200 digests identical to the validated run (section 3).
  - Peak RSS 22.8 GB at 23 threads.
  - 617 memory-budget stops (560 before), with 107.5M applies against 96.6M.
  - The goal fold was active on 26,824 plans.
  - Both runs are dominated by the 600 s wall backstop (4,204 and 4,389 CAPPED-DECISION). That backstop moves
    with timing, so the counts are not comparable one for one.
- **Goblins 42-game pool**, with both checks on:
  - 0 Pareto mismatches and 0 goal-fold mismatches (719 plans checked).
  - Digests identical to the goal-fold-only run.
  - Every flag line that differs from that run's check arm is on a plan the board budget capped (31 such plans).
- **Goblins 42-game pool**, plain (the goal-fold-only run's config):
  - 18 memory-budget CAPPED, where that run had 0. These are seeds 30133979 T6 and 30131479 T3. That run had
    finished them under the 400,000-apply retry.
  - The DOMINATED-LAND witnesses on those plans change, because the search behind them is partial.
  - Two APPLY-DROP lines on 30131075 T2 m2 are missing. That decision hit the 600 s **wall** backstop here (the
    game ran 604 s against 453 s). The cause is contention: this run shared a saturated SMT box with the Maze pool,
    where the reference ran nearly alone. Maze's total CPU is unchanged (41,395 against 41,224 user-s).
  - Applies 14.9M against 17.6M; peak RSS 4.7 GB at 8 threads.
- Smoke: ALL PASS, 0 configs changed.

**Open question for the owner of the gate.** The 768 MB budget now caps Goblins plans that the retry used to
finish. A board-sharing lever (section 5) would remove most of those stops. So would a larger budget, if thread
counts allow it.
