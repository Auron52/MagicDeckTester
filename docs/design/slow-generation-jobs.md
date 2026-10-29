# Slow generation jobs -- tracker

USER 2026-10-27: *"Let's keep track of the slow jobs. It might be necessary to do some optimizations."*

One section per deck/stage whose generation spends hours in a tail of degenerate rollouts. Each entry records
the evidence (where the numbers came from), the hands, and a mechanism *hypothesis*. A hypothesis is not a
diagnosis until a single-rollout repro has been profiled (census / odometer, see
`keepgen-cost-concentration.md`). Add a new run's numbers to the deck's section rather than a new section.

Machine context: this box has 23.5 GB RAM, and every engine run sets `MTG_RSS_CAP_GB=20`. The planned +16 GB
upgrade would raise that headroom; the cap must be re-derived after it is fitted. A run that reaches the cap
aborts itself, so the journal and its rolling backups bound the loss.

## Melira Pod -- mulligan (keep table), recipe `fast` (R=30), d3 b3, K=23

Run: artifact worker `artifact-queue-d7d6a5`, frozen at `60f525f17`. Generation binary: PGO, src tree
`aba41d81c`. The run started 2026-10-25 19:16 UTC, was paused by the USER on 2026-10-26 22:22 UTC (27 h in), and
resumed 2026-10-27 12:25 UTC.

Evidence: `decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.slow.log` (rollouts over 30 s) and
`logs/Melira_Pod_mullgen/gen.log` (monitor lines and the top-3 list), both in the worker's frozen worktree.
Snapshot at 2026-10-27 19:11 UTC:

- **874 SLOW-ROLLOUTs (over 30 s), 50.8 core-hours between them.**
  - Duration: median 48 s, p90 143 s, p99 1,411 s.
  - Hand size: 579 at size 7, 246 at size 6, 45 at size 5, 4 at size 4.
- **The worst single rollouts, each one core for hours:**

  | time | size, draw/play, r | seed | hand |
  |---|---|---|---|
  | 36,891 s (10.2 h) | size 6, draw, r=20 | 18054083110755917170 | Kitchen Finks, Forest, Felidar Guardian, Caves of Koilos, Carrion Feeder ×2 |
  | 30,253 s (8.4 h) | size 7, play, r=1 | 4354685594354933543 | Orzhov Basilica, Murderous Redcap, Caves of Koilos ×2, Carrion Feeder ×3 |
  | 22,027 s (6.1 h) | size 7, play, r=1 | 4354685568366855579 | Kitchen Finks ×3, Caves of Koilos ×2, Carrion Feeder, Bloodthrone Vampire |
  | 7,642 s (2.1 h) | size 6, draw, r=1 | 4354686363641241394 | Orzhov Basilica, Forest, Carrion Feeder ×4 |
  | 1,778 s | size 7, play, r=1 | 4354685676544180110 | Reclamation Sage, Birds of Paradise, Ignoble Hierarch ×2, Chord of Calling, Carrion Feeder ×2 |
  | 1,583 s / 1,410 s / 1,166 s / 1,161 s | size 7 | — | Carrion Feeder ×2–4 with Chupacabra / Ranger / Finks |

- **87% of the slow-rollout time is in hands holding Carrion Feeder.**

  | card | slow rollouts | core-hours |
  |---|---|---|
  | Carrion Feeder | 557 | 44.4 |
  | Caves of Koilos | 325 | 36.3 |
  | Kitchen Finks | 635 | 32.4 |
  | Forest | 195 | 18.2 |
  | Orzhov Basilica | 313 | 17.9 |
  | Bloodthrone Vampire | 263 | 14.4 |
  | Felidar Guardian | 107 | 12.5 |

- **Memory and durability.**
  - Generator RSS: 9.6 GB → 10.9 GB → 16.5 GB by 18:47 UTC, against the 20 GB cap. The previous session saw
    16 GB in the same phase.
  - At 19:11 UTC the monitor warned that the journal had not been written for 4,444 s while 28,098 rollouts
    had been fed in the last 300 s. That work is unbanked: a kill or a cap abort discards it.
  - Throughput: roll7 sat at 0/s for about 2 h (16,202–22,503 s into the session, the tail of a sub-wave),
    then recovered to 93–111/s.
  - `keepgen-producer-barrier-and-durability.md` gives the discriminator between a producer barrier and cost
    concentration. Here the journal mtime is frozen (barrier-like) while SLOW-ROLLOUT size<7 entries keep
    appearing (concentration-like), so both may be present.

