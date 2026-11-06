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
- TAIL CORPUS ON CURRENT LISTS (ct.*, 02:29-03:00): 44 of 45 historical slow games (originals up to 4.6 h)
  now finish <= 214 s (kitty 2hg gi20037 214 s, kitty keepab_static gi674 204 s, fungusB 2hg 161 s, alchemist
  overnight d4 gi490 153 s; most << 60 s): keep tables removed most bottoming-trial cost. The one exception:
  edf_keepab_static_gi821 = 1,881 s (31 min, t7) -- decisions of 70-237 s wall with ~500 units each: ~200
  enumerations x ~2,200 plans x ~280k mana-payment calls (hand: Overgrowth, Wild Growth, Cloud of Faeries,
  Adarkar Wastes, Living Wish). That is a BRANCHING/payment explosion, a collapse candidate in its own right.
- CENSUS (census.tsv, 27,086 budgeted decisions >= 2000 units or >= 50 ms priced): ratio priced/(L x 25 us)
  p50 0.56, p99 7.4, p99.9 30, max 763 (edf gi821). Trips: X=10 162 (0.6%), X=20 49, X=50 16; regression
  decks max ~74 (alchemist gi170 t5), mostly <= 30.
- PRICED CEILING A/B v1 (abx_oldtrip.*, held-out seeds +7e6, 139 budgeted shapes x X{0,10,20,50} + tail x
  X{10,20,50}): quality X=10 7 games worse (+8 turns / 10,950), X=20 2 worse, X=50 0. BUT a PATHOLOGY: the trip
  went through decisionwork::Exceeded(), which also disables plan-cache hits/stores, makes rollouts instant
  no-wins and stops no-win stores -> tripped decisions got SLOWER (goblins2hg gi30 X=10: 168 s vs 29.5 s; 104 s
  priced vs a 13.5 s limit; alchemist gi490 X=10 628 s and t6->t8). FIXED 98d69de23: the priced trip reaches
  the search ONLY through SearchBudget::Overrun (per-pass rollback path); Exceeded() is units-only again.
  goblins gi30 X=10 after fix: same units, priced 23.6 s (= no-ceiling), deep pass wasted (idwaste 69k,
  commit 749 units) -- overshoot because the uncharged path checks Overrun sparsely.
- A/B v2 (fixed trip, abx.*, 03:21-): tail corpus X=10/20/50 shrink kitty static 204->20/27/47 s, niv smoke
  86->15/19/67, kittyv2 123->47/48/74, snow 71->34/46/68, all same win turn; BUT alchemist overnight d4 gi490
  153 s t6 -> X=10 337 s t8, X=20 227 s t8, X=50 75 s t6, and edf lookahead gi214 121 s -> X=10/X=20 > 40 min
  (X=50 105 s). Regression shapes (partial ~2.7k games): X=10 1 game worse, X=20/50 0. => tight X cuts a
  CRITICAL decision (combo search) and the game then costs more; X=50 clean so far. Report:
  `scripts/ab_x_report.py abx --tail0 ct` in build/scratch/budget.
- PRICED START GATE (76a9e229d): FullSearchLine's ladder declines to START a pass whose projected priced cost
  does not fit the remaining priced allowance (per-job "decision_priced_gate", env MTG_DECISION_PRICED_GATE,
  default on when the ceiling is armed). Built 04:06, NOT yet measured.
