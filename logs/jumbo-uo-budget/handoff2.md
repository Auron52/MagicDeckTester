# Handoff 2: Jumbo unlock-order price -- large re-measure DONE; CPU cuts + price variants IN PROGRESS (2026-11-21 ~16:30 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1cd888d17514cedb`, branch `wip/jumbo-uo-budget`.
Scratch `S=/workspaces/MagicDeckTester/build/scratch/claude_jumbo_uo_budget`. Earlier history: `handoff.md` (same dir).

## USER rulings (verbatim)
- 2026-11-20: "I'm okay with trying (a), but we will reserve judgement until we see the results. 20% extra is kind of
  painful, but the other question is how fast or slow Jumbo Cactuar is compared to Hinata?"
- 2026-11-21: "How is the quality comparison with this on vs off? I'm looking for averages, not absolute numbers. I'm
  talking about vs lever off. Maybe we should also test each option on a larger set of games to get more accurate
  numbers."
- Adoption bar (CLAUDE.md): every slower game must recover at `--depth 8 --budget-ms 0`, and every DECK must be net <= 0
  vs the committed tree ("+1 is not neutral. 0 is neutral.").
- Standing: "3x is not a CPU licence" -- cut a fix's CPU cost, never justify it by headroom.
- Coordinator (2026-11-21): adopt NOTHING yet; do NOT push phase-1-2-deck-analyzer (wip branch only). Task: (1) profile
  where the shipped lever's +18% Jumbo CPU goes (walk vs extra decisions) and find EXACT cuts first (byte-identical play,
  proved on an identity slice; adoptable without a quality A/B); (2) build the weight-free price variant (no per-search
  minimum charge; residual carried within the decision), check whether Bruna's +0.0006 turns/game goes away; if it does,
  A/B on Jumbo 50k + Bruna 10k (fresh gi beyond 950000; Angels/Boars/Five Colour/Kitty may drop to 1k), paired vs ON and
  OFF with CIs, d8 b0 recovery on every slower game. Bar: net <= 0 vs shipped ON on every deck at the lowest CPU. Big
  runs on claude_box.lock, replays in the lane (a job in N lane slots runs at most N processes). Hand off at ~250K.

## Commits (all on wip/jumbo-uo-budget)
- 900f472e4 per-job manifest field `uo_price_w` (W per job). PUSHED.
- 9cdd8d5e8 docs: the 400k-game re-measure section in docs/design/jumbo-apply-drop-order-2026-11-18.md. PUSHED.
- 0634c6d81 `MTG_UO_BUDGET_RESID` / manifest `uo_price_resid` (default off): residual carried per decision (g_uo_resid,
  scoped by DecisionBudgetScope), no per-search floor; cut tested only where a unit is charged. NOT pushed.
- 8eaecbd00 `MTG_SPS_EQUIP_MEMO` (default ON, intended EXACT cut): SubsetPayableSequential memoised for subsets holding an
  unlock/trailing equip (paymemo::Probe fn SPSU, raw-id key = rank-ordered casts + every other sel member in sel order);
  bypassed while the price is on. NOT pushed, NOT yet built/verified.
- 102922d37 `MTG_UO_BUDGET_REPLAY` / manifest `uo_price_replay` (default off): cachecharge Rec.uo; a computation whose only
  decision units are the uo price's (ceil mode) stays storable in mode 1 and a hit replays them. NOT pushed, NOT built.
Binaries: `$S/mtg.w` = 900f472e4 (used by ab2); `$S/mtg.r` = 0634c6d81 (resid); `$S/mtg.m` = 102922d37 (memo + replay),
appears when the queued build finishes.

## Measurements so far
### A. 400k-game re-measure (DONE, doc section committed + pushed). gi 900000+, binary mtg.w, 32 threads.
Mean per-game turn delta, paired, non-win = 9 (+ = slower), 95% CI. Full output `logs/jumbo-uo-budget/ab2/compare2.txt`.
- Jumbo 50k: on vs off -0.00018 (-0.00033,-0.00003), CPU x1.183 (226 vs 191 ms/g). pc16 vs off -0.00002, vs on +0.00016
  (+0.00001,+0.00031), CPU x1.084 off / x0.916 on. pc32 vs on +0.00010 (CI incl 0), x1.098/x0.928. pc64 vs on +0.00008
  (CI incl 0), x1.111/x0.939. p16 = pc16 quality, x1.115/x0.943. Net vs on: pc16 +8, pc32 +5, pc64 +4, p16 +8.
