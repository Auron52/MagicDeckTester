# Single-consideration: where spells are considered multiple times, and the collapses

**USER directive (2026-08-14, verbatim):** "The main idea is to figure out where we are
considering spells multiple times and try to figure out a way to consider them only once."

This is the successor arc to main-phase classification (`main-phase-classification.md`), which
was step 1 of the same principle: the classification partition gives each spell ONE phase, so
the "cast now vs after combat" question stops being asked twice per turn. This document maps the
*remaining* multiplicity, with measurements, and records the collapses in order.

## The instrument

`MTG_CONSIDER_STATS=1` (default off, zero cost off; TurnSolver.cpp `namespace considerstats`)
attributes every `CollectActions` harvest to its **call-site context**, built from thread-local
markers: `solve`/`enum` caller, `m1`/`m2` phase, `.live` (executor-level, no search frame),
`.root` (committed-decision enumeration, `g_bp_root_enum`), `.fsN` (FSLine nest), `.m2solve`
(inside `SolveSecondMainInSearch`), `.bpcont` (breakpoint continuation enum), `.tranche`
(group-wave re-enumeration). Per site it counts calls, Actions emitted, per-card emissions, and
**distinct per-decision states** (BuildSimKey xor a decision epoch `g_decision_epoch`, bumped at
each committed-decision driver) — so `dup_calls = calls - distinct` is the honest fraction a
per-decision memo could collapse. Run one game, `--threads 1`.

## The measured map (one game each, seed 300001 gi=0, d3 budget 200ms, classify-family levers on)

**FiveColour** (deck_feeds_combat = true, no collapse): 1.68M harvests, **4.5M action
considerations in one game**. Dominant sites:

| site | calls | actions | distinct states | dup |
|------|-------|---------|-----------------|-----|
| solve.m2.fs3.m2solve (greedy 2nd main in rollout interiors) | 708k | 3.15M (70% of game) | 143k | **80%** |
| enum.m1.fs3 (rollout-interior enumerations) | 335k | 355k | 85k | 75% |
| solve.m1.fs3 | 288k | 335k | 115k | 60% |
| enum.m1.root (committed root) | 889 | 889 | 889 | 0% |

Maelstrom Archangel alone: 819k considerations at the one m2solve site. The root is already
single-consideration; the duplication lives in the rollout interiors, overwhelmingly the greedy
second main that every rollout ply re-solves.

**Hinata2** (deck_feeds_combat = false, total-Main2 collapse): 138k harvests, 86k considerations.
Same shape (solve.m2.fs3.m2solve 42k calls, **86% dup**), plus a collapse-specific finding:
**~51k harvests (37% of all calls) are main-1 harvests that return ZERO actions** — the filter
erases everything, but `CollectActions` still pays the full harvest first (and even those empty
harvests are 84% state-duplicates).

## Collapse #1 — greedy-Solve memo (`MTG_SOLVE_MEMO`, built 2026-08-14, DEFAULT OFF)

`TurnSolver::Solve` is a pure function of (state, phase): no draws, no budget, no cutoff
dependence. The memo (TurnSolver.cpp `namespace solvememo`; `Solve` is now a thin wrapper over
`SolveUncached`) caches the Plan per decision:

* **Key = `BuildBreakpointKey`** (BuildSimKey + floating mana + spells_cast_this_turn +
  casts_remaining), NOT BuildSimKey — that key's own history (a BuildSimKey-only first attempt
  silently changed Hinata's play) is exactly the mid-turn trap. One shared key, one place to
  keep exact.
* **Scope = one decision** (`g_decision_epoch`): the key's library digest (size + front) only
  implies content within a decision — the TranspositionTable's own scoping argument.
* **Search interiors only** (`g_cs_solver_nest > 0 || g_fsline_nest > 0`): live executor calls
  and human play bypass, so ship d0 configs are untouched by construction.
* **`MTG_SOLVE_MEMO_VERIFY=1`**: on every hit also recompute uncached and compare field by
  field.

**Validation so far (2026-08-14):** verify mode: 5c gi=0 **796,611 hits / 0 mismatches** (70%
hit rate), hinata gi=0 90,448 / 0 (82%). Memo on-vs-off: game logs byte-identical (modulo
runId); 5c wall 7.52 s -> 5.97 s (−21%) on one game. Suite smoke with memo on + full-battery
identity arms: see the session results below this line as they land.

## Ranked next collapses (not yet built)

2. **Skip the always-empty m1 harvest on total-Main2 decks** (~37% of hinata's harvest calls).
   Exactness condition: with `deck_feeds_combat == false`, no haste access and no scaling
   attacker (the same inputs the classifier reads), every class maps Main2 — but a provider
   `MainPhaseOverride` returning Main1/Both would break a blanket skip, so the skip must be
   derived per-hand from the real classifier, or simply left to the memo (a hit returns the
   empty plan without harvesting; covers all but the ~16% distinct states).
3. **Enum-side memo**: `EnumeratePlans` in rollout interiors (enum.m1.fs3: 335k calls, 75% dup).
   The bp-enum cache (`EnumerateBreakpointPlans`, same key) is the proven pattern; heavier
   values (plan vectors), so measure before building.
4. **Land-fan sharing**: `EnumeratePlansWithLand` re-enumerates the full spell odometer once per
   land candidate (5c: 78k host calls -> 335k inner enums, ~4.3x). The user's land-after-draws
   TIMING doctrine (recorded in `main-phase-classification.md`, unimplemented) removes part of
   this fan at the root by construction: plans with affordable draws emit ONLY the defer
   variant, and the land is chosen once, post-draw.

## Status

* Instrument + collapse #1 built on branch `phase-1-2-deck-analyzer`, both DEFAULT OFF.
* Adoption path for #1: verify-mode sweeps over more seeds/decks, suite smoke + battery
  identity (zero diverged games required — this is a pure perf change, identity is the bar),
  then default-ON + `MTG_NO_SOLVE_MEMO` hatch. No GT rebaseline needed if identity holds.
