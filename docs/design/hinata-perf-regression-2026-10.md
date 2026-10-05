# Hinata search cost regression since 2026-09-11: bisect (2026-10-05)

## Question

On 2026-09-11 the overnight `hinata_overnight_d3_s4004` cell (400 games) took 215,851 ms of summed
game time (main-tree `test/logs/overnight/batch.log`, binary `56021774`). On 2026-10-05 the same cell
on `soldiers-analysis` took 1,452,687 ms (units 21,857,541). That is about 6.7x, with hinata2hg at
about 6.3x. The user's view was that this is a real performance regression and not the deliberate
Hinata budget change (the b30 adoption). This report tests that claim.

## Verdict on settings vs work: the settings are the same, the work is greater

The play settings are identical at every commit tested:

- **The overnight cells set their own settings.** The d3 cells pass `depth 3, budget_ms 10,
  ignore_play_profile`. The d5 cells use `value_play` depth 5 but override the budget to 20. The
  manifests are identical in the 09-11 tree and today's tree.
- **The b30 adoption came before the baseline.** `value_play` (`target_depth 5, budget_ms 30`) was
  adopted on 2026-09-08, before `56021774`, and is byte-identical at every commit. The only change
  to `Hinata2.value.json` is that `0cd1f5f4` added `mull_gen_depth/mull_gen_budget_ms`, which play
  does not read.
- **The rest of the deck folder is unchanged.** `.profile.json`, `.cod` and `.escgate.json` have
  the same blob at every commit.
- **The budget-to-units conversion is unchanged.** `NODES_PER_VIRTUAL_MS = 900` at both ends.
- **The settings each run actually used match.** The `[play]` lines read `depth=3 budget=10ms
  source=cli(--ignore-play-profile)` and `depth=5 budget=30ms source=value_play` at every commit.

So the extra cost comes from more search work at the same settings. The user is right that this is
not the b30 change.

## The 6.7x figure is mostly contention; the clean regression is about 2.1-2.6x

I re-ran all 12 Hinata overnight d3/d5/2HG cells (3,200 games each run) alone on the box with
`--threads 20`. Per-game digests reproduce the GT and the soldiers run exactly. The table shows
summed per-game ms, with `units_total` in brackets:

| cells          | 09-11 run (56021774) | pre-purge 06e75449 | d1832b52 (purge) | tip 2d5ca753 | soldiers run (contended) |
|----------------|----------------------|--------------------|------------------|--------------|--------------------------|
| hinata d3 x4   | 912,041              | 786,204 (31.3M)    | 2,469,624 (68.4M)| 2,085,236 (79.7M) | 4,915,576 (79.7M)   |
| hinata d5 x4   | 763,794              | 856,346 (35.7M)    | 1,478,073 (60.6M)| 1,630,544 (64.7M) | 3,937,875           |
| hinata2hg d5 x4| 213,803              | 262,510 (10.7M)    | 712,815 (18.7M)  | 563,089 (21.4M)   | 1,343,514           |

- **The soldiers run was about 2.4x contended.** It used the same units as my clean tip run but
  took 2.36x the ms, which fits the "two heavy runs at once" picture behind the OOM.
- **The clean regression from 09-11 to tip is 2.29x CPU at d3, 2.13x at d5 and 2.63x on 2HG.**
  From pre-purge to tip, units are 2.55x, 1.81x and 2.00x.

## Per-step factors (units are deterministic; ms is CPU summed over games, box otherwise idle)

| interval | d3 units / ms | d5 units / ms | 2HG units / ms |
|---|---|---|---|
| `56021774` -> `06e75449` (09-11 to 10-03, ~370 src commits) | ms 0.86x | ms 1.12x | ms 1.23x |
| **`06e75449` -> `d1832b52`** | **2.18x / 3.14x** | **1.70x / 1.73x** | **1.75x / 2.72x** |
| `d1832b52` -> `2d5ca753` (12 purge follow-ups + later) | 1.17x / 0.84x | 1.07x / 1.10x | 1.14x / 0.79x |

**The 09-11 to 10-03 stretch is flat.** On the 80-game probe (d3 b10, seed 4004, games 0-79), units
were 1.67M at 09-11, then 1.67M (`1b409dce`), 1.93M (`b106ffd9`), 1.98M (`6b95f7f1`), 1.82M
(`ebc09d8d`), 1.82M (`211b165a`) and 1.79M (`9f2fa0db` = `06e75449`). The 24 commits between
`9f2fa0db` and `06e75449` are byte-identical.

**After the purge, no single step reaches 1.3x.** On the probe, `d1832b52` to `82ed58ca` (purge
steps 28-29, including the full Ponder order axis) is 1.17x. From `82ed58ca` to `2e85e9fa` it is
1.09x, and `2e85e9fa` is byte-identical to `d264c556`.

