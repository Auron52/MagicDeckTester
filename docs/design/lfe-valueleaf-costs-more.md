# LandfallElementals value leaf: every candidate costs MORE than heuristic rollouts

**Status (2026-10-27): ROOT-CAUSED, no defect.** The leaf and leafless shapes cost more at b20 because
they SPEND the budget and the rollout ladder does not. Measured at equal cost (budget curves, below), the
model leaf dominates the rollout ladder's own curve, and the no-leaf escalation is level with it. The USER
chose **staged at b20** on 2026-10-27, see "Decision". The USER's position stands:
*"Heuristic rollout should never be what we settle on."* The original record follows.

## What the run measured

`bash scripts/valueleaf.sh run decks/LandfallElementals`, frozen at 57a46e33c (src 841e1e41), finished
2026-10-24 16:39 UTC, rc=0. Queue: `logs/vlq_landfallelementals/` (DECISIONS.txt, leafcal.json).
Phase E A/B, 8 seeds x 1000 games per arm against live (no sidecar, heuristic rollouts):

| arm | delta avg turn | t | cost (units vs live) | core-s vs live |
|---|---|---|---|---|
| staged | -0.0423 | -15.4 | 1.555x | 1.82x |
| nlesc | -0.0239 | -12.6 | 1.327x | 1.45x |
| nlfit | -0.0134 | -4.6 | 1.283x | 1.35x |
| emul | -0.0093 | -4.0 | 1.373x | 1.28x |

All four are better on every seed and all four cost more, so E.5 adopted nothing ("a TRADE, the
user's call"). E.7 and E.6 were skipped because no leaf is live.

## Why it looks like a defect rather than a trade

* A value leaf replaces the horizon rollout with an O(1) evaluator. On every other deck the leaf
  shapes are cheaper than, or close to, live. A leaf that costs 1.3-1.6x MORE in deterministic units
  means the search is doing more work around it, not less.
* Phase C0 leaf calibration logged `H5 paired 50 moved 0 ... units 1.000x NOT ENGAGED (identical units
  -- stale engine?)`: at H5 the greedy and 1-ply leaves produced byte-identical work, so the setting
  under test never took effect at that depth.
* Even the leafless shapes (nlesc, nlfit) cost more than live, so the extra cost may sit in a code
  path shared by every non-live arm (sidecar presence switching on the H-cell ladder, or escalation)
  rather than in the evaluator itself.

## First things to check

1. Where the extra units go: a census or unit-site split of staged vs live on a few paired games.
   (Note: `scripts/turn_census.py` currently refuses its own output -- the `u_*` buckets do not sum
   to `units` on 3162/3240 rows, so a Consume site is missing a bucket; fix that first.)
2. Why H5 is NOT ENGAGED in calibration on this engine.
3. Whether the sidecar's presence arms something Landfall-specific (land-drop / fetch breakpoints,
   Reef lines) that live never pays for.

Phase A context: many Landfall row games ran 1-4 h each (one hit the 14,400 s cap), which is a
separate cost problem worth checking with the same census.

## Resolution (2026-10-27): the arms are compared at unequal COST, not unequal efficiency

### Live under-spends its budget

At d5 b20, 1000 games (seed 600000), `MTG_ROLLOUT_STATS` / `MTG_ESC_JOB_STATS`:

| arm | units | units after the budget ran out | mean committed depth |
|---|---|---|---|
| live (rollout ladder) | 16.26M | small | 2.21 |
| nlesc | 20.5M | 0.58M | 2.82 |
| nlfit | 20.1M | -- | -- |
| emul | 22.0M | 5.44M | -- |
| staged | 24.34M | 7.27M (30%) | 3.02 |

The rollout ladder stops well inside its budget (~40% of it): its next pass's estimated cost does not
fit, so it commits shallow. The other shapes fill the budget and admit passes that overrun it (the
value-leaf start gate is relaxed to 1.10 x `MTG_VALUE_STARTGATE_ALPHA` 8.0 = 8.8; overrun ceiling 25x
budget). They buy depth, and the depth is what costs. Nothing in the leaf path wastes work.

