# Worldfire APPLY-DROP line-keep fix: report (steps 1-3)

Written by the parent session from the agent's final message (its harness blocks report files).
Branch: wip/worldfire-ad (4238d6010, a8735ea32, 55c6c1538, 93c8677e1).
Handoff for the remaining steps: logs/worldfire-ad/handoff2.md.
Measurements are also in docs/design/worldfire-apply-drop-2026-11-25.md.

## Build and units

- The failed rebuild was a stale precompiled header, not a duplicate include. Every file includes
  `ai/HeuristicArm.h` by one path. The compiler saw origin's slot count (1077) where the edited header
  has 1080. Touching the sources and rebuilding cleared it.
- Full unit suite: 4,197/4,197 pass. All predicted values in `test_worldfire_line_keep.cpp` held when
  checked by hand.
- One existing test changed. The Niv hole-1 test (`test_niv_payment_holes.cpp`) asserted that, with the
  prepay lever off, Curiosity's {U} is stranded. MULTI now catches that strand by itself, and the
  board was checked by hand to have a legal payment. That block now pins MULTI off, and a new block
  checks MULTI's catch.
- The Windswept Heath / Boars failures were a harness problem, not the code. A binary copied under
  `build/scratch/` reads `cards.json` from the primary checkout (older) unless `src`/`decks` symlinks
  sit beside it. With the symlinks added, the results did not change.

## Repros (APPLY-DROP count, base -> new)

| repro | base | new |
|---|---|---|
| gi447 T3 | 12 | 2 |
| gi65 T4 | 2 | 0 |
| gi125 T4 | 8 | 0 |

The 2 left on gi447 are a Personal Tutor that is not in the plan at all, which this fix cannot reach.
Base has them too.

## CPU: `MTG_LINE_KEEP_RITUAL_TIGHT` (default ON)

RITUAL as first written took the trial payments from 76k to 300k on a 6-deck probe, most of them
wasted. The tight form adds two exact cuts:

- **Joint check:** the board alone must pay the ritual's cost plus the rest of the line, minus the
  ritual's mana. Every valid repair satisfies this.
- **Pre-check:** the cheap check before the trial counts the ritual's mana as supply for the rest of
  the line.

The 112 probe games are identical with and without the cuts.

| arm | instructions | vs off |
|---|---|---|
| off | 562.89 G | - |
| on, without the cuts | 575.91 G | +2.31% |
| on, shipped | 565.89 G | +0.53% |

## Gate (Worldfire, same 23,000 seeds, MTG_MANA_ORACLE=2)

| arm | games | APPLY-DROP |
|---|---|---|
| off | 23,000 | 6,924 |
| on | 23,000 | 2,019 |
| off | first 6,000 | 1,767 |
| on | first 6,000 | 548 |
| on, without MULTI | first 6,000 | 1,376 |
| on, without SUSPEND | first 6,000 | 822 |
| on, without RITUAL | first 6,000 | 939 |
| on, without the TIGHT cuts | first 6,000 | 548 |

- APPLY-DROP-ORDER went 453 -> 437. ORACLE-GAP stayed at 1.
- Of the 2,019 left, 1,085 drop a cast that is not in the plan, which this fix cannot see.
- Most of the rest are single-Ponder lines where the engine and oracle boards differ on a Flooded
  Strand crack (e.g. s30000196 T4), plus multi-ritual lines.

## Play A/B (queued, not yet run)

- Pooled manifest: 94,800 games, seed base 4870000.
- Every deck plays standard at 400 games/arm and 2HG at 200; Worldfire plays 3,000/arm in each format.
- Arms: all-off vs all-on, plus a without-TIGHT-cuts arm on the ritual decks (Cyclone, Dragonstorm,
  Hinata, Shadowborn, Worldfire).
- It runs under the box lock (chain pid in `build/scratch/worldfire-ad/chain.pid`) and finishes when
  `build/scratch/worldfire-ad/ab_out/DONE` appears. Analyse with `logs/worldfire-ad/ab_report.py`.

## Remaining

Analyse the A/B per deck (net, t, units/CPU), add games on decks that move, check every slower game at
`--depth 8 --budget-ms 0` (with `MTG_NC_SEARCH=1` for clairvoyance where needed), root-cause any
cluster, then commit and push.