**This branch adds nothing.** `soldiers-analysis` (`0303292c`, whose src tree equals `2d5ca753`'s)
gives the same units and digests as `d264c556` on every arm, fixed and play-settings alike.

## Culprit: d1832b52 "refactor(search): no greedy or heuristic substitute inside the search window"

This is the squashed no-greedy purge, steps 1-27, following the USER HARD RULE of 2026-09-30. It
replaces every greedy or heuristic decision inside the search window with searched branching:

- main 2 gets the sub-decision axes, the breakpoint variants and the land drop;
- breakpoint waves now run at non-hosted breakpoints (step 3a);
- every breakpoint has an EMPTY ("done with this phase") arm;
- the Ponder order axis is always branched;
- the dig loop runs in both mains.

The per-step snapshot binaries were lost in the `/tmp` wipe, so the steps inside the squash cannot
be bisected by git. `docs/design/no-greedy-in-search-window.md` already bisects the d8 b0 cost to
step 3a (wave branching, 84% post-apply duplicates) and records Hinata at 1.70x wall at step 26.

### Mechanism

`MTG_ROLLOUT_STATS` on the 80-game d3 b10 probe, `06e75449` -> `d1832b52`:

- **The search tree roughly tripled.** Main-2 plans enumerated went from 319k to 924k (`fs_main2
  decomp`), plans scored from 329k to 927k (`cand_scored`), and rollout calls from 166k to 465k.
  Main 2 alone now enumerates 263k axis variants.
- **The budget does not cap this.** At b10 the per-decision budget is 9,000 units. Iterative
  deepening (`TurnSolver.cpp`, ~line 56000) start-gates only rung 2 and above. Rung 1 always runs,
  and a pass aborts only at the proportional overrun guard of 25x the budget (`MTG_OVERRUN_PROP`,
  `kOverrunBudgetMult = 25`).
- **So rung 1 now runs over budget.** Before the purge, the rung-1 commit cost 5.06k units per
  decision, under budget. After it, rung 1 costs 14.5k units, 1.6x over budget. The number of
  decisions committing at depth 1 went from 123 to 196 (d3 commits from 312 to 261), and
  depth-1-committed decisions alone account for 2.84M of the 3.0M added units.
- **Wall time grew faster than units.** Wall per unit rose 1.44x at d3 (25.1 to 36.1 µs per unit)
  and 1.56x on 2HG: the units do not bill all the new work (applies and post-apply dedup of
  duplicate variants, enumeration). Later perf commits brought it back to 26.1 µs per unit at tip.

In short, the budget is a per-rung start gate rather than a cap. Widening the depth-1 tree therefore
scales cost almost linearly, at fixed settings.

## Does the extra work improve play? (paired per-game, same seeds, a loss scores as 9; negative = faster wins)

| comparison | d3 b10 (n=1600) | d5 b20 (n=1200) | 2HG d5 (n=400) |
|---|---|---|---|
| `06e75449` -> `d1832b52` (the purge) | **-0.058 t/g, t=-6.9** (127 faster / 38 slower) | -0.011, t=-1.0 | -0.033, t=-1.7 |
| `d1832b52` -> tip | -0.011, t=-1.7 | **+0.038, t=+3.1 (worse)** | +0.035, t=+1.9 |
| `06e75449` -> tip (net) | **-0.069, t=-7.5** | +0.027, t=+1.9 | +0.003, t=+0.1 |
| `56021774` run -> tip | -0.068, t=-6.8 | +0.012, t=+0.8 | +0.010, t=+0.5 |
| `56021774` run -> `06e75449` | +0.001 | -0.015 | +0.008 |

- **At d3 b10 the extra work clearly pays.** Hinata wins about 0.07 turns per game faster.
- **At the d5 settings that matter for shipped play, it buys nothing measurable.** The purge's d5
  gain was not significant, and the follow-up commits from `82ed58ca` to tip gave it back and more
  (+0.038 t/g, t=+3.1). Net d5 is flat to slightly worse at about 1.8-2.1x the cost.
- **This d5 give-back deserves its own look.** Candidates are steps 28-36: the full Ponder order,
  stillborn decline, the m2 dedups, the unconditional m2 waves (step 33) and the ladder clamp.

## Not done here

No fix was attempted. Two levers look obvious but each needs a user ruling:

- budget rung 1, or lower `kOverrunBudgetMult` (a truncation change);
- the "SOUND pre-apply continuation dedup" that the purge doc already lists as the top performance
  item.

Raw data was in a scratch directory and is not kept. The probe and overnight-cell manifests
were copies of the tier manifest's Hinata jobs.
