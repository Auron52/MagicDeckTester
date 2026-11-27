# Beam staged: option (ii) numbers + cheaper staging (agent report, 2026-11-25)

The beam does not help broadly, so a fleet-wide rule is out. My recommendation is option (2): an admission step in the value-leaf pipeline, with a new cheaper staged beam (SG50) as the beam's only form. Under the pipeline's existing adoption rule, Creature Giving would be refused the beam and run without it at x1.40 units. Whether to accept that cost is the one question for you.

The harness blocked me from writing `logs/beam-staged/report.md` (subagents may not write report files), so its content is below for you to commit. The raw tables and verdicts are committed as data. Branch `wip/beam-staged` is pushed at **96261e889** (code in 258d4fccc). The push touched `src/`, so a Windows CI run was triggered; I did not watch it.

## Are not all decks using the beam?
No. It only runs inside the escalation, and only on decks whose value_play block is on:
- **Depth 5:** only on decks that set `beam_width`: Anti-Lifegain, Creature Giving and Dragonstorm. Hinata has `beam_width: 0`. The other nine d5 decks have no width.
- **Depth 3:** a fixed W20 beam on every such deck. No deck plays at d3, so it only fires in the suite's pinned d3 rows.
- **d4, d6, d7:** never.

## Step 1: cheaper staged variants
All are default OFF, complete at `--budget-ms 0`, and use one depth-independent rule with no per-deck setting. The top W plans always expand; plans beyond W only while the escalation budget has their share left.
- **SF50 / SF25:** extra plans while Used < 0.5 (or 0.25) x Limit.
- **SG50:** ordered re-price. The k-th extra plan is admitted while Used < 0.5^k x Limit.
- **`MTG_ESC_BEAM_D5_ALL`:** new arm that puts the W3 ld2 beam on every d5 deck.

**Byte-identical when off:** CG A/S/SK/N, CG 2HG S, AL A and DS SK all match wip/beam-cpu's digests and units. D on CG equals A, and frac 1.0 equals S. Unit tests pass 4163/4163.

## Step 2: one pooled A/B
One batch: 1,452 jobs, 303,000 games, 1 h 48 min at 32/32 busy, fresh seeds 46.1M and 47.1M. "net" is slower minus faster games versus today (negative is better). CPU is units versus today. K = `MTG_LEAF_KILL_ONLY_NOM2`.

**Creature Giving / Anti-Lifegain / Dragonstorm** (play n=5000 each, 2HG n=2000 each):

| arm | CG play | CG 2HG | AL play / 2HG | DS play / 2HG | 3-deck play net | 3-deck 2HG net | CPU play / 2HG |
|---|---|---|---|---|---|---|---|
| AK | -2 | 0 | 0 / 0 | 0 / 0 | -2 | 0 | x0.99 / x0.96 |
| NK (no beam) | -61 (63/2), x1.40 | -37, x1.50 | -10 / -10, x1.19 | -5 / -5 | **-76** | **-52** | x1.28 / x1.33 |
| SK | -35, x1.28 | -24 | -7 / -4 | -4 / -5 | -46 | -33 | x1.18 / x1.15 |
| SF50K | -4 (17/13) | -5 | -5 / 0 | -3 / -5 | -12 | -10 | x1.08 / x1.04 |
| SF25K | **+8** (6/14) | -4 | -4 / 0 | -2 / -2 | +2 | -6 | x1.01 / x0.99 |
| **SG50K** | **-27 (34/7), x1.00** | -10 (15/5) | -6 (6/0) / -3 | -1 / -3 | **-34** | **-16** | **x1.00 / x0.98** |

- **SG50K is strictly better than today at today's cost** (CG t=-4.2, AL t=-2.3). It keeps about 45% of the no-beam gain on CG for none of the cost.
- **No beam is still better than SG50K on CG:** +0.0074 turns at t=+5.2, and +0.0150 at t=+4.9 in 2HG. On AL and DS the gap is about 0.0008.
- **The flat share is the wrong shape.** It spends its share on the first nodes it reaches and starves the rest.
- **The d3 W20 beam is neutral:** every staged arm plays identically to today at d3 b10. NK at d3 equals AK at d3 (-7, x0.85), and that gain comes from K, not from removing the beam.

**The fleet** (the other 10 d5 decks, n=1000 + 500 2HG), W3 beam on everywhere (D):

