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