- Bruna 10k: on vs off -0.00080 (-0.00135,-0.00025). pc16/32/64 IDENTICAL: vs on +0.00060 (+0.00012,+0.00108), net +6;
  p16 +5. CPU within noise.
- Angels/Boars/Five Colour/Kitty 2k: no turn moved on any arm.
- d8 b0: 18 slower games (vs on or off), 82 replays, ALL recover, all arms byte-identical to on at d8 b0
  (`logs/jumbo-uo-budget/ab2/recov2.txt`).
### B. Profile (Jumbo gi 719000-719399, 4 threads, lane; `$S/prof/{off,on,on_stats,on_census}/`)
- CPU ms/game: off 103.6, on 122.2 (+18.6 ms, +18%). units/g off 5689, on 5755 (+1.2%).
- Walk wall (MTG_UO_PRICE_STATS): enum 147,719 searches / 2.54M steps / 5.37 s; apply 58,046 / 0.23M / 0.89 s
  => 15.7 ms/game = ~84% of the delta. The rest (~3 ms) is the extra plans/decisions (+1.2% units).
- MTG_PAY_CENSUS: SubsetPayableSequential 360,709 calls, 188,727 exact repeats (132,652 on the same board object),
  false-verdict calls 18.71 Gcyc vs 1.95 true; repeats 8.32 Gcyc. Existing SPS memo served only 13,008 hits (it skips
  every subset with an unlock/trailing equip -- exactly the lever's subsets). => motivates MTG_SPS_EQUIP_MEMO.
### C. Bruna screen of the residual variant (DONE; `$S/scr/`, merged view `$S/scr_compare.txt`; gi 900000-909999, mtg.r;
ran contended, so its CPU column is NOT comparable). `on` 1000-game slice identical to ab2.
- rc8 (resid+cut W8) vs on +0.00050 (+0.00006,+0.00094) net +5; rc16 same +5; r16 (resid, no cut) +0.00040 net +4.
- The SAME games lose under every price variant: 900389 4->5, 901553 4->5, 904788 6->7, 908617 4->5 (+905149 5->6 on
  pc/rc, +905415 on pc). => the Bruna loss is NOT the per-search floor. Hypothesis: cachecharge mode 1 refuses to STORE
  any plan-cache computation that charged a unit, so any uo charge turns free cache hits into full-price recomputes.
  MTG_UO_BUDGET_REPLAY (102922d37) tests that.

## Queued / running jobs (leave them)
1. Lane build of 102922d37 (memo + replay): box_lane PID 199788. Log `$S/build_m.log` (DONE = a line `rc=N`); on rc=0 it
   copies build/Release/mtg to `$S/mtg.m` ("snap-m" in its task output).
2. Lane single-game diagnostics (on vs r16, Bruna gi 908617 and 904788, JSON logs for first-divergence): box_lane PID
   276284, script `$S/diag/run.sh`, output `$S/diag/<arm>.<gi>/`, DONE marker `$S/diag/DONE`. Low priority now.
3. Memo checks, chained after job 1: waiter PIDs 283403/283808/283809 (a bash `until grep rc= build_m.log` loop, then
   `box_lane.sh --slots 4 -- $S/prof/run_m.sh`). Runs, on mtg.m, Jumbo 400 games gi 719000-719399 4 threads:
   m_verify (MTG_PAY_PROBE_MEMO_VERIFY=1: the "SubsetPayableSequential+equips" mismatch count MUST be 0, and its hit
   count), m_on / m_on_nomemo / m_off / m_off_nomemo (identity per game + CPU), then m_loss (`$S/prof/loss.json`: the 6
   Bruna loss games under on / pc16 / pcx16 (price+cut+replay) / px16 (price+replay), MTG_PLAN_POOL_STATS=1).
   Outputs `$S/prof/<tag>/batch.{out,err}`, DONE marker `$S/prof/DONE_m`. If build_m fails, the waiter exits 1.