* A tighter staged alpha only moves the cost into escalation (alpha 1: 4.381 at 20.15M, escalation 8.4M;
  alpha 2: 4.362 at 21.5M; alpha 4: 4.347 at 23.07M).
* A lower depth cap at b20 does not reduce the leaf arms' cost: the budget is binding, not the depth.
* The B&B win-turn cut (`state.turn_number > cutoff`, cutoff = incumbent - 1) is NOT a cost difference.
  A rollout incumbent can only cut horizon-leaf evaluations, and those are free in a leafless pass.

### Budget curves (delta avg win turn vs live b20; units and pooled core-ms vs live b20)

Seeds 600000.. x 1000 games, paired per seed; 8 seeds where marked, otherwise 4. Report:
`build/scratch/curve_report.py <out> liveb20` (scratch, build volume).

| arm | seeds | delta | t | units | core-ms |
|---|---|---|---|---|---|
| live b30 | 4 | -0.0185 | -8.3 | 1.410x | 1.393x |
| live b40 | 4 | -0.0312 | -9.3 | 1.770x | 1.618x |
| nlesc b10 | 8 | +0.0021 | 1.4 | 0.959x | 1.060x |
| **nlesc b11** | 8 | -0.0012 | -1.1 | 1.006x | 1.123x |
| nlesc b12 | 8 | -0.0046 | -2.7 | 1.044x | 1.162x |
| nlesc b14 | 4 | -0.0125 | -3.8 | 1.116x | 1.349x |
| nlesc b16 | 4 | -0.0160 | -4.7 | 1.190x | 1.258x |
| nlfit b10 / b12 / b14 / b16 | 4 | +0.0110 / +0.0038 / -0.0007 / -0.0062 | | 1.015-1.190x | |
| emul b10 / b12 / b14 / b16 | 4 | +0.0245 / +0.0188 / +0.0058 / -0.0017 | | 0.736-1.141x | |
| staged b8 | 8 | -0.0081 | -3.2 | 1.116x | 1.333x |
| staged b10 | 8 | -0.0156 | -7.0 | 1.208x | 1.426x |
| staged b12 | 4 | -0.0245 | -7.4 | 1.306x | 1.479x |
| staged b14 | 4 | -0.0322 | -6.4 | 1.373x | 1.709x |
| **staged b16** | 8 | -0.0344 | -11.5 | 1.434x | 1.723x |

The pooled core-ms column comes from runs that shared the box, so it is only indicative. The clean wall
check below replaces it.

### Clean wall check (each arm alone on the box, held-out seed 604000, 1000 games, same binary)

| arm | avg win turn | delta | units | core-ms |
|---|---|---|---|---|
| live b20 (run twice) | 4.387 | -- | 16.07M | 286.3k / 299.1k (mean 292.7k) |
| nlesc b11 | 4.388 | +0.001 | 1.009x | 0.984x |
| staged b16 | 4.357 | -0.030 | 1.396x | 1.535x |
| staged b20 (2026-10-27, live re-run alongside: 299.9k) | 4.353 | -0.034 | 1.528x | 1.754x (vs the 3-run live mean 295.1k) |

Held out, b16 -> b20 buys only -0.004 more for +0.13x units and +0.23x wall. In units staged b20 still
beats live's curve (live interpolated to 1.53x units is about -0.023). In wall it is about level with
live's curve (live b40 is -0.031 at 1.62x), so b16 is the better staged point.

### Where staged's extra cost at b20 goes (seed 600000, 1000 games)

| | live | staged |
|---|---|---|
| total units | 16.26M | 24.34M |
| units after the budget ran out | 1.19M (7%) | 7.27M (30%) |
| units within the budget | 15.07M | 17.07M (1.13x) |

