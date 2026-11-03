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

## Current state / running (2026-11-03 ~02:10 UTC)
- Commits (local, branch budget-rebalance): 4794e6320 ledger + counters (play-inert); f168d5f17 stage 2 +
  pay/simkey counters + priced decision ceiling (off) + census w_* columns; 68a709952 plan-cache route key
  fold + diagnostics. Built into the worktree's build/Release (02:06).
- PURITY VERDICT (armA vs armB, MTG_PLAN_CACHE_KB=1024, 259 of 260 jobs; armB's last Hinata game was
  stopped): win turn identical in all 59,515 games, units differ in 78 (alchemist 5, bruna 1, edf 2,
  kittyv3 36, melira 8, niv 14, snow 12), one digest (bruna d5 s3003). => the plan caches are NOT yet pure.
  Deterministic repro: MTG_PLAN_CACHE_KB=1 (every store refused) vs default, single games in
  build/scratch/budget/leak/*.json (runner leak/run.sh).
  * CAUSE 1 (FIXED, MTG_PLAN_CACHE_ROUTE_KEY default on): lists derived inside an equip-route / AA-route
    walk (route not re-emitted -> free) were served to ordinary frames, skipping the route trial a
    recompute runs. The m1 memo already refused this; m2 memo + bp cache did not. Fixes kitty + alchemist
    repros (5 of 10).
  * CAUSE 2 (OPEN, subagent on it): the bp continuation cache serves CONTENT a fresh derivation does not
    produce (MTG_BP_ENUM_VERIFY: EDF e68 36 CONTENT-DIFF, served 8 vs fresh 7, extra plan Overgrowth +
    Overgrowth + Cloud of Faeries). Context word and a wide state fingerprint are EQUAL at store and hit;
    an immediate re-derivation at store matches; ~25 memo knock-outs change nothing. History-dependent
    input not yet found.
  * Diagnostics added: MTG_PLAN_CACHE_COST_VERIFY (recompute every hit under a cost scope; EDF/Bruna/
    Melira: 0 bad -> hits never skip charges, the leak is CONTENT), ctx/fingerprint print at
    CONTENT-DIFF, STORE-REDERIVE-DIFF check, MTG_BP_DERIVE_ISOLATE_IN experiment.
- STAGE 2 VALIDATED (run_stage2.sh, 02:07-02:23 UTC): V0 (MTG_TIER1=0, ROUTE_KEY=0) == armA on all 260
  digests; 1 game's units differ (alchemist d5 s3003 gi45, the plan-pool game that also flipped t1_reg->armA).
  V1 (defaults) vs V0: only alchemist (6) + kittyv3 (151) games move (route-key fix; win turns identical, one
  kitty digest) + snow d5 s2002 gi76 +2 units (plan pool, 590 refusals). ZERO tier-1 refusals: no allocation
  binds. Per-cache max (MB) vs allocation: tt 5.0/8, fsl 14.7/16, m1memo 6.0/8, gamelc 14.5/24, mana 16.0/16
  (wholesale-clears in 10 games; content-pure per MTG_MANA_CACHE_VERIFY, charges no units -> wall only).
- PRICES REFIT (v0.txt, contended): simkey 7.8 us 41.5%, enum 26.8 us 16.7%, apply 5.6 us 14.6%, pay 2.2 us
  13.8%, plans 2.1 us, step 20.3 us, odo 1.0 us, greedy 0.7 us. Slowest 1%: actual/pred p95 3.7 (units 15.4).
  In WorkCounters.h kPriceNs (commit after 4901452e4).
- RUNNING (02:29 UTC): run_census_tail.sh -> ct.* + census.tsv: tail corpus (45, weight-first) + regression
  (260) pooled, defaults, MTG_MAX_GAME_WALL_SEC=3600, census MIN_UNITS 2000 or MIN_PRICED_MS 50, job-tagged.
  Analyse: scripts/census_priced.py census.tsv --by-deck; marker census_tail.DONE.
- Per-job X lever for the A/B: manifest "decision_priced_x" (ValueArm), env MTG_DECISION_PRICED_X otherwise.
- Tail corpus READY: build/scratch/budget/tail_corpus.json (45 one-game jobs, mostly d5 b20; originals
  up to 4.6 h) + tail_corpus_notes.md. Not yet run on HEAD (MTG_MAX_GAME_WALL_SEC=3600, alone on box).
- Cyclone mulligan generation in .claude/worktrees/cyclone-landing905: stopped deliberately by the USER
  (via another agent); restart planned by the user. Not ours.

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