| deck | play net | 2HG net | units |
|---|---|---|---|
| Alchemist, Auras, Burn, Knights, Slivers, Treasure Hunt | 0 | 0 (Burn -1) | x0.95–1.00 (identical games) |
| Critter Lifegain | 0 | 0 | x1.12 / x1.10 |
| Hinata | +11 (1/12), t=+3.0 | +6 | **x1.89** |
| Melira Pod | +7, t=+2.7 | 0 | x1.12 (cpu x1.43) |
| Vampires | +2 | +7, t=+2.7 | **x2.81 / x2.26** |
| **Fleet sum** | **+20** | **+12** | **cpu x1.67 / x1.59** |

Staging changes nothing here (DSG50 plays the same games as D). My unverified guess: the value beam also turns on probe value recording, which disables the strict FS cut and the DVS tail-skip in the probe. That makes every decision dearer on decks that rely on those cuts.

## Step 3: slower games
All 32 slower games (SK 11, SG50K 14, NK 7) **recover at d8 b0**. All 32 also give the same turn on both arms under `MTG_NC_SEARCH=1`, so they are clairvoyant as well. None is real.

## Options (your three)

| option | play net (CG / AL / DS / fleet) | 2HG net | CPU (units) |
|---|---|---|---|
| today | 0 | 0 | 1.00 |
| (1) fleet-wide rule (SG beam on every d5 deck) | -27 / -6 / -1 / +20 = -14 | -10 / -3 / -1 / +12 = -2 | CG/AL/DS x1.00; fleet x1.67 |
| (2) admission, existing adoption rule | -61 / -6 / -1 / 0 = **-68** | -37 / -3 / -3 / 0 = **-43** | CG x1.40, AL x1.02, DS x0.99 |
| (3) delete the beam, CG/AL/DS on NK | -61 / -10 / -5 / 0 = **-76** | -52 | CG x1.40, AL x1.19, DS x1.19 |

For (2), the existing rule (`play_setting_stage.py` / `vlq_adopt_pick.py`) takes the cheapest arm not notably worse than no beam (notably = more than 0.002 turns at t > 2). AL and DS admit the beam; CG and the whole fleet refuse it. The stricter wording "net better and not dearer" admits nothing anywhere, which is option (3).

## Admission design (not implemented)
- **Where:** one extra arm in `scripts/play_setting_stage.py`, in the same pooled batch: the picked play setting with and without `beam_width 3 / ld 2`, with SG50 as the only beam form. The baseline is no beam, using the same 8 seeds x 1000 games and the same picking rule.
- **What it writes:** `--write` writes `beam_width` when the beam wins and removes it otherwise, so old and new decks go through the same step.
- **Engine changes:**
  - Replace the `depth == 5` switch with "the deck's width at its play depth".
  - Delete the d3 W20 regime.
  - Delete the fixed-width cut.
- **Cost:** about 8,000 games x the deck's ms/game with the beam on:
  - CG about 4.4K core-s (about 2.5 min on 32 threads).
  - AL and DS about 1.2K each.
  - Hinata about 70K (about 36 min).
  - All 13 d5 decks about 130K core-s (about 70 min).
  - d6/d7 decks are not measured yet.

## Recommendation
Option (2), with SG50 as the beam's only form and the d3 W20 regime deleted. This is "helps some, hurts others": the beam saves 16–29% of units on CG/AL/DS, is inert on six fleet decks, and hurts four. Deleting it (3) gives the best play but costs CG x1.40.

## Question for you
Creature Giving's beam saves 29% of units but plays measurably worse than no beam, even staged (+0.0074 turns, t=5.2; 34 games of 5000). The quality-first adoption rule would remove it. Should CG pay x1.40 units for -61 net at play and -37 in 2HG? My default is to let the rule decide, so CG runs without the beam. K (the kill-only identity) is in most arms here and is still your separate call.

Files are in `/workspaces/MagicDeckTester/.claude/worktrees/beam-staged/`:
- `src/ai/TurnSolver.cpp`, `src/ai/HeuristicArm.h`, `src/ai/ValueArm.h`, `src/runner/BatchRunner.cpp`
- `logs/beam-staged/mk_ab.py`, `summarize.py`, `pair.py`, `slower.py`
- `logs/beam-staged/ab_tables.txt`, `slower_verdicts.txt`
- raw run output in `/workspaces/MagicDeckTester/build/scratch/beam-staged/ab` and `sl2`

Nothing of mine is still running. My context is under the 250K handoff point.
