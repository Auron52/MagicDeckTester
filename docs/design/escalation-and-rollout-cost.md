# Escalation / heuristic-rollout cost: levers, measurements, and the telemetry to diagnose them

**Status:** partial results shipped (gated, byte-identical off); the big lever is still open.
Self-contained so any agent/session (esp. one fighting budget-starved escalation on a deck with a
value-leaf, e.g. Hinata) can pick it up without re-running the dead ends.

Companions: `learned-d0-policy.md` (the value-leaf hybrid), `model-performance-levers.md` (the lever
menu), `overnight-audit-2026-07-11.md` (the escalation remaining-budget regression),
`anytime-search-budget-prediction.md` (spend a given budget better).

## The problem

The value-leaf hybrid (`FullSearchLineHybrid`, `TurnSolver.cpp`) runs a cheap value-leaf search, then
for an **unverified** line committed below `value_trust_depth` **escalates**: it re-runs the full deep
search with the exact heuristic leaf (`SimulateToEnd` rollouts). Two pains:
- **Escalation dominates wall-time** where it fires (measured d5: TH ~63%, burn ~83% of search time),
  because it re-runs the whole search with the expensive rollout leaf.
- **Budget starvation:** the escalation spends only the *remaining* shared budget, so under a tight
  budget it can't finish and the value-leaf line stands unescalated → a play regression. (This is the
  concrete Hinata symptom on the branch that gave Hinata a `value.json`.)

## The measurement that reframes it (do this FIRST on your deck)

Use the **deterministic** rollout-cost counter, NOT wall-clock (a shared machine makes wall-clock lie —
we saw "17–35% speedups" that were pure contention noise, and even *inverted* orderings):

```
MTG_VALUE_MODEL=1 MTG_ROLLOUT_STATS=1 build/Release/mtg decks/<deck> \
    --games 120 --seed 1001 --depth 5 --max-turns <mt> --lookahead-bottoming --threads 12
# -> [rollout-stats] calls=<N> turn_steps=<S> steps_per_call=<S/N>
```

What we found (d5, 120g, s1001):

| deck | SimulateToEnd calls | steps/call | truncation headroom |
|------|--------------------:|-----------:|---------------------|
| TH   | 10.0M               | 1.12       | meaningful (rollout-heavy) |
| burn | 1.46M               | 1.47       | small |
| antilife | 2.2K            | 1.90       | ~none (barely rolls out) |

**Key insight:** rollouts are *short* (~1–2 turns), not long play-to-the-end. Rollout cost is dominated
by the **COUNT** of numerous short rollouts (TH's per-turn dig search spawns ~10M), not by rollout
length. So any "truncate the long tail" lever has limited headroom, and the win is entirely a function
of how rollout-heavy the deck is. **Profile your deck before picking a lever.**

## Levers, with verdicts

### 1. Truncated rollout + value-leaf tail — SHIPPED (gated), lossless, modest, deck-dependent
`MTG_ROLLOUT_HORIZON=K` (default -1 = off/unlimited): after K simulated turns with no win yet,
`SimulateToEndImpl` caps the tail with the O(1) value-leaf estimate instead of playing to game end.
Bridges the pure value leaf (K=0) and the full rollout (K=∞). Only caps when a value model is attached
(value-less decks are byte-identical).
- **Lossless at K=2** on TH and burn (8/8 seed×deck, identical LP; deterministic).
- Work saved (rollout turn-steps): **TH ~23%, burn ~9%, antilife ~0** at K=2 (K=3 barely helps: ~3%).
- So: a real but *modest, TH-shaped* win. Worth turning on for rollout-heavy decks; useless for light
  ones. Validate on YOUR deck with `MTG_ROLLOUT_STATS` + a multi-seed LP check before adopting.
- Adopt per-deck (archetype provider / a profile knob), not the root, once validated.

### 2. Confidence-gated escalation — deck-dependent, needs a model, quality risk
Skip escalations predicted to be *no-ops* (the heuristic re-search won't change the pick). Dataset via
`MTG_ESCALATION_DUMP=<path>` (one row per escalation: taken, wt_changed, turn, committed, gap, est_wt,
+46 midgame features); analyze with `scripts/esc_analyze.py` (logistic + rank-AUC + precision-at-skip).
- **TH: real** (AUC 0.844 vs 0.718 depth-only; skip 40% of escalations at 4.7% false-neg).
- **burn: dead** (AUC 0.49 = chance; its no-op escalations are irreducibly unpredictable).
- So a per-deck lever, not universal; and it *skips* escalations (quality risk) rather than cheapening
  them. Not a fit for budget starvation where you still want the escalation's answer.

### 3. Warm-start B&B cutoff — DEAD (measured), reverted
Seed the escalation's B&B with the value-leaf's committed win turn as an initial cutoff. Measured
**subsumed by `MoveOrderPlans`**: ceiling ~9% on TH (and unsound at that — the value-leaf estimate is
optimistic and over-pruned 2 wins); a *sound* cutoff prunes strictly less. The incumbent already
tightens within the first plan or two, so an initial cutoff only prunes cheap late-winning branches, not
the expensive near-optimal exploration. Do not re-run.

### 4. Budget levers (the direct fix for "can't finish escalation") — NOT built
- **Fresh-budget escalation:** give the escalation a full budget instead of the remaining one
  (`overnight-audit-2026-07-11.md`). Simplest; but double-spends budget on escalated decisions.
- **Budget reservation / better mid-line prediction** (`anytime-search-budget-prediction.md`): stop
  burning budget on abandoned deep lines; reserve enough for the escalation up front.
- **Anytime escalation:** if the budget expires mid-escalation, commit the best *completed*
  iterative-deepening pass instead of discarding it.

### Not applicable here
Control-variate / variance-reduction levers are for the **NC reshuffle search** (which samples). The
clairvoyant search's rollout is *deterministic* (one rollout per leaf), so there is no sampling variance
to reduce — only cheaper rollouts (lever 1) or fewer rollouts (search-breadth pruning, orthogonal).

## The still-open big lever

The dominant cost is the **count** of short rollouts (TH ~10M/120g), driven by per-turn search breadth
(dig-line enumeration). Cutting that — not rollout length — is where a large win would be. That is a
search-breadth / candidate-pruning problem (what `MoveOrderPlans` + B&B already partly do), distinct
from everything above. Start by attributing TH's 10M calls to their call sites
(`TurnSolver.cpp` 5975 / 6507 / 6687 / 6971) before designing a cut.

## Code pointers
- Truncation: `SimulateToEndImpl` in `TurnSolver.cpp` (the `s_roll_horizon` cap).
- Telemetry: `g_rollout_calls` / `g_rollout_steps` + `RolloutStatsReporter` (flag-gated, hot-loop-free off).
- Escalation: `FullSearchLineHybrid` (the `escalate` block); `MTG_ESCALATION_DUMP` rows next to it.
- Scripts: `scripts/esc_analyze.py`, `esc_collect.sh`, `esc_time_probe.sh`, `rollout_horizon_sweep.sh`.
