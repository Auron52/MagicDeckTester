# IDG handoff: the iterative-deepening start-gate trial (worktree .claude/worktrees/idgate, branch wip/idgate)

The handoff was written at ~250K context on the parent's request, while waiting on a queued job.

## The task (from the parent's brief, condensed)
At d5 b20, FullSearchLine's ladder START GATE refuses pass 2 with budget to spare. Maze's End s3 T4 is the example: pass 1 used 5,103 of 18,000
units; the gate's estimate was 5,103 x kDefaultGrowth 6.0 = 30,618 > 1.1 x 12,897 remaining; the real pass-2 cost is 735 and it finds T5.
Source: /workspaces/MagicDeckTester/.claude/worktrees/maze-f7/logs/maze/F7-2-s3.md. The steps were: census the gate, design a better predictor,
build it behind an EnvOn(..., true) lever, then run units, then smoke+regression, then a held-out A/B on every deck that moves
(overnight-tier seeds, ONE pooled `mtg --batch`, arms innermost), then d8 b0 recovery of every slower game, then the s3 check. Do NOT accept
GT and do NOT push the shared branch; push wip/idgate only.

Machine rules: one heavy job at a time, run via /workspaces/MagicDeckTester/scripts/heavy_run.sh, at full nproc, with
`export MTG_RSS_CAP_GB=20 MTG_BUILD_JOBS=12`. Build only with ./build.sh. No timeouts. Waiters key on a PID or a DONE marker, never on
pgrep -f. Batch scratch goes under /workspaces/MagicDeckTester/build/scratch/idgate/.
Parent note (AB-ORDER): put arms innermost and alternate the arm order per seed/chunk (logs/idgate/mkab.py does this), and leave
`same_seed_inflight` at its default 0. Read wall with logs/idgate/ab_wall.py (a copy of wip/ab-order a25cd46d6:scripts/ab_wall.py):
`ab_wall.py <manifest> <stdout> --base off --arms on`. Units remain the primary metric.

## USER rulings (verbatim, as given in the brief)
- "the top priority are optimizations and fixes"
- "A budget may re-price work, never make a line unfindable"
- On search: "Do NOT add complexity whose purpose is to dodge a depth increase". A predictor that simply forecasts correctly is not that.
- A change to depth economics that is a SETTINGS change (e.g. changing depth/budget per deck) is the USER's. Report it as such; do not
  make it.

## The design that was built: START-AND-ABORT ("trial"), not a better point estimate
Census finding (below): the estimate does not predict whether a pass fits. For passes on the measured ratio, the trial fits about 50% of
the time at EVERY estimate/allowance overshoot bin, up to 64x and beyond. For the 6.0 bootstrap, the fit rate falls from 44% (overshoot
1-2x) to 6% (8x and up). No cutoff on any predictor separates the passes that fit from those that do not. So the gate keeps its
estimate. A pass the estimate REFUSES is started anyway as a TRIAL, with its overrun ceiling set to the gate's own allowance
(alpha x remaining, halved under g_reserve_value_pass). A trial that hits that ceiling is rolled back through the existing abort path:
the prior pass commits, and a strictly better win the trial already proved is kept (s_id_anytime).
- It is inert at b0, because the gate never refuses there. It is monotone in budget. Passes the estimate admits keep the 25x
  proportional ceiling as before. Everything is in units, so it is deterministic and thread-invariant.
- SCOPE: rollout-leaf ladders only (`!vl_active`), which covers the heuristic ladder and the hybrid's escalation. Value-leaf and
  leafless probes are excluded for three reasons: their relaxed gate refuses only when about 0% of the budget is left; a leafless pass
  already stops at exhaustion; and the probe's leftover budget funds the FIT pass or escalation that follows it.
- LEVER: `MTG_ID_GATE_TRIAL`, DEFAULT ON (`EnvOn("MTG_ID_GATE_TRIAL", true)`), and per job as heurarm slot `ID_GATE_TRIAL`.
  Manifest form: `"flags": {"MTG_ID_GATE_TRIAL": false}`. Flags must be JSON booleans; numbers make the batch exit with a type_error.