- PLAN-CACHE PURITY FIXED (subagent, branch bpleak-fix 5dce817c4, cherry-picked as 92685645b): four more leaks,
  each with a =0 lever: MTG_PLAN_CACHE_TRAITS_KEY (applying plan's PlanTraits read by payment holds during a
  mid-apply continuation derivation: edf e68, snow s91), MTG_PLAN_CACHE_PERM_KEY (per-permanent state by copy
  number: melira m13), MTG_PLAN_CACHE_SCALAR_KEY (scripted_* + turn counters + m1_hand: bruna b134),
  MTG_MANA_CACHE_RAMP_ALT [superseded at rebase by origin's MTG_MC_FEED_ALT] (mana cache replays the ramp-feed-alt verdict a cached failure skipped: niv n83 --
  that game also moved with MTG_MANA_CACHE=0 before). All 6 repros: default units == KB=1 units, CONTENT-DIFF 0.
  OPEN (user's call): MTG_MANA_CACHE_X_TRAITS / _X_PAYCTX would close 40 colour-only mana-cache diffs on n83
  (no unit effect measured; key cost on the hottest path).
- PURITY AT SCALE (run_purity2.sh): p2a (default, new binary) DONE: vs V1 the purity fixes move 6 digests (bruna
  x3, niv x3), 1 win turn (niv d0, better), 156 games' units (niv 95, niv2hg 25, snow 16, melira 13, bruna 4, edf 3).
  p2b (MTG_PLAN_CACHE_KB=1, every store refused) still RUNNING (much slower); pure => p2b == p2a in every unit.
- A/B v2 VERDICT NEEDS CARE: it ran on the PRE-fix binary with a near-full global plan pool at 32 threads, i.e.
  play was pool-interleaving dependent. edf gi214 X=10: 2,148 s in the batch, but 123 s (t4) alone on the fixed
  binary -> that blow-up was the impurity, not the ceiling. alchemist gi490 X=20 t6->t8 REPRODUCES alone (real):
  the t2/t3 decisions need ~30x L priced, a trip discards the one deep pass (idwaste ~18k) and commits a 21-unit
  depth-1 pass. And the ceiling does NOT bound edf gi821: at X=50 its decisions still spend 84-238 s priced
  (ratio up to 529) -- the heavy work never reaches an Overrun check (subagent investigating WHERE; candidate:
  the EDF combo route has no budget checks).
- EDF EXPLOSION (subagent 2, worktree branch from ba8bf8a0e): anatomy + sound collapses of the land-Aura /
  untapper enumeration (~2,000 plans and ~280k payment calls per enumeration).
- RUNNING (04:31 UTC): run_ab_x3.sh -> abx3.*: PURE binary, arms X{0,20,30,50,100} gate on + X=50 gate off, held-out
  regression shapes + tail corpus (own X=0 arm), 18 threads. Report: `scripts/ab_x_report.py abx3`.
- Per-job X lever for the A/B: manifest "decision_priced_x" (ValueArm), env MTG_DECISION_PRICED_X otherwise.
- Tail corpus READY: build/scratch/budget/tail_corpus.json (45 one-game jobs, mostly d5 b20; originals
  up to 4.6 h) + tail_corpus_notes.md. Not yet run on HEAD (MTG_MAX_GAME_WALL_SEC=3600, alone on box).
- Cyclone mulligan generation in .claude/worktrees/cyclone-landing905: stopped deliberately by the USER
  (via another agent); restart planned by the user. Not ours.

## Live at compaction (2026-11-03 04:40 UTC)
- Unit tests on the combined branch (857ec8ae8): 2420/2420 pass.
- RUNNING, all harness-tracked: p2b (purity KB=1 arm, 108/260 jobs at 04:39) -> compare `cmp_runs.py p2a p2b`
  (pure => 0 unit diffs); abx3 (A/B v3, 259/1104 jobs) -> `ab_x_report.py abx3`; subagent 2 (EDF explosion
  anatomy + collapses, also asked WHERE the heavy work sits relative to Overrun checks).
- Surface to USER (not decided): X choice for the priced ceiling (X=50 clean on quality so far, small tail gain;
  tight X loses Alchemist turns); tier-1 sizes (tt 8, fsl 16, m1memo 8, gamelc 24, mana 16 MB); mana-cache
  context fold (MTG_MANA_CACHE_X_TRAITS / _X_PAYCTX); branch not pushed (push would also run Windows CI).

## Live at compaction #3 (2026-11-03 ~08:25 UTC)
- RUNNING (harness-tracked bah6y2896): build/scratch/budget/run_final2.sh -> final.DONE. Builds a18ae02bf+ (mana 24 MB,
  gate OFF, X=100) with retries; ABORTs on a stale binary; runs smoke + regression in this worktree; prints
  "mana24 vs gate-off" (EXPECT 0 play changes; gate-off wins saved in build/scratch/budget/gateoff_wins/) and
  "mana24 vs origin" (expect the 6 known play changes, 0 win-turn changes).
- RUNNING: tier-2 overflow SUBAGENT, worktree .claude/worktrees/tier2-overflow (branch tier2-overflow from a18ae02bf);
  user ruled the mana key folds OK "as long as truly isolated" (relayed). Its result arrives as a notification.
- NEXT after final.DONE: (1) confirm "build ok" + 0 play changes mana24 vs gate-off; (2) explain the 6 play-changed
  games (bruna regression d3 s2002 / d5 s2002 / d5 s3003 x2, kittyv3 smoke d5 + regression d5 s2002) with
  `python3 test/explain_game.py <mode> <case> <gi> --old-bin <origin-baseline build/Release/mtg>`; (3) accept smoke +
  regression with --deck= every deck EXCEPT merfolk,merfolk2hg (check the filter keeps merfolk rows untouched; one-line
  ACCEPT note naming antilife2hg/fluctuator2hg as origin-stale); run test/check_gt_logs.py; commit GT; push
  budget-rebalance (separate branch OK; shared branch only when the feature is complete -- USER).
- USER RULINGS today: X=100; Q1 PRICED_STOP stays OFF ("only for things expensive enough to be a real issue"); Q2 mana
  24 MB fine (tier-1 50-100 MB flexible); Q3 key folds OK if truly isolated; Q4 accept via the normal process, focus
  on slower games; Q5 push to a separate branch now, shared branch when complete; tier-2 overflow wanted for caches
  that would hit us heavily.
- Scratch worktrees (mine, no unique work): .claude/worktrees/origin-baseline (origin 980616e55, its test/logs hold
  the baseline wins), .claude/worktrees/mf-bisect (at ff7a14850). Remove with scripts/safe_worktree_remove.sh later.
- Later stages: re-price the budget (moves every deck's play; full held-out A/B per deck), pool bottoming-trial
  budget, EDF remaining wastes (whole-list derivation for one entry, copy-twin subset skip, payment refine 22%).

## MERFOLK KEEP TABLE WAS NEVER COMMITTED (found 2026-11-03 08:20 UTC) -- do NOT accept merfolk rows
- Every merfolk smoke/regression cell FAILs on origin's own binary with the same got= as ours. Bisect: the binary of
  ff7a14850 (the commit that wrote those GT rows; same src as 43f7650d5) gives d0 4.6840 etc. -- NOT its own GT.
- Cause: 43f7650d5 "Adopt Merfolk exhaustive mulligan profile" accepted GT measured WITH the keep table on disk
  (docs/design/analysis-Merfolk.md: "live at decks/Merfolk/Merfolk.keepmodel.exhaustive.profile.json"; pre-table smoke
  d0 4.6840 / d3 4.4400 / d5 4.4200 = exactly today's values) but never committed the gzipped .json.gz the repo tracks
  (.gitignore:98 ignores the raw .json). No copy anywhere on this container. USER told; recovery is theirs
  (another container?) or a mullgen regen (user's call).
- Accept policy: accept smoke + regression for every deck EXCEPT merfolk/merfolk2hg (their red is the true signal).
  antilife2hg / fluctuator2hg d5 s2002 are origin-stale too (origin binary got= identical, both better) -> accept, noted.
- Gate-off tiers vs origin (07:38 binary): smoke 54,075 games 1 play-changed 0 win-turn; regression 75,244 games 5
  play-changed (bruna x4, kittyv3) 0 win-turn. fivecolour gi103 back to T6.
- Builds: 9p "Cannot allocate memory" fails -j32 builds when two run at once; run_final2.sh retries and ABORTS on a
  stale binary (run_final.sh carried on with the old binary -- killed it).
- CI on origin/budget-rebalance (pushed 9b813b4ac, USER OK for a separate branch): ubuntu, windows, parity all green.

## USER 2026-11-03: mana tier-1 24 MB; TIER-2 OVERFLOW wanted
- USER: tier-1 total 50-100 MB per thread is flexible -> mana 16 -> 24 MB (a18ae02bf), total 80 MB. NOTE: solvememo
  is NOT byte-capped (portable entry cap) and peaks at 73 MB (p99.9 24 MB) -- outside that 80 MB; told the user.
- Fill cost measured: Snow regression d5 s2002 gi80 (only game > 24 MB uncapped, 33.7 MB): play identical (unwon,
  527,310 units) at uncapped / 24 / 16 MB / cache off; wall 14.7 / 15.0 / 15.9 / 16.5 s (contended).
- USER: "be ready to use the tier 2 caching if we need it ... for any caches that will hit us heavily". Subagent
  (worktree .claude/worktrees/tier2-overflow, branch tier2-overflow from a18ae02bf) building a generic tier-2
  overflow store: shared, RAM-sized, a tier-2 hit charges exactly the miss's units (mana: 0, pure wall); wire mana,
  then m1memo, fsl; evaluate solvememo; purity bar = tier-2 size tiny vs large at a shrunk fixed tier-1 => 0 unit diffs.
  Mana caveat: colour-only hit diffs (niv n83) could become machine-dependent in tier 2 -> X_TRAITS/X_PAYCTX folds.
- RUNNING: build/scratch/budget/run_final.sh -> final.DONE (gate-off tiers vs origin; mana-24 build; tiers again;
  mana24 vs gate-off must be 0 play changes; then accept smoke + regression with the origin-stale note).

## TIERS vs ORIGIN (2026-11-03 07:00-07:45 UTC)
- Baseline arm: worktree .claude/worktrees/origin-baseline (origin 980616e55, Release), smoke + regression; diff
  per game with build/scratch/budget/wins_diff.py <base wins> <new wins>.
- ORIGIN'S OWN GT IS STALE for merfolk (all smoke + regression cells; ff7a14850 adopted a value sidecar and
  re-accepted only part), antilife2hg d5 s2002 and fluctuator2hg d5 s2002: the origin binary FAILs them too,
  with the same got= as ours. Accepting our run promotes those; the note must say they are origin's.
- Ours (gate ON, X=100) vs origin, paired: smoke 54,075 games, 4 play-changed, 0 win-turn changes; regression
  75,244 games, 6 play-changed (bruna x4, kittyv3, fivecolour), ONE win turn: fivecolour d5 s3003 gi103 6->7.
  Bisect: X=0 => origin units exactly; X=100 with MTG_DECISION_PRICED_GATE=0 => origin too; X=200/1000 gate on
  => origin. So the START GATE declined a pass the ceiling never cut (projection prev x prev/prev2 overshoots).
  A/B v3 X=50 vs 50ng had shown the gate buys nothing (held-out 8,511 vs 8,461 s, tail 5,160 vs 5,143 s, same
  turns). => gate default OFF (commit after 0833e3502). Re-running both tiers on that build.

## EDF COLLAPSES VALIDATED (run_edf.sh, 06:24 pre-rebase binary, done 06:40 UTC)
- Regression tier ea vs p2a: 260 jobs / 59,369 games, win-turn diffs 0, UNIT diffs 0 => the collapses
  (TUTOR_PIN_ALIAS, SPWF_CANON_MEMO, RH_MOVED_VIA_CANDS) are exact at scale. Wall B/A 0.857 (edf 0.617).
- Tail corpus (45 games; pool A 24 thr stop OFF, pool B 8 thr MTG_BP_DERIVE_PRICED_STOP=1), wall s (turn):
  gi821 A0 468.5(7) A100 463.1(7) B50 492.0(7) B100 492.8(7) -- was the 3,600 s cap in every A/B v3 arm.
  gi214 105/113/104/110 (4). Win turns X=100 == X=0 on all 45. Sums A0 2025, A100 1861, B50 1796, B100 1876;
  max game under 8.5 min in every arm (loaded box; gi821 178 s quiet). PRICED_STOP at X=100 buys nothing;
  at X=50 it shows kittyv2 gi1723 4->6 (the known X=50 loss). Recommendation to USER: leave it OFF.

## REBASED onto origin/phase-1-2-deck-analyzer (2026-11-03 06:40 UTC; pre-rebase ref budget-rebalance-prerebase)
- One conflict, src/core/SpellEffects.cpp: origin had landed the SAME ramp-feed hit fix independently
  (MTG_MC_FEED_ALT, ManaCacheEntry::feed_alt, stored only for negatives). Took origin's; DROPPED ours
  (MTG_MANA_CACHE_RAMP_ALT / ramp_feed_alt no longer exist). Equivalent: the public entry reads the flag only
  after a FAILED first pass. Kept our verify-only additions (dbg_pay_ctx/dbg_traits, out_vok, McBoardCmp).
- NOT yet built: run_edf.sh is running on the PRE-rebase 06:24 binary (same path); build after edf.DONE
  (a -j32 compile beside two batches risks OOM on this 23.5 GB box). Then unit tests, smoke/regression A/B
  vs origin GT, accept (play changes: route key, purity fixes, tier 1, X=100, EDF collapses).

## USER DECISION 2026-11-03: priced ceiling X=100 (default now 100 in DecisionWorkMeter.h; "for now").

## Live at compaction #2 (2026-11-03 ~06:40 UTC)
- EDF subagent 2 DONE (write-up docs/design/edf-tutor-pin-alias-2026-11-03.md). Cherry-picked its 4 commits
  (55ddfc4a7 MTG_BP_TUTOR_PIN_ALIAS, e3d46c06f MTG_SPWF_CANON_MEMO + MTG_RH_MOVED_VIA_CANDS, 36f12368c
  MTG_BP_DERIVE_PRICED_STOP default OFF, 8feea40c3 docs). Exact collapses, default ON: gi821 767 s -> 178 s
  quiet box, identical play. Where the tail work sits: breakpoint-continuation derivations run INSIDE one
  apply (BpEnumEntryFor -> BpDeriveContinuationList -> EnumeratePlans), uncharged, no Overrun check; the
  only sound check site is eval_and_push (= the PRICED_STOP proposal; repro: worst decision 47 -> 25 s
  priced at X=50, but game +20% priced from re-derivation).
- Binary rebuilt 06:24 UTC with all of it; unit tests 2420/2420.
- RUNNING (harness-tracked b1eedgdg3): build/scratch/budget/run_edf.sh -> edf.DONE.
  Pool A ea.* (24 thr, RSS cap 14): reg_manifest (compare `cmp_runs.py p2a ea` => expect 0 unit diffs)
  + tail corpus tailx0_/tailx100_ (stop OFF). Pool B eb.* (8 thr, RSS cap 6, MTG_BP_DERIVE_PRICED_STOP=1):
  tail tailx50_/tailx100_. Read: `ab_x_report.py ea` (needs tailx0 arm; regression jobs have no _x suffix ->
  only the TAIL CORPUS table is meaningful), eb via a merged read or per-game SLOW-GAME grep.
- Next: if ea == p2a on units, the collapses are exact at scale; judge gi821/gi214 tail at X=0/100; decide
  whether PRICED_STOP earns a place (surface to USER). Then rebase onto origin (44 commits behind) before GT.

## A/B v3 FINAL (2026-11-03 05:47 UTC; pure binary, gate on unless "ng"; MTG_MAX_GAME_WALL_SEC=3600)
Held-out regression shapes (seed+7e6), ~31,400 paired games per arm vs X=0:
| X | moved (worse) | sum wall | p99.9 | max |
| 0 | - | 8845 s | 24.9 s | 102.5 s |
| 20 | 2 (2, elementals) | 8715 | 22.3 | 97.4 |
| 30 | 1 (1, elementals) | 8528 | 21.5 | 81.6 |
| 50 | 0 | 8511 | 22.1 | 70.8 |
| 50ng | 0 | 8461 | 20.9 | 72.3 |
| 100 | 0 | 8406 | 22.3 | 69.6 |
Tail corpus (45 games; gi821 hits the 3600 s game cap in EVERY arm, sums below exclude it): X=0 1881 s,
X=20 4549, X=30 2928, X=50 1560, X=50ng 1543, X=100 1668. Quality: X=20 alchemist gi490 6->8; X=30/50/50ng
kittyv2arch gi1723 4->6; X=100 none.
**EDF gi214 BLOWS UP under a tight ceiling:** X=0 136 s / 170k units; X=30 1655 s / 188k; X=20 3147 s / 206k. The
trip changes play (units +21%) into states where UNCHARGED work explodes (pay 28M -> 984M calls, plans 112k ->
93M, odo 6.7M -> 481M) -- the same land-aura enumeration explosion as gi821, and the ceiling cannot stop it
because that work never reaches an Overrun check. => the ceiling does NOT bound the real tail; the EDF route
must reach a budget check first (subagent 2's question). gi821 itself: 2.6-2.9M units in 3600 s at every X.
Branch base 3640b6bf1 is 44 commits behind origin (25 src files): rebase before any GT accept.

## PURITY CONFIRMED (2026-11-03 ~05:00 UTC): plan caches are tier 2
- p2a (default) vs p2b (MTG_PLAN_CACHE_KB=1, every plan store refused), regression tier on the pure binary:
  260/260 jobs identical, 50,760 games, 0 win-turn and **0 unit** differences. Plan caches buy 2.57x wall
  overall (hinata 6.8x, alchemist 4.5x, cyclone 4.1x; contended run). => the plan caches (bp continuation, m1/m2
  enum memos) stay in the RAM-derived shared GLOBAL pool (membudget::DefaultPlanCacheKb) = tier 2: wall-only,
  play is independent of the machine's RAM.

## A/B v3 partial reading (2026-11-03 04:55 UTC; abx3 ~1% of 189,870 games, all tail-corpus arms in)
- READ WITHOUT --tail0: abx3 has its own same-binary tailx0 arm (ab_x_report.py now prefers it).
- Tail corpus, win turn per X (0/20/30/50/100): alchemist gi490 6/**8**/6/6/6; kittyv2arch r8 gi1723
  4/4/**6**/**6**/4 (also 6 at 50ng). Every other tail game keeps its win turn at every X.
- Mechanism (single-game censuses, leak/a490_x*, leak/k1723_x*): (a) Alchemist t2 is NOT a tail decision -- 12 s,
  2.3x L units, but 33x the reference price per unit; X=20 cuts the pass that finds the line (commits a 21-unit
  pass). The game's real outlier, t4 (121 s, 26x L), trips at X>=30 harmlessly. (b) Kitty gi1723's first three
  decisions are genuinely heavy (ratios 70/185/34, 25/61/10 s); at X=50 all trip and t4 discards a 47k-unit pass
  for a 1.6k one; anytime rescue fired (idwaste_rescuable) but cannot recover what a cut pass never reached.
  At X=100 only the 61-s decision trips; t4 holds, 96 s -> 73 s.
- => a ratio-of-L ceiling cannot tell a useful deep pass from a wasted one; X <= 50 is lossy on the tail corpus.
- Tier-1: per-cache peaks are under allocation everywhere except mana (16/16, wholesale clear in 10 games, wall
  only) -> candidate: mana 24 MB. Peak per-thread total ~56 MB of 72 MB allocated.

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

## Live 2026-11-03 ~08:45 UTC: REBASED onto origin 12a476cda (Merfolk keep table shipped)
- USER: the Merfolk keep table is on origin now (12a476cda); merfolk/merfolk2hg come back INTO the accept.
- Origin also brought engine fixes (keep-table cache 6f6b0a8e7, sac-land class pin, Vampires mana-clean), so the
  pre-rebase final run (run_final2.sh) was stopped (my own run) and superseded.
- One conflict, src/ai/ManaOracle.cpp PayEnum dedup: kept BOTH -- base key MidTurnStateKeyOfPayment(sc) (ours,
  tutor-pin cleared) then origin's t_plan_srcs tapped-flag fold.
- origin/budget-rebalance (9b813b4ac) is now behind a REBASED history: force-push it (own branch, no one else uses it).
- RUNNING: build/scratch/budget/run_final3.sh -> final3.DONE. Builds, runs smoke+regression, diffs vs committed GT
  (test/gt_logs), then moves origin-baseline to 12a476cda and builds it (for explain_game --old-bin).
- NEXT: expect the 6 known play changes (Bruna x4, Kitty v3 x2, all better); anything else -> check on the origin
  binary (stale GT vs ours); explain the slower/changed games; accept smoke+regression for ALL decks; check_gt_logs;
  commit; force-push budget-rebalance. Then the tier-2 overflow subagent's patches.

## ACCEPTED + PUSHED 2026-11-03 09:31 UTC (727dea8dd on origin/budget-rebalance, force-pushed after the rebase)
- Smoke + regression accepted for ALL decks incl. Merfolk (table shipped on origin). 0 win-turn changes from this
  branch; 6 same-turn line changes (bruna x4, kittyv3 x2); antilife2hg/fluctuator2hg d5 s2002 origin-stale (the
  12a476cda binary plays our line). Plain --accept dropped origin's 3 notes -> restored verbatim.
- Tier makespans looked 1.7x origin's (smoke 8m18 vs 5m54, regression 11m13 vs 6m49) at IDENTICAL units: the
  tier-2 subagent ran 2x8 threads beside them. PAIRED bench (run_pairbench.sh: 6 heavy smoke jobs, both binaries
  concurrently, 11 thr each): CPU 1447 -> 1470 s (+1.5%), wall 159 -> 167 s, maxrss 1.16 -> 1.12 GB, units equal.
  The residual +1.5% (hinata +3.7%, niv +5%) is consistent with the tier-1 caps (mana 24 MB measured +2% on snow80)
  -- what the tier-2 overflow is meant to recover. Re-bench after integrating it.
- NEXT: tier-2 overflow subagent's patches (rebase onto 727dea8dd, verify 0 play/unit change, re-bench); then
  the re-price stage (charge uncharged work).

## Live at compaction #4 (2026-11-03 ~12:00 UTC) -- branch HEAD de98b05a5 (pushed to origin/budget-rebalance)
- DONE since #3: tier-2 overflow series cherry-picked (4a10eaf60..d00d39b0f; mana only; pure + isolated, no wall
  gain; docs/design/tier2-cache-overflow.md). MTG_MANA_CACHE_CHOSEN_KEY default ON 2e64c6b5f (correctness: ETB
  chosen colour missing from the tier-1 mana key; held-out Snow+Alchemist 2,900 games 0 win-turn changes);
  GT accepted 26dbad212 (smoke 165/165 identical; regression only snow d5 s3003 gi52 T7 cast order).
  USER rule: subagents <= ~250K context, then handoff doc + NEW subagent (memory subagent-context-cap-250k).
- PLAN-CACHE PRICED DEFECT (found by the tier-2 agent): under X=100 a plan-cache hit skipped priced work, so
  tier-2 residency (RAM) moved play (KB=1: 3 win turns). FIX = priced ledger 3f7e31665 (MTG_PLAN_PRICED_LEDGER,
  default ON; per-game seen-key shadows make t_priced as-if-unlimited-cache; handoff
  docs/design/plan-priced-ledger-handoff.md). plan5 probe: KB=1 == default in all 5. FSL is NOT affected (tier 1:
  fixed per-thread 16 MB + 2M-entry cap).
- RUNNING: build/scratch/budget/run_ledger.sh -> ledger.DONE (harness task b5bslqeqf). Builds; tiers at default vs
  GT (test 4: expect 0 or few diffs -> explain each, accept); concurrent regression manifest at MTG_PLAN_CACHE_KB=1
  -> kb1_wins/kb1.log vs the default tier run (test 3: MUST be 0 digest/unit/win-turn diffs). CI watch b7n9t1mq0.
- NEXT: read ledger.DONE output; if test 3 != 0, root-cause (do not accept). Else accept any test-4 changes
  (save/restore the '# accepted-with-regressions' notes -- plain --accept drops them), check_gt_logs, commit,
  push budget-rebalance. Then a paired wall bench vs 26dbad212 (run_pairbench.sh pattern) -- test 5.
  Then: re-price stage (charge uncharged work); pooled bottoming budget; EDF remaining wastes.
  Shared-branch push only when the feature is complete (USER).

## Live 2026-11-03 ~12:40 UTC: ledger tests 3+4 PASSED; test 5 running

- CI for de98b05a5: ubuntu, windows, determinism parity all green.
- Test 3 (MTG_PLAN_CACHE_KB=1 vs default, full regression tier, 264 jobs / 75,244 games): 0 play changes,
  units IDENTICAL (358,188,724 both), 0 unit-diff jobs. Plan-cache residency no longer moves play.
  Core-ms 1.368x for KB=1 (the cost of no cache; contention also differed). [plan-ledger] muted_recomputes=127.7M,
  forced_misses=16,964, shadow_peak_keys=31,589 per thread (3 caches) -- ~1.5 MB/thread.
- Test 4 (default ledger build vs GT): smoke 54,075 and regression 75,244 games, 0 play changes. Nothing to accept;
  regression tier ALL PASS 264/264.
- Test 5 RUNNING: build/scratch/budget/run_ledger_bench.sh -- same binary (de98b05a5 src), MTG_PLAN_PRICED_LEDGER=0
  (== 26dbad212 behaviour; the only src diff since then is the flagged ledger) vs =1, regression manifest, both
  arms concurrent at 16 threads. Output lb_on/lb_off.{log,err}, marker ledgerbench.DONE.
- NEXT after test 5: re-price stage (charge uncharged work; moves every deck -> held-out A/B per deck), pool the
  bottoming-trial budget, EDF wastes.

## Live 2026-11-03 ~13:30 UTC: test 5 PASSED; FINISH LINE running; then Cyclone via the artifact queue (USER)
- Test 5 (ledger ON vs OFF, same binary, regression manifest, concurrent 16+16 threads): core-ms 0.998, units identical
  (358,188,724), 0 unit-diff jobs. The ledger is free.
- USER 2026-11-03: pause the rebalance after items 1-2 (test 5 + tail corpus on HEAD), push to the SHARED branch, then run
  the pipeline overnight picking up Cyclone THROUGH THE ARTIFACT QUEUE. Re-price / pooled bottoming / EDF wastes = later.
- Rebased onto origin 50b685d98 (ManaOracle conflict: origin's AgeIsInert end-board fold + our MidTurnStateKeyOfPayment;
  GT conflicts taken from origin, to be re-accepted). 0e41b20ba commits Cyclone's screened value_play d1/b1 K=21 block
  (the queue freezes on the tip, so it must be committed).
- RUNNING build/scratch/budget/run_finish.sh (task bb9lbw8j8 -> finish.DONE): build, smoke+regression, wins_diff vs
  origin GT (origin_gt/), then tail_corpus.json alone at 32 threads (tail_fin.log/.err, MTG_MAX_GAME_WALL_SEC=3600).
  Prior tail (abx3, pre-EDF-collapse binary): 44/45 < 10 min; edf_keepab_static_gi821 hit the 3600 s wall at X=0 and X=100.
- RUNNING run_cycprobe.sh (task bqvz10tep): scratch COPY of the Cyclone journal under the rebased binary -> does the
  resume gate accept play digest a7b3171c556b4179? If not, MTG_KEEP_RETAIN_FOREIGN=a7b3171c556b4179 is a USER decision
  (asked 13:20 UTC; rec: retain).
- Cyclone journal: originals in .claude/worktrees/cyclone-landing905 (+ .prestart-20261103T1300 copies, .bak1/.bak2,
  archive build/scratch/cyclone-mullgen-run-20261031/cyclone-paused-journal-20261102.tgz sha e35aeaf1...).
- QUEUE ROUTE after the push: (1) worker tree .claude/worktrees/artifact-queue/.claude/worktrees/artifact-worker-
  artifact-queue-d7d6a5 (clean, no inflight) -> checkout --detach origin tip; (2) copy journal + gencache.json into its
  deck dir; (3) drop "unpredictable_cyclone" from exclude in scripts/artifact_queue.json on artifact-queue-tool (+
  mulligan_env MTG_KEEP_RETAIN_FOREIGN if the USER says so), push, pull into the tool checkout; (4) start the worker
  harness-tracked with the GIT_CONFIG gh-credential env + MTG_RSS_CAP_GB=20, confirm gen.log says RESUME(journal).

## Live at compaction #5 (2026-11-03 ~13:55 UTC) -- USER going to bed; overnight = tail -> shared push -> Cyclone queue
- DONE: test 5 (ledger free). Rebased onto 50b685d98, GT accepted 82b9ea50f (pushed to budget-rebalance; CI watch task
  bfqr14wry). All 5 combination-only changes are ORIGIN-STALE (origin-baseline worktree now at 50b685d98, built,
  reproduces hinata d0 gi60/gi316, alchemist d0 gi430, elementals gi46, th gi467 digests).
- Cyclone digest PROBE PASSED on the rebased binary: play digest a7b3171c556b4179 unchanged, discovery cache hit (21
  buckets), RESUME(journal) reloaded 1,647,274 cell-sides. NO MTG_KEEP_RETAIN_FOREIGN needed. (USER: "It should resume on
  the branch.") Scratch copy build/scratch/budget/cycprobe/ (disposable).
- RUNNING: build/scratch/budget/run_tail.sh (task bbig4axnz -> tail.DONE; tail_fin.log/.err). Item 2 = no game at the
  3600 s wall backstop except possibly edf_keepab_static_gi821 (EDF combo route has no budget checks; known, surface it).
- NEXT (in order, all mine):
  1. Read tail.DONE: report max wall / which games > 10 min. A game at the wall other than gi821 -> investigate before push.
  2. PUSH TO SHARED: git fetch; if origin moved, rebase budget-rebalance onto it (src change -> rebuild + smoke check;
     GT conflict -> regenerate per CLAUDE.md); then `git push origin budget-rebalance:phase-1-2-deck-analyzer`.
     Watch CI (do not wait on it).
  3. CYCLONE VIA QUEUE: worker tree .claude/worktrees/artifact-queue/.claude/worktrees/artifact-worker-artifact-queue-d7d6a5
     (clean, no inflight.json) -> `git -C <wt> checkout -q --detach origin/phase-1-2-deck-analyzer`; copy
     `Unpredictable Cyclone.keepmodel.exhaustive.raw.json.journal` + `.keepmodel.gencache.json` from
     .claude/worktrees/cyclone-landing905/decks/Unpredictable Cyclone/ into <wt>/decks/Unpredictable Cyclone/ (cmp after);
     in the artifact-queue tool checkout (check `git status -sb` == origin/artifact-queue-tool) drop
     "unpredictable_cyclone" from "exclude" in scripts/artifact_queue.json, commit, push; start harness-tracked from that
     checkout: `MTG_RSS_CAP_GB=20 GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=credential.helper GIT_CONFIG_VALUE_0=
     GIT_CONFIG_KEY_1=credential.helper GIT_CONFIG_VALUE_1='!gh auth git-credential' python3 scripts/artifact_queue.py run`;
     set logs/artifact_queue/artifact-queue-d7d6a5/worker.pid; remove the stale STOP file there if `run` refuses.
     Confirm <state>/unpredictable_cyclone.mulligan.log / the deck's logs/*_mullgen/gen.log says RESUME(journal) ... continuing
     and NOT a fingerprint MISMATCH -- if it does, `artifact_queue.py stop` at once (journal originals stay in landing905).
     Then kill -0 waiter on the worker pid + replace cron 42d6d235 with one naming the worker.
  4. Close-out: delete cycprobe/, report to USER (tail result, push sha, CI, worker state). Later stages (re-price,
     pooled bottoming budget, EDF wastes) stay paused per USER.

## Tail corpus on HEAD (2026-11-03 ~13:38 UTC) -- item 2 DONE

45-game tail corpus, rebased build, alone on the box at 32 threads: **wall 247 s total**, 0 abandons,
0 wall backstops. Max game edf_keepab_static_gi821 **240 s** (won T7; it was at the 3600 s wall at
X=0 and X=100 before). Next slowest 87 s (kitty_v1vscur_2hg_cur gi20037), then 79 s, 67 s. Every game
< 10 min. Items 1-2 complete -> pushing to phase-1-2-deck-analyzer, then Cyclone via the queue.

## Overnight chain DONE (2026-11-03 ~13:58 UTC)

- Pushed budget-rebalance to phase-1-2-deck-analyzer: 50b685d98..0b7d581f5 (fast-forward; origin had not moved).
- Cyclone via the queue: `exclude` emptied on artifact-queue-tool, worker artifact-queue-d7d6a5 started
  (harness-tracked), journal copied into the worker tree (cmp OK) -> RESUME(journal) ... continuing.
- `build/scratch/budget/cycprobe/` deleted. Later stages (re-price uncharged work, pooled bottoming budget,
  EDF wastes) remain PAUSED per the USER. Scratch worktrees still to clean via safe_worktree_remove.sh:
  origin-baseline, mf-bisect, tier2-overflow, plan-priced-ledger, plan-priced-ledger-base.

## RESUMED budget work (USER 2026-11-03 23:54 UTC: "pause it for the time being ... get back to the budgeting changes")
- Cyclone mulligan PAUSED via `artifact_queue.py pause` at 73.0% frozen (resumable: `start` from the artifact-queue
  checkout; journal copy *.prepause-20261103T2355 in the worker tree). Status cron ff3f092e deleted.
- Stage now: RE-PRICE (plan item 2). Step 1 = quiet-box cost fit on HEAD: build/scratch/budget/run_fit.sh -> fit.txt /
  fit.DONE (regression manifest, 32 thr, MTG_TIER1_STATS). Then `scripts/cost_fit.py fit.txt` vs the provisional
  contended prices in src/core/WorkCounters.h.

## Live at compaction #6 (2026-11-04 ~00:50 UTC) -- RE-PRICE stage, prototype built, A/B not yet launched
- USER 2026-11-04: approved the plan -- renormalise on the MEDIAN deck, let the curve show what the heavy decks lose;
  "the 'raise budget' option is still on the table" per deck.
- QUIET-BOX FIT DONE (build/scratch/budget/fit.txt, run_fit.sh; 13,525 searched games >= 50 ms): prices ns
  step 21426, greedy 569, enum 18455, plans 2866, odo 511, apply 8097, pay 2561, simkey 3749, interior 586.
  priced actual/pred p95 1.57 (slowest 1%: 1.78) vs units-only 3.29 (7.45). Per-deck priced ns/unit (aggregate):
  goblins 14.8k ... median 28.3k ... hinata 40.4k, alchemist 45.7k, kittyv3 54.9k, dragonstorm 63.8k, edf 103.5k.
- SRC committed as WIP on budget-rebalance ONLY (NOT shared; prototype, default OFF => only the price constants change default play, via
  the X=100 priced ceiling's trip points):
  * src/core/WorkCounters.h: kPriceNs refit + kInteriorNs=586.
  * src/ai/DecisionWorkMeter.h: namespace pricedbudget at end of file -- UnitNs() (per-job arm valuearm
    priced_unit_ns wins, else MTG_PRICED_BUDGET + MTG_PRICED_UNIT_NS default 28300; 0 = off), Bill(n) = (t_acc +
    n*kInteriorNs + (t_priced - t_wm)) / R with carry, Drop(), Scope (decision-root RAII; t_active gate so only a
    FINITE decision root's work is billed; nested roots keep the outer's pending).
  * src/ai/TurnSolver.cpp ~1935: PricedUnits() in ConsumeAt / ConsumeAtN (null/unlimited/inactive => n, Drop);
    cache-hit replays (kCacheHit, ~4185) pass bill=false; CacheChargeNote still notes raw n (storability).
    pricedbudget::Scope at both decision roots (FullSearchLineHybrid fs_decision_root ~99949, SolveWithLookahead
    decision_root ~102372).
  * src/ai/ValueArm.h priced_unit_ns arm; src/runner/BatchRunner.cpp parses "priced_unit_ns".
- BUILD: first build OK (without the arm); rebuild WITH the arm running (task brnefxshf -> build_pb2.log). Check exit.
- NEXT:
  1. Sanity: one budgeted game priced on vs off (units must differ, no hang); one budget-0 game on vs off (must be
     byte-identical: inert at unlimited); unit tests (build/Release/mtg-test, 2450 expected).
  2. LAUNCH build/scratch/budget/run_pb.sh (harness-tracked) -> pb.DONE; manifest pb_ab.json from mk_pb.py: 736 jobs /
     126,580 games, arms x0 (off) / x28 (R=28300) / x20 / x14, held-out reg shapes seed+7e6 (budget-0 excluded) + 45
     tail-corpus games each. Runs from the budget-rebalance worktree. Read: `python3 scripts/ab_x_report.py
     build/scratch/budget/pb` (x0 = baseline). Report per-deck quality + tail per R to the USER; heavy decks (edf,
     dragonstorm, kitty, alchemist, hinata) are the ones expected to lose -> per-deck budget raise is the USER's call.
  3. Tier-2 purity under priced on: regression manifest, priced on, MTG_TIER2_OVERFLOW_KB=1 vs default -> 0 unit diffs.
  4. Then pooled bottoming budget; EDF remaining wastes.
- Cyclone mulligan PAUSED (resumable via `artifact_queue.py start` in the artifact-queue checkout).

## Live 2026-11-04 00:40 UTC: priced-budget A/B RUNNING (pb_ab.json, 126,580 games, 32 threads)
- Build trap: the first build_pb binary was compiled while ValueArm.h was mid-edit -> mixed Arm layouts across TUs
  (per-job priced_unit_ns never reached the reader; 9 Cyclone remand unit tests read a shifted ceiling). A clean
  rebuild fixed both: 2450/2450 tests. WIP 9f94d1087 (comment on the right field) is the A/B binary's tree.
- Sanity (sanity.json, 5 decks d3): budget-0 games byte-identical priced on vs off (digest + units, all 5); budgeted
  games move (EDF, Snow digests change), no hang.
- Arm meaning (corrected): R is priced ns per billed unit, so LOWER R = LESS search. x28 = median deck unchanged,
  x20 ~0.7x, x14 ~0.5x priced work per budget. Heavy decks (EDF 103.5k ns/unit) get ~0.27x at x28.
- Running: build/scratch/budget/run_pb.sh -> pb.log / pb.err / pb.txt, pb.DONE at the end (harness task blhevfl4r).
  Read: python3 scripts/ab_x_report.py build/scratch/budget/pb
- NEXT: report per-deck quality + tail per R to the USER (heavy-deck budget raise is the USER's call); then the
  tier-2 purity test under priced on (regression manifest, MTG_TIER2_OVERFLOW_KB=1 vs default, 0 unit diffs);
  then pooled bottoming budget; then EDF wastes. GT accept owed before any shared push (refit prices move play).

## RESULT 2026-11-04 01:04 UTC: priced-budget A/B DONE (logs/budget/pb_report.txt)
Held-out regression shapes, ~31.5k paired games per arm (sub-1ms games carry no SLOW-GAME line: 9/44/53/63 games
unpaired at x0/x28/x20/x14 -- all played). Win-turn delta vs x0 (+ = slower), wall over the held-out games:
| R (ns/unit)   | moved | better/worse | summed delta | /game   | wall sum | p99   | p99.9 | max   | tail corpus sum/max |
| off (x0)      |   -   |      -       |      -       |    -    | 11853 s  | 6.6 s | 30.2 s| 90 s  | 1635 s / 314 s      |
| 28300 (x28)   |  55   |   27 / 28    |     -1       | -0.0000 | 11572 s  | 6.3 s | 31.0 s| 98 s  | 1278 s / 275 s      |
| 20000 (x20)   |  84   |    9 / 75    |    +75       | +0.0024 | 10076 s  | 5.4 s | 25.9 s| 84 s  |  999 s / 171 s      |
| 14150 (x14)   | 162   |   21 / 141   |   +136       | +0.0043 |  8908 s  | 4.7 s | 21.6 s| 74 s  |  874 s / 162 s      |
- x28 per deck: kittyv3 +6 (0/6), edf +3, niv +3, alchemist +1 worse; cyclone -9 (12 better), goblins -4, landfall -2
  better. Heavy decks lose as expected; cheap-per-unit decks gain.
- The held-out TAIL is Cyclone's (23 of the 32 slowest games in every arm, ~27% of wall); at x28 Cyclone gets MORE
  search (+8% wall) and plays better. So x28 does not shorten the held-out tail; the tail corpus drops 22%.
- Decision is the USER's (R, and any per-deck budget raise for kittyv3/edf/niv). Running now: tier-2 purity under
  priced on (run_purity_pb.sh -> pp_a/pp_b, purity_pb.DONE).

## 2026-11-04 ~02:30 UTC: the priced budget was NOT DETERMINISTIC -- three channels fixed (WIP 4a8aba3fa)
- Found by the tier-2 purity run under priced ON (95/260 unit diffs). It was not tier 2: same binary, same jobs gave
  different units at 32 vs 32 threads and 2 vs 32. Root: the per-kind work counts (workctr) were not a pure function of
  the game -- harmless while only diagnostics read them, but the priced budget bills from them (and the SHIPPED X=100
  priced ceiling trips on them, so default play has had the same latent exposure, rarely triggered).
- Channels (found with a temporary per-caller kPay counter + ASLR-off repeat + memo key dumps):
  1. FluctKeepPayable 4-slot memo: slot hash had &cost_def (ASLR) + std::hash names (platform); outlived the game.
  2. manaoracle refine memo (t_refine_memo): outlived the game.
  3. BuildBreakpointKey folded g_incinerator_pin_cursor with no pins installed (cursor never reset, advances on every
     unpinned trigger) -> identical states keyed apart by earlier games' triggers (MidrangeBurn).
  Plus the tier-2 mana entry now records its solve's counted work and a tier-2 hit replays it.
- Checks so far: priced-off counts identical 2 vs 32 threads and with ASLR on; priced-on units identical 32/32/2 on a
  24-job subset; 2450/2450 unit tests. RUNNING: run_det_full.sh (regression manifest, priced ON: 16 vs 8 threads, and
  tier 2 default vs absent) -> det_full.DONE.
- CONSEQUENCE: the 01:04 A/B table above was measured on the non-deterministic build. Its per-deck x28 deltas (27 better
  / 28 worse) are within what scheduling alone moved; RERUN pb_ab.json on the fixed build before anyone decides on R.

## Live at compaction #7 (2026-11-04 ~02:45 UTC)
- Tree: budget-rebalance @ bef762a7e (src = WIP 4a8aba3fa: priced budget + refit + determinism fixes + tier-2 work
  replay). build/Release is that tree, clean (temp diagnostics removed). Nothing on the shared branch.
- RUNNING (harness tasks): run_det_full.sh -> build/scratch/budget/dA/dB/dC.{log,err}, det_full.DONE (priced ON;
  expect 0 unit-diff jobs for 16 vs 8 threads AND tier2 default vs KB=1); CI watch on bef762a7e.
- NEXT:
  1. Read det_full output. If any diffs remain: same method -- per-game workctr counts priced OFF across thread counts
     (detcmp.py), then a temporary per-caller kPay counter (return address via dladdr + addr2line), then memo-key dumps.
  2. Rerun the A/B on the fixed build: bash build/scratch/budget/run_pb.sh (pb_ab.json unchanged, 126,580 games,
     ~25 min), report with scripts/ab_x_report.py build/scratch/budget/pb. Report per-deck quality + tail per R to the
     USER (R = priced ns per billed unit; 28,300 = median deck unchanged; lower R = less search). Per-deck budget raise
     (Kitty v3 / EDF / Niv were the losers on the bad build) is the USER's call.
  3. Then pooled bottoming budget, then remaining EDF wastes. GT accept owed before any shared push (refit prices +
     determinism fixes move budgeted play through the X=100 ceiling).
- Cyclone mulligan generation stays PAUSED (USER).

## 2026-11-04 02:35 UTC: determinism VERIFIED at full scale; A/B rerun LAUNCHED
- det_full (regression manifest, 260 jobs, priced ON): 16 vs 8 threads **0 unit-diff jobs** (369,148,027 units both);
  tier-2 default vs absent (KB=1) **0 unit-diff jobs**. Priced OFF tier-2 purity was already 0 diffs.
- CI for bef762a7e: run 41758970791 (watch still pending at launch).
- A/B rerun on the fixed build (build/Release from 4a8aba3fa): `run_pb.sh` -> `build/scratch/budget/pb.{log,err,txt}`,
  marker `pb.DONE`. The non-deterministic run's outputs moved to `build/scratch/budget/pb_nondet/`.
  Report: `python3 scripts/ab_x_report.py build/scratch/budget/pb`. R choice + any per-deck raise are the USER's.

## RESULT 2026-11-04 ~03:05 UTC: priced-budget A/B on the DETERMINISTIC build (logs/budget/pb_report_v2.txt)
Same manifest/arms as 01:04. Numbers reproduce the non-deterministic run almost exactly, so that table stood.
| R (ns/unit)   | moved | better/worse | summed delta | wall sum | p99   | p99.9 | max   | tail corpus sum/max |
| off (x0)      |   -   |      -       |      -       | 11601 s  | 6.6 s | 27.9 s| 90 s  | 1640 s / 312 s      |
| 28300 (x28)   |  56   |   28 / 28    |     -2       | 11527 s  | 6.2 s | 31.5 s| 97 s  | 1277 s / 272 s      |
| 20000 (x20)   |  85   |    9 / 76    |    +76       | 10021 s  | 5.4 s | 26.3 s| 93 s  | 1000 s / 174 s      |
| 14150 (x14)   | 162   |   21 / 141   |   +136       |  8837 s  | 4.6 s | 21.4 s| 71 s  |  877 s / 160 s      |
- x28 per deck: kittyv3 +6 (0 better/6 worse), edf +3, niv +3, alchemist/dragonstorm/hinata/2hg singles +1;
  cyclone -10 (13 better / 4 worse), goblins -4, landfall -2. x28 is a REDISTRIBUTION (heavy-per-unit decks lose
  search, cheap ones gain), quality-neutral in sum; it barely moves the held-out tail (Cyclone's). Tail corpus -22%,
  but fungusB_2hg gi4525 grew 149 -> 272 s (t6 -> t7): a cheap-per-unit deck given more search.
- DECISION OWED BY THE USER: R, and any per-deck budget raise (kittyv3 first). Default taken meanwhile: priced budget
  stays OFF on the branch (nothing ships until the USER picks); work continues on the pooled bottoming budget.


## 2026-11-04 ~03:40 UTC: USER "leave Cyclone alone, focus on the fully complete decks"; plan items re-scoped
- Priced-budget A/B EXCLUDING Cyclone (held-out, ~31.1k games/arm):
  off 8500 s p99 4.65 p99.9 15.3 max 79 | x28 15/24 +8, 8130 s (-4%) 4.26/14.4/63 | x20 5/66 +66, 7160 s (-16%)
  3.60/12.6/65 | x14 10/124 +129, 6425 s (-24%) 3.13/11.2/39. Cyclone's -10 was what made x28 look neutral.
- POOLED BOTTOMING BUDGET -- DEFERRED (no target on complete decks). MTG_ROLLOUT_STATS + MTG_BOTTOM_TRIAL_TRACE (now
  names job/gi) on the x0 arm: with Cyclone 2,656 s of 12,368 s user (21.5%), 1,883 trials; WITHOUT Cyclone 286 s of
  9,516 s (3.0%), 39 trials, ALL in two tail-corpus EDF games played deliberately without the keep table
  (edf_keepab_static gi821, edf_keepab_lookahead gi214). No shipped config of a complete deck runs a bottoming trial
  (all have keep tables). It matters only for table-less decks (new decks, Cyclone, generation) -- revisit there.
- EDF REMAINING WASTES -- DEFERRED. They were found on edf_keepab_static gi821 (no keep table). Shipped EDF is 4.1% of
  held-out wall, max game 15.9 s. Held-out wall by complete deck: hinata 12.4%, snow 11.4% (max 78 s), melira 6.6%,
  alchemist 6.1%, fivecolour 5.4% (max 55 s), landfall, edf, niv ...
- NOW: rebased budget-rebalance onto origin 68a94b54d; rebuild OK, 2544/2544 tests. Running smoke then regression
  (priced budget OFF: refit prices + determinism fixes move budgeted play through the X=100 ceiling) -> accept -> the
  increment can land on the shared branch once the USER is happy. R / per-deck raise still the USER's call.
- TIERS on the rebased tree (budget-rebalance b00e7f153, priced budget OFF): smoke 169/169 PASS, regression 271/271 PASS
  (references included), BOTH with 0 play-changed / 0 slower / 0 faster games vs committed GT. Nothing to accept: the
  refit prices + determinism fixes + tier-2 work replay are byte-identical on both tiers. Not pushed to the shared
  branch (feature not complete until the USER rules on R / per-deck raise).

## 2026-11-04 ~05:00 UTC: USER direction + per-deck R result
- USER: lower R + price the budget appropriately makes sense, BUILT IN so budget settings do not change; the TOP point is
  that expensive cases must be priced so a LOWER BUDGET BRINGS COST DOWN ("far too many examples of us using budget 1-3
  and it taking hours"). Also: show any cases giving up a little quality for much better tail/total.
- Per-deck R (each deck's own aggregate ns/unit from fit.txt; ps_ab.json, Cyclone excluded, fresh x0 on the rebased
  binary): 31,082 paired, 27 better / 10 worse, summed -20 (sign p~0.01); wall 8789 -> 8866 s (+0.9%), p99.9 14.8 ->
  16.3, max 78 -> 75. Within-deck redistribution is a QUALITY win at ~same cost; it does not shrink the tail.
- Low-quality-cost candidates (pb.err per deck, R lowered for that deck == budget x0.71 / x0.5): snow x14 0.61x wall
  +1/325 max 79->39 s; fivecolour x20 0.85x +1, max 52->14; melira x20 0.89x +0 / x14 0.78x +2; landfall x20 0.85x +0;
  dragonstorm x20 0.69x -2; alchemist x20 0.83x +3; colossus/selesnyav2/pd/mirrorwing 0.59-0.90x, 0..+1. NOT: hinata,
  edf, kittyv3, niv, bruna, elementals, creature. CONFIRMING: conf_ab.json (4x games, seeds +2e6, x0/x20/x14) ->
  conf.{log,err}, conf.DONE; report with ab_x_report.py build/scratch/budget/conf.
- Leak: fungusB 2hg gi4525 (d7 b1000) 109 s -> 373 s under pricing (units 4.8M -> 9.3M): priced work escapes the bill
  somewhere. Single-game census running: leak/f4525_x{0,1}.census.tsv.
- Low-budget generation replay (the USER's top point): build/scratch/budget/replay/Melira Pod (copy: cod, profile,
  buckets, value.json, gencache) + melira_slow.log (302 SLOW-ROLLOUTs, 19,153 s logged, d3 b3). Replay =
  MTG_DECISION_WORK_X=1000 MTG_KEEP_BENCH=1 MTG_KEEP_REPLAY_LIST=<log> [MTG_KEEP_BENCH_BUDGET=b] mtg-analyze <copy>.cod
  --gen-mulligan fast. The gencache fingerprint MISMATCHES -> discovery reruns once (11,200 rollouts); do it on a
  quiet box at 32 threads, then arms budget {1,3,10} x priced {off,on}.
- CONFIRM (conf_ab.json, 4x games, seeds +2e6; dq = summed win-turn delta, + worse): snow x20 +1/1300 0.72x wall p99
  28.3->18.5 (CANDIDATE); selesnyav2 x14 0/1800 0.74x; colossus x14 -1/1767 0.77x; mirrorwing x20 +3/2100 0.86x
  (marginal). NOT small: alchemist +13, landfall +14, dragonstorm +8, fivecolour +8, melira +6, pd +3 at x20.
  The 325-game first read was too small. Proposed to the USER (their call): Snow 0.7x, Selesnya v2 / Colossus 0.5x.
- fungusB gi4525 is NOT a leak: billing exact (each big decision ~2.4-2.7M billed units both arms); the deck is cheap
  per unit (21.9k) so pricing at 28.3k gives each decision 1.4x priced work, and the game went a turn longer.
  Per-deck R removes it by construction.
- MELIRA LOW-BUDGET REPLAY (replay/mrep_b{1,3,10}_p{0,1}.out; 6 arms concurrent, 5 cores each): only 32 of 302 slow
  lines replay (215 seed mismatches + 55 label skips: bucketing changed). Those 32 logged 2,468 s at b3; now 28 s
  (max 12 s): the historical hours-long cases are FIXED on HEAD. Ordinary rollouts: b1 0.21 s, b3 0.35 s, b10 0.54 s
  (10x budget = 2.6x cost: a FLOOR). Pricing ~no effect (Melira ~median price).
- RUNNING: scale_ab.json -- 40 complete decks, d3, b{1,3,10,30} x {x0, x1=per-deck R}, 100 games each, MTG_TIER1_STATS
  -> scale.{log,err,txt}, scale.DONE. Question: where does cost stop falling with budget, and which work is the floor.
- SCALING SWEEP (scale_ab.json, 40 complete decks, d3, 100 games/cell): total wall b1/b3/b10/b30 = 690/964/1531/2332 s
  (x0) and 695/950/1459/2342 (per-deck R); max game b1 15 s, b3 28 s -- NO hours-long games on HEAD. Budget 1 = 30% of
  budget 30 (a FLOOR). Worst floors: giants 75%, edf 57%, galadriel 55%, angels 52%, alchemist 47% (giants/galadriel/
  angels are cheap in absolute; EDF/alchemist/hinata ~0.8-1 s/game at b1). EDF b1: pay = 60% of priced work.
- FLOOR MECHANISM (leak/edf_b1_x28.census.tsv): at b1 the decision budget is L=900 units; EDF decisions bill p50 6x,
  p90 26x, max 129x L under median R (overrun ceiling 25x L, escalation, fresh budgets per bp re-solve). Median R cuts
  EDF only 0.90x at b1 (billed units 3.4x, priced 0.89x): the work is not stopped by the budget.
- PRICED CEILING SWEEP (ceil_ab.json; 8 expensive decks x 200 games, priced budget OFF; dq vs X=100):
  b1: X30 0.92x +10, X10 0.70x +48 | b3: X30 0.92x +1 (hinata max 27.4 -> 13.5 s), X10 0.82x +17.
  X30 at b3 ~free; earlier play-budget tail corpus found X30 lossy (kittyv2arch gi1723 4->6) -> low-budget-only = USER.
- RUNNING: c40_ab.json -- all 40 decks, b1/b3, X100 vs X30, 200 games -> c40.*, c40.DONE.
- C40 (all 40 complete decks, 200 games, X30 vs X100, priced budget off): b1 0.93x wall, p99.9 7.9 -> 5.8 s, max 14.9 ->
  12.5, dq +3 (3/5 of 7,971); b3 0.95x, p99.9 12.7 -> 10.3, max 26.4 -> 20.6, dq +3 (0/3 of 7,983). Modest.
- OPEN, ALL USER CALLS: (1) build-in shape for per-deck R (fit table in the profile vs derived in-engine); (2) Snow 0.7x /
  Selesnya v2 + Colossus 0.5x search; (3) a tighter priced ceiling (X30) at low budgets; (4) push the tier-neutral
  increment (refit + determinism fixes) to the shared branch now or with the feature.

## 2026-11-04 07:00 UTC -- do the historical slow GENERATION rollouts still happen? (RUNNING)

USER: the "budget 1-3 taking hours" cases are mulligan-generation rollouts (Melira d3 b3 single rollouts of
10.2 h / 8.4 h / 6.1 h; EDF 3.9 h; Kitty ~1 h). "If they don't really anymore then we may be optimizing for a
problem that doesn't happen as much anymore." Start with every slow rollout on record.

- New diagnostic `MTG_KEEP_REPLAY_LOGGED_SEED=1` (ExhaustiveKeep.cpp): the replay list runs each line from
  its LOGGED seed + hand (`run_rs`, split out of `run_one`), so logs written under older buckets replay
  (the earlier Melira replay lost 215 of 302 lines to SEED MISMATCH). Labels that are not current bucket
  reps fall back to the card name's bucket.
- Corpus (`build/scratch/budget/hist/mk_corpus.py`): union of every slow log of the CURRENT lists, deduped:
  Melira 1,243 (64 core-h logged), EDF 3,959 (126), Kitty v3 19,442 (661), Snow 685 (457 replayable:
  Kaldring left the list), LFE 372, Goblins 187, Creature Giving 30, slivers 6. Archived lists (Kitty v2,
  Selesnya v1, Mirrorwing v1/v2) excluded. Cyclone has NO slow log: its gen is d1 b1 and its worst rollout
  is 8.3 s. Hinata2's log holds only discovery / play-digest lines.
- Run: `hist/run_hist.sh` (+ `run_hist2.sh`, Melira/LFE relaunched at 6 GB after 3/2 GB RSS caps
  aborted them), each deck at its CURRENT gen setting, buckets from the shipped table
  (`MTG_KEEP_BUCKETS_FROM`, no discovery), Release, disjoint cores. Report: `python3 hist/report.py`.
- First results (worst-logged first): Melira 10.2 h -> 1.2 s, 8.4 h -> 0.7 s, 2.1 h -> 21.6 s; Goblins
  37.6 min -> 0.7 s; Kitty 1 h-class -> 23-37 s; still-slow: Goblins 98 s / 90 s, Creature Giving 46 s.

### 08:00 UTC -- interim: the tail is NOT gone; it moved, and depth 1 is where it lives
- 5,064 lines replayed so far. Deep gen settings (Melira d3/b3, Kitty d2/b3, Goblins d6/b6): the famous
  hours-long rollouts are now seconds-minutes (Melira 10.2 h -> 1.2 s; Kitty ~1 h -> 23-79 s, ratio 3%).
  But 31 lines got SLOWER than logged, 11 of them Goblins >2x and >60 s (worst 43 s -> 1,624 s; Melira
  405 s -> 1,396 s). Confound to rule out first: the replay's per-process RSS caps (2-6 GB) shrink the
  shared plan / tier-2 pools vs generation's 20 GB (`probe/run_gob.sh`: branch vs primary binary, cap 20).
- d1/b1 decks (EDF, LFE -- their real gen setting): the worst hands STILL run 20-30+ min (LFE four in flight
  at 17-31 min when it hit a 6 GB cap; EDF six in flight > 20 min). Budget-independent: one EDF hand is
  59 ms at d5/b20 and 357 ms at d5/b1 but > 20 min at d1/b1, d1/b3 AND d1/b20 (`probe/run_probe.sh`).
- MECHANISM (perf on the stuck d1 probe; census shows ONE decision that never closes): SolveWithLookahead ->
  FullSearchLineHybrid -> FSLineWinComplete -> FSLineWin -> SimulateToEnd -> GameEngine::RunTurnFrom ->
  AIEngine::TakeTurn -> BpDeriveContinuationList / EnumeratePlansWithLand (+ TapForCostSharedOnce). At depth 1
  every root candidate is followed by a whole-game playout INSIDE the decision; the playout's per-turn plan
  enumeration explodes on Displacer/Drake boards. The priced ceiling (X=100 => ~2.25 s at b1) is only checked
  between passes (budget->Overrun()), never inside the playout, so nothing bounds it.
- Side finding: a direct `mtg-analyze --gen-mulligan` on a deck whose value sidecar lacks mull_gen_* runs at
  gen-default d5/b20 (sidecar value_play replaces the profile's keys). mullgen.sh carries the setting over
  first, so production is fine; affects Colossus Hammer, EDF, Landfall, LFE, Midrange Burn on direct calls.

### 08:55 UTC -- Goblins: a 35-50x ENGINE cost regression on the same play; bisect RUNNING
- Worst Goblins line (seed 12671395107054027213, size7 draw r=26; logged 43 s at d6/b10 on 719cffd38,
  10-25). Same line, d6/b10, cap 20 GB, single core, win turn 4 in EVERY run (same play, only cost moved):
  old binary 719cffd38: 42.7 s (10-25 deck inputs) / 39.6 s (current inputs) -- reproduces the log;
  primary build/Release (built 11-03): 1,103 s; this branch: 1,479 s (current inputs) / 2,164 s (old inputs).
  Inputs barely matter -> it is CODE, landed between 719cffd38 and 11-03. (At b6, today's setting: primary 586 s,
  branch 970 s; branch at a 2 GB RSS cap 1,624 s -> small caps cost another ~1.7x.)
- BISECT (RUNNING, background): worktree `.claude/worktrees/gob-bisect` (detached, scratch), `git bisect start
  68a94b54d 719cffd38 -- src`, `git bisect run build/scratch/budget/hist/probe/bisect_step.sh` (build Release,
  replay the line; GOOD < 120 s, a probe still running at 240 s is stopped and logged BAD). Progress:
  `build/scratch/budget/hist/probe/bisect.log`; full output `probe/bisect_run.log` (prints "first bad commit").
  Step 1: 9ebe3df41 BAD. When done: `git -C .claude/worktrees/gob-bisect bisect reset`, read the culprit,
  then remove the worktree with scripts/safe_worktree_remove.sh.
- Melira (all 1,239 lines, d3/b3, branch binary, 6 GB cap): total 7.5 core-h vs 64.1 logged (12%), but the
  tail stays: 4,167 s (logged 1,994 s), 3,965 s (logged 6.1 h), 2,296 s (logged 226 s), 1,396 s, 872 s ...
  -- the Carrion Feeder family; several got WORSE, likely the same regression as Goblins.
- STILL RUNNING: Kitty (~8k of 19,442 lines; max 79 s, ratio ~3.7%) and EDF at d1/b1 (`run_hist3.sh`,
  stuck on its 6 worst hands > 80 min). EDF single-hand probe d1/b1 (census) still running (> 60 min on a hand
  that is 59 ms at d5/b20); d1/b3 and d1/b20 probes stopped by me at > 60 min. LFE d1/b1 aborted at its 6 GB
  cap with four rollouts in flight at 17-31 min.
- OPEN USER CALLS (new, in addition to the four above): (5) the Goblins regression fix once bisected;
  (6) the depth-1 playout explosion -- bound the in-playout work by the decision's priced ceiling (a play change
  at low budgets; held-out A/B) and/or EDF/LFE's generation depth (a settings call); (7) hold decisions 1-4
  until 5-6 are settled (recommended).

### 09:45 UTC -- USER RULINGS on 5-7; 6(a) step 1 RUNNING
- USER 2026-11-04: (5) wait for the actual commit; a mistake -> just fix it; intentional -> still dig for ways
  to avoid paying such a high cost. (6) try (a), bound in-playout work by the decision's priced ceiling (the
  others are less connected to budgeting). (7) HOLD 1-3 unless they are shown to help the HEAVY TAIL; not keen
  on a new per-deck setting for a few percent.
- Replay results (report.py): Kitty v3 16,984/19,442 lines, max 79 s, 5% of logged -- its tail is gone.
  EDF d1/b1: 7,838 s (logged 7,125) and 6,058 s (logged 14,213) still; 7 more lines in flight > 2 h.
  Melira d3/b3 max 4,167 s. Snow / Creature Giving / slivers max 47 s.
- Bisect step 2: 4df5e5201 GOOD (28.6 s); culprit in (4df5e5201, 9ebe3df41].
- The stuck d1/b1 decision (perf, live): SolveWithLookahead -> SimulateToEnd -> ApplyPlanDirect -> ApplyPlanDirect
  -> BpDeriveContinuationList -> BpEnumEntryFor -> EnumeratePlansWithLand -> EnumeratePlans -> TapForCostSharedOnce:
  a breakpoint-continuation DERIVATION inside one rollout step. The existing MTG_BP_DERIVE_PRICED_STOP (11-03,
  OFF by USER ruling "only for things expensive enough to be a real issue"; it bought nothing on d5/b20 play)
  polls exactly this walk. Step 1 of (a): measure it on the d1/b1 gen tail -- `hist/stop/run_stop.sh` (EDF top
  14 lines on cores 20-23, LFE top 30 on 24-27, X=100) vs the off arm in hist/*.out. Out: hist/stop/{EDF,LFE}.out.
- CAUTION reading probe/census_d1b1.tsv: its rows are the PLAY-DIGEST games, which ran at d5/b20 (the sidecar
  mull_gen bug), not the d1/b1 replay; the d1/b1 decision never closed, so it has no row.
- Small fix EDITED, NOT BUILT (the running replays use this worktree's binary): MulliganProfileIO.h -- a sidecar
  without mull_gen_* / expected_buckets keeps the profile's values (generation-only keys; play-neutral).

### 10:45 UTC -- 6(a) PRICED_STOP measured on the gen tail; Goblins culprit found; play A/B READY
- PRICED_STOP=1 vs off, rollout replays paired on (size side r, seed), `python3 build/scratch/budget/hist/stop/pair.py`:
  4,312 paired lines, **0 win-turn diffs**. EDF d1/b1 10 lines 47,146 s -> 236 s (13,124 s -> 109 s max; single-hand
  probe 8,613 s -> 10.9 s); LFE 13 lines 1,237 -> 312 s; Melira 1,085 lines max 4,167 -> 3,690 s (its tail is NOT
  derivation -- next to investigate); Goblins 187 max 1,650 -> 996 s; Kitty top-3000 / Snow / CG neutral.
  Still running at compaction: stop/run_stop2.sh (Melira, ~160 lines left) and stop/run_stop3.sh (Kitty top 3000).
- GOBLINS (5): bisect first bad = d2184261b MTG_SUBSET_SAC_OUTLETS (intentional correctness fix, default ON). Branch,
  line d6/b10: on 1,479 s, SAC_OUTLETS=0 80 s, DEV_SEQ=0 1,473 s (not it), both 92 s; win turn 4 everywhere. Census
  (probe/gc_on.tsv vs gc_off.tsv): space_odo 635M vs 31M, pay_calls 231M vs 8.8M, dupc_sac_for_mana 66M vs 1.5M, plans
  only 4.6x -> waste = selections the one-use-per-sac-source dup rule rejects AFTER pay/rules work. SUBAGENT running
  (worktree .claude/worktrees/sac-fold, branch sac-fold, cores 26-29,31, scratch build/scratch/budget/sacfold/):
  exact fold so the walk never visits those selections. Bisect worktree reset + removed (refs check OK).
- UNCOMMITTED src in this worktree (built 10:4x, unit tests NOT yet run): MulliganProfileIO.h mull_gen fix; per-job
  arm `bp_priced_stop` (ValueArm.h, BatchRunner.cpp parser, TurnSolver.cpp BpDerivePricedStopOn replaces the static
  env read). Next: run build/Release/mtg-test, commit.
- PLAY A/B READY (not launched): build/scratch/budget/mk_ps.py -> pstop_ab.json (368 jobs, 63,290 games, arms _x0 = stop
  OFF / _x1 = stop ON, X=100); run build/scratch/budget/run_pstop.sh (cores 0-25, threads 26) once the stop2/stop3
  replays finish; stop my EDF off-arm replay (run_hist3.sh, pid 907159) at launch. Read: scripts/ab_x_report.py
  build/scratch/budget/pstop. Adoption = default ON if every deck net <= 0, then smoke/regression GT accept.

### 11:50 UTC -- 6(a) ADOPTED on the branch: MTG_BP_DERIVE_PRICED_STOP default ON (7d44be043, pushed)
- Gen tail (stop/pair.py, final): 4,936 paired lines over 7 decks, 0 win-turn diffs (EDF 10, LFE 13, Melira 1,239,
  Goblins 187, Kitty top-3000, Snow 457, CG 30). EDF 47,146 s -> 236 s on its paired lines.
- Play A/B (pstop_ab.json, 26 thr, 20 min): 31,588 paired held-out games, 0 win turns, 0 UNITS moved; tail corpus 45/45
  identical -> in play at shipped settings the stop never trips, so smoke/regression GT should be byte-identical.
- REVERSES the 11-03 USER ruling ("PRICED_STOP stays OFF") on the strength of the gen-tail data + USER's 11-04 "give (a)
  a try" -- SURFACED to the USER for confirm/veto; one-line revert (EnvOn default in BpDerivePricedStopOn).
- Also committed: 31170cccd (sidecar mull_gen fix), 73afdd405 (per-job arm bp_priced_stop). mtg-test 2544/2544.
- Remaining heavy tail: Melira d3/b3 (6 lines > 600 s, max 3,690 s with the stop; Carrion Feeder family) -- NEXT.
  Goblins 996 s max -> the sac-fold subagent (worktree sac-fold) is on it. LFE replays hit 5-6 GB RSS caps (exit 137)
  at 4-7 threads: a memory question to look at.

### 12:25 UTC -- PRICED_STOP default ON stands (USER); Melira tail is not SAC_OUTLETS
- USER 11-04 on the 11-03 reversal: "If I ruled on it and there is new information that shows it is worthwhile then
  that can be retracted. What matters to me is the impact in terms of quality and performance." -> default ON stays.
- Melira d3/b3, three ~650 s lines (hist/mel/run_mel.sh, single core, X=1000): default vs MTG_SUBSET_SAC_OUTLETS=0
  274/276, 294/291, 517/520 s, win turns equal -> NOT the Goblins mechanism. Solo cost is ~2.4x below the contended
  4-thread replay. Turn census on the two worst lines running (hist/mel/run_mcen.sh -> cen_<seed>.tsv, cores 6-7).
- Sac-fold: first subagent hit the 250K cap; commit 9984a7572 on local branch sac-fold, exact (verify: 19,983,527
  flagged, 0 passed the dup check, 0 brute mismatches; 25-job regression sample digests = GT, units equal, wall
  354 -> 148 s) but does NOT fire on the Goblins gen line (other walk). Handoff build/scratch/budget/sacfold/HANDOFF.md;
  a second subagent continues from it.

### 12:55 UTC -- sac-dup fold ADOPTED on the branch; budget work PARKED for the Cyclone queue run (USER)
- 118b8ef36 MTG_SAC_DUP_WALK_FOLD (default ON, exact; cherry-picked from local branch sac-fold 9984a7572) + f8e7f0f64
  per-job arm `sac_dup_fold`. Verify: 91.6M flagged on the Goblins gen line / 20.0M on the 25-job regression sample,
  0 passed the dup check, 0 brute mismatches, 0 unfolded. 25-job sample digests = GT. Held-out A/B (mk_sdf.py ->
  sdf_ab.json, goblins+goblins2hg, +8e6, X=100): 675 paired, 0 moved, wall 208 -> 95 s. mtg-test 2544/2544.
  Gen line: dupc_sac_for_mana 40.3M -> 0, enum_enter -29%, wall only -2% (cost there = 140M pay_calls).
- Not done, candidate exact skip: dupc_fold_prefix (61M/line with the fold) in the two-stage driver; needs a
  straddle-aware prefix map (see build/scratch/budget/sacfold/HANDOFF.md RESOLVED section).
- MELIRA TAIL (hist/mel/cen_<seed>.tsv, solo single core): worst lines 1,729 s / 1,568 s, 3-4 decisions on T5-T6 hold
  ~95%. Each: enum_emitted / space_plans ~1.1e8 plans, space_odo ~2.2e8, w_odo 3.5-5.7M -- but units 3.2K-148K
  (~100 ms/unit). So the cost is a huge UNBILLED plan space (likely Carrion Feeder / Kitchen Finks sac-target
  subsets), not rejection waste. NEXT: name the plan family (enum census of one heavy decision), check whether it is
  an identity fold candidate, and whether the priced ceiling (X) bills w_odo here -- replays ran X=1000.
- PARKED: the box goes to the artifact queue (Cyclone mulligan resume) per USER 2026-11-04. No engine runs of mine
  are active. Branch budget-rebalance is NOT on the shared branch (feature incomplete).

### 2026-11-05 22:05 UTC -- RESUMED (USER: "pause the niv mizzet run, so we can pivot back to the budget rebalancing")
- Queue: Cyclone DONE + LANDED on phase-1-2-deck-analyzer (e4ad12898 artifacts, e7882bfb7 GT). NivMizzetCombo mulligan PAUSED
  (worker d7d6a5 stopped; claim paused; `python3 scripts/artifact_queue.py run` in .claude/worktrees/artifact-queue resumes it).
- Melira tail, sharpened: in cen_10372713029942101962.tsv the heaviest decision (T6 main-2, budget 2,700 units) ran 631 s against
  a PRICED ceiling of 2,700 x 25,000 ns x 100 = 6.75 s (93x over) -- the census already ran at the shipped priced X=100
  (MTG_DECISION_WORK_X=1000 there is the separate UNIT ceiling). The enumeration never polls decisionwork::PricedExceeded()
  (only the BP-derivation stop and SearchBudget::Overrun do). 1,400 enumeration calls emit 116M plans (~83K per call),
  space_dedup 44M; board = 3x Carrion Feeder + 3x Kitchen Finks + Bloodthrone Vampire in hand.
- SUBAGENT running (worktree off 108072a7e, local branch mel-fold, scratch build/scratch/budget/melfold/): name the plan
  family, build an EXACT identity fold if one exists (verify mode + per-job arm + digest equality), and REPORT (not build)
  where a priced poll in the enumeration would bound it. Order follows "collapse wasted search first".

### 2026-11-05 ~23:00 UTC -- REBASED onto phase-1-2-deck-analyzer; Melira dup-action fold ADOPTED; tiers RUNNING
- Rebased onto origin/phase-1-2-deck-analyzer (one trivial conflict, TurnSolver.cpp ConsumeAtN signature next to the new
  lahist block); pre-rebase head tagged locally br-prerebase-20261105 (69c463748). check_gt_logs 1002 consistent; branch GT
  == shared GT. mtg-test 2593/2593. Force-pushed 1a58a0e35; CI green (ubuntu, windows, determinism parity).
- 88b398400 MTG_DUP_ACTION_FOLD (default ON, exact; cherry-picked from local branch mel-fold 13feb049b): in EnumeratePlans'
  no-mana-side walks, independent actions equal in every field but `eval` (an outlet's canonical single sac of the
  counter-less Kitchen Finks vs the persist block's explicit K=1 sac of the same Finks) are walked only in canonical
  (eval-desc, slot) prefix order; the signature dedup ignores eval and eval feeds only plan.value, so the returned list is
  identical. Verify (MTG_DUP_ACTION_FOLD_VERIFY): Melira worst line 177.9M flagged, KEPT_BY_DEDUP 0, BRUTE_MISMATCH 0;
  regression tier 44,689 flagged, 0/0. Full regression tier 265/265 digests = GT (verify and on arms; re-checked by me
  with melfold/scripts/cmpgt.py); units moved on 3 Melira jobs only. Worst line (cap 3 GB): emitted 302M -> 127M, wall
  2,187 -> 1,473 s; heaviest decision 1,205 -> 548 s. Per-job arm "dup_action_fold". mtg-test 2594/2594.
- What remains of that tail is NOT exactly foldable: 110,591 plans -> 3,363 distinct end states, but which body dies
  matters (Celes counters, persist return), so only a state-level dedup (MTG_CAND_DEDUP, default off, lossy question)
  could collapse it.
- PRICED CEILING on that decision (fold subagent, reported not built): a poll in the enumeration would NOT bound it.
  (1) The priced ledger (MTG_PLAN_PRICED_LEDGER) bills only what an UNLIMITED plan cache would recompute; the 110K-plan
  lists are refused by the real cache and recomputed muted -> only 4.9% of visits billed (=0 probe: decision stops at
  2,896 units / 182 s instead of 631 s). (2) Even unmuted, emitted plans (build/sort/dedup) are unpriced: ~20x under.
  CONFOUND being checked: those replays ran at MTG_RSS_CAP_GB=3 (the cache pool derives from it); generation runs at 20.
- RUNNING (harness-tracked, build/scratch/budget/chain_tiers.sh): smoke -> regression on 88b398400 (logs tiers/*.log,
  markers tiers/{smoke,regression,probe}.DONE), then melfold/run_cap.sh: worst line at cap 20 then cap 20 +
  MTG_PLAN_CACHE_KB=0 (out melfold/cap/status).
- NEXT: if tiers match GT -> push to shared branch (feature = re-pricing + priced stop + sac fold + dup-action fold).
  Hand-off doc for the rest (docs/design/budget-rebalance-followups.md).

### 2026-11-06 -- LANDED on phase-1-2-deck-analyzer (rebased onto df30bfce1); NO GT accept needed from this branch
- First tier run (on 36d3672bc): 4 equal-score EDF/Bruna digest movers, reproduced by origin's own binary (origin drift from
  a705f3dea); origin's dfd675afb accept then covered them, so that accept was dropped in the second rebase.
- Second rebase onto df30bfce1 (13 new origin commits, Shadowborn levers default ON): rebuilt; smoke 164/176, regression
  262/282, mtg-test 2609/2611. ALL 32 moved cells and BOTH unit failures (test_landfall.cpp:216 fetchland drop = two
  landfall triggers, gets one; test_pirates_provider.cpp:371 47 deck folders vs 46) reproduce EXACTLY on origin
  df30bfce1's own binary (build/scratch/budget/ctrl2/, worktree .claude/worktrees/origin-ctrl): origin drift from
  25261417f "Analyze Modern RW Burn and gate fetchland hold search" (Burn / AntiLife / FiveColour; AntiLife d0
  4.892 -> 4.999, FiveColour d0 5.456 -> 5.566 are WORSE). NOT accepted here -- another session's unexplained change;
  surfaced to the USER. This branch moves nothing relative to origin's binary.
- Melira cap probe (melfold/run_cap.sh, worst line, fold build): cap 3 GB ~1,473 s; cap 20 GB 644 s (logged 1,993 s),
  win turn 7; unbounded plan cache killed at the 20 GB RSS cap -> a real pricing gap; deferred.
- FEATURE LANDED: re-pricing refit + determinism fixes, MTG_BP_DERIVE_PRICED_STOP, MTG_SAC_DUP_WALK_FOLD,
  MTG_DUP_ACTION_FOLD (+ per-job arms; MTG_PRICED_BUDGET prototype stays default OFF). Further work:
  docs/design/budget-rebalance-followups.md.
