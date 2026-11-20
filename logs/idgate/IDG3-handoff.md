# IDG-3 handoff (worktree .claude/worktrees/idgate, branch wip/idgate). Written at the parent's request (~247K).

Read logs/idgate/IDG-handoff.md (design, census, tooling) and logs/idgate/IDG2-handoff.md (sr1 A/B, d8 b0, s3) first.

## USER rulings (verbatim, as given in the briefs)
- (2026-11-20) "We need a full quality vs time comparison with this, like with every other trade."
  This is a TRADE (better but more expensive): adoption is the USER's call. Do NOT adopt, do NOT accept GT.
- "the top priority are optimizations and fixes"
- "A budget may re-price work, never make a line unfindable"
- "Naturally, I'm expecting that this means we are matching the references and all other bars. If not, we should focus on doing so first."
- "Do NOT add complexity whose purpose is to dodge a depth increase". Depth/budget-per-deck is a SETTINGS change = the USER's.
- Adoption bar: per-deck net <= 0 ("+1 is not neutral. 0 is neutral."); every slower game recovers at `--depth 8 --budget-ms 0`.
- "Draws Diverge is not an acceptable explanation, since at d8 b0 we should still be able to find the line."
- Parent rules: heavy runs via scripts/heavy_run.sh (Maze's End batches have lock priority; tell the parent first if one hold
  would exceed ~2 h); MTG_RSS_CAP_GB=20; scratch in build/scratch/idgate/; waiters on kill -0 / DONE markers, never pgrep -f;
  check the `[batch] heartbeat` in the first 10 min; do NOT cap workers to dodge memory; MTG_PGO=0 on all arms; push wip/idgate
  only (Windows CI watch in background with --interval 60); no timeouts; do not kill runs you did not start; hand off at ~250K.
- The 3 known non-recoveries are pre-existing depth-monotonicity defects agent DM-1 is fixing: mazesend s3003 gi73,
  martyr s2002 gi149, gods s2002 gi16 (d3). Name them, do not chase them.

## Done (commit 9d4429b61 on wip/idgate, plus this handoff commit)
### 1. Overrun side effect: the trial's COLD estimate (MTG_ID_GATE_COLD, heurarm ID_GATE_COLD, default ON, acts only with the trial)
- Mechanism (mz73 trace logs/idgate/d8_sr1/mz73_b20_t1.txt): an aborted trial still stores every completed subtree in the
  GAME-persistent caches, so the next decision's pass 1 costs ~10 units; the 6.0 bootstrap estimate (60) admits pass 2 with the
  25x ceiling and it runs 46,669 (T3: pass 1 = 25, pass 2 = 92,474). It cascades.
- sr1 census (build/scratch/idgate/sr1/err `[idg]` lines, top-level rollout ladders, budgeted): admitted passes that ran past
  the gate allowance cost off 67.5M units (36.8M over the allowance) vs on 105.5M (63.9M over); delta +38M of the +88M total.
  Bootstrap-admitted passes alone: off 44.6M vs on 77.1M. Warm c1 (c1/limit < 1/1024) overruns: off ~10.5M, on ~19.2M.
- Fix: each persistent-cache entry records its COLD cost (its own units + the cold credit of persistent hits beneath it) in
  existing padding (FSLineEntry::cold, static_assert on its size; inwin::Entry's redundant `win` slot -> `cold`, reads now use
  line.win_turn; rgc::E::cold after `bool win`). Every hit adds it to `coldcost::t_credit` (thread_local, deltas only). The
  ladder tracks cc_prev/cc_prev2 (pass cost + credit delta) and, under the trial on rollout ladders, the estimate is
  max(warm, cold-ratio x cold cost). A trial that fits completes exactly as an admitted pass, so the only change is the
  ceiling (allowance instead of 25x) of a pass the cold estimate says will not fit. Accounting is play-inert (entry sizes and
  byte caps unchanged) -> the off arm must still equal GT. `[idg]` lines gain `cc1=`.
