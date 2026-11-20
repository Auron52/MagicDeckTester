# Maze's End P5b-4: the uncut-twin land-order fold measured (results)

Branch `wip/maze-p5b2`. Code under test: e2b2516ee (`MTG_FOLD_LAND_ORDER_SEEN`, the "skip a twin only after an
UNCUT evaluation" amendment). Diagnostics: cafaae5cc (inert by default; 6 A/B jobs re-run byte-identical in digest
and units). Scratch: `build/scratch/maze/p5b3/` (A/B `ab/`, d8 `d8/`, traces `diag/`, ref bench `rb/`).

## Gates

- Build clean. Units: 3,721/3,721 cases pass. Scenarios: 584/584.
- std gi661 (seed 183662, d1 b1), the P5b-2 loss, now plays T5 on both arms with the same digest
  (`skipped=3 cut-twin-searched=35`).

## Held-out A/B (one pooled batch, 11,200 jobs, arms innermost; seeds std 301001-304001 + gi, 2HG 311001-314001 + gi)

| cell | n | off | on | better/worse | net | units/game off -> on |
|---|---|---|---|---|---|---|
| std d5 b20 | 400 | 5.5725 | 5.5675 | 2/0 | -2 | 112,798 -> 112,951 (+0.14%) |
| std d3 b10 | 400 | 5.6125 | 5.6125 | 0/0 | 0 | 79,799 -> 79,411 (-0.49%) |
| std d1 b3 | 1000 | 5.6950 | 5.6940 | 1/0 | -1 | 34,050 -> 33,720 (-0.97%) |
| std d1 b1 | 1000 | 5.7190 | 5.7190 | 1/1 | 0 | 31,660 -> 31,493 (-0.53%) |
| **std total** | 2800 | | | **4/1** | **-3** | -0.41% |
| 2HG d5 b20 | 400 | 5.4600 | 5.4600 | 0/0 | 0 | 115,937 -> 116,451 (+0.44%) |
| 2HG d3 b10 | 400 | 5.4875 | 5.4875 | 0/0 | 0 | 74,932 -> 74,435 (-0.66%) |
| 2HG d1 b3 | 1000 | 5.5430 | 5.5430 | 0/0 | 0 | 34,190 -> 33,790 (-1.17%) |
| 2HG d1 b1 | 1000 | 5.5420 | 5.5430 | 0/1 | +1 | 29,619 -> 29,461 (-0.53%) |
| **2HG total** | 2800 | | | **0/1** | **+1** | -0.39% |

Play churn: 9 of 1000 games at d1, 2-6 of 400 at d3/d5 (P5b-2 had ~1%; P5b-2's units saving was -0.8% / -1.4%, now
-0.41% / -0.39% because cut twins are re-searched). Slower games: 2HG d1b1 g491 (5 -> 6), std d1b1 g945 (5 -> 6).

**Bar: std passes (net -3). 2HG FAILS by one game (net +1, 0 better / 1 worse).** Default left OFF.

Census over the whole A/B (`MTG_LAND_TWIN_STATS`): checked 34,759,481; skipped 398,068, of which **3 rest on a
first-win horizon exit**; cut-twin-searched 456,023; closed clean 12,621,393 / cut 10,077,721; upgraded 22,108;
unclosed 11,662,299.

## d8 b0 of both slower games (20M-unit ceiling, ignore_play_profile)

| game | off / on | units off -> on |
|---|---|---|
| std d1b1 g945 | T5 / T5, same digest | 14,541 -> 14,541 |
| 2HG d1b1 g491 | T5 / T5, same digest | 279,206 -> 273,980 (-1.9%) |

Both recover. At b0 the census shows `cut=0` (every window closes clean), and the skips remove units. Both games also
play identically on both arms at d1 b0.

## Reference bench (fold ON, binary e2b2516ee, committed policy = engine default d5 b20, pooled, 32 threads)

`scripts/ref_bench.py --deck maze_s_end`: **11/11 tie the USER** (human 5.818, search 5.818; s1 is a no-win on both),
0 short, 0 faster. Log: `build/scratch/maze/p5b3/rb.out`.

## Root cause of the two losses (bisected with MTG_LAND_TWIN_SKIP_LIMIT; traced with MTG_LAND_TWIN_TRACE)

- **2HG g491** (seed 313492): skip #134 of 168 flips it (#133 alone keeps T5). It is a T3 lookahead candidate inside
  the T2 root plan "Gond Gate: Burgeoning + Exploration". The twins are Selesnya Guildgate*(T) / Maze's End*(T) /
  Amulet* in either land order, and each candidate's evaluation is ONE depth-0 `SimulateToEnd` rollout, cutoff 6.
  The kept twin's rollout returned 8 and the skipped twin's (when searched) returned 9: the two rollouts DIFFER, but
  neither beats the cutoff, so the skip drops no line.
- **std g945** (seed 303946): skip #5 flips it. The same shape at T4 (Izzet Guildgate* / Maze's End*(T) under two
  Burgeonings): kept twin 7, skipped twin 9, cutoff 6. Neither is a candidate either.
- **What flips the game is the side effect of the skipped rollout.** The very next, non-twin candidate (no fold
  involvement) rolls out to 9 at cutoff 7 (then 8 at cutoff 9) when the twin's rollout ran first, and to 7 / 7 when it
  did not. Rollouts do not enforce the budget, so only cached state can produce that difference.
- **That cache is unsound with the fold OFF too.** `MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1` on flag-off g491 (d1 b1):
  6 STALE win hits (cached=8, fresh=7) and 2 STALE no-wins, both attributed **"SAME-STATE DRIFT"**. Re-rolling the
  identical stored state later gives a different result (8 then 7; 7 then 8). So the depth-0 rollout in Maze's End is
  history-dependent: it reads something outside its TT key.
- So the losses are cache-order chaos on top of a flag-independent rollout-drift defect, not a line the fold made
  unfindable.
- Secondary: the twins' rollout VALUES differ (8/9, 7/9). The depth-0 rollout is not order-invariant (the
  `BuildSimKey` comment already says so: "greedy tie-breaks read vector order"). A skip that rests on a pure rollout
  is therefore not an exact identity under any budget, even when it closes uncut. It did no harm in either game,
  because both values failed the cutoff.

## Options (the USER's / parent's call; nothing adopted)

1. **Fix the rollout drift first** (flag-independent; "the top priority are optimizations and fixes"). Find what the
   Maze depth-0 rollout reads outside `BuildSimKey`, starting from the two SAME-STATE DRIFT states above.
   - Repro: g491 with the fold off, `MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1`, `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2`,
     `--seed 313492 --game-index 491 --depth 1 --budget-ms 1 --threads 1`.
   - Then re-run this A/B unchanged (manifest `ab/manifest.json`).
2. Make a pure depth-0 rollout evaluation non-vouching (Close() not called at the three SolveWithLookahead sites when
   `sub_depth <= 0`). This removes the order-dependent leaf case from the skips. It does not address (1).
3. Adopt as is: std -3 / 2HG +1 (combined 4 better / 2 worse), every slower game recovering at d8 b0. This does not
   meet the per-deck bar, so it is not an agent's call.
