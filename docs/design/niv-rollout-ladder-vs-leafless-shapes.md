# NivMizzetCombo: replacing the rollout ladder with a leafless shape (PARKED 2026-10-24)

**Status: PARKED by the USER on 2026-10-24 ("we'll park this for now until the reset"). Nothing is built
and nothing changed in play. This is the measurement record and the proposal to resume from.**

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

1. **The leafless probe is not cheap on Niv's early turns.** With no leaf estimate there is no
   branch-and-bound bound, so a leafless pass has ~2x the leaves of the rollout pass at the same depth
   (emulated-ladder same-depth trees, d1: rollout/leafless leaves 0.53, B&B prunes 36.5 vs 0). From turn 1 the
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

## Reproducing

The scratch scripts were `build/scratch/niv/shape/run1.sh`..`run5.sh` (build volume, not in git). Each
arm is one `mtg --batch` job of the form
`{"name":..., "deck":"<variant>/NivMizzetCombo.cod", "profile":"<variant>/NivMizzetCombo.profile.json",
"games":300, "seed":600000, "depth":5, "budget_ms":20}`, plus the arm keys in the table. Run it from the
checkout (`env -C <tree>`) on its PGO binary, with the instruments above set. The leafless R calibration is
emulnl with no `emul_r` on seeds 630000/631000 x 200 under `MTG_ROLLOUT_STATS=1`, read back with
`scripts/vlq_emul.py r <batch.err> <profile>`.