- Code (src/ai/TurnSolver.cpp, line numbers at the WIP commit):
  - ~119794: the `namespace idgcensus` census printer, enabled by MTG_IDG_CENSUS.
  - ~119832: `IdGateTrialOn()` and its design comment.
  - ~121740: the trial arm inside the ladder gate (`est_refused && !vl_active && IdGateTrialOn() && gate_allow >= 1.0`).
  - ~121797: the trial ceiling armed after the proportional overrun ceiling.
  - The abort path counts trial aborts in MTG_ROLLOUT_STATS (`gate_trials` / `trial_aborts` / `trial_abort_units` in the
    `[rollout-stats] id_pass` line).
  - src/ai/HeuristicArm.h: slot `ID_GATE_TRIAL` at ~1024, name at ~1990.
- Census instrument: `MTG_IDG_CENSUS=1` prints one `[idg]` line per top-level gate consultation (print-only).
  `MTG_IDG_CENSUS=2` also runs each REFUSED pass as a shadow trial on a budget COPY. NOTE: mode 2 PERTURBS play and units, because
  the shadow's units go into the per-game meter and it writes the decision's caches. 28 smoke jobs' digests differed from GT under it.
  Use mode 2 for the census only, never in an A/B arm.
  `[idg]` fields: k = pass, b = 1 for the bootstrap estimate, c1/c0 = previous pass costs, est, rem, lim, a = alpha,
  fit (0 refused / 1 admitted / 2 trial), act = actual cost, st (0 completed / 1 aborted / 2 aborted but rescued), pw/w = win turn
  before/after, vl = shape (0 rollout, 1 model, 2 leafless, +4 escalation), sh/sht/shw = shadow cost/tripped/win.

## Census (smoke tier, d0/d3/d5 at the shipped budgets, trial OFF arm, MTG_IDG_CENSUS=2)
Data: build/scratch/idgate/smoke1/{idg_off.txt,idg_on.txt,err,out}. Per deck: logs/idgate/census_smoke_perdeck.txt.
There were 86,204 gate consultations. By shape and estimate type (`refused` counts the estimate's refusals; the shadow trial
says whether each would have fit within alpha x remaining):

| shape | est | n | refused | rem/lim at refusal (med) | trial fits | better win (fit) | trial aborts | rescued better | units fit / abort / left unspent |
|---|---|---|---|---|---|---|---|---|---|
| rollout | boot 6.0 | 10,200 | 716 | 0.59 | 149 (21%) | 31 | 567 | 67 | 0.80M / 3.85M / 5.00M |
| rollout | measured | 15,327 | 2,332 | 0.50 | 1,040 (45%) | 55 | 1,292 | 27 | 3.32M / 5.71M / 17.3M |
| escalation | boot | 1,889 | 491 | 0.50 | 86 (18%) | 21 | 405 | 22 | 0.31M / 1.51M / 1.90M |
| escalation | measured | 1,496 | 503 | 0.27 | 120 (24%) | 5 | 383 | 10 | 0.26M / 0.74M / 1.33M |
| model probe | both | 16,206 | 639 | ~0.00 | 254 | 13 | 385 | 0 | out of scope |
| leafless probe | both | 41,086 | 1,514 | ~0.00 | 741* | 490* | 773 | 0 | out of scope (*it "fits" by truncating at exhaustion) |

Estimate quality on ADMITTED rollout passes, actual/estimate: bootstrap median 0.167 (the real pass-2 growth median is 1.0, not 6.0;
p90 is 30x), and measured ratio median 1.0 with p10 0.33 and p90 103. Both errors are wide in both directions.
Trial success by floor(log2(est/allow)), rollout-leaf only: for measured-ratio passes the fit rate is 69%, 46%, 47%, 50%, 54%, 50%, 54%
across bins 0-6, so the estimate says nothing about fit. For the bootstrap it is 44%, 18%, 10%, 6%, 6%, 0%, 5%.
Decks with the most rollout refusals at d5: mazesend 812 (36% of the budget left; 176 fit, 16 better; 636 abort, 52 rescued; abort
waste 3.24M units against ~21.5M for the job), stuffy 271, sea 240, mazesend2hg 194, sea2hg 160, merfolkturns 156, martyr 138,
shadowborn 125.

## Results so far
- Units: mtg-test passes 3716/3716 with the trial ON and OFF (logs/idgate/unit_on.log, unit_off.log). This was the pre-scope build.
  The scoped build has not run the unit tests yet; rerun `./build/Release/mtg-test` on build2.
