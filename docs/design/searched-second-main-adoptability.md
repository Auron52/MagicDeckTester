# Searched second main (`MTG_SEARCH_SECOND_MAIN`) — why it is not adoptable yet

**Status:** root-caused, one fix attempted and REFUTED, budget sweep in flight.
**Goal (USER, 2026-08-16):** move the searched second main and searched breakpoint
logic toward adoptability.

## Measured position at HEAD

Regression tier, levers on vs the levers-off GT, per-GAME weighted (26,300 games):

| deck | better | worse | mean | win->loss |
|---|---|---|---|---|
| hinata | 43 | 58 | **+0.0131** | 8 (vs 5 back) |
| antilife | 7 | 15 | **+0.0071** | **3 (vs 0 back)** |
| fivecolour | 48 | 26 | -0.0125 | 0 |
| burn | 20 | 0 | -0.0067 | 0 |
| creature_giving | 19 | 1 | -0.0086 | 0 |
| **TOTAL** | 137 | 100 | **-0.0008** | 11 / 5 |

Overall ~zero. Three decks clean positives (burn has **no** game worse at all);
two decks pay for it. Those two are the whole adoption problem.

**Single-lever attribution** (`MTG_SEARCH_SECOND_MAIN=1` alone, nothing else):

| deck | mean | win->loss |
|---|---|---|
| antilife | +0.0048 | 2 |
| hinata | +0.0006 | 0 |

So this lever owns ~2/3 of antilife's regression and essentially **none** of
hinata's. Hinata's +0.0131 is a different cause and needs its own isolation.

## Root cause: TRUNCATION, not valuation

Exemplar `antilife_regression_d3_s3003 gi=245` (seed 3248, `--depth 3
--ignore-play-profile`), which loses outright at both d3 and d5, so it is not
budget churn:

```
budget 10   LOSS        budget 0 (unbounded)   win T6
```

The failing line casts **Aria of Flame twice with no Tainted Remedy live** — the
opponent goes 20 -> 30 -> 40, since Aria's `etb_opponent_lifegain: 10` is only a
payoff once a Remedy inverts it. Base casts Remedy first, then Aria for 10 damage,
and wins T6.

**But the same search at budget 0 does NOT cast unbacked Aria.** The bad cast is a
*symptom* of the search exhausting its allowance and committing to a subset it
explored early — the classic truncation shape this repo bars, not a modelling gap.

## What is NOT the cause (checked, so nobody re-checks)

* **Not the m2 memo.** `MTG_NO_M2_SEARCH_MEMO=1` reproduces identically.
* **Not a missing guard call.** `SubsetHasUnbackedGiftDamage` /
  `SubsetHasUnbackedAltPayload` ARE reached on the searched-m2 path:
  `SolveWithLookahead` -> `EnumeratePlansWithLand` -> `EnumeratePlansWithLandUncached`
  -> `EnumeratePlans` -> `eval_and_push` (`TurnSolver.cpp:12538-12552`), with no
  `is_pre_combat` gating. The greedy twin is `SolveUncached::consider`
  (`7819-7828`). Those two are the complete choke set.
* **The predicate is narrow, but widening it is the WRONG fix.**
  `SubsetHasUnbackedGiftDamage` triggers only on
  `tmpl == DirectDamage && params.opponent_lifegain > 0` (built for Fiery Justice).
  Aria is `template: custom` with `etb_opponent_lifegain`, so it returns false at
  `TurnSolver.cpp:1296`. Widening it to any unbacked `etb_opponent_lifegain` would
  break the family's contract ("only prunes a STRICTLY DOMINATED cast ... so it
  cannot drop a winning line -- the unbounded-search bar", `TurnSolver.cpp:1274-1279`):
  Aria is a **verse engine** that can pay its 10 back, which is why
  `DecisionProviders.cpp:1799-1822` deliberately applies only a HALF-weight penalty.
  A flat prune here would itself be lossy truncation.
* **Not lever-inert to widen anyway.** Anti-Lifegain plays a real searched second
  main in BASE (`DeckUsesSecondMain` is true on `lifegain_to_loss`,
  `GoldFishRunner.cpp:57` -> `SetSearchPostCombat(true)`). `MTG_SEARCH_SECOND_MAIN`
  only decides whether the m2 *inside the search* is searched or greedy
  (`SolveSecondMainInSearch`, `TurnSolver.cpp:491`). So a predicate change would
  move base play too.

## Attempt 1 — greedy floor: REFUTED, do not retry as stated

Idea: never let the searched m2 return worse than the greedy m2 it replaced.

Implemented as "take greedy when `greedy.value > searched.value`". **Measured 305
games worse across FIVE decks (net +410), against 8 worse with no floor at all** —
including goblins and burn, which the lever had not touched.

**Why it fails, and the reusable lesson:** the two `value` fields are not on the
same scale. A greedy `Solve` value is a single-turn heuristic score; a
`SolveWithLookahead` value is horizon-evaluated. Ranking them against each other
picked greedy nearly always and destroyed the search's multi-turn planning.

A narrowed form (fall back only when the searched plan has NO actions) does not fix
the exemplar — the searched plan is not empty; it is actively bad. Backed out; base
restored byte-identical.

**If a floor is retried, it must compare OUTCOMES (simulated win turn), never
`Plan::value` across the two solvers.**

## In flight: the budget sweep

If the failure is truncation, the lever should become net-positive once the second
main can afford to search. Sweeping antilife d3/d5 x seeds 2002/3003 at budgets
10/20/40/80/160, base vs lever, both arms pooled (`/tmp/manifest_ssm_budget.json`,
5,500 games per arm), results in `logs/ssm_budget/{base,ssm}/wins`.

The adoptability question this answers: **is the lever safe at some budget, and is
that budget one we actually run at?** The regression tier uses 10 (d3) and 20 (d5);
if it only becomes safe far above that, the honest answer is that the lever needs a
cheaper second main, not a bigger allowance.

## Open

* Hinata's +0.0131 is unattributed and is the LARGEST single blocker. It is not this
  lever (+0.0006 alone). Isolate the remaining five levers against it.
* `MTG_BP_SITES=63` (searched breakpoint sites) has not been isolated at all.
