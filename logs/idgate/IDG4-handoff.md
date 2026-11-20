# IDG-4 handoff (worktree .claude/worktrees/idgate). STOOD DOWN by the parent 2026-11-20 21:27 UTC; box reserved for Maze's End.

Read IDG3-handoff.md (design, rulings, QvT design) first. All rulings there still apply. Repeated verbatim:
- (2026-11-20) "We need a full quality vs time comparison with this, like with every other trade." -> TRADE: do NOT adopt, do NOT accept GT.
- "the top priority are optimizations and fixes" / "A budget may re-price work, never make a line unfindable"
- "Naturally, I'm expecting that this means we are matching the references and all other bars. If not, we should focus on doing so first."
- "Do NOT add complexity whose purpose is to dodge a depth increase". Adoption bar: per-deck net <= 0 ("+1 is not neutral. 0 is neutral."),
  every slower game recovers at --depth 8 --budget-ms 0. "Draws Diverge is not an acceptable explanation, since at d8 b0 we should still be able to find the line."
- USER 2026-11-20 21:27: "At this point I want to wrap up Maze's End as soon as we can." / "So having other nonsense in the way doesn't make sense."
- Parent rules: each HEAVY STAGE gets its OWN scripts/heavy_run.sh (never one heavy_run around a multi-stage chain: a light stage reads as a
  tail and the next stage runs unlocked); a parent priority gate may SIGSTOP queued waiters (expected, do not touch); MTG_RSS_CAP_GB=20;
  scratch in build/scratch/idgate/; waiters on kill -0 / markers; never cap workers; MTG_PGO=0 on wall arms; push wip/* only; no timeouts.

## Branches
- wip/idgate (pushed, tip f676da765): the old base. CI 44686024181 on f676da765 = success. Do not rebase it (pushed).
- **wip/idgate-r2 (LOCAL ONLY, checked out in the worktree, tip 1aed850a7)**: the same 6 commits cherry-picked onto origin
  phase-1-2-deck-analyzer 1f3c23fab. Two conflicts resolved: HeuristicArm.h (ID_GATE_TRIAL appended after SUMMIT_TWIN_FOLD, enum and name
  table); TurnSolver.cpp gamelc hit lines ~118191/118208 (kept upstream's `gt >= 1` devseq line, plus the IDG-3 `coldcost::t_credit +=
  ...cold` return). NOT yet built or unit-tested. Push it as wip/idgate-r2 once it builds.

## Measurements (IDG-4)
### sr2 (old base f676da765 binary, smoke+regression A/B, 599 jobs/arm; build/scratch/idgate/sr2/)
- off == GT on all 599 jobs (the cold accounting is play-inert). 95 jobs move.
- units on/off = 720.0M / 678.2M = **1.062x** (sr1 1.130x). CPU on/off = **1.072x** (sr1 1.102x). Wall NOT usable (1.54x): sr2 ran
  unlocked beside a 12-job build from 07:16 and was SIGSTOPped by the parent 07:33-~08:00 (swap); quote units/CPU only.
- Per game: 80 faster / 18 slower, **net -62** (sr1: 82/6, net -82). Decks net > 0: martyr +2 (5 slower), hydras +2, merfolkturns +1;
  stuffy2hg 0. Slower list: build/scratch/idgate/sr2/slower.txt (18 games; includes the 3 DM-1 games mazesend s2002 gi73, martyr s2002 gi149).
- trialstats: 21,614 trials, 61.2% finished (784 found a better win), 8,377 aborted (502 rescued).
- Over-allowance census (all [idg] lines with act > lim): off 3,572 passes / 140.2M units (identical sr1/sr2), on sr1 4,490 / 181.5M ->
  on sr2 4,208 / **132.3M** (excess over allowance 104.1M -> 59.0M; bootstrap-admitted 87.2M -> 53.6M). The cold estimate works.
### Memory blow-up: ROOT-CAUSED (step 1)
- Repro: build/scratch/idgate/mem/rep/run.sh (ovn_ab_rest mix, 32 threads): batch parse of 5,105 jobs takes ~100 s, then RSS climbs
  ~1 GB/s from play start (decay0 run: 1.1 G -> 11.3 G in 10 s). dirty/muzzy decay 0 changes nothing -> LIVE memory, not retention.
- jemalloc heap profile (rep/prof2/jp.3018100.66.i66.heap, 15.3 GB): 92% under AIEngine::HandleMulligan -> BottomCards ->
  RolloutWinTurnFrom -> ... SolveSecondMainInSearch -> SolveWithLookahead -> EnumeratePlansWithLand -> AppendSubdecisionAxes ->
  PbAxesAndTwins -> PbLazyMaterialize / Plan copies. A plan-materialisation explosion in the pre-keep-table bottoming rollouts.
- Single game: Merfolk Turns overnight d3 s4004 gi123 (manifest mem/mft123.json, and mft123off.json = trial off) exceeds **12 GB alone on
  1 thread, BOTH arms** -> pre-existing on the old base, not the trial. The primary checkout's binary (origin ~8fbbc5023) plays it in 14 s
  at 87 MB. Upstream fixes since the idgate merge-base bc1d28bbd: cc6edaeef "Legend-keep turn fan twins lazy clone blocks lazily (no
  materialise): Merfolk Turns 5.5 GB/game peak", edf5915a8 "Mana oracle reads its observe-mode menu lazily: Merfolk Turns gi13 9.6 GB
  abort -> 95 MB", and the Merfolk Turns keep table adoption. **The memory fix is the rebase (wip/idgate-r2), not new code.** Not yet
  verified on the r2 binary (chain4 below).
- Earlier probes (chain3 mem/*: hinata/merfolk 2-job batches 0.6-1.2 GB) did not reproduce: wrong games. Profiles are small (all 59
  ovn profiles resident at 1 thread = 36 MB). Five Colour gi45 alone = 200 MB.

## Remaining, in order (only when the parent releases the box)
1. Relaunch chain4 harness-tracked (run_in_background), it takes the lock per stage:
   `cd /workspaces/MagicDeckTester/.claude/worktrees/idgate && bash logs/idgate/chain4.sh > /workspaces/MagicDeckTester/build/scratch/idgate/chain4.log 2>&1`
   Stages: build (logs/idgate/build4.done rc=; chain4.STOP on failure) -> unit4_on/off.log -> 1-thread mft123 + mft123off on r2
   (mem/<tag>.r2/, marker mem/R2_SINGLE_DONE; expect << 12 GB) -> full-thread ovn-shape repro `rep/run.sh r2` (rep/r2/rss.txt, 150 s of
   play, cap 20; must not trip) -> smoke off arm vs GT (build/scratch/idgate/smoke_off_r2, `cmp_arms.py <out> test/regression_gt.txt`
   must show 0 mismatches; if origin's smoke GT is itself stale, compare against a trial-off run of the primary binary instead).
   Arm a `kill -0 <pid>` waiter on the chain and on the markers.
2. Push wip/idgate-r2; watch CI (--interval 60) in the background.
3. QvT: rebuild the manifest from the r2 tree (built already for the old base at build/scratch/idgate/qvt/m.json, same command):
   `python3 logs/idgate/mkqvt.py logs/idgate/ovn.json build/scratch/idgate/qvt/m.json --mults 1.05,1.1` (11,384 jobs, 284,288 games; arms
   off / on / offx105 / offx110, budgets 10->11,12; 14->15,16; 20->21,22; 30->32,33; 40->42,44; 80->84,88). Mults chosen from sr2's
   1.06x units / 1.07x CPU. Run `bash scripts/heavy_run.sh bash logs/idgate/run_batch.sh build/scratch/idgate/qvt/m.json build/scratch/idgate/qvt`
   (its own heavy_run; expect ~2.3 h + ~4 min parse; parent approved the hold with conditions: after 1-2 pass, may be paused in the queue,
   check `[batch] heartbeat` + RSS in the first 10 min). Then `python3 logs/idgate/qvt.py build/scratch/idgate/qvt/m.json
   build/scratch/idgate/qvt/out build/scratch/idgate/qvt/wins --base off --arm on` -> per-deck table (units + CPU) for the USER.
4. Slower games -> `python3 logs/idgate/d8.py <slower.txt> <outdir> --par 6`; attribute unrecovered by MTG_ID_GATE_TRIAL=0 / MTG_ID_GATE_COLD=0;
   name the DM-1 games (mazesend s3003 gi73, martyr s2002 gi149, gods s2002 gi16).
5. Fill docs/design/id-start-gate-trial.md Results (sr1, sr2, memory root cause, QvT, d8). No adoption, no GT accept.
