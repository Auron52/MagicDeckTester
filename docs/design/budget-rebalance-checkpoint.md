# Budget rebalancing + 2-tier caching — checkpoint

Branch `budget-rebalance`, worktree `.claude/worktrees/budget-rebalance` (cut from 3640b6bf1).
Started 2026-11-03.

## The brief (USER 2026-11-03)
- Improve and rebalance budgeting: no significant quality loss, cut unnecessarily long tails; use the
  time we are given better. Cutting total time at constant quality is a bonus.
- Fix the caching/budgeting interactions that produce inconsistencies, likely with a 2-tier cache:
  tier 1 = small, fixed-size, per-thread, budget-UNCHARGED; tier 2 = larger, machine-dependent,
  shared, budget-CHARGED (a hit costs what a miss costs, so tier-2 residency never moves play).
- Take UNCHARGED work into account.
- Goal: the tail is BOUNDED under any finite budget -- no 1 h+ games; tail in minutes, preferably
  seconds -- while accepting some differentiation (a heavy game may cost more than a normal one) where
  quality needs it. The question is how far we can rebalance while gaining.

## Findings so far
- Budget map (agent, 2026-11-03): per-pass ceiling 25x L (MTG_OVERRUN_MULT), escalation fresh 0.5L
  ladder, path-to-trust can admit passes after the budget is spent; a decision ~35-40L in practice,
  a turn ~100L (each bp re-solve / TakeTurn entry gets a fresh DecisionBudget()). Per-decision
  (MTG_DECISION_WORK_X) and per-game (abandon_units) ceilings are OFF in play. Uncharged: plan
  enumeration (~8 us/plan), bp derivations, greedy walk (Snow-only W=16), put-back fans, dedup/keys,
  cache-hit-charge mode-1 recomputes; bottoming trials = C(7,k) full games at play settings.
- Historical SLOW-GAME logs: budgeted games up to 3,506 s / 1.3M units (Kitty keepgen), 16,722 s
  (contended screen), 360 s at 900 units. Most are Kitty / EDF, 10-05..10-16.
- Melira 2026-09-06: a plain decision ceiling on MISPRICED units costs quality (X=10 t5->t6) --
  re-pricing must precede any ceiling.

- Cache inventory (agent, 2026-11-03): see docs/design/two-tier-search-cache.md. HEAD has GLOBAL plan
  + FSL pools (per-thread admission only on origin/cyclone-mem); TT cap and m1memo byte cap derive from
  RAM/workers -> machine-dependent play.
- RETRACTED: the "Cyclone GT mismatch" was MY stale binary (primary `./build.sh | tail` hid a failed
  build; build/Release/mtg was Oct 31 12:54). The worktree binary (HEAD src) reproduces GT exactly.
- Tier-1 footprint, regression tier, 32,874 searched games (HEAD src, `t1_reg`):
  free-hit caches (tt fsl m1memo solvememo gamelc mana, sum of own peaks): p99 17.6 MB, p99.9 33.6,
  max 83.8 (Snow); coverage 32 MB 99.86%, 48 MB 99.98%, 64 MB 100%. Plan caches (bp/enum) p99 28 MB,
  p99.9 116, max 1,003 MB (Dragonstorm d5 gi200) -- these decide the size if they stay in tier 1.
  Largest free-hit: solvememo (max 53), mana (46), fsl (15), gamelc (16), m1memo (8), tt (5).
- Ledger verified play-inert: vs the stale baseline every differing game is Cyclone (469 / 0 others).
- Tail at HEAD, regression tier (uncontended): searched 10,954 core-s; p50 28 ms, p99 6.0 s, p99.9 24 s,
  max budgeted ~43 s (Snow d5); top 1% of games = 39.5% of time, top 5% = 70%. Worst us/unit:
  Alchemist 350 (uncharged). Budget-0 backstops: Cyclone 102 s, Melira 51 s.

## Stage 1 built (uncommitted, worktree)
- `src/core/Tier1Ledger.h`; hooks: TT (TranspositionTable.h, per-entry 64/80 B, copy/move/dtor),
  FSL (FslPoolAcquire/Adjust/Release), plan pool (Acquire/Release -> plan_bp / plan_enum), m1memo,
  solvememo (+m2), gamelc, mana cache; BeginGame in ClearPerGameCaches; BatchRunner per-game dump
  `MTG_TIER1_STATS=<path>`; analysis `scripts/tier1_stats.py <dump> --by-deck`.

## Current state / running
- `build/scratch/budget/run_purity.sh` (worktree binary 00:52): arm A default -> armA.*, arm B
  MTG_PLAN_CACHE_KB=1024 -> armB.*; DONE marker purity.DONE. Compare with `scripts/cmp_runs.py`.
- Tail-corpus agent: building build/scratch/budget/tail_corpus.json (historical slowest budgeted games
  on current lists) + tail_corpus_notes.md.

## Next steps
1. Purity verdict -> plan caches to tier 2 (shared RAM-sized pool, as HEAD's global pool already is) or
   keep in tier 1.
2. Stage 2: fixed per-thread tier-1 allocations (TT, FSL, m1memo, solvememo, gamelc, mana) in a portable
   currency; remove RAM/worker-derived bounds.
3. Cost model: fit per-game wall on workctr counts (armA) -> prices for uncharged work.
4. Run the tail corpus on HEAD; then budget rebalancing (re-price + whole-decision allowance) as a curve.
