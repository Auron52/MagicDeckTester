# One rollout ladder that skips unnecessary leaves: measured, built, and why it cannot pay much (P16, 2026-11-13)

Status: FINDINGS + a default-off prototype on the local branch `p16-ladder` (not pushed to the shared branch). No
shipped deck changes. Every adoption below is a USER decision.

Follows `docs/design/hydras-shapes-vs-heuristic-ladder.md` (why nlfit lost to the ladder; the nlfitL climb) and
`docs/design/fit-climb-fleet-screen.md` (nlfitL on the fleet: TRADE or NULL, never a clean win).

## The USER's shape, and the question it raises

The USER asked for *"the heuristic ladder, but without running unnecessary leaves"*:
- *"nlfit should not search less. It should search approximately the same as the heuristic ladder or even a bit more
  because it doesn't pay the early rollouts."*
- *"we should aim to make them as similar as possible except for nlfit not running unnecessary leaves."*
- *"Why do we need an extra ladder in the first place?"*

Without the probe, the shape is heur's ladder d1..depth under heur's own gate. It can then save only the rollouts in
shallow passes that a deeper pass supersedes. So the first job was to measure those rollouts, and to measure what
skipping them buys when the skip is perfect.

## Measurement (details and tables: `logs/hydras-research/P16-measure.md`)

Instrument `MTG_P16_TRACE=1` (print-only, default off): one line per decision. It lists every pass and the units
charged inside the horizon rollout (`SimulateToEnd`).

1. **Superseded rollouts are 18.6% of heur's units on Hydras.** That is 35.2% on T1 and 0.7% from T2 on. On the FIT
   decks: Alchemist 8.0%, EDF 10.8%, Snow 4.8%, Midrange Burn 22.5%.
2. **They are not waste.** A perfect-foresight oracle knows heur's committing depth K for every decision. It runs the
   passes below K leafless and only K on rollouts, and plays the same win turns. It saves only **1.8% units / 8.7%
   core-s** on Hydras, for three reasons:
   - The rollouts of pass k-1 pre-pay pass k's leaves. The per-game rollout cache stores every turn boundary a
     rollout passes, and pass k's leaves are those boundaries. The ID-order hints add to it.
     - First-decision example, game 710001: heur's d5 pass costs 6,333 units after rollout warm-ups and 9,749 cold.
     - On average over the same states, the emulated ladder's d2 rollout pass costs 1.29x heur's, and its d3 pass
       1.06x.
   - **T1's shallow rollout passes are the next decision's committing pass.** The game line cache keys on the
     remaining depth, and T1's pass k+1 holds T2's pass-k nodes. The oracle's T1 saving (-21%) is mostly given back
     on T2+ (+19%).
   - They are the gate's growth ratio.
3. **Keeping rollouts on the last two passes (K-1, K) is the true ceiling:** Hydras **0.922x units / 0.887-0.900x
   core-s**. Midrange Burn 0.892x, EDF 0.962x, Alchemist 0.968x, Snow 0.991x units. The prize is single-digit, and
   reaching it requires knowing K before the passes run.
4. **K cannot be predicted from leafless passes.** On the same state, the rollout/leafless cost ratio of one pass
   varies ~10x within a depth. Hydras d2: p10 7.5, median 16, p90 33. Alchemist d1: 73 / 159 / 487.
   - This is the Niv finding again (`niv-rollout-ladder-vs-leafless-shapes.md`).
   - It is why the existing emulated ladder (`{ladder: emulated, leaf: none}`, emulnl, which IS this shape built
     2026-09-09) mispredicts.
   - On Hydras, emulnl costs 1.042x units / 1.006x core-s (frozen E0 R) and 1.214x with a learned R.
   - It calls "commit" at d2 in 349 of 1000 T1 decisions, and in 320 more it wastes a leafless d3 before replaying d3
     on rollouts.
5. **nlfitL's excess is the probe.**
   - 483 of 1000 first decisions fail the probe. There nlfitL spends 2.02x heur: the probe costs 4.22M, and the
     FIT+climb+calibration 4.10M, which is heur's whole ladder.
   - The 517 probe-proved decisions cost 0.72x heur, but heur's own ladder verified 457 of those 517 too.
   - Every "capped proof probe in front of the ladder" is dearer than heur on Hydras: 1.04x to 1.47x on first
     decisions, for any cap.

## What was built: `value_play.rollout_from = s` (default 0 = off)

The skip-leaves ladder, inside `FullSearchLine`'s own pass loop:
- Passes 1..s-1 run LEAFLESS. A proven win commits there, exactly where the rollout pass would have stopped on it.
- The rollout ladder starts at s. Pass s is admitted as the ladder admits its first pass, s+1 on the bootstrap growth,
  then the measured ratio.
