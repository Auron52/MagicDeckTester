# Pod-pair enumeration explosion — root cause of the 2026-09-05/06 OOM streak and Melira's monster games

## The finding (measured 2026-09-06)

`MTG_POD_HAND_PAIR` (default ON, adopted a1680351 2026-09-04 to make the s1/s10 reference lines
enumerable) multiplies Melira's combo-turn enumeration catastrophically:

- **A/B on seed 901838 gi=88** (a 21.7-min game in the phase-A batch), solo, single-threaded,
  byte-budgets attached, otherwise identical: pair ON **8:07.67**, pair OFF **1.10 s** — **443x**.
- **MTG_ENUM_HIWATER_KB instrumentation** over a 32-thread phase-A batch caught single
  `EnumeratePlansWithLand` results of **1.6M–3.08M plans (up to 9.4 GB for ONE call)**, all on
  combo-shaped states (turn 5–8, battlefield 11–15, hand 2–5), re-derived repeatedly because the
  plan-cache byte budget rightly refuses to hold GB-scale entries.
- This one mechanism explains: the six OOM kills (before the byte budgets landed), the residual
  ~12–18 GB transients that force 24 threads instead of 32, phase A's 20–90 s (and up to 1500 s)
  monster games, and the mulligan `recommend` probe's 25-minute rollouts + its own 23 GB OOM —
  i.e. Melira mulligan generation being infeasible.

## Why: the subset walk is exponential in candidate count

The plan enumerator odometer-walks subsets of the candidate-action pool. The pairing feature
inflated that pool on exactly the boards that were already rich:

1. **Hand-Pod pairing** (`emit_pod_source` over hand pods): ActivatePod fans per
   (distinct victim class x distinct fetch target + no-fetch) — ~20–40 actions per pod source,
   now emitted for battlefield AND hand pods.
2. **Closer-castable loop unlock** (`closer_castable`): loop/burst variants (one per
   outlet x persist body x purpose) now emit whenever a closer CLASS card sits in hand, not only
   when a closer is live — several more independent actions.

Each extra candidate doubles the subset space. Pre-pair combo states enumerated 28K–65K plans;
post-pair 1.6–3.1M. The subset RULES (SubsetHasStrandedPodActivation /
SubsetHasUnclosedPersistLoop) reject bad subsets but run per-subset — they do not shrink the walk.

## The fix direction (recommended): mutex groups pruned AT THE ODOMETER

Precedent: the MTG_EDF_SEQ_ETB prune ("pruned at the odometer and never reaches eval_and_push at
all"). Add mutex groups over candidates where selecting two is illegal or dominated, enforced as
prefix-pruning in the odometer so the walk itself collapses:

- **Per pod source: at most one ActivatePod** (LEGALITY: the Pod taps to activate; two
  activations of one source cannot coexist in a plan). Walk factor for a source drops from
  2^40 to 41.
- **At most one persist-loop action per subset** (DOMINANCE: no plan needs two infinite loops;
  the better one subsumes the other).
- (Optional, measure first) per (source, victim): at most one fetch target — dominance, only
  ever take one fetch per activation slot.

This keeps every single-choice line enumerable (the s1/s10 reference lines survive), changes
searched play only by removing illegal/dominated subsets, and turns the pairing's exponential
contribution into a polynomial one.

## Bar for adoption

- All 10 `references/Melira_Pod` games still matched or beaten (the pairing's raison d'etre).
- Suite smoke/regression: no play regressions (melira keys may legitimately move if dominated
  subsets were being scored — inspect, don't assume).
- g88 repro: order-of-magnitude wall reduction with pair ON.
- Phase-A batch at 32 threads: no guard-kill, RSS bounded.

## Status

- 2026-09-06: measured + designed (this doc). Byte budgets (MTG_FSL_POOL byte-accurate,
  MTG_PLAN_CACHE_KB) already shipped — they bound MEMORY but not the walk TIME.
- Interim: any Melira label/mulligan run remains monster-prone until this lands; do not size
  Sunday runs assuming current per-game costs are inherent.