**Mechanism hypothesis (UNVERIFIED).** The deck runs two free sacrifice outlets: Carrion Feeder costs {B} to
cast and sacrifices a creature for a +1/+1 counter; Bloodthrone Vampire sacrifices for +2/+2. It also runs two
persist creatures, Kitchen Finks and Murderous Redcap, and the blink creature Felidar Guardian. Every
sacrifice is an activation with no mana cost, so it can sit at any point in the turn. Each persist return
re-triggers an ETB, and Redcap's ETB carries a target choice. With 2–4 Feeders the outlets can also eat each
other. The searched turn's branching would grow combinatorially in the number of sac-able bodies, which fits
both the hand list (Feeder ×2–4) and the RSS growth.

**DIAGNOSED 2026-10-27 (replay of the 62.9 s rollout, `Kitchen Finks x2; Caves of Koilos x2; Carrion Feeder x3`,
size 7 draw r=0, via `MTG_KEEP_REPLAY` on the frozen binary -- play digest b2c25a26e00f1eba reproduced).** The
hypothesis above was half right. The fodder sacrifices were already gated (`MeliraPodProvider::FodderSacUseful`,
the USER's 2026-09-05 rule), so the generic sac was not the problem. The cost is the COMBO-ACTIVE case: on the
board `Carrion Feeder x3, Kitchen Finks x2, Celes, Rune Knight` (Celes = a live loop-closer) the persist-loop
block in `CollectActions` emitted K=1 / growth-K variants for EVERY (outlet, persist body) pair, 12 loop actions,
crossed by the breakpoint enumeration into 13,824 plans per node. Two pre-combat decisions (t4, t5 of the
rollout) each ran into the 1M-unit decision work ceiling (`MTG_DECISION_WORK_X=1000` x b3), ~25 s each; an
ordinary rollout costs ~0.25 s (40-rollout sample: 25 ms - 1 s).

**Fix: `MTG_POD_LOOP_FOLD` (TurnSolver.cpp, default ON).** One loop variant per DISTINCT (outlet, body) state:
twin outlets / twin bodies (same card, tap state, counters, everything `PermIsPlainForFold` reads) give states
that differ only by permuting identical permanents, so the twin pair adds no reachable outcome. Oldest copy kept.
Single replay on the new binary: **69.1 s -> 5.2 s (13x), units 2.17M -> 229k, same win turn (6).**

**Replay A/B, same binary, `MTG_POD_LOOP_FOLD=0` vs `1`** (`MTG_KEEP_REPLAY_LIST`: every 8th slow-log line at or under
150 s, plus 1,000 ordinary sampled rollouts; generation suspended so both arms had the box):

| population | n | wall off -> on | avg win turn off -> on | on better / worse |
|---|---|---|---|---|
| slow tail | 98 | 14,212 s -> 2,146 s (**6.6x**) | 6.29 -> **5.98** | **30 / 0** |
| ordinary | 1,000 | 377 s -> 327 s (1.15x) | 5.764 -> 5.764 | 1 / 1 |

The freed budget buys play on the tail (30 rollouts win a turn earlier, none later), and the ordinary rollout is
untouched. Generation was restarted on the fixed commit with `MTG_KEEP_RETAIN_FOREIGN=b2c25a26e00f1eba` (USER
2026-10-27: "it would be nicer if we could retain what we have done") -- the rollouts banked under the old engine
stay in the journal.

**After the restart (2026-10-27 20:41 UTC, PGO, play 008e4fce2050a0cd).** The resume kept **600,866 completed
cell-sides (9,394,320 rollouts)** from the old engine and dropped 3,152,320 PARTIAL size-7 records, which re-roll.
The in-memory work since the journal's last write (17:56) was also lost. The SIGSTOP used for the A/B inflates
the last few pre-restart SLOW-ROLLOUT ages (~1,620 s each); ignore those entries.

| | before (12:25 session, 60f525f17) | after (99977194b, first 65 min) |
|---|---|---|
| size-7 rollouts | 560k in 28,700 s, with ~2 h at 0/s | 542k in 3,900 s, 100-150/s, no stalls |
| journal | unwritten for up to 8,852 s (subwave 274,256) | written continuously (subwave 35,031) |
| generator RSS | 16.5 GB | 4.8 GB |
| worst rollout so far | 22,027 s (6.1 h) | 2,082 s, the SAME hand and seed |

Still open: that hand (`Kitchen Finks x3; Caves of Koilos x2; Carrion Feeder x1; Bloodthrone Vampire x1`, size 7
play r=1, seed 4354685568366855579) still costs 35 min. It has two DIFFERENT free outlets (Feeder, Bloodthrone),
which the twin fold does not merge. That is the next replay to profile.

2026-10-28 02:43 UTC: the size-7 floor pass FINISHED (3,152,324 rollouts = 788,081 cells x 2 x play/draw) in
~5.9 h; the sub-tables are rolling at ~142/s, RSS 5.8 GB. New entry in the top 3: **994 s, `Llanowar Wastes x2;
Voice of Resurgence; Kitchen Finks x3; Bloodthrone Vampire`** (size 7 play r=1, seed 4354686331573145191), with NO
Carrion Feeder. The residual tail is therefore Bloodthrone Vampire + several Kitchen Finks, not the Feeder twins.

**Originally listed checks** (1 is answered above; 2-3 remain):
1. Single-rollout repro of the 7,642 s hand (Basilica, Forest, Feeder ×4: the smallest hand, nearly pure
   Feeder), run under the per-decision census. Does the time go to sacrifice-activation enumeration, and
   are interchangeable outlets/bodies being pooled the way the sac-outlet pooling collapse already does
   elsewhere?
2. Whether the free-sacrifice menu is reachable from positions where no payoff exists. On a goldfish board
   with no death trigger, persist return or counter consumer that matters, sacrificing is a pure loss.
   `SelfSacHasDeathPayoff` is the existing gate shape. If the menu is open without one, that is a sound
   dominance to collapse.
3. The unbanked-journal warning: a 4,400 s gap at 16 GB RSS is the durability risk named in the barrier
   doc. Check whether defect 1 (producer barrier) is still open at `60f525f17`.

**PAUSED by the USER 2026-10-28 03:43 UTC** ("cancel it for now ... restart it overnight to finish"), in the size-7
refine phase: 6.9% of 1,576,162 cell-sides frozen and accelerating (~100/s at pause). Estimate given to the USER
for the remainder: refine ~1-3 h, validation ~0.5-1 h, value leaf ~0.5-1 h (the generator's own "~91 h" guide is
pessimistic; it said 79 h for Goblins, whose size-7 phase took 11.4 h). Journal copied to
`.journal.prepause-20261028` (377 MB). Owed: the replay of the 994 s Bloodthrone hand
(`build/scratch/melira_replay/bt.err`, `MTG_DECISION_PROGRESS` + `MTG_BP_DERIVE_SLOW_MS` output).

**Bloodthrone replay DONE 2026-10-28** (the 994 s hand, `Llanowar Wastes x2; Voice of Resurgence; Kitchen Finks x3;
Bloodthrone Vampire`, size 7 play r=1, via `MTG_KEEP_REPLAY` on the frozen binary, play digest 008e4fce2050a0cd
reproduced). **640.8 s, win turn 6, only 374k units.** Almost all of it is ONE decision, rollout t6 pre-combat:
640.5 s for 356k units, about 1.8 ms per unit against ~0.03 ms per unit for an ordinary decision. That makes it a
**per-unit cost**, not branching inside the search, and the budget never binds because the work is not charged.
- Where the time goes: **906 `bp-derive-slow` breakpoint derivations, ~295 ms each, 331 s in total**, each deriving
  **86,016 plans** from `subsets_entered=65535` (2^16 subsets of breakpoint-relevant actions). The board at those
  derivations is Celes, Rune Knight (a live loop-closer) + Bloodthrone + 2-3 Carrion Feeder (Chord fetched them) +
  3 Kitchen Finks, with mixed tap states.
- So the Feeder twin fold does not cover this. The outlets are not twins (different cards, or different tap
  states), and the cost sits in the breakpoint subset enumeration over the free sacrifice / loop actions, which
  `ChargeDecisionUnits` does not price.
- **Next (owed, not started):** census of which 16 actions enter the subset set on this board, and whether any are
  dominated or identical, e.g. a sacrifice of a Finks that already has a -1/-1 counter vs one without, or a tapped
  vs untapped Feeder used as an outlet where tap state is irrelevant to a free activation. Also whether the derive
  should be charged in units, so a finite budget re-prices it (BUDGET MAY RE-PRICE rule). Repro: the
  `MTG_KEEP_REPLAY` command in `build/scratch/melira_replay/` (bt.err holds the full output).

**RESTARTED 2026-10-28 10:59 UTC** (same recipe `fast`, frozen tree 99977194b, journal retained: 600,866 cell-sides
re-admitted, refine resumed). Status 12:18 UTC: refine **36.3%** of 1,576,162 cell-sides frozen at 4,500 s, ~130
cell-sides/s (roll7 ~180/s), idle 0.0%, load 32/32. At that rate the refine finishes in roughly 2 h, so ~14:30 UTC,
then validation. The slow-rollout tail persists: a 176 s size-7 Reveillark/Redcap/Feeder hand in the current top-3.

Status 14:18 UTC: refine **70.0%** frozen at 11,700 s, idle 0.0%, load 32/32, but freezing has slowed from ~130 to
~28 cell-sides/s (rollouts still ~140/s, so the cells now left need more rollouts each to converge). At that rate the
remaining 472k take ~4.7 h, so ~19:00 UTC before validation. The tail is still the Feeder/Bloodthrone sac-loop board:
a **779 s** size-7 hand (`Llanowar Wastes, Ignoble Hierarch, Forest, Caves of Koilos, Carrion Feeder x2, Bloodthrone
Vampire`, draw r=2, seed 15755400944471724403), consistent with the breakpoint subset derive found in the replay above.

Status 16:18 UTC: refine **80.0%** frozen at 18,900 s, idle 0.0%, load 32/32. Freezing keeps slowing: ~20
cell-sides/s now (vs ~28 at 14:18 and ~130 at 12:18), and rollouts are steady at ~150/s. The remaining 314k would take
~4.4 h at the current rate, so ~20:40 UTC at the earliest. The top-3 slow list is unchanged since 14:18 (worst 779 s).

Status 18:18 UTC: refine **86.1%** frozen at 26,100 s, idle 0.0%, load 32/32, rollouts steady ~150/s. Freezing is down
to ~11 cell-sides/s (130 -> 28 -> 20 -> 11 over the day), so a linear ETA is no longer meaningful; the 218k left would be
~5.5 h at today's rate and the rate is still falling. New worst rollout: **1,994 s** (33 min), size 7 draw r=8,
`Orzhov Basilica, Kitchen Finks x2, Caves of Koilos x2, Carrion Feeder x2`, seed 10372713029942101962 -- another
Feeder board, and no Bloodthrone this time, so the sac outlet in the loop is the Feeder itself.

Status 20:18 UTC: refine **90.6%** frozen at 33,300 s, idle 0.0%, load 32/32, rollouts ~150/s. Freezing ~8 cell-sides/s
(still falling); 147k left. If `cap=30` (R) bounds each cell-side's rollouts, the worst case is 147k x 30 / 150/s
~ 8 h, so the refine should land by ~04:30 UTC 2026-10-29. Worst rollout still the 1,994 s Feeder hand; a new 405 s
Recruiter/Chord/Feeder hand entered the top-3.

Status 22:18 UTC: refine **93.8%** frozen at 40,500 s, idle 0.0%, load 32/32, rollouts ~155/s, freezing ~5.7
cell-sides/s (~27 rollouts per freeze, i.e. the remaining cells mostly run to the R=30 cap). 97k left: worst case
97k x 30 / 155/s ~ 5.2 h, so ~03:30 UTC 2026-10-29. Top-3 slow list unchanged.

**Melira Pod mulligan DONE 2026-10-28 23:27 UTC: VALIDATION PASSED** (refine finished between 22:18 and 23:20 UTC,
much sooner than the worst case). Pooled A/B new vs old **-0.0126 t** (25/32 seeds better, mean/se -4.59); the
visibility regression run moved GT net faster (searched faster 23 / slower 20). The table is live in the worker's
frozen tree, not yet landed. The value leaf started 23:32 UTC and was **PAUSED by the USER 2026-10-29 00:08 UTC**
(machine shutdown for a memory upgrade); `artifact_queue.py start` on the artifact-queue checkout resumes it.
The mulligan's whole wall here was ~2 days of refine across three sessions, dominated by Carrion Feeder sac-loop
hands (worst single rollout 1,994 s); the breakpoint subset-derive cost above is still owed as an engine fix.

## LifegainHeliodCombo -- first mulligan profile (started 2026-10-29 06:30 UTC)

`bash scripts/mullgen.sh run decks/LifegainHeliodCombo fast` in `.claude/worktrees/lhc-gen`, frozen at 1d78d3511
(src 07c4ce01d). Discovery (400 probes, threshold 0.01): one merge, Soul Warden + Soul's Attendant -> K=14,
~77.5k distinct hands. USER 2026-10-29: *"Yes, Soul Warden and Soul's Attendant should merge."* -- the discovered merge is APPROVED. No `buckets.json` written mid-run (a policy enters bucket_fp); write one after this run lands if a regeneration should pin it. Voice of the Blessed / Ajani's Pridemate (dist 0.0175, the nearest unmerged pair) stay APART -- USER agreed 2026-10-29: Voice's {W}{W} is 2 devotion for Heliod's 5 vs Pridemate's 1, the only goldfish difference and the measured deviation. The value leaf follows
when this validates. Melira Pod's paused value leaf stays paused until then (stages are serial).
- 2026-10-29 06:32 UTC: PGO build stage (first step of the run).
- 2026-10-29 07:07 UTC: mulligan DONE, **VALIDATION PASSED** (48,866 entries): keep -0.2337 t vs static (4/4,
  mean/se -26.95), confounded bottoming -0.0350 t (4/4). Landed af14097d9 + GT b8b87b27a (only lhc/lhc2hg rows moved,
  every key faster; references 0 drift). Overnight lhc rows were resized to 500 b20 first (aba9d4340).
- 2026-10-29 08:26 UTC: **value leaf started** -- `bash scripts/valueleaf.sh run decks/LifegainHeliodCombo`
  in lhc-gen at b8b87b27a, alone on the box; queue `logs/vlq_lifegainheliodcombo`. Melira's value leaf stays paused.
- 2026-10-29 08:41 UTC: **value leaf DONE in 15 min** (phase A 2,500 games -> 11,577 rows; every phase one pool, no
  slow games). The trained model LOST to the leafless shapes and stays staged (`logs/eval/...STAGED.json`, -0.00075 t
  at 0.46x vs no sidecar). E.5 installed `nlesc` (escalation, leaf none: -0.00287 t at 0.555x); E.7's grid then
  replaced it with `nlfit_d6b20` (single ladder, leaf none, relaxed alpha, target_depth 6, budget 20: +0.00025 t =
  one game in 8,000, at 0.773x). Net vs before: ~-0.0026 t at ~0.43x search units. Trust d5 REJECTED (never engaged:
  byte-identical on 8/8 seeds); E.6 not applicable (no ladder escalations). Two recorded TRADES not taken (live
  nlesc at d5b30 / d5b40: -0.00013 t at 1.08x / 1.15x -- one game). Landing = sidecar + lhc smoke/regression GT.
- 2026-10-29 08:52 UTC: **LHC landed**: sidecar + lhc smoke/regression GT pushed c67260501. LifegainHeliodCombo
  generation is DONE (keep table + leafless d6/b20 sidecar); the lhc overnight GT waits for the next full overnight run.
- 2026-10-29 08:55 UTC: **Melira Pod value leaf RESUMED** (artifact-queue worker artifact-queue-d7d6a5, pid 493949,
  in-flight job at 99977194b; its table already landed 9af42771d). The worker continues the fleet queue afterwards.
- 2026-10-29 10:53 UTC (Melira value leaf): phase A was already banked (11,489 rows); C0 nothing to calibrate (profile
  pins leaf depth 0). Phase C matrix (started 08:57) ran ~20,500 games at 32/32 workers; now in its TAIL: one game,
  s10010 gi=308, running 1.6-1.7 h in 7 cells (H4, H5, V4-V8) -> 7/32 cores busy. The matrix's wall-clock backstop
  (predictive cut 120 min ~10:57, hard cap 210 min ~12:27) is the designed bound; left to it. Same Carrion Feeder
  sac-loop tail family as the mulligan's refine (worst rollout 1,994 s).
- 2026-10-29 11:11 UTC: Melira value leaf -- phase C finished 11:09 (no backstop cut; ~2 h 12 min). C.5 risk gate CLEAN (<=0.19% abandoned per cell; no verdict moved). Phase D: crossover 1->2 ... 5->6, trust d5 NOT trusted (V4 gap +0.0013, 95% upper 0.0027 > tol). Phase E (56 A/B jobs) started 11:10.
- 2026-10-29 11:54 UTC: Melira value leaf -- phase E done 11:26: live leaf kept (staged +0.00025 @1.00x; nlesc +0.0054 @1.19x; nlfit +0.021 @0.80x; emul -0.0011 @1.21x = TRADE, user's call). E.7 grid (live d5/b60) picked d4b60 (+0.0011 t, t +2.05, 0.97x units); trades d5b90 -0.0019 @1.13x, d5b120 -0.0030 @1.21x; d6bX == d5bX at every budget (depth 6 never reached). E.7b nothing to recheck. E.6 running.
- 2026-10-29 11:58 UTC: Melira value leaf DONE 11:54 (worker log: valueleaf KEPT INCUMBENT). Result branch artifact-refresh/melira_pod-20261029-1154 NOT auto-landed (cherry-pick conflict): its keep table is byte-identical to the landed one (no-op); value.json differs ONLY in target_depth 5->4 (E.7 d4b60) -- HELD for USER sign-off, as is its ledger entry. Live emul_r [6.78,57.62,124.87,312.92] vs recalibrated [6.58,4.51,2.24,1.29] = the phase-E emul trade (-0.0011 @1.21x). Rescue branch artifact-refresh/rescue-artifact-queue-d7d6a5-20261029T115512 only records a stale deletion of the tracked Dragonstorm journal (file present, plus backups, in the worker tree) -- nothing lost. Worker continuing to minotaur.
- 2026-10-29 12:10 UTC: USER: play settings never below depth 5 -> play_setting_stage.py clamps the grid at d5 (853ac54f4; copied into the worker's frozen tree for Bruna). Melira landed = ledger entry only (kept-incumbent; d4b60 NOT landed; table no-op). Budget 60 confirmed by USER (the 2026-10-21 emulated-ladder adoption). OWED: AccomplishedAlchemist and Giants ship d4/20 from E.7 (2026-10-22/25) -- move to d5 with their smoke+regression GT when the box is not generating (one heavy process at a time). Worker claimed Bruna (valueleaf) 12:01.
- 2026-10-29 12:45 UTC: USER adopted Melira d5/b120 (trade, Melira 0.16x Hinata): sidecar budget_ms 60->120, melira/melira2hg d5 rows 60->120 in all tiers; smoke+regression (deck-filtered) 0 slower / 1 faster each, refs 689 with 0 drift; accepted. Overnight melira rows -> next periodic run. USER: LHC b30/b40 and Melira emul_r recalibration stay OFF.
- 2026-10-29 12:55 UTC (2-hourly check): worker alive; Bruna value leaf in PHASE A tail (started 12:18; heartbeat 2/32 busy, slowest game 0.44 h, per-game hard cap 4 h) -- the one designed barrier before training, left running. Using the idle cores: AccomplishedAlchemist + Giants d4 -> d5 (USER floor) deck-filtered smoke+regression at 8 threads with a memory watchdog.
- 2026-10-29 13:05 UTC: AccomplishedAlchemist + Giants back to d5/b20 (USER depth-5 floor). Deck-filtered tiers: smoke 0 slower / 1 faster; regression 1 slower (alchemist s2002 gi127 5->6 = churn, 5 at 4x/16x) / 1 faster; refs 689 0 drift. Accepted smoke+regression; overnight rows -> next periodic run.
- 2026-10-29 14:55 UTC (2-hourly check): worker alive, box full (load 32). Bruna value leaf: phase A done 13:48 (tail cleared, 0 games >= 2 h), C 13:49-14:04, C.5 CLEAN, D trust UNSET; E.5 ADOPTED nlesc (escalation, leafless) -0.0034 t (t -2.35) at 0.79x units vs live (staged 1.00x tie; nlfit -0.0025 @0.77x; emul +0.0003 @0.72x). E.7 grid (200 jobs, d>=5 clamp active) running since 14:13.