- It keeps the ladder's start gate, 25x overrun ceiling and anytime rule.
- There is no probe, no calibration and no R.
- It applies at every budget. At an unlimited budget the ladder reaches `depth` on rollouts either way, so nothing
  becomes unfindable.
- It only applies to decks with no value model, and never inside rollouts or the hybrid's escalation.
- Plumbing:
  - `MulliganProfile.h` `rollout_from`;
  - `MulliganProfileIO.h` (parse; a sidecar holding only this key is read);
  - `ValueArm.h` `t_deck_rollout_from`;
  - `AIEngine.cpp` `DeckShapeScope`.
- Unit test `test/unit/test_rollout_from.cpp`.
- `rollout_from: 0` in a presence-only sidecar is byte-identical to no sidecar (100/100 Hydras job digests).

The measurement arm `MTG_P16_ORACLE=<file>` (+ `_KEEP`, `_ARM`) exists only to produce the ceilings above. It reads
another run's answers, so it is never a play shape.

### Screen (train seeds, 1000 Hydras / 400 per deck, pooled with heur)

| deck | s=2 units / core-s / delta | s=3 | s=4 | keep-2 oracle (ceiling) |
|---|---|---|---|---|
| hydras | 0.986x / 0.964x / +0.0020 (0/2) | 1.015x / 0.992x / -0.0030 (4/1) | 1.245x / 1.147x / -0.0060 (6/0) | 0.922x / 0.887x |
| midrangeburn | 0.980x / 0.963x / -0.0050 (2/0) | 1.357x / 1.202x / -0.0100 (4/0) | 1.667x / 1.419x | 0.892x / 0.863x |
| alchemist | 1.365x / 1.513x | 2.613x | 2.667x | 0.968x |
| edf | 1.481x / 1.387x | 2.256x | 2.481x | 0.962x |
| snow | 1.910x / 1.954x | 3.786x | 3.779x | 0.991x |

**Why a fixed s fails on the FIT decks:** the forced first rollout pass overshoots. Where rollouts are dear, heur's
gate stops at d1-d2 on T1 (Alchemist 151/400, EDF 166/400, Snow 225/400). `rollout_from` then runs a d2+ rollout pass
heur would never have started. On Hydras (T1 commits at d5 in 690/1000) and Midrange Burn, s=2 is safe and saves a
little.

## Held-out A/B

Setup:
- Hydras: 8,000 games/arm, seeds 750000+off.
- Midrange Burn: 4,000 games/arm, seeds 760000+off.
- ONE pooled batch, arms innermost, keep tables attached, `MTG_RSS_CAP_GB=20`.

| deck | arm vs control | delta t (se, t) | better/worse | blocks b/w/t | units | core-s |
|---|---|---|---|---|---|---|
| hydras | rollout_from=2 vs heur | **+0.00125 (0.00053, +2.36)** | 4/14 | 0/6/2 | 0.993x | 0.997x |
| hydras | nlfitL vs heur | -0.00038 (0.00072, -0.52) | 18/15 | 3/2/3 | 1.164x | 1.038x |
| hydras | rollout_from=2 vs nlfitL | +0.00162 (0.00074, +2.20) | 11/24 | 0/6/2 | 0.853x | 0.960x |
| midrangeburn | rollout_from=2 vs heur | +0.00025 (0.00103, +0.24) | 8/9 | 4/3/1 | 0.952x | 0.936x |
| midrangeburn | ship (nlfit) vs heur | -0.00250 (0.00187, -1.34) | 32/21 | 5/2/1 | 1.056x | 0.860x |
| midrangeburn | rollout_from=2 vs ship | +0.00275 (0.00185, +1.48) | 20/32 | 3/5/0 | 0.901x | 1.089x |

**Depth parity** (train-seed traces, decisions paired at the same played state):

| deck | turn | same committed depth as heur | s2 shallower | s2 deeper |
|---|---|---|---|---|
| Hydras | T1 | 92.7% | 31 | 42 |
| Hydras | T2+ | 89.3% | 25 | 74 |
| Midrange Burn | T1 | 81.8% | 54 | 19 |
| Midrange Burn | T2+ | 68.3% | 11 | 85 |

The perfect-foresight oracle is 100.0% on every decision. Every mismatch is where a rollout was skipped, as the USER
asked. The cause is the gate's input: pass s+1 is gated on the bootstrap growth 6.0 instead of heur's measured
c_s/c_{s-1}, because c_{s-1} was never measured.

