# The plan cache is sized outside the memory budget (deferred, 2026-10-17)

## What happened

EldraziDisplacerFlicker's mulligan generation (`mullgen.sh run ... fast`, d1 b1, 32 workers, PGO) was
aborted by the engine's own RSS watchdog after 2.5 h:

```
[rss-cap] mtg-analyze: rss=17.74G EXCEEDS the cap 17.60G (MTG_RSS_CAP_GB) -- aborting ...
pools: fsl=0M/1M(of 1605M) plan=89M/3199M(of 3200M)
```

Nothing was lost (the journal resumed: 238,181 cell-sides), but the box sat idle until it was noticed.

## Why

`src/core/MemBudget.h` derives every result-neutral cache from `budget = MemTotal / 2`, EXCEPT the
whole-plan caches:

| | derivation | this box (23.5 GB, 32 workers) |
|---|---|---|
| budget | MemTotal / 2 | 11.7 GB |
| reserve | workers x 250 MB | 8.0 GB |
| cache (TT + FSL) | max(2 GB, budget - reserve) | 3.7 GB |
| **plan cache** | **workers x 100 MB, independent of budget** | **3.2 GB** |
| RSS cap | 3/4 MemTotal | 17.6 GB |

So on a many-core / small-RAM box the plan cache alone is ~86% of the cache share the budget
allows, on top of it. It filled (3,199 of 3,200 MB) and the heavy EDF tail rollouts (100-375 s each)
supplied the rest.

## Workaround in use

Resume with `MTG_PLAN_CACHE_KB=1572864 MTG_MEM_BUDGET_MB=10240` (plan 1.5 GB; TT/FSL floor 2 GB).
Result-neutral by the pools' contract; the run was already in the pool-binding regime.

## Proposed fix

Take the plan cache out of `cache_mb` like the other pools (e.g. plan = min(workers x 100 MB,
cache_mb / 3), with TT/FSL sharing the rest), so the sum of the bounded caches can never exceed
`budget - reserve`. Byte-identical where no pool binds; check with smoke/regression digests.
Not done mid-generation because it changes `HEAD:src` (PGO stamp) under a running freeze.

## Done (2026-10-18)

`DefaultPlanCacheKb()` is now `min(workers x 100 MB, cache_mb / 4)`, so TT (1/3) + FSL (2/5) + plan
(<= 1/4) of `cache_mb` stay inside `budget - reserve`. This box: plan 3.2 GB -> ~0.93 GB. A 47 GB /
32-worker box is unchanged (cache 15.5 GB, plan 3.2 GB). An explicit `MTG_PLAN_CACHE_KB` still wins.
Checked with unit tests and the smoke/regression digests on this box (where the new cap applies).
