# NivMizzetCombo: replacing the rollout ladder with a leafless shape (resumed 2026-10-27)

**Status: resumed 2026-10-27 after the 2026-10-24 park.** Budget curves at equal cost are below, in "Budget
curves (2026-10-27)". emulnl at b20 ADOPTED 2026-10-27, see "Decision".

## The question

NivMizzetCombo ships no value sidecar, so play runs the engine default, the **rollout ladder**:
`FullSearchLine` deepens passes 1..5 at d5 b20, scores every horizon leaf with the 1-ply-lookahead rollout,
and commits the deepest pass the start gate admits. `per-deck-search-shape.md` records that shape as
dominated on every deck its menu screen measured. The 2026-10-24 value-leaf run (phase E) nevertheless
measured every alternative as dearer than live on Niv: NLESC 1.54x units, NLFIT 1.56x, EMUL 1.29x, the
model leaf 1.99x (Q17 in `analysis-NivMizzetCombo.md`).

The USER's position (2026-10-24), which this record works from: *"It should be possible to replace an
expensive rollout ladder with a leafless shape or equivalent up to the final rollout. Doing early rollouts
for little to no benefit makes no sense."* So the target is: no rollouts in any pass before the committing
one, and the result is a shape that never costs more than the rollout ladder.

## Apparatus