**Slower games recover.** The first three of Hydras' 14 slower games (gi 226, 715, 1223, each T5 -> T6 at b20) win T5
at `--depth 8 --budget-ms 0` under rollout_from=2. heur does too. The loss is budget spend, not a hole.

## Verdict

- **`rollout_from` fails the bar on Hydras.** It is +0.00125 (t +2.36, 4/14) for 0.7% fewer units and 0.3% less
  core-s. Do not adopt.
- **On Midrange Burn it is NULL quality vs heur and cheaper** (0.952x units, 0.936x core-s). But Midrange Burn ships
  nlfit, which is better than both (-0.0025 vs heur) and cheaper in core-s. Do not adopt there either.
- On Alchemist / EDF / Snow, any fixed s is 1.4x-3.8x dearer (screen above). Those decks commit at d1-d2 on T1 and
  the forced rollout pass overshoots.
- **The reason is structural, not a tuning gap.** On Hydras the most a perfect predictor could ever save is 7.8%
  units / ~11% core-s (keep-2 oracle). Any real predictor must estimate a skipped rollout pass's cost from a leafless
  pass, and that ratio varies ~10x on one depth. A wrong call costs more than the prize:
  - over-admitting runs a pass heur would never start (Alchemist/EDF/Snow s>=2: 1.4-3.8x);
  - under-admitting searches less, which is the USER's original complaint (Hydras s2: +0.00125).
- The emulated ladder (emulnl), the pre-existing build of this exact shape, measures 1.042x on Hydras for the same
  reason.

### So why does nlfit "lose" to the ladder, in one paragraph

The heuristic ladder's shallow rollout passes are not overhead. Each one is the cheapest way to compute part of the
next pass:
- its rollouts are the next pass's leaves (per-turn boundary cache);
- its best plans order the next pass (ID hints);
- on T1 its subtrees ARE the next decision's committing pass (game line cache);
- its cost is the gate's growth ratio.

A leafless pass provides none of these except, rarely, a proof. On Hydras the rollout ladder itself already verifies
457 of the 517 wins the leafless probe proves. nlfit is cheaper only because it searches less. nlfitL searches the
same, and pays heur's rollout cost plus the failed probes. A shape with "the ladder minus unnecessary leaves" exists
only as the oracle, because "unnecessary" can only be known after the pass that makes it so.

## Fleet

The A3 ceilings (keep-2 oracle) say where the shape could pay at all:

| deck | ceiling (units) |
|---|---|
| Midrange Burn | ~11% |
| Hydras | ~8% |
| EDF | ~4% |
| Alchemist | ~3% |
| Snow | ~1% |

The two decks with a ceiling worth chasing were A/B'd above, and neither adoption holds. No wider fleet screen was run:
- the shape is off by default;
- on the three rollout-heavy FIT decks the screen already shows 1.4x-3.8x;
- every other FIT deck ships nlfit, where this key is inert, because it applies only with no value model.

## Open USER questions

1. **Hydras stays on heur?** Nothing in the ladder family beats heur on Hydras at equal quality:
   - nlfitL: -0.00038 (t -0.52) at 1.164x units;
   - rollout_from=2: +0.00125 at 0.993x;
   - emulnl: 1.042x;
   - nlfit: 0.81x but worse.
   - The USER said *"The heuristic ladder would not be okay."* This measurement says the ladder IS the efficient
     member of this family on Hydras. The remaining levers are per-node cost (the rollout itself, ~5x a tree unit in
     wall), not branching. That is P13's territory.
2. **Drop `rollout_from` and the P16 oracle?** Both stay on the local branch only. The oracle is a measurement tool.
3. **Midrange Burn:** heur is cheaper in units than its shipped nlfit (0.947x) but worse (+0.0025, t 1.34) and dearer
   in core-s. No change recommended.

## Artifacts
- Branch `p16-ladder` (local; `wip/p16-ladder` on origin): P12/P14 prototype commits + P16 instruments + `rollout_from`.
- Scratch `build/scratch/p16/`:
  - `A2/` (traced, all arms);
  - `O2/`, `O3/` (oracle keep-1 / keep-2, pooled with heur);
  - `S1/` (rollout_from screen);
  - `B/` (held-out A/B);
  - `rec/` (d8 b0 recoveries);
  - scripts `parse_p16.py`, `pairT1.py`, `splitL.py`, `superpos.py`, `capprobe.py`, `estD.py`, `mkoracle.py`,
    `pair.py`, `parity.py`, `changed.py`, `mkvariants.py`, `mkmanifest.py`, `run.sh`.