About 75% of the gap is overrun. The relaxed start gate lets the value ladder start a deeper pass whose
estimated cost is up to ~8.8x what remains; the rollout ladder does not start a pass that does not fit.
Staged also runs a heuristic-rollout escalation over the leaf line (3.9M units, 16%; the rollout line was
taken in 485 decisions), so it is mostly, not wholly, off heuristic rollouts. Per unit it is ~1.1x wall
(model evaluation). The overrun is also where the gain comes from (alpha sweep at b20, one seed, about
+/-0.008): alpha 1 +0.004 @ 1.24x units, alpha 2 -0.015 @ 1.32x, alpha 4 -0.030 @ 1.42x, alpha 8
(current) -0.043 @ 1.50x. Tightening the gate leaves the escalation overhead in place, which is why even
staged b8 costs 1.12x. **Option kept for the USER: staged b20 with alpha 4** lands near staged b16 without
lowering the nominal budget. It needs a per-deck alpha (today alpha is global; only a batch job can override
it) and an 8-seed curve plus a held-out wall check first.

### Reading

* **The model leaf (staged) dominates the rollout ladder's curve.** Staged b16 gives -0.034 for 1.43x
  units. Live needs b40 (1.77x units) for -0.031. In wall terms the staged premium is larger (1.53x).
  Live's curve interpolated to 1.53x core-ms is about -0.026, so staged still wins, by a smaller margin.
* **The no-leaf escalation is level with the ladder.** nlesc b11 is level on quality and level on units
  and wall, held out. At a higher budget, nlesc tracks live's curve; it does not beat it.
* emul and nlfit sit on or behind live's curve. nlfit pays a per-game R calibration (see
  `niv-rollout-ladder-vs-leafless-shapes.md`, mechanism 4).

### Decision (USER 2026-10-27): staged at b20

The USER chose **staged at b20** (-0.034 turns at 1.53x units / 1.75x wall, held out), *"as long as it
continues to be significantly cheaper than Hinata"*. That is a standing CONDITION: re-check the deck's
play-row cost per game against Hinata's in every tier accept. b20 also keeps the never-lower rule. What settled the cost question: at play settings (d5 b20) LandfallElementals costs
310 core-ms per game in the 2026-10-27 regression tier, 0.15x Hinata (2,118). Staged b20 projects to
about 545, 0.26x, mid-pack next to Melira and NivMizzetCombo. (`test/suite_cost.json`, 10-23, still
showed 2,179 ms/game. That figure predates the keep table, when every mulligan bottom was a full trial
game.) The sidecar shipped is `logs/eval/LandfallElementals.value.STAGED.json`, byte-identical. It has
no `value_play` block, so it plays the built-in d5/20 at the global start-gate alpha 8. Owed: profile
the ~1.15x per-unit model cost and the 16% rollout escalation, to remove waste rather than to make it
affordable.

The options as they stood before the decision:

Both good points are below b20. That conflicts with the 2026-10-22 rule that a deck's play budget is
never lowered (`.claude/skills/value-leaf.md`). The precedent is KittyEquipment's emulated shape, adopted
at b14. Options:

1. **staged at b16 (agent recommendation):** -0.03 turns at 1.4x units / 1.5x wall. It beats anything
   live gets from more rollout budget.
2. **nlesc at b11:** off heuristic rollouts at no cost and no change in quality.
3. Keep live b20: rejected by the USER's position above.

### Pipeline gap this exposed

`valueleaf.sh` phase E.7 (the budget-curve step, `scripts/play_setting_stage.py`) skips a deck that has
no live sidecar. So on exactly the decks where every shape spends more than the rollout ladder, the
equal-cost comparison that would settle the question is never run, and E.5 reports a "trade" instead.
It has a second gap as well: its grid never goes below b0, because of the never-lower rule, so both good
LandfallElementals points (b11, b16) would fall outside the grid even if it ran. Its rule also reports a
dearer-but-better arm as a TRADE without comparing it with live's own curve (live at 1.5b / 2b).

**Deferred fix (depends on the USER's budget answer):** on a no-sidecar deck, E.7 should run live's own
curve (b0, 1.5b0, 2b0) beside the shape grid. It should then report each shape's position against that curve
(dominates / on / behind) rather than "TRADE". Whether the grid may go below b0 for a shape, as opposed to a
budget change on the same shape, is the open USER question above. Owed alongside: NivMizzetCombo
(`niv-rollout-ladder-vs-leafless-shapes.md`, "Budget curves") is the second such deck.
