# Maze's End P5d -- P5d-6 handoff (2026-11-20). Predecessors: logs/maze/P5d5-handoff.md (+ P5d4, P5d3: USER rulings VERBATIM)

Worktree `.claude/worktrees/maze-p5d`, branch `wip/maze-p5d`, rebased onto origin/phase-1-2-deck-analyzer 8fbbc5023 (no
merge). Push ONLY wip/maze-p5d. Scratch `build/scratch/maze/p5d6/` (= `$O`). Every heavy job goes through heavy_run.sh, with
MTG_RSS_CAP_GB=20 and MTG_BUILD_JOBS=12.
**TEMPORARY HOLD (coordinator, USER-sanctioned):** run tiers with `VPC_SKIP=1`, and run the Maze reference gate separately:
`MTG_BIN=<bin> python3 test/viewer_protocol_check.py --strict --only Maze_s_End --threads $(nproc)` under heavy_run. The chain
below already does both.

## Done (commits on wip/maze-p5d)
- Rebase: skipped the two GT-accept commits (P5c 3ceb22633 and P5d d616a508a), so GT must be re-accepted (below). Resolved the
  DecisionProviders conflict as follows. `PlanListFilterLive = MazeUnpayablePruneLive || MazeHeapFilterLive || MazeIdleFetchLive`.
  `PlanListAdmit` runs, in order: the unpayable prune, `MazeHeapTreasureAdmit` (when MazeHeapFilterLive), then `MazeIdleFetchAdmit`.
  HeuristicArm.h and DecisionProviders.h keep both sides. In the ledger, P5d's section comes after HEAD's F5 section.
- `9f815199b` reveal guard `MTG_MAZE_IDLE_FETCH_REVEAL`, a proper default-OFF lever (slot after MAZE_IDLE_FETCH), plus the
  g320 root cause in the ledger.
- `b1b836b3b` Q1/Q2 levers, all default OFF: `MTG_MAZE_FETCH_TIER_FULL` (C2), `MTG_MAZE_FETCH_RANK_COND` (fetch 1000 only
  when <= 2 names remain after it; otherwise 100, or 99 while a Guild Summit is in hand), `MTG_MAZE_MANABOND_DISCARD`
  (MazeDumpDiscards + new hook `DecisionProvider::CrossingDumpYesOrder`, used at SpellEffects' Manabond yes/no:
  2 = prune "no", -1 = "no" first, 0 = default; prune (a) and the fetch eval's no-drop tier count Manabond only when every
  discard is free), and `MTG_MAZE_DISCARD_BURGEONING` (sub-lever, default ON).
- `42ff28a4d` Q2 amendment: land tutors are HELD while no Maze's End is ours on the battlefield or in hand (the g21 root
  cause, below). Unit tests in `test/unit/test_mazes_end_p5d6.cpp`. Units 3776/3776 pass (`$O/units.log`).
- GOVERNING-PRINCIPLE CLASSIFICATION (Q2, CrossingDumpYesOrder). Clear, pruned: every nonland discard is free.
  Close, "no" ordered first: a tutor held, or a kept card going for one land. Close, default order: everything else.
  Q1 is ORDER only (an eval), with no prune. Prune COUNTS per class are NOT yet measured. That needs a census
  (count the CrossingDumpYesOrder return values over the A/B).

## 4-salt blind ensemble (d8 b0, mtg.p6, rev = reveal guard vs cand = shipped): ALL FOUR PERSIST
d5 g219 rev 6/5/6/6 = 23 vs cand 5/5/6/6 = 22 (+1); 2HG g108 7/6/6/9 = 28 vs 6/6/6/7 = 25 (+3); 2HG g161 8/7/8/7 = 30 vs
7/7/8/7 = 29 (+1); 2HG g21 7/7/9/6 = 29 vs 6/7/6/6 = 25 (+4). Total +9, so these are NOT clairvoyance. (9 = unwon by T8.)
- g21 s3 ROOT CAUSE (`$O/g21c`, `$O/g21r` logs; `python3 $O/turns.py $O/g21c $O/g21r`). At T3 no Maze's End is on the
  battlefield, so (a) is not live at the root. The guard changes deeper subtree values, and the T3 choice flips: cand plays
  Cliffgate and casts Sylvan Scrying for Maze's End (wins T6); rev plays Gond Gate and casts Guild Summit. Then at the T4 end
  step rev's Manabond yes puts 2 lands and DISCARDS THE ONLY SYLVAN SCRYING, with no Maze's End in hand or play, and the game
  is never won. Fix candidate: the Q2 amendment in `42ff28a4d` (tutor held -> "no" first). The A/B arm `revq2` measures the
  composition.