- Rejected alternative: capping EVERY admitted pass at the allowance. The kOverrunBudgetMult calibration comment (TurnSolver.cpp
  ~119935) records that Remaining-anchored ceilings cost 2.03x work on Snow and a 50k floor measured far worse.
- `g++ -fsyntax-only` passes. NOT yet built / unit-tested / measured (that is the queued chain).

### 2. Memory blow-up: diagnosis so far (no fix yet)
- Every ovn1 attempt dies 16-45 s after start (rss past 3/4 of the cap at ~16 s), with BOTH arms in flight; the decks in flight
  vary per attempt (part3: 32 merfolkturns d5; loop2/loop6: 20-23 hinata d5; loop3: 12 martyr d5; loop5: 12 vampires d5).
  Hinata games last seconds. So it is not one game growing and not the trial's arm: it looks like the base per-game footprint
  x 32 concurrent games of the heavier decks (sr1 ran at 3.9-9.3 G with its mix).
- Not the keep tables (CachedExhaustiveKeep is process-global, positioned reads, not mmap; ProfileCache LRU 3, shared_ptr).
  Merfolk Turns has NO keep table (pre-table mulligan plays every candidate bottom as a full game: a likely per-game hog).
- Tier-1 caches are fixed per thread (~80 MB: tt 8, fsl 16, m1memo 8, gamelc 24, mana 24) plus inwin 24 MB and rgc (262,144
  entries) per worker; the pools the watchdog reports (fsl, plan, t2) were small at every abort. So most of the ~17 GB is in
  something untracked (search stacks/states, solvememo, plan vectors, nested mulligan engines) -- the probes will say.
- Probes queued in the chain (script build/scratch/idgate/mem/probe.sh: /usr/bin/time -v + 1 s VmRSS/RssAnon/RssFile samples
  in <tag>/rss.txt, MTG_TIER1_STATS per-game cache peaks in <tag>/tier1.txt, MTG_TT_STATS): hin_on_old, hin_off_old,
  mft_on_old, mft_off_old (old binary = build/scratch/idgate/ovn1/mtg.bin, the IDG-2 build), hin_on_new (new binary),
  hin_off_old_t8 (--threads 8: DIAGNOSTIC ONLY, per-game memory scaling; not a run setting). Each = 2 chunks of 25 games
  (hinata/merfolkturns overnight d5 s4004 o0+o25). Outputs build/scratch/idgate/mem/<tag>/{time.txt,rss.txt,tier1.txt,out,err,DONE}.
- Next: read peak RSS on vs off (is the trial the hog?) and RSS at 32 vs 8 threads (per-game marginal). If per-game x
  concurrency: a fix that bounds the per-game footprint (a per-job/per-game cache budget, release of trial/decision state,
  or bounding whichever untracked structure tier1.txt / a heap profile names). NOT a worker cap. jemalloc is linked
  (MALLOC_CONF=prof:true may work for a heap profile if the lib was built with prof).

## The queued chain
- `logs/idgate/chain3.sh` under `scripts/heavy_run.sh`, PID **2157421** (queued on flock behind ~6 Maze jobs at 06:10 UTC; log
  build/scratch/idgate/chain3.heavy.log). ONE lock hold, about 1-1.3 h: build (logs/idgate/build3.log, marker build3.done
  `rc=N`; chain3.STOP on failure) -> units in place (unit3_on.log, unit3_off.log with MTG_ID_GATE_TRIAL=0; last line rc=) ->
  the 6 memory probes (marker build/scratch/idgate/mem/PROBES_DONE) -> sr2 = `run_batch.sh logs/idgate/sr_ab.json
  build/scratch/idgate/sr2 MTG_IDG_CENSUS=1` (1,198 jobs, off vs on-with-cold, same shape as sr1; marker sr2/DONE) ->
  logs/idgate/chain3.start/.end timestamps. Nobody is waiting on it now (my waiters die with me): IDG-4 must arm a
  `kill -0 2157421` waiter.
