# Maze's End P5b-2: the land-order fold as a FIRST-REACHED dedup (results)

Branch `wip/maze-p5b2` (worktree `.claude/worktrees/maze-p5b2`, from `wip/maze-shared` 7626b6105 + P5b's three
commits ported). Scratch: `build/scratch/maze/p5b2/`.

USER (VERBATIM): "the order doesn't matter much here unless the second land is played after other things or we are
paying for a cost like Gateway Plaza".

## What was built (`MTG_FOLD_LAND_ORDER_SEEN`, heurarm `FOLD_LAND_ORDER_SEEN`, DEFAULT OFF)

P5b's fold (`MTG_FOLD_LAND_ORDER`, still present, still default OFF) deleted entries from the ENUMERATION, keeping
the SORTED twin. That had two order effects. It kept the sorted twin even when the search would reach the other one
first. Deleting from a RANKED continuation list also renumbers every later rank: `bp_choice k` indexes the list, and
wave 0 takes ranks < W.

"First reached" cannot be decided inside the enumeration. The root's visit order depends on the escalation's probe
ranks, which are search history, and on the wave schedule. The continuation list, meanwhile, is a memoised function
of the state that the executor replays for lockstep. So the fold moved out of the enumeration into the search loops'
POST-APPLY DEDUP SETS, which already skip a candidate whose post-apply state an earlier sibling reached. Those sets
are `bp_seen_states` (FSLineWin node children, variants, waves, tranches; SolveWithLookahead candidates, waves,
tranches) and `m2_seen_states`.

`SeenKey(state)` = `BuildDedupKey` of a copy in which the battlefield positions of our lands that entered this turn
are re-filled in card-number order, within one commutation class >= 0 (`LandEntryOrderClass`, read post-apply). So:
- no list, rank, memo key, executor replay or visit order changes;
- the twin the loop reaches FIRST is scored, and the later one is skipped (a variant; an ordinary plan only records,
  as before).

**Identity.** Key equality proves the two states are the same board up to those positions. Every other field is
compared exactly: hand, tapped flags, counters, chosen colours, life, library, pins and turn counters. Attachments
and pins use card numbers, never battlefield indices. The USER's carve-outs are class -1 and keep their exact
position:
- an as-enters colour, Gateway Plaza, Gond Gate, The Black Gate and Thran Portal;
- every land while two Amulets are out;
- Guild Summit splits Gates from non-Gates.

The engine residual is the same as P5b's: vector-order tie-breaks in a greedy playout or a payment. Deck-gated to
static extra land drops without landfall (Maze's End only); off in human play and under MTG_UNPRUNE.

Unit cases (`test_mazes_end_p5b.cpp`, two new) check:
- the two orders collide, and so do three lands in any order;
- the canonical order keys exactly as before;
- a tapped flag, the hand, or lands that did not enter this turn still separate the states;
- Gateway Plaza, Cliffgate, two Amulets and the Summit Gate/Maze's End pair keep their order (two Gates under Summit
  still fold);
- another deck gets the exact key.

All 73 Maze unit cases pass.

## Held-out A/B (one pooled batch, 11,200 jobs, arms innermost, P5b's shapes)

Seeds: std 181001-184001 + gi, 2HG (life 30, heads 2) 191001-194001 + gi.

| cell | games | off | on | better/worse | net | units/game off -> on |
|---|---|---|---|---|---|---|
| std d5 b20 | 400 | 5.5575 | 5.5575 | 0/0 | 0 | 114,202 -> 115,421 (+1.07%) |
| std d3 b10 | 400 | 5.6200 | 5.6175 | 1/0 | -1 | 81,214 -> 81,116 (-0.12%) |
| std d1 b3 | 1000 | 5.6790 | 5.6800 | 0/1 | +1 | 35,651 -> 34,710 (-2.64%) |
| std d1 b1 | 1000 | 5.7130 | 5.7140 | 1/2 | +1 | 29,957 -> 29,283 (-2.25%) |
| **std total** | 2800 | | | **2/3** | **+1** | -0.81% |
| 2HG d5 b20 | 400 | 5.4950 | 5.4925 | 1/0 | -1 | 127,387 -> 126,914 (-0.37%) |
| 2HG d3 b10 | 400 | 5.4050 | 5.4050 | 1/1 | 0 | 66,643 -> 65,805 (-1.26%) |
| 2HG d1 b3 | 1000 | 5.5220 | 5.5230 | 1/2 | +1 | 33,660 -> 32,892 (-2.28%) |
| 2HG d1 b1 | 1000 | 5.5680 | 5.5670 | 1/0 | -1 | 29,031 -> 28,311 (-2.48%) |
| **2HG total** | 2800 | | | **4/3** | **-1** | -1.43% |

