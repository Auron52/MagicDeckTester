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

## Second sighting (same day, 2026-08-12)

During the Mirrorwing SituationalCardRank sweep, one variant (Gold-Rush-high group-cap
ordering, throwaway scaffolding) flipped one game's plan shape and turned it into a
**286-second game** (`SLOW-GAME` at d5 b20, seed 5060 gi55 — ~100x normal wall). A mid-run
`gdb` backtrace showed the worker pinned in the same `TapForCostBacktrackWorker` recursion.
Two independent sightings in one day, both Mirrorwing wide boards, both budget-immune
(payment solves have no budget check). The deterministic repro died with the deleted
scaffolding; to re-find one, instrument a step counter (`MTG_TAP_BT_PROBE`) and run
mirrorwing suite cases at 4x budget.

## Fix (2026-08-12): identical-sibling collapse — ADOPTED

Implemented the interchangeability collapse as a **sibling symmetry break inside the
worker's candidate loop** (`s_dup_of_buf` in `TapForCostBacktrackWorker`,
SpellEffects.cpp): at the top-level call, each candidate is chained to the nearest earlier
candidate with the same `CardDefinition` and the same payment-relevant permanent state
(counters, storage battery + hold flag, unreserved, untapped-at-entry, dork
tap-eligibility — `CanTapNow` is invariant during a payment, so it is folded into the
chain once). At any node, if a chain member is currently untapped, it was explored earlier
at that node and failed, so the candidate's subtree is isomorphic to a proven failure and
is skipped; members tapped by an ancestor on the DFS path are walked past. This collapses
the reachable tapped-set space from 2^k toward (count+1) per identical class — the plan
odometer's activation-family lesson applied to payments — and, unlike the memo, it also
works on n>64 boards (where the memo is disabled; the b200 repro saw n=84).

**Byte-identical** (only failure-isomorphic subtrees are skipped, so the first payment
found and the exact sources it leaves tapped are unchanged): verified by a 100-game
single-threaded on/off diff (`MTG_NO_TAP_DUP_COLLAPSE=1`, identical output) and a full
smoke suite vs GT (33/33 PASS, digests unchanged, 0 play-changed).

**Measured**: the original 14-hour command (`mirrorwing s5005 d3 b200 x100`) now completes
in minutes (135,706 whole-subtree skips; worst residual game 123 s, gdb-confirmed to be
plain wide search at 4x budget, not payment). `MTG_TAP_STATS=1` prints a
`DUP COLLAPSE: identical-sibling skips=` line; `MTG_NO_TAP_DUP_COLLAPSE=1` is the
standing same-binary A/B lever (must stay byte-identical — a digest diff is a bug).

Residual: mixed-class boards (many near-identical but not def-identical sources) still
multiply as (count+1)^classes; no sighting of that being pathological yet. A hard node
cap + greedy fallback remains the documented next lever if one appears.

## Status

FIXED (identical copies) — sibling collapse adopted 2026-08-12; see above. The
node-cap fallback direction stays unimplemented until a residual sighting demands it.
*(2026-09-03: the residual DID appear — Mirrorwing, 2026-08-16, tap-backtrack-mixed-class-sighting.md
— and was closed by the source-count gate fixes 2c6bc641/f990bcfe, not by the node cap, which
remains unimplemented and no longer needed.)*