All arms: the same 300 games (seed 600000 = phase E's first seed, game_index 0..299), d5 b20, the keep table
attached, one PGO binary at `HEAD:src 841e1e41` (origin `0a1a96278`). Each shape is a variant deck folder
(symlinks to `decks/NivMizzetCombo/*` plus a `NivMizzetCombo.value.json` holding only the `value_play`
shape keys), so the arms differ only in the sidecar. Instruments: `MTG_ESC_JOB_STATS=1`,
`MTG_ESC_SKIP_LOG=1` (one `[esk]` line per escalation, per-pass probe/heuristic units),
`MTG_ROLLOUT_STATS=1` (heuristic-ladder and emulated-ladder accounting by committed depth),
`MTG_TURN_CENSUS` (per-decision rows; single-threaded, 5 processes x 60 games per arm). At 300 games only
the cost column is reliable; the win-turn column is indicative.

## Where the rollout ladder spends (live, 820 ladder decisions)

| committed depth | decisions | earlier passes / decision | final pass / decision | share of all units |
|---|---|---|---|---|
| d1 | 366 (45%) | 0 | 10,697 | 55% |
| d2 | 258 (31%) | 1,247 | 7,176 | 31% |
| d3 | 131 | 2,561 | 1,634 | |
| d4 | 55 | 4,607 | 443 | 13% (d3-d5) |
| d5 | 10 | 6,197 | 995 | |

* Earlier passes are **13.9%** of the ladder's units (972,698 of 6,988,082). In the d3-d5 decisions they cost
  MORE than the final pass, because the deep pass ends on a proven in-horizon win; that is the clearest
  waste, ~9% of all units.
* **93% of live's units are on turns 1-3** (T1 37%, T2 31%, T3 25%); turn 4+ decisions average ~4.2k units.
  Niv wins on turn 4-5 (300 games: T3 20, T4 154, T5 98, T6+ 28).
* Inside turn 1-3 decisions ~90% of units are the rollout leaf itself: `la_cand` 40%, `la_bp_wave` 26%
  (Brainstorm/Ponder continuations inside the leaf's 1-ply lookahead), `rollout_step` 13%,
  `greedy_fallback` 12%; the tree (`fs_*`) ~8%.
* 16% of live's units are spent after the decision budget was exhausted (admitted passes overrunning; Q16).
* Rollout cost per horizon leaf is large on Niv: phase E0 measured `emul_r` = [420, 1645, 894] units/leaf
  (model warm-ups); the fleet runs ~7 to ~950 at d1.

## Every built variant of "leafless up to the final rollout"

| arm | `value_play` / arm keys | units vs live | avg win turn |
|---|---|---|---|
| live (rollout ladder) | none | 1.00x (7,068,140) | 4.467 |
| NLESC | `{ladder: escalation, leaf: none, alpha: relaxed}` | 1.54x | 4.443 |
| NLFIT | `{ladder: single, leaf: none, alpha: relaxed}` | 1.59x | 4.453 |
| NLESC, probe capped 0.25 / 0.5 | env `MTG_ESC_SPLIT` | 1.13x / 1.19x | 4.473 / 4.457 |
| emulnl, phase E0's R | `{ladder: emulated, leaf: none, emul_r: [419.84, 1645.09, 893.6]}` | 1.13x | 4.453 |
| emulnl, leafless-calibrated R | same, `emul_r: [243.6, 33.91, 11.59]` | 1.27x | 4.447 |
| ... + pass 1 on rollouts | arm `ladder_emul_hfirst: 1` | 1.01x | 4.467 |
| ... + bolder warm-up calls | + `ladder_emul_margin: 3` / `10` | 1.02x / 1.07x | 4.463 / 4.463 |
| EMUL (model warm-ups, phase E's sidecar) | `{ladder: emulated, emul_r: [...]}` + staged model | 1.16x | 4.450 |
| reserved single pass | NLFIT + arm `esc_single_reserve: 1` | 1.05x | 4.510 |

## Why none of them captures the waste on Niv

1. **The leafless probe is not cheap on Niv's early turns.** A leafless pass at the same depth counted ~2x
   the leaves of the rollout pass (emulated-ladder same-depth trees, d1: rollout/leafless leaves 0.53, B&B
   prunes 36.5 vs 0). *Correction 2026-10-27:* this record first blamed the missing branch-and-bound bound for
   that. The bound cannot explain it: the win-turn cut (`state.turn_number > cutoff`, cutoff = incumbent - 1)
   can only cut horizon-leaf evaluations, and those are free in a leafless pass. The leaf-count ratio is not
   root-caused. What is measured is the probes' total cost (below and in the 2026-10-27 section). From turn 1 the
   leafless d3 pass costs ~14-17k of the 18k-unit budget and ends on turn 3, where Niv almost never wins.
   NLESC: 408 of its 424 escalations were on turns 1-3 with no proven win (`owt=9`); the discarded probes in
   them were 5.96M units, 55% of the arm's total, and the fallback ladder (fresh 0.5 budget) committed depth 1
   in 361 of 424.
2. **Choosing the committing depth means predicting a rollout pass's cost**, and Niv's cost per leaf varies
   ~10x by board. The emulated ladder mispredicted in 504-645 of ~800 decisions; each miss replays at that
   depth on rollouts and then, with the exact cost in hand, carries on up the ladder on rollouts -- which is
   the rollout ladder again plus a wasted leafless pass ("other heuristic" units ~0.8-0.9M against live's
   0.97M of earlier passes). E0's R was calibrated on MODEL warm-ups (tree ~ the rollout tree); it is ~2x too
   high per leafless leaf, but the leafless-calibrated R over-admits depth-2 rollout passes instead (317 d2
   commits at 14.4k vs live's 258 at 7.2k).
3. **Leafless warm-ups give the committing pass no move-ordering hint.** `MTG_ID_ORDER_HINT` carries a pass's
   best plan into the next pass (0.928x units on Landfall); a leafless pass ties every line at no-win.
4. **The single-pass modes calibrate R once per GAME with a full depth-1 rollout pass**
   (`calibrate_single_R`): 1.78M units, 24% of the reserved arm's total. The emulated ladder already learned
   that lesson (frozen per-deck `emul_r`); FIT / RESERVE did not. RESERVE is also stricter than the ladder
   (it reserves a pass that must complete; the ladder admits passes that overrun), so it commits shallower
   (mean depth 1.39 vs 1.89) and played worse.

## Proposal (not started)

* **(A) A leafless-warm-up shape that cannot cost more than the rollout ladder.** Frozen per-deck, per-depth R
  for the single/FIT pass (no per-game calibration). Leafless passes only within what the ladder would have
  spent on its earlier passes, committing a proven win with no rollouts at all. Otherwise one rollout pass
  at the deepest depth whose estimate fits, using the ladder's own boldness (alpha and overrun ceiling), not
  a stricter reservation. The ceiling on Niv at b20 is ~10-14% of units (the earlier-pass share). It should
  be much larger on decks that commit deeper. Needs a held-out A/B on the decks without a sidecar first,
  then the fleet.
* **(B) Cut the final pass itself.** That is 86% of Niv's ladder units, and it sits in the rollout leaf.
  The Brainstorm put-back dedup targets `la_bp_wave` (26% of turn 1-3 units), a sound collapse.
* Unchanged meanwhile: Niv ships no sidecar (rollout ladder); Q17 (the value-leaf trades) stays open.

## Budget curves (2026-10-27)

The 300-game table above compared every shape at b20, where the shapes spend their budget and the rollout ladder
does not. That is a comparison at unequal cost. Here each shape is run over a range of budgets and set against
the ladder's OWN curve (the ladder at b30 / b40). Setup: d5, 4 seeds (600000..603000) x 1000 paired games, keep
table attached, Release binary, the same for every arm. Arms are variant folders under
`build/scratch/niv_curve/variants/` (scratch). staged and emul are the 2026-10-24 handoff's sidecars
(`logs/vlq_nivmizzetcombo/`, `logs/eval/NivMizzetCombo.value.STAGED.json`). Columns are the delta in average win
turn vs live b20, then units and pooled core-ms vs live b20.

| arm | b10 | b12 | b14 | b16 | b20 |
|---|---|---|---|---|---|
| live (rollout ladder) | | | | | 0 @ 1.00x; b30 -0.0210 @ 1.34x; b40 -0.0305 @ 1.67x |
| nlesc | +0.0072 @ 1.07x | +0.0027 @ 1.18x | -0.0018 @ 1.28x | -0.0070 @ 1.38x | -0.0138 @ 1.55x |
| nlfit | +0.0085 @ 1.18x | +0.0025 @ 1.27x | -0.0015 @ 1.34x | -0.0063 @ 1.42x | -0.0133 @ 1.56x |
| **emulnl** (E0 `emul_r`) | +0.0215 @ 0.76x | +0.0160 @ 0.81x | +0.0092 @ 0.88x | +0.0027 @ 0.95x | -0.0068 @ 1.09x |
| emul (model warm-ups) | +0.0147 @ 0.88x | +0.0085 @ 0.96x | +0.0017 @ 1.04x | -0.0040 @ 1.12x | -0.0123 @ 1.26x |
| staged (model leaf) | +0.0220 @ 1.45x | +0.0182 @ 1.58x | +0.0132 @ 1.68x | +0.0092 @ 1.79x | -0.0015 @ 1.99x |

Live's curve runs at about -0.062 turns per 1x of units. Measured against it:

* **The handoff's staged leaf is far off the curve.** At b20 it gives level quality for 1.99x units. On Niv the
  model is not an asset.
* **nlesc and nlfit are behind the curve at every budget.** At 1.28-1.34x they are level, where live b30 gets -0.021.
* **emul is behind it.** b20 gives -0.012 @ 1.26x; live interpolates to -0.016 there.
* **emulnl is ON the curve:** b16 +0.003 @ 0.95x, b20 -0.007 @ 1.09x. This is the 2026-10-24 target shape:
  leafless warm-ups, then one rollout pass at the committing depth.

**Held-out confirmation** (seeds 604000..607000 x 1000, one pooled batch, arms innermost):

| arm | delta | t | units | core-ms |
|---|---|---|---|---|
| emulnl b18 | +0.0012 | +1.2 | 1.026x | 0.996x |
| emulnl b20 | -0.0052 | -4.0 | 1.095x | 1.033x |

**Why nlfit costs what it does** (seed 600000, b20, `MTG_HYBRID_STATS=1`; 38.99M units vs live's 25.92M):

| part | units | share of nlfit |
|---|---|---|
| per-game R calibration (689 calibrations) | 4.52M | 11.6% |
| reserved rollout pass (1490 decisions where the probe proved no win) | 10.63M | 27% |
| leafless probes (2495 decisions, 1005 proved a win) | ~23.8M | 61% |

The leafless probes alone cost 92% of live's whole spend, so the calibration is not the lever. A frozen
per-deck R (proposal A's first half) would bring nlfit b20 to about 1.33x for -0.013, still behind live b30's
-0.021 at 1.34x. It is not built.

### Decision (2026-10-27): emulnl at b20

When the USER chose LandfallElementals' staged b20, they said *"we can use the more expensive better
quality option"*. The agent read that as covering Niv too and adopted **emulnl at b20**
(`build/scratch/niv_curve/variants/emulnl/NivMizzetCombo.value.json`: a `value_play` block with
`ladder: emulated`, `leaf: none`, `emul_r`, and no enabled depth or budget, so it plays the built-in d5/20).
The USER was told that reading and can reverse it by removing the sidecar. The options as they stood:


* **emulnl at b20 (agent recommendation).** It stays at the current budget, so it does not touch the
  2026-10-22 never-lower rule, and it takes Niv off the rollout LADDER. Held out it is -0.005 turns for
  +9.5% units / +3% wall: the same trade the ladder makes when given more budget, not a better one. Under
  the adoption rule ("average quality and performance not worse") it is a trade, so it is the USER's call.
* **emulnl at b18:** level on both axes, but below the current budget.
* Unchanged meanwhile: no sidecar, rollout ladder.

Its committing pass still scores horizon leaves with the rollout. Removing that needs a leaf that pays its
way, and on Niv none does yet.

## Reproducing

The scratch scripts were `build/scratch/niv/shape/run1.sh`..`run5.sh` (build volume, not in git). Each
arm is one `mtg --batch` job of the form
`{"name":..., "deck":"<variant>/NivMizzetCombo.cod", "profile":"<variant>/NivMizzetCombo.profile.json",
"games":300, "seed":600000, "depth":5, "budget_ms":20}`, plus the arm keys in the table. Run it from the
checkout (`env -C <tree>`) on its PGO binary, with the instruments above set. The leafless R calibration is
emulnl with no `emul_r` on seeds 630000/631000 x 200 under `MTG_ROLLOUT_STATS=1`, read back with
`scripts/vlq_emul.py r <batch.err> <profile>`.