Play churn is ~1% of games (985-397 same-digest per cell).

For comparison, P5b's search-only form measured 1 better / 4 worse, net +3 over 2,800 std games.

Units fall wherever the budget does not bind. At d5 b20 they are flat or up: the freed budget is spent elsewhere,
mostly on later waves. The 20-game d5 b20 probe (seeds 181001+) was byte-identical in play and spent +8.9% units.

## d8 b0 on every slower game (20M-unit ceiling, ignore_play_profile)

All 6 recover with the SAME digest on both arms:

| game | d8 b0 off / on | units off -> on |
|---|---|---|
| std d1b1 gi661 | T5 / T5 | 32,561 -> 31,684 |
| std d1b1 gi772 | T6 / T6 | 27,454 -> 26,412 |
| std d1b3 gi484 | T5 / T5 | 8,188,529 -> 7,521,015 |
| 2HG d1b3 gi494 | T5 / T5 | 10,068,575 -> 9,130,744 |
| 2HG d1b3 gi843 | T5 / T5 | 891,552 -> 830,377 |
| 2HG d3b10 gi119 | T6 / T6 | 235,173 -> 235,173 (the fold never fired) |

At b0 the fold removes 3-9% of units on the games where it fires and plays identically: the identity holds at
unlimited budget.

## Root cause of the losses: the later duplicate is a second, warmer look under a truncating budget

- **std gi661** (seed 183662, d1 b1, `MTG_FS_ROOT_DUMP=2`). The T2 root node hosts two base plans, each holding
  Exploration and a land: Azorius + Exploration, then Talon; and Talon + Exploration, then Azorius.
  - Flag off, the Azorius-first child scores **7** and the Talon-first child scores **6**. Same board, different
    value.
  - At b3 and b20, the Azorius-first child itself scores 6. So at b1 the first evaluation was budget-truncated, and
    the second evaluation of the same board, with caches warmed by the first, found the better win.
  - Flag on, the Talon-first child is skipped as the duplicate. The root keeps "Talon + Exploration, stop" (6 in
    the cut search) and the game goes T5 -> T6.
- **std gi772** (seed 183773). Same mechanism inside the lookahead. The T1 root candidate "Gond Gate + Amulet" has
  its tail scored 6 with the flag off and fails to beat 7 with it on: a twin was skipped in a later lookahead turn.
- **std gi484** (d1 b3). The MULLIGAN sequence differs: bottoming plays candidates out through the same search.

So it is a budget effect and not an order effect. The dedup skip assumes the kept twin's value is the board's value.
Under an anytime budget it is only a lower bound, and the repeat evaluation is effectively a free re-search. The
losses concentrate at d1 b1/b3 (std 1 better / 3 worse there). At d3/d5, std is 1/0 and 2HG is 2/1.

## Verdict

Not adopted. The bar is per-deck net <= 0 plus d8 recovery. 2HG passes (net -1). Maze's End fails by ONE game
(net +1, 2 better / 3 worse), and every slower game recovers at d8 b0. Shipped DEFAULT OFF; =1 arms it. This is the
USER's call.

**Possible amendment (not built): skip the later twin only when the kept twin's evaluation was COMPLETE.** Record
the canonical key only after the first twin's search finished without its (sub)budget cutting it. This makes the
skip an identity under a finite budget too: a truncated first look leaves the second look in place. It needs a
"was this candidate's evaluation cut" signal from FSLineTail / the lookahead, which does not exist today.

## Stage 4: units, scenarios, Maze tiers (binary dfd9a4edb; later commits only change comments)

- All units pass: 3,720 cases. Scenarios pass: 584 of 584.
- `--smoke --regression --deck=mazesend,mazesend2hg` with the flag OFF: every Maze key is BYTE-IDENTICAL to the
  coordinator's integration-engine run `build/scratch/maze/push2/r2/tiers.log` (12:27 UTC) -- all 12 keys.
- Several keys FAIL against the committed GT on both arms. The GT predates P7's default-ON folds (last GT commit
  2f67da724; P7 is 9df0783f7, a1b421030, 91cfd6bff). That accept belongs to the integration push, not to this
  change.

With the flag ON (`MTG_FOLD_LAND_ORDER_SEEN=1`), only these three d5 keys move:

| key | flag off | flag on |
|---|---|---|
| mazesend_smoke_d5_s1001 | 5.7273/26183576 | 5.7273/6c193946 (same score) |
| mazesend_regression_d5_s2002 | 5.6576/ac05acc5 | 5.6576/aa5601b0 (same score) |
| mazesend_regression_d5_s3003 | 5.6733/f87213f8 | 5.6667/41c1f693 (one game faster) |

mazesend2hg, d0, d3 and the backstop rows do not move. No other deck can move (deck gate). No GT is accepted: the
fold is not adopted.