- g108 s4: repro `$O/rp.sh g108r 202109 108 4 1 MTG_MAZE_IDLE_FETCH_REVEAL=1` was still running at handoff (PID 2308091,
  output `$O/g108r/`, `$O/g108r.out`; cand `$O/g108c` = 7). Compare the two with turns.py.

## RUNNING chain (`$O/chain.sh`, PID 2323136; markers `$O/{build3,units,tiers,vpc,ab}.done`)
build3 DONE (binary snapshot `$O/mtg.q` = 42ff28a4d minus the comment edit, same code) -> units DONE (pass) -> `VPC_SKIP=1
regression.sh --both` (`$O/tiers.log`) -> Maze VPC (`$O/vpc.log`) -> pooled A/B `$O/m_ab.json`, 26,900 jobs, on mtg.q
(`$O/ab.out`, `$O/ab.err`; ~5-6 h; check the heartbeat in the first 10 min). Arms, all on the same seeds: ship / rev / c2 /
cond / q2 / revq2 in clairvoyant AND blind (suffix B, blind_salt 1). q2nb (Burgeoning KEPT) is blind only, on d5 + 2HG. Rows:
d5 b20 1000, 2HG d5 b20 300, d3 b10 300, d1b3 200, d1b1 200, and d0 400 (clairvoyant only; no rev/revq2 at d0).
ARM THE WAITER: `while [ ! -f $O/ab.done ]; do sleep 60; done`, and the parent's 2-hourly CronCreate (the run spans the
13:30-21:00 UTC window).
Analyse: `python3 build/scratch/maze/p5d/anab.py $O/ab.out shipB` (blind) and `... ship` (clairvoyant).

## REMAINING (in order)
1. Tiers: the Maze keys move (P5d is not on origin). Accept `--deck=mazesend,mazesend2hg` after confirming that only Maze keys
   moved, then run check_gt_logs. Re-run smoke with the 42ff28a4d binary if src moved after the chain's build (comment-only:
   no play change). Commit GT and push wip/maze-p5d.
2. A/B tables per row, blind and clairvoyant, for each lever vs ship (net <= 0 per deck at every row; std and 2HG co-equal).
   Then d8 b0 on every game slower than ship for each candidate lever (one pooled batch, 50M ceiling, same_seed_inflight 0,
   blind slower games at their salt).
3. BURGEONING PER FORMAT: compare q2B vs q2nbB on std d5 and on 2HG d5. Report Burgeoning against the other Manabond-ranked
   cards per format. Consider a format-split variant (Burgeoning kept when opponent_heads > 1). The 2HG treatment is the USER's.
4. Prune counts per class (census) for the governing-principle report.
5. Ledger P5d-6 section in `docs/design/analysis-Maze's End.md`. Report verdicts and recommendations to the parent: the
   reveal guard has a blind +9 on its 4 d8 non-recoveries, so it is NOT a clean win as it stands.

## USER rulings (VERBATIM) -- all of P5d5-handoff's still apply (Q1, Q2, Q2 follow-up, Q2 addendum, governing principle,
"Okay, sure. I have no issue with trying out experiments and adopting or rejecting them based on results.", "We need a full
quality vs time comparison with this, like with every other trade."). New this session (Q5, Burgeoning):
- "5. Yes, Burgeoning is probably droppable. It is a bit too unreliable."
- "It's a lot better in 2HG, though."
- "There it is actually potentially better than the others at times."
- "But still less consistent for sure."
Coordinator's reading: droppable in std; in 2HG keep it in the ranking (it may outrank other ramp at times), report per-format
numbers, and leave the 2HG decision to the USER. The report must show Burgeoning's SPREAD (per-game variance / tail:
how often it is much worse) alongside the averages, per format, so the USER can weigh the 2HG trade.
