# TapForCostBacktrackWorker exponential blow-up on wide boards (open defect)

## Observation (2026-08-11)

An orphaned Mirrorwing probe (`mtg "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" --games 100
--seed 5005 --depth 3 --budget-ms 200 --max-turns 8` — the shape of an overnight-classify 4x
re-run) sat at 99% of one core for **~14 hours** inside a single game. `gdb` backtrace: the
worker thread was recursing through `TapForCostBacktrackWorker` (ManaPayment.cpp) —
`CanTapNow` at the leaf, alternating worker/lambda frames all the way down — i.e. the mana
payment BACKTRACK solver itself, not the plan odometer and not the group waves.

The process predated the day's engine changes (launched 09:42, binary built before the
Gold-Rush breakpoint work), so this is a **pre-existing, latent** blow-up.

## Mechanism (hypothesis, unverified)

The backtracker explores per-source tap alternatives with a memo
(`TapBacktrackMemoHash` over a (mask, pool) pair). On a fanned-out Mirrorwing board — dozens
of near-identical sources (Treasures from copied Gold Rushes, dork chains) with a large
multi-pip target (a strive surcharge / batch pre-pay combined cost) — the alternative space is
exponential in sources, and the memo apparently fails to contain it (distinct masks over
interchangeable sources hash as distinct states). A budgeted search does NOT bound this: one
`TapForCost` call has no node cap and no budget check, so `--budget-ms` cannot save the game
once a pathological payment solve starts.

Why it surfaces at raised budgets: bigger per-decision budgets let the search reach/score wider
boards (more fan-out copies resolved in sim), whose payment solves are the pathological ones.
Suite budgets (b10/b20/b50) have not hit it; 4x/16x classify re-runs and unbounded audits can.

## Fix directions (when picked up)

- **Interchangeability collapse**: canonicalize identical sources (same name/produces/state) so
  the memo key is a multiset count, not a per-permanent mask — the same lesson as the plan
  odometer's activation-family grouping (k+1 states instead of 2^k).
- **Node cap + fallback**: a hard step cap on the backtracker; on overflow fall back to the
  greedy single-pass payment (payment completeness is a quality nicety, never worth hours).
- Repro to find first: instrument a step counter (`MTG_TAP_BT_PROBE`) and re-run the command
  above to identify the game index and board shape.

## Status

OPEN — recorded on discovery; not scheduled. Cheap mitigation if it bites a run: kill the
stuck game's process; suite budgets are unaffected so far.
