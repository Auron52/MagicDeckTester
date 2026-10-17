# EDF generation shape: breakpoint-continuation and enumeration cost (2026-10-17)

Companion to the EDF generation-cost investigation (route pre-gate, ETB-counter fold, payability supply
gate). Scope here: the breakpoint-continuation path (`BpEnumEntryFor` / `BpDeriveContinuationList`) and the
enumerators (`EnumeratePlans`, `SolveUncached`). Probe: the mulligan comp scorer at the generation shape
(`MTG_SCORE_HANDS=24 MTG_SCORE_R=4 MTG_EQUIV_DEPTH=1 MTG_SCORE_BUDGET_MS=1 MTG_DECISION_WORK_X=1000`, seed
424242), 12 threads.

## Census (print-only counters, all default OFF)

`MTG_BP_USE_CENSUS`, `MTG_ENUM_MEMO_STATS`, `MTG_EDF_RANGE_STATS` (added with this work), plus the existing
`MTG_PAY_CENSUS`, `MTG_BP_ENUM_PROBE`, `MTG_BP_KEY_CENSUS`, `MTG_ROLLOUT_STATS`.

* **Continuation lists: 35,525 lookups, 17,153 derivations (89 per rollout), every derivation a distinct
  key.** `MTG_BP_KEY_CENSUS` distinct = misses, no clears: the cache loses nothing to eviction, and exact
  repeats inside a decision are already served (18,372 hits). The folds split only 1,120 boards (17,153
  full keys vs 16,033 board-only keys).
* **By consumer:**

  | consumer | derivations | plans / list | Gcyc in derivation | read afterwards |
  |---|---|---|---|---|
  | eligible wave variant (`bp_choice` apply) | 16,857 | 3.2 | 7.7 | 2.1 reads / list |
  | canon (`BpUnbranchedCanon`) | 286 | 366 | 12.1 | only `front()`; 30 of 286 ever read by a branching consumer |
  | arm-new stillborn measure | 10 | 4,173 | 15.1 | only the LENGTH (`== 0`?) ; never read by a variant |

  So 296 derivations (1.7%) cost 27 Gcyc, 3.5x what all 16,857 eligible derivations cost.
* **enummemo: 23 hits / 8,700 misses** (116 promotion visits). It cannot see the continuation path at all
  (17,153 calls bypass it on `g_bp_enum_depth`, 884 on `g_bp_root_enum`); the states it does see almost
  never recur inside a decision. Not a key-fragmentation or cap problem.
* **Derivations charge no budget units at this shape**: `MTG_NO_BP_ENUM_CACHE=1` gives identical
  `work_units` and identical probe stdout.
* **EDF range projection (`EdfRangeAurasBehindUntappers`, 14% inclusive)**: 2.59M calls, 1.21M reach the
  candidate loop, 4.64M candidate orders, 4.08M walks, 303k moved / 905k unchanged.
  `OrderPrepaysWholeTurn` returned true 8 times in 1.44M calls.
* **Pay census**: `OrderPaysSequentially` 3.21M exact repeats of 6.24M inside a decision (3.15M on the
  same GameState object). `SubsetPayableWithFilters` 2.28M "repeats" of 17.0M, but almost all are
  interchangeable COPIES at different candidate indices, not the same question (an index-keyed memo
  found 400 hits in 5.1M calls).

## Adopted (each default ON, `=0` restores the old path, verify mode where applicable)

| lever | what | probe instr (marginal) | identity batch instr (marginal) |
|---|---|---|---|
| `MTG_PAY_PROBE_MEMO` | order-probe memo scoped to one enumeration's (state, cands) | -0.7% | -0.7% |
| `MTG_ENUM_AURA_SORT_SKIP` | skip the two per-plan Aura ordering stable_sorts when no candidate keys 1 | -4.5% | -3.2% |
| `MTG_PLAN_SORT_BY_INDEX` | stable-sort plan lists by index, move each Plan once | -2.0% | -1.5% |
| `MTG_ENUM_PLAN_RESERVE` | reserve each emitted plan's action vector | -0.9% | -0.7% |
| `MTG_EDF_RANGE_PREFIX_FAIL` | a failed walk prefix refutes every later candidate order sharing it | -0.9% | -3.2% |
| all five | | -9.4% | -11.2% |

Identity: EDF d0 x1000 / d3 b10 x100 / d5 b20 x60, Hinata2, Bruna d3 b10 x40, Snow, KittyEquipment d3 b10
x30 (seed 2002): digests, units, per-game `.wins` and per-game `.units` identical for all-on, all-off and
each lever off alone. Verifiers on the same batch: pay-memo 0, aura-sort 0 of 25.4M, plan-sort 0 of 1.09M,
prefix 0 payable of 14.9M skipped walks.

## Deferred

1. **The arm-new stillborn measure derives the whole continuation list to read whether it is empty**
   (`ApplyPlanDirect`, "WAVE 0's STILLBORN TEST"). Its huge lists are breakpoints where a LAND arrived:
   every continuation that plays it is "new", so nearly nothing is filtered. `ArmNewDupAll` reads only
   `g_bp_base_lens[0] == 0` (and `g_bp_base_dlen0` in that case alone). The header's "n > 0 costs nothing
   extra, the variants fill this entry" did not hold at the generation shape: none of the 10 entries was
   ever read by a variant. Options, none byte-identical by construction: (a) an early-exit enumeration
   that stops at the first survivor of the new-only filter (the filter must stay the one authority -- the
   header's lockstep warning); (b) defer the measurement to the end of the apply and derive only when
   `ArmNewDupAll`'s other conditions hold (needs the derivation context the site binds, which dies with
   the site).
2. **Canon derives a 366-plan list to take `front()`.** A top-1 derivation would still evaluate every
   subset (the order needs every value), so the saving is the emission / signature / dedup / copy share,
   and 10% of these lists are later read in full. Any change to which lists are derived also moves the
   cache's clear-on-full timing; `MTG_BP_ENUM_VERIFY` has measured a CONTENT-DIFF residue on hits
   (0.0079%), so a different hit pattern is not provably play-identical and must be checked per game.
3. **`plan_signature` (the EnumeratePlans dedup key) is ~3% of the probe** in string building (seven
   vectors of strings, sorted and concatenated, for ~2.1M emitted plans). An append-based builder with a
   verify mode comparing it to the current one is a safe follow-up.
4. **`SubsetPayableWithFilters` repeats are copy-equivalence, not identity.** The lever there is the
   interchangeable-copy fold (the enumerator's equivalence classes), not a memo.
5. **The analyzer's comp scorer (`MTG_SCORE_*`) never calls `TurnSolver::ClearPerGameCaches`**
   (`AIEngine::RolloutKeepWinTurn` does not; only the game-start path in AIEngine does), so the thread_local
   plan caches survive from one scored rollout to the next on a worker, and the work-stealing item order
   decides which rollouts share a worker. On this probe no derivation key ever repeated across rollouts, so
   output was stable, but it is the cross-game-state shape `docs/design/batch-run-to-run-nondeterminism.md`
   forbids.