- After sr2: `python3 logs/idgate/cmp_arms.py build/scratch/idgate/sr2/out test/regression_gt.txt` (off must == GT on all 599:
  proves the accounting is play-inert), then units on/off (sr1 on/off was 766.1M/678.2M = 1.130x), `ab_wall.py
  logs/idgate/sr_ab.json build/scratch/idgate/sr2/out --base off --arms on` (sr1 wall 1.099x), `slower.py logs/idgate/sr_ab.json
  build/scratch/idgate/sr2/wins` (sr1: 82 faster / 6 slower / net -82), trialstats.py on sr2/err, and the census again (the
  python one-liners above, now with cc1) to confirm the admitted-over-allowance excess fell toward off's 36.8M.
  Note: the chain uses build/Release/mtg; with MTG_PGO=0 not needed (run_batch.sh calls the binary directly).

## Quality-vs-time design (step 3)
- Convention: the base's OWN budget curve at equal cost (docs/design/niv-rollout-ladder-vs-leafless-shapes.md "Budget curves",
  docs/design/lfe-valueleaf-costs-more.md, value-leaf.md E.7): run the base at several budgets and compare the change against the
  base curve interpolated at the change's measured cost (units, and cpu/wall), per deck.
- Tools (written, untested on real data): `logs/idgate/mkqvt.py <in> <out> [--chunk 25] [--mults 1.1,1.2]` builds per job
  (chunked, seed = base + offset, game_index = offset, arm order ROTATED per chunk, MTG_ID_GATE_TRIAL explicit on every arm)
  the arms off@b, on@b, offx110@round(1.1b), offx120@round(1.2b) (budgets are integer ms: 10->11,12; 14->15,17; 20->22,24;
  30->33,36; 40->44,48; 80->88,96). `logs/idgate/qvt.py <manifest> <out[,out2]> <wins dir> [--base off --arm on]` prints per
  deck and ALL: net turns (loss = 9) of each arm vs off@b, units and cpu ratios, and matched@u / matched@cpu = net(on) minus the
  base curve at on's cost ('*' = extrapolated). Matched <= 0 = the trial beats the base given the same extra cost.
- Size: from logs/idgate/ovn.json (142 jobs, 71,072 games/arm), 4 arms = 11,384 jobs, 284,288 games, est. ~70-75 core-h,
  ~2.3 h wall at 32 threads -> OVER the 2 h hold: tell the parent before launching. Pick the multipliers from sr2's measured
  on/off cost ratio (if it falls to ~1.05x, use --mults 1.05,1.1 where integer budgets allow; b10's smallest step is 1.1x).
  One pooled batch, arms innermost, MTG_PGO=0, through heavy_run, with the memory fix in.
- The binary changed, so the ovn1 .wins (old binary) are NOT reusable for the on arm; re-run everything in the one QvT batch.
- Then: every slower game -> `python3 logs/idgate/d8.py <slower.txt> <outdir> --par 6` (d8 b0); attribute unrecovered ones by
  lever flips (MTG_ID_GATE_TRIAL=0, MTG_ID_GATE_COLD=0); name the 3 DM-1 games.

## Remaining, in order
1. Arm a waiter on PID 2157421. Read build3/unit3; if the build or units fail, fix and requeue.
2. Read the memory probes; decide and implement the memory fix (not a worker cap); verify by replaying the failing mix
   (e.g. resume the ovn1 mix briefly) under heavy_run within the first minutes, and that off == GT is unaffected.
3. Read sr2 (above). If the cold fix moves units/wall the wrong way or the off arm is not GT, root-cause first.
4. Build the QvT manifest (mults from sr2), tell the parent the expected hold (~2+ h), run it pooled, check the heartbeat.
5. qvt.py per deck; slower list; d8 b0 recoveries; attribution.
6. Fill the Results section of docs/design/id-start-gate-trial.md (sr1, sr2, QvT table, d8, memory fix), commit, push
   wip/idgate, and report the per-deck quality-vs-time table to the parent for the USER. No adoption, no GT accept.

## Measurements so far (IDG-3)
- sr1 census excess numbers above. No new runs finished.