4. Box-lock screen chain (chain_scr.sh, flock PID 200977): FINISHED 15:53 UTC rc=0 (section C).

## UPDATE (after the handoff was written): jobs 1 and 3 are GONE
The lane build (PID 199788) and the chained memo-check waiter (283403/283808/283809) were TERMINATED while still waiting
for lane slots. The build never started (no `$S/build_m.log`), so this was NOT a compile failure, and `$S/mtg.m` does not
exist. Only job 2 (diagnostics, PID 276284) is still queued. The stale objects for ValueArm.h dependents and TurnSolver.cpp
are already deleted in `build/wt/agent-a1cd888d17514cedb`.
FIRST STEP for the successor: re-run the build of 102922d37 in the lane:
`cd <worktree> && bash /workspaces/MagicDeckTester/scripts/box_lane.sh --slots 4 --cap-gb 3 -- bash -c "./build.sh > $S/build_m.log 2>&1; echo rc=\$? >> $S/build_m.log"`,
and on rc=0 run `cp build/Release/mtg $S/mtg.m`. Then run `box_lane.sh --slots 4 --cap-gb 3 -- $S/prof/run_m.sh` (DONE marker
`$S/prof/DONE_m`). Arm the waiters on PIDs or markers yourself.

## Remaining, in order
1. When `$S/prof/DONE_m` exists: check m_verify mismatches (grep "\[pay-memo\]" batch.err) = 0; per-game identity of
   m_on vs m_on_nomemo and m_off vs m_off_nomemo (turn + digest + units in .wins / batch.out); CPU m_on vs m_on_nomemo.
   If exact and it cuts CPU, run a cross-deck identity slice under claude_box.lock (memo on vs MTG_SPS_EQUIP_MEMO=0, two
   processes; every suite deck ~200-500 games, Jumbo 2000). If any mismatch: root-cause the key (sel inputs not keyed?)
   or drop it.
2. m_loss: do pcx16/px16 recover the Bruna loss games (turn == on's)? If yes, screen Bruna 10k (gi 900000-909999) for
   pcx16 (and pcx32/64) under the lock vs ab2's on; if net <= 0 vs on, run the full A/B (Jumbo 50k + Bruna 10k at gi
   950000+, others 1k; arms off, on (with memo), pcx16, pcx32, pcx64 and whichever else is cheap) with mkab2.py-style
   manifest + compare2.py + mkrec2.py/recov2.py (d8 b0 in the lane). If no: investigate the first divergence in the
   diag logs (job 2) before trying more variants.
3. Other exact-cut ideas not yet tried: the DFS copies a GameState snapshot per node with a live sibling (UoDfsSnap);
   a sound per-child first-step pre-test could skip both copy and walk. The apply-site search (0.89 s/400 games) is
   unmemoised.
4. Doc: add a section to docs/design/jumbo-apply-drop-order-2026-11-18.md (profile, memo, resid screen, replay result);
   commit; push wip/jumbo-uo-budget only. Report to the coordinator; adoption is the USER's call.
Box rules: one big run on claude_box.lock with nproc threads, MTG_RSS_CAP_GB=40, MTG_PGO=0 on all arms; lane max 4 slots;
`setsid ... &` forks, so take the flock PID from ps, not $!; no rm on globs (use fresh dirs); never timeout / pkill -f.
The lane and the lock have been congested (waits of 1-2 h).

## UPDATE 2: job 2 (diagnostics) FINISHED (`$S/diag/`, DONE marker present)
Single-game CLI repros (binary mtg.r, 1 thread) reproduce both losses: Bruna gi 904788 on 6 / r16 7; gi 908617 on 4 /
r16 5. JSON game logs: `$S/diag/<arm>.<gi>/*.json` (diff on vs r16 for the first divergent decision). MTG_UO_PRICE_STATS
for the r16 games: 904788 enum 7,156 searches / 80,693 steps -> 5,033 units charged, apply 1,174 / 2,558 -> 132 units;
908617 enum 401 / 1,234 -> 75 units, apply 2,886 / 9,869 -> 631 units, cuts 0 in both. So in these games the charge is NOT
tiny: 904788's enumeration bills ~5k units over the game. Both "the charge itself" and "charging computations are not
cached" remain candidate mechanisms; the m_loss run (pcx16 / px16) separates them.
