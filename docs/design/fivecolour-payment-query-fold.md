# Making the FiveColour mulligan run feasible: fold the payment QUERIES

2026-08-16. Self-contained; read this and `fivecolour-mulligan-and-slow-atom.md` §5 and you can start.
**Status: measured and scoped. NOT implemented — step 0 below is deliberately unfinished.**

## The whole problem in one ratio

At K=27 (fetch cycle merged, already by-construction in `DiscoverEquivalence`) the size-7 phase is
1,977,898 cells. At R=10 — the floor for a shippable runtime profile — that is 39.6M rollouts.

| rollout rate | wall clock (23 cores) |
|---|---|
| 110/s/core (the skill's guide) | **~4.3 h** |
| 4.8/s/core (FiveColour, measured) | **~100 h** |

Neither K nor R needs to change. **Close the rate gap and the run lands in hours.**

## What the gap is made of (measured at HEAD, 12 games, seed 1001, `--threads 1`)

| deck | payment entries/game | nodes/game | nodes/entry |
|---|---|---|---|
| FiveColour | **5,567** | 224,625 | 40.3 |
| Goblins | **6** | 12 | 1.9 |

~930x more payment questions; only 21x more expensive per question. And the cache is NOT leaking —
`miss=66,804` equals `top-level entries=66,804` exactly, at a 91.7% hit rate. So these are 5,567
**genuinely distinct** questions per game, which no cache can absorb.

This also retro-explains the parked flow-guided tap order (`flow-guided-tap-order.md`): it cut
backtracker nodes 13.6x and moved runtime ~1% on BOTH workloads. It made each answer cheaper while
the engine went on asking a thousand times more often. **Do not re-attempt cost-per-answer work here.**

## STEP 0 — ATTRIBUTE THE 5,567 BEFORE DESIGNING ANYTHING

Do not skip this. Twice on 2026-08-16 a confident causal story about this deck was written up and then
refuted by a five-minute measurement (see `hypothesis-before-measurement-mirrorwing` and the CORRECTION
headers in the two sibling docs). The design below is contingent on what step 0 finds.

`BatchPrepayMainCasts` (TurnSolver.cpp ~8879) ALREADY folds a whole plan's casts into ONE combined
cost and a single `TapForCostBacktrack` (~9002/9019), and it is called once per applied plan
(~11002). So there are two very different worlds:

* **(a) The calls are per-PLAN prepay.** Then the fold already exists and 5,567 just means "5,567
  plans applied per game". The lever is then search/enumeration BREADTH, not payment at all, and
  everything below is the wrong tree.
* **(b) The calls are per-SUBSET / per-cast.** Then the fold is missing at the enumerator and the
  design below applies.

Cheapest attribution: add a thread_local counter per call site (the `TapForCostShared` public entry in
ManaPayment.cpp ~651, the two `BatchPrepayMainCasts` backtracks, and the per-cast fallback), print
under the existing `MTG_TAP_STATS` dumper, and run the 12-game FiveColour command above. One build,
one run. Note `MTG_SEL_MANA_GATE` is **default OFF**, so the enumerator's selection-exact gate is NOT
the source.

## The design, IF step 0 says (b)

Answer one stronger question per enumeration instead of thousands of per-subset ones.

Today each candidate subset asks "can this board pay THIS cost, with this tap-state and floating?".
The board is invariant across the whole enumeration (a payment only TAPS; nothing enters, leaves, or
changes colour — the same invariant the mana-cache key already relies on). So the enumeration could
compute ONCE a compact description of what the board can pay, and answer each subset from it.

The right shape is the flow oracle's, generalised from a yes/no on one cost to a *frontier*: the
max-flow in `TapFlowInfeasible` (SpellEffects.cpp) already models sources → colours → demand exactly
and has **zero bail clauses** on all 12 suite decks (verified 2026-08-16), which is what makes a
precomputed answer trustworthy. Candidate frontier: per colour-set, the max total mana extractable —
i.e. fold on the REALISED COLOUR SET rather than the source permutation, which is the direction both
independent measurement paths pointed at (slow-rollout ranking and the depth-matrix perf profile).

Soundness bar, non-negotiable: the frontier must be an EXACT decision or a conservative OVER-estimate
that then falls through to the real solve. An under-estimate silently drops legal casts — the exact
failure mode the domain-source under-count caused before (`60b56ae1`, the FiveColour claude-play
sweep's 14/18 games).

## Acceptance

* **Byte-identical** is the bar if the frontier is exact — it only skips questions whose answer it
  reproduces. Verify with smoke + regression digests, no rebaseline.
* If it is not byte-identical it is a heuristic and must clear BOTH seed sets summed, per
  `heuristic-optimization.md`. (The board-lethal short-circuit cleared at −0.1563; the flow-order tap
  order failed the holdout after looking good on smoke. Only the sum decides.)
* The number that matters is **rollouts/s/core on the gen workload**, not play wall-clock. Re-measure
  the 12-game entries/game table above; it is deterministic and immune to load, unlike wall time
  (see `thread-wall-timings-and-misdirected-logs`).
