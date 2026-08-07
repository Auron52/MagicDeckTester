# Deferred: generic plan-enumeration product backstop

**Status: DEFERRED (2026-08-07).** Not being built now; parked here per the deferred-work rule.

## Problem

`TurnSolver::Solve` / `EnumeratePlans` walk a mixed-radix odometer whose position count is
`prod_g(1 + |group_g|) * 2^|independent|`. The walk is not budget-interruptible: the
`SearchBudget` node budget is only consulted *between* rollout nodes, so a single Solve call
whose product is astronomical runs to completion no matter the budget. FiveColour proved the
failure mode: one analyzer scoring game sat ~3.4 h single-threaded inside one call chain
(bound ≈ 7.4e7 per call, thousands of calls in the FSLine recursion), silently stalling the
whole run on a thread-pool join. ~1.5 % of that deck's games were degenerate.

## What was actually shipped instead (2026-08-07)

Structural collapses in the partition passes (see `PlanGroupKey` in `TurnSolver.cpp` and the
FiveColour ledger's "Stage 4 DEGENERATE ATOM" entry): mutually-exclusive action families
(activation kinds keyed by source, free-cast variants keyed by bank slot, >=2-variant
SacForMana sources) become odometer *groups* (k+1 states) instead of independent bits (2^k).
That removed the observed atom class entirely (worst observed bound after: < 1e6; the 3.4 h
game now takes 4.5 s). Byte-identical for all existing decks (smoke + fingerprints).

## The deferred idea

A *generic* backstop for shapes no grouping fixes (e.g. a future deck with 20 genuinely
independent actions):

- Compute `bound` (double) after the partition passes — the code to do this already exists in
  `ReportEnumBound` (the `MTG_ENUM_STATS` instrument, inert by default; `MTG_ENUM_STATS_MIN`
  sets its watermark).
- If `bound > CAP` (env `MTG_ENUM_PRODUCT_CAP`, default high, e.g. 1e8): deterministically
  SHRINK the action set until under the cap — drop the lowest-`SituationalCardRank` group or
  independent bit first (reuse `CapGroupsBySituationalRank`'s ranking), never mid-walk
  truncation (positional bias, non-obvious determinism). Log per the no-silent-caps rule.

## Why deferred

- The shipped structural fix removed every *observed* atom; the backstop had no live test case.
- The risk is real but bounded: a cap that fires changes play for that hand, so its default and
  its drop policy deserve an A/B pass (regression fingerprints + a degenerate-hand corpus),
  which is out of scope mid-deck-onboarding.
- The diagnosis tooling (`MTG_ENUM_STATS`) is already in-tree, so a future hang can be
  shape-captured in minutes rather than re-derived from gdb stack samples.

## Pointers

- FiveColour atom autopsy + fix details: `docs/design/analysis-FiveColour.md` (Stage 5
  verdicts, "Stage 4 DEGENERATE ATOM").
- Dragonstorm precedent for targeted collapses: `docs/design/dragonstorm-mulligan-tractability.md`.
- Repro method: `MTG_DUMP_WINS` straggler sweep -> single-game `--seed <base+gi> --game-index
  <gi>` repro -> `MTG_ENUM_STATS=1 MTG_ENUM_STATS_MIN=1e6` shape capture.
