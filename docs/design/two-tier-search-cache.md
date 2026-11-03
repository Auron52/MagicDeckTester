# Two-tier search cache (design, 2026-11-03)

Status: DESIGN, being built on branch `budget-rebalance` (checkpoint:
`docs/design/budget-rebalance-checkpoint.md`).

## The USER's design (2026-11-03)
- **Tier 1: small, FIXED-size, per-thread, budget-UNCHARGED.** USER: *"my idea was 50-100 MB per
  thread. We would want to measure it, though, and ideally choose a number that fits most games."*
  A hit is free. Because the size is a constant (not derived from RAM or worker count) and the cache
  is private to the thread and scoped to the game, its contents -- and so every hit -- are a function
  of the game's own history. Play and units are therefore identical on every machine, at every thread
  count, whatever ran before on the worker.
- **Tier 2: larger, machine-dependent, SHARED, budget-CHARGED.** Sized from the machine's RAM. A hit
  is charged exactly what a miss would charge, and has exactly the miss's side effects, so tier-2
  residency changes wall clock only, never play or units.

## Why (the inconsistencies at HEAD 3640b6bf1)
Cache inventory, 2026-11-03 (default settings, this box: 24 GB, 32 affinity CPUs):
| cache | bound at HEAD | hit | consequence |
|---|---|---|---|
| Transposition table (per decision; bottoming shares one across a loop) | `DefaultTtCapEntries` = RAM/workers | free (skips rollout steps) | play/units MACHINE-dependent when it binds |
| FSL line cache | one GLOBAL pool, `DefaultFslPoolKb` | free | play/units depend on OTHER THREADS and machine |
| Rollout pre-combat memo (m1memo) | byte cap from BudgetMb/Workers | free | clear point machine-dependent -> play |
| bp-continuation cache / enum memo / canon-front | GLOBAL plan pool `DefaultPlanCacheKb`; mode-1 hit=miss | charged-equal (only zero-charge computations stored) | wall only by construction, but the clear-all on pool refusal reads the global PoolUsed() |
| payable-mana cache | byte cap from RAM | free, not unit-charged | wall only |
| ~12 others (solve memo, m2 memo, xmmemo, gamelc, route memos, ...) | fixed constants | free | already tier-1 shaped |
`Workers()` is the affinity count, not `--threads`, so a 1-thread run on this box gets the same caps
as a 32-thread one -- but a 47 GB box or Windows CI gets different ones.
(The per-thread plan pool `MTG_PLAN_CACHE_PER_THREAD` / `MTG_FSL_PER_THREAD` from the cyclone-mem
work lives only on `origin/cyclone-mem`; HEAD kept the global pools.)

## The contract
1. **Tier 1 holds anything whose hit is free.** All such caches charge their bytes to ONE per-thread
   ledger with a fixed byte budget (`MTG_TIER1_MB`, default chosen by measurement: the per-game peak
   that covers most games). Admission and eviction depend only on that ledger, i.e. on this game's
   history. Cleared at game start.
2. **Tier 2 holds only PURE computations:** result, charged units and observable side effects are a
   function of the key alone. Then "charge what a miss charges" is a recorded number, and a hit can
   replay it exactly. A computation is made pure by running it with its context-dependent nested memos
   scoped fresh and its side channels isolated (the `MTG_BP_DERIVE_ISOLATE` pattern). A zero-charge
   derivation with exact key and isolated side channels is already pure.
3. The TT and the FSL cache are not pure (FSL entries are reused across deepening passes as bounds;
   a rollout's cost depends on the memos nested inside it), so they live in tier 1 only.

## Allocation inside tier 1 (USER 2026-11-03)
*"we should allocate Tier 1's space between a number of different caches as optimally as we can. Some
may not need Tier 2 cache space at all, but that option is on the table when we think it would help."*
So tier 1 is split into per-cache allocations, sized from the ledger's per-cache per-game peaks and the
composition at the total's peak; a cache may also get a tier-2 share when that would buy wall.
And confirmed: *"Tier 2 is essentially just to improve performance of heavier games when we have extra
RAM. It changes no play."*

## Size constraint (USER 2026-11-03)
50-100 MB is a starting point, not a fixed choice -- *"we are not stuck with that option if a
different size produces much better results. However, we do need to keep the per-thread cache in a
reasonable area. For example, the slow machine has 12 threads and only about 11 GB maximum"* -- and
*"we want to avoid oversubscribing all of the memory that we could use for the Tier 2 cache as well."*
Tier 1 is fixed and identical on every machine, so it is sized against the SMALLEST target: 12 threads
x tier1 must stay a modest share of ~11 GB (100 MB -> 1.2 GB), leaving the process baseline and a
useful tier 2. Choose the smallest size that covers most games' per-game peak.

## The ledger currency must be PORTABLE
Every existing size estimate (ApproxPlanBytes, the mana cache's entry size) uses `vector::capacity()`
and `sizeof(Action)` / `sizeof(Plan)`, which differ between libstdc++ and MSVC. Fine for a wall-only
bound; NOT for tier 1, whose admission decides play: Linux and Windows would admit differently and the
CI determinism-parity job would catch it. Tier-1 admission is therefore charged in a portable
deterministic currency -- element COUNTS (`size()`, never `capacity()`) times fixed per-element
constants, calibrated to approximate Linux bytes. Stage 1 (measurement) uses the existing estimates.

## Stages
1. Ledger, accounting only (`src/core/Tier1Ledger.h`, `MTG_TIER1_STATS=<path>` per-game dump). Play
   byte-identical.
2. Tier-1 admission against fixed per-cache allocations (portable currency); replaces `DefaultTtCap`,
   the m1 memo's RAM-derived byte cap, and the FSL/plan GLOBAL pools for admission. Play moves once
   (deterministically) where those caps bound -- measured as a play change.
3. Tier 2 for pure computations, shared and RAM-sized; wall only.

## Open measurements
- Per-game peak bytes per cache, all caches, across every deck at play settings -> the tier-1 size.
- How often tier 1 refuses at that size, and what the refusal costs (units, play) -- refusal is
  deterministic, so this is a quality/wall question, not a consistency one.
- Tier 2's wall recovery (Kitty's Q57: +13-27% thread time from hit=miss recomputes).