- The first smoke A/B (build/scratch/idgate/smoke1) is INVALID as an A/B. Its trial was unscoped, and the off arm ran census mode 2,
  whose shadow units were billed to it. Indicative only: 61 of 231 jobs moved, mostly a few turns either way.

## Running job (queued behind other sessions' heavy jobs at handoff)
- `logs/idgate/chain_sr.sh` (PID 1728696) waits for `logs/idgate/build2.done`. build2 is the SCOPED trial; it runs as heavy_run PID
  1728637, log logs/idgate/build2.log. The chain then runs the CLEAN smoke+regression A/B: manifest logs/idgate/sr_ab.json, 1,198 jobs
  (599 cases x off/on), with MTG_IDG_CENSUS=1 (print-only). Output goes to build/scratch/idgate/sr1/{out,err,wins/,DONE}.
  logs/idgate/chain_sr.STOP is written if the build fails.
- When it is done: `python3 logs/idgate/cmp_arms.py build/scratch/idgate/sr1/out test/regression_gt.txt`. Every off-arm job should
  equal GT (if one does not, the GT is stale or the base moved; check before trusting the run). The script lists the moved jobs with
  avg and units. Then `python3 logs/idgate/ab_wall.py logs/idgate/sr_ab.json build/scratch/idgate/sr1/out --base off --arms on` for
  wall.

## Remaining steps, in order
1. Read the sr1 A/B as described above. List the decks that move, with units and wall per deck.
2. Held-out A/B on EVERY deck that moves: overnight seeds, ONE pooled batch, sized to the effect.
   `bash logs/idgate/mkmanifest.sh overnight <deck,deck,...> > logs/idgate/ovn.json`, then
   `python3 logs/idgate/mkab.py logs/idgate/ovn.json logs/idgate/ovn_ab.json --chunk 25`. The chunks alternate the arm order, set
   seed = base + offset, and set game_index = offset. Run it with `logs/idgate/run_batch.sh <manifest> <outdir>` under heavy_run, with no
   census env. Bar: every deck's net (paired turns) must be <= 0 ("+1 is not neutral. 0 is neutral."). Pair the per-game results from
   wins/<arm>__<job>.wins; loss = 9.
3. Every game the trial makes slower must recover at `--depth 8 --budget-ms 0` (repro: `--seed base+gi --game-index gi`).
4. The s3 check needs the F7 fix (c7af5242a, on wip/maze-f7 only; it is NOT in this base). Without F7 the T5 line is never offered.
   Build a scratch tree with F7 cherry-picked on top of wip/idgate. Then run
   `MTG_BIN=<that mtg> python3 /workspaces/MagicDeckTester/build/scratch/maze/r1/handoff2.py references/Maze_s_End/claude_s3_gi2.json 4 -- --depth 5 --budget-ms 20`.
   Expect T5 with the trial and T6 with MTG_ID_GATE_TRIAL=0. `MTG_TRACE=search` should show pass 2 running as a trial.
5. If a deck fails the bar, root-cause it before rejecting. Possible follow-ups: abort a trial early on an in-pass progress projection
   (the root-candidate fraction done), and the P18 / FIT kDefaultGrowth sites (~121109, ~121223, ~122956, ~123056), which are not
   touched here.
6. Write docs/design/id-start-gate-trial.md (the code comment already points to it) with the census, design and A/B. Commit it, push
   wip/idgate only, and report: census, design, per-deck A/B, d8 recoveries, s3, commits. Do not accept GT, and do not push the shared
   branch.

## Commits
See `git log origin/phase-1-2-deck-analyzer..wip/idgate`. That range holds a single WIP commit containing the src changes and the
logs/idgate tooling (force-added).

## ADDENDUM (after handoff): the clean smoke+regression A/B FINISHED
build2 rc=0 (scoped trial). sr1 batch DONE rc=0 (build/scratch/idgate/sr1/{out,err,wins}). (The waiter's exit 1 was only `cat` of the
absent chain_sr.STOP file.) `cmp_arms.py` -> logs/idgate/sr1_cmp.txt: off arm == GT on ALL 599 jobs (baseline clean); 90 jobs move;
total units on/off = 766.1M / 678.2M = **1.130x**. Next agent: start at Remaining step 1 with sr1_cmp.txt (per-job turns + units), then
ab_wall.py for wall, then the held-out A/B on the moved decks. Unit tests not yet re-run on build2.
