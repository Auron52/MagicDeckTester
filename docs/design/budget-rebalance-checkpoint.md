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

## Current state / running (2026-11-03 ~01:25 UTC)
- Commit 4794e6320 (local, branch budget-rebalance): ledger + work counters, play-inert (verified).
- UNCOMMITTED in worktree, compiled OK in isolation, NOT yet built into build/Release:
  * stage 2 tier-1 admission (MTG_TIER1, default ON; =0 legacy): Tier1Ledger.h AllocBytes/Fits/portable
    constants (Action 544, Plan 552, PhasePlan 568, FSLineEntry 56, node 64); TT Store/StoreNoWin,
    FslPoolAcquire/Adjust/Release (+FslBytes/FslHintBytes, FslCap fixed 2M), m1memo ByteCap + portable
    entry bytes, gamelc CapBytes + portable nb, mana ByteCap + portable eb; DeterminismReport prints
    [tier1] refusals. Default allocations MB: tt 8, fsl 16, m1memo 8, gamelc 24, mana 16 (plan_* and
    solvememo not tier-1 admission categories).
  * work counters kPay (ManaPayment.cpp TapForCostShared) + kSimKey (BuildSimKeyBase); priced sum
    workctr::t_priced with kPriceNs table (pay/simkey prices 0 until refit).
  * turn census columns w_step_c..w_simkey (thread_local, exact).
  * PRICED DECISION CEILING: DecisionWorkMeter.h PricedScope / PricedExceeded / PricedLimitNs
    (MTG_DECISION_PRICED_X default 0 = off; MTG_DECISION_PRICED_REF_NS default 25000), armed at both
    decision roots (FullSearchLineHybrid _dwp_fs, SolveWithLookahead _dwp).
- Purity run: build/scratch/budget/run_purity.sh. Arm A (default) DONE: reproduces t1_reg on all 260
  jobs (1 game units 38292->38279, Alchemist d5 s3003 gi45 -- global plan pool refused 627 stores in
  t1_reg => first evidence plan-cache residency is NOT perfectly pure). Arm B (MTG_PLAN_CACHE_KB=1024)
  was on its last job (hinata_regression_d5_s3003) at 01:22; marker purity.DONE; compare with
  `scripts/cmp_runs.py armA armB` in build/scratch/budget. Already visible: tiny pool makes games MUCH
  slower (dragonstorm gi200 19 s -> 148 s; alchemist d3 gi26 -> 311 s) -> plan caches need big space.
- Tail corpus READY: build/scratch/budget/tail_corpus.json (45 one-game jobs, mostly d5 b20; originals
  up to 4.6 h) + tail_corpus_notes.md. Units overspend dominates generation tails (Niv vlq row 547M
  units, EDF 904M at b20: no keep table -> bottoming trials x C(7,k), 25x per-pass ceilings); Kitty is
  mispricing (2-27 ms/unit). NOT yet run on HEAD. Run it with MTG_MAX_GAME_WALL_SEC=3600 so a 1 h+ game
  is reported, not waited on; alone on the box.
- Primary checkout build/Release/mtg rebuilt to HEAD (00:49); the session-start build had failed
  silently (`| tail`), see memory build-pipe-masks-failure.
- Cyclone mulligan generation (NOT mine) in .claude/worktrees/cyclone-landing905 stopped ~23:44 UTC
  11-02 mid-refine with no completion/error line; journal + .bak1/.bak2 intact. Reported to user.

## Next steps
0. Read armA-vs-armB; build worktree (stage 2 + counters + priced ceiling); verify MTG_TIER1=0 is
   byte-identical to armA and MTG_TIER1=1 play-identical where no allocation binds; refit prices with
   pay/simkey; run the tail corpus; turn census for the per-decision priced distribution -> X.
1. Purity verdict -> plan caches to tier 2 (shared RAM-sized pool, as HEAD's global pool already is) or
   keep in tier 1.
2. Stage 2: fixed per-thread tier-1 allocations (TT, FSL, m1memo, solvememo, gamelc, mana) in a portable
   currency; remove RAM/worker-derived bounds.
3. Cost model: fit per-game wall on workctr counts (armA) -> prices for uncharged work.
4. Run the tail corpus on HEAD; then budget rebalancing (re-price + whole-decision allowance) as a curve.

## Budget side: findings + plan (2026-11-03)
- Cost fit (scripts/cost_fit.py, armA, 15,824 searched games >= 50 ms, contended wall): per-event prices
  apply 20.4 us (59.6% of fitted time), enum call 23.0 us (15.9%), plan produced 5.9 us (15.1%), odometer
  visit 1.13 us (4.4%), charged rollout step 17.9 us (3.5%), greedy visit 0.75 us (1.0%), interior unit
  ~0. actual/pred p95 2.98 (units-only 4.54); max 518x (units-only 5955x). NO uncharged rollout steps
  occur at tier settings (step_f = 0). Unexplained time beyond 3x: EDF 129 s (mana payment: perf on
  edf gi143 = 38% TapForCost*/ManaCacheKey/SnapPayFields, 11% BuildSimKeyBase, 10% copies), then
  colossus 17.6 s (fixed ~100-450 ms per-game overhead with ~no search), fivecolour2hg 12.8, alchemist
  12.6. Counters `pay` (TapForCostShared) and `simkey` (BuildSimKeyBase) added for the next fit.
- PLAN, staged:
  1. PRICED DECISION CEILING (tail bound, minimal blast radius): keep units for every in-search choice
     (start gate, exhaustion, pass ceilings); add a per-decision ceiling on PRICED work (all kinds, charged
     or not) at X x the decision's budget, via decisionwork::Exceeded -> Overrun -> the running pass rolls
     back, ID commits the deepest COMPLETED pass (turn-complete). Generous X => most games byte-identical.
     X chosen from the per-decision priced-work distribution (turn census w_* columns), then a quality /
     tail curve over X. Melira 2026-09-06 showed a UNIT ceiling at X=10 lost a turn: pricing first.
  2. RE-PRICE the budget itself (charge priced work, renormalise NODES_PER_VIRTUAL_MS so the median deck
     searches as much as today) -- moves every deck's play; full held-out A/B bar per deck.
  3. Bottoming trials: pool their budget (they multiply a mulligan's cost by C(7,k)).
