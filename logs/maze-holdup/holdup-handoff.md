# Maze's End hold-up: handoff (2026-11-25, agent 1 -> next agent)

Branch `wip/maze-holdup`, worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-holdup`, NOT pushed yet.
Scratch: `/workspaces/MagicDeckTester/build/scratch/maze-holdup/`.

## Done (commits on wip/maze-holdup, based on origin/phase-1-2-deck-analyzer f9e182ad4)

- `69fc2db81 .. 0ca4abfd2`: the whole wip/maze-reserve stack (20b9994d5..b33104a81) cherry-picked onto origin. Its
  prefix (6d94..56f2 = RANK_OBJECT_COLORS, MAZE_RANK_SPENDABLE, hold-up list, provider hold CPU) is the SAME patches as
  wip/maze-rank3's prefix, kept identical on purpose so the parent's rebase drops the duplicates. One conflict:
  HeuristicArm.h, where origin deleted UO_BUDGET_PRICE/CUT. Resolved as origin's slots plus RANK_OBJECT_COLORS and
  MAZE_HOLD_UP.
- `5412ef79c`: MTG_MAZE_HOLD_UP is DEFAULT ON (`EnvOn(.., true)`; heurarm comment updated). The RESERVE mode is DELETED:
  MTG_MAZE_HOLD_RESERVE and its heurarm slot, DecisionProvider::PaymentReserve, MazesEndProvider::PaymentReserve,
  MazeHoldReserveOn, ReserveHoldOut, HoldPick (HoldOut* return int again), the reserve branch of ProviderHoldUpTargets,
  phu_held2, and every reserve unit test. Kept: the exact gate (HoldSupply COVERS), the exact infeasibility half
  (HoldSupplyMax), VERIFY/PROBE/STATS diagnostics. Units 4184/4184 PASS.
- `a68b2f2c3`: exact CPU cuts in ManaPayment.cpp, plus an insertion sort in MazesEndProvider::PaymentHoldUp (the same
  order as stable_sort, without its buffer). The cuts:
  1. ProviderHoldUpTargets runs the gate before the amount bound. All three tests only drop a target, so the order
     changes nothing.
  2. A per-payment amount memo (`amount_covers`). PaymentManaCovers is monotone in need: every credit is non-negative
     (SourceMaxNet, aura, lens and scaled terms), so the early stop is equivalent to the full sum.
  3. PaymentTapped is computed lazily, only when a target reaches the held trials (PayProviderHoldUp passes nullptr).
  4. COVERS(target, 0) on each held board before its solve.
  5. A payability-dominance skip (HoldCostWithin): a target within a kept one is kept, and a target containing a dead
     one is dead.
  Units 4184/4184 PASS. Also adds `logs/maze-holdup/ab_gen.py`, the final A/B generator (force-added).

## Measurements (120-game slice: 40 play d5 b20, 40 2HG, 40 d0; perf stat instructions:u, --threads 2)

| binary / arm | G instr | vs off |
|---|---|---|
| base (5412ef79c) off (MTG_MAZE_HOLD_UP=0) | 1261.15 | - |
| base on | 1331.62 | x1.0559 |
| base probe (decided, discarded) | 1330.37 | x1.0549 (0 digest diffs vs off) |
| cut1 (cuts 1, 3, 4, 5 and the sort) on | 1327.97 | x1.0530 |
| cut2 (a68b2f2c3 = cut1 + amount memo) on | 1326.78 | x1.0520 |

- Play identity: cut1 on, cut1 VERIFY and cut2 on vs base on: 0 digest diffs and 0 units diffs on 120/120 jobs.
- cut1 VERIFY: kept 1.63M, trial-cuts 1.53M, infeasible 384k, dominance 21k, held 43k, all 0 MISMATCH.
- So the exact cuts took the overhead 70.5G -> 65.6G (-7%). Small.
- STATS (cut2): entry 7.92M, no-targets 5.86M, trial 2.02M, kept-hall 1.63M, kept-solve 440k, joint-no 234k,
  joint-yes 233k, single-tries 505k, fired 81k, amount-skip 2.97M, gated 1.55M, unpayable 435k, kept-dom 2k,
  dead-dom 19k, held-hall 43k, held-solve 302k.
- Perf (base probe vs off, flat samples, overhead ~7.5k samples). Roughly:
  - GameState copies (memmove, operator=, vector op=): ~24%. About half are the PayProviderHoldUp `pre` copy, on every
    trial, which is wasted when all targets are kept.
  - Backtrack solves: ~25%.
  - Amount bound (UntappedManaUpperBound / SourceMaxNetLive): ~16%.
  - Gate scans: ~12%.
  - ProviderHoldUpTargets + PaymentHoldUp self: ~13%.
  - PaymentTapped: 4% (now lazy).
- The cut2 perf probe run FAILED: p_probe2.data is 1.7 KB and only 5 jobs ran. Re-run it if a fresh profile is needed.

## Remaining, in order

1. Optional bigger cut (NOT exact by construction; needs a BoardsEqual verify). Avoid the `pre` copy in
   PayProviderHoldUp: take a PayRefineSnap, pay, and run the kept tests. Copy the paid board and restore in place only
   when some target is not kept. Under VERIFY, compare the restore with a full copy via BoardsEqual. The blocker is
   that a payment which changes battlefield or graveyard size (sacrifice, Deathrite) cannot be restored, so a pre-pay
   predicate is needed. Expected about -10% of the overhead. If it is not done, report it as an idea.
2. Final A/B: `python3 logs/maze-holdup/ab_gen.py build/scratch/maze-holdup/ab.json` from the worktree. That is
   14,000 jobs: play 31100000+gi n=5000 and 2HG 31300000+gi n=2000, arms off (MTG_MAZE_HOLD_UP=0) and on. Copy the
   binary to scratch first. Run ONE batch under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
   `MTG_RSS_CAP_GB=40 MTG_PGO=0 --threads $(nproc)`, from the worktree. Report net (slower - faster, negative =
   better) and the cpu ratio per format. Then run d8 b0 on every slower game (lane, <=2 slots). Each one either
   recovers or is clairvoyant (MTG_NC_SEARCH=1).
   - Note: on origin, MTG_LINE_KEEP_CRIT is default ON, and TapForCostSharedCore skips the line-keep rung when
     `phu_pay_first` is set. So hold-up ON now also turns LKR off for those payments. The A/B measures the combined
     effect; mention it.
3. Update `docs/design/maze-hold-reserve-2026-11-24.md`: reserve deleted, list default ON, the cuts with numbers.
4. Push `wip/maze-holdup` (only that branch; never phase-1-2-deck-analyzer, no push_queue). Write
   `logs/maze-holdup/report.md`: what was deleted, the CPU cuts with evidence, final nets/CPU, slower-game verdicts.
   Do NOT accept GT.

## USER ruling (verbatim, 2026-11-25), answering "Maze hold-up list default ON + delete the reserve mode?"

"Okay, those ideas are all fine and it would be nice to cut the CPU if possible, but we'll see whether that is practical."

Standing rules (from the brief):
- Cut CPU wherever possible; a fix's CPU cost is never justified by suite headroom.
- Prefer exact (byte-identical) cuts; a lossy cut needs an A/B.
- Play settings > d3 >> d0.
- Do not touch wip/baldur-fed (Maze payment path) or wip/maze-rank3 (rank fix) areas; the parent integrates.

## Running jobs

None. All lane jobs finished: job1-job4, each with a DONE marker in scratch (base.DONE, perf.DONE, cut1.DONE,
cut2.DONE). The binaries mtg.base, mtg.cut1 and mtg.cut2 are in scratch, alongside measure.sh (arms off/on/probe/
stats/verify on the slice) and cmp.py (per-job digest/units identity).
