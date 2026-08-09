# FiveColour value leaf — the measured result (2026-08-09)

Pipeline: `bash scripts/valueleaf.sh run decks/FiveColour`, queue `logs/vlq_fivecolour/`.
Model staged at `logs/eval/FiveColour.value.STAGED.json` (12,663 rows from 2,500 games,
held-out RMSE 0.5562). **Nothing adopted** — this doc is the decision input.

## Provenance, stated plainly

Rows and training are from `1605b9e`. The 52-cell matrix spans several engine strata by explicit
user policy (regenerate only the games that were never completed — chunk-granular resync, never a
whole-cell redo), so the "rebaseline" pass was a genuine no-op: 0 chunks dropped, 52/52 cells still
at 400 games. Phases D and E were then run at `f713a0d`, and the freeze re-stamped to it with the
reasoning recorded in `logs/vlq_fivecolour/driver.log`.

Phase D is pure arithmetic on the table, and phase E measures BOTH arms at the same commit, so the
A/B is internally consistent whatever engine trained the model. The one real caveat: the
`eval_model`'s labels predate the Unite split heuristic (`ca9c50b`) and the honest lethal detector
(`14185a0`), so they are slightly off-distribution for the engine that now plays the deck. That
makes the measured gain a LOWER bound — a retrain on post-`14185a0` rows can only fit the current
engine better.

## Phase D — the derivation

```
trust=UNSET  no_fb=False   crossover (c -> take@hc): 1->1 2->1 3->1 4->2 5->4 6->5 7->5 8->5
```

`trust=UNSET` is honest, not a failure: V8=5.0204 never comes within tol=0.002 of the converged
heuristic H5=5.0165, so the engine escalates at every depth rather than trusting the leaf outright.
`no_fb=False` because that same 0.0039 gap sits well inside the 0.020 margin — the leaf is weak but
not so weak that an escalated line must ALWAYS be taken.

## Phase E, part 1 — the sidecar A/B (THE result)

8 seeds x 1000 games, paired, both arms at `f713a0d`:

| arm | avg | delta | paired t | better/worse/tied | same-digest | core-ms | s/game |
|---|---|---|---|---|---|---|---|
| live (no sidecar) | 5.06137 | — | — | — | — | 50,891,862 | 6.36 |
| **staged** | **5.05150** | **−0.00988** | **−3.13** | **6/2/0** | 0/8 | 51,658,781 | 6.46 |

Negative delta = better. Play changes on every seed (0/8 identical digests).

**This model buys quality, not speed** — it is 1.5% MORE expensive, where other decks got 1.6–25x
cheaper. That follows directly from `trust=UNSET`: the engine escalates at every depth and pays for
the probe on top, so the gain comes from the crossover table (when to TAKE an escalated line), not
from replacing rollouts with the leaf. Worth remembering before quoting the value leaf as a speed
lever on a deck whose leaf does not converge.

## Phase E, part 2 — the play-policy sweep

4 seeds x 500 games. `dflt` = the staged model with NO `value_play` block, i.e. the built-in default
(d5, budget 20, full escalation ladder, no cap) — exactly what the A/B above measured. The d4/d5/d6
arms are ENABLED blocks with `escalation_cap == target_depth`.

| arm | avg | delta | t | better/worse/tied | s/game | vs dflt |
|---|---|---|---|---|---|---|
| dflt (base) | 5.03600 | — | — | — | 3.56 | 1.00x |
| d4 | 5.04500 | +0.00900 | +2.56 | 0/4/0 | 3.93 | 1.10x |
| d5 | 5.03850 | +0.00250 | +1.99 | 0/3/1 | 2.27 | 0.64x |
| d6 | 5.03600 | +0.00000 | +0.00 | 2/2/0 | 2.84 | 0.80x |

* **d4 is strictly dominated** — worse on all four seeds AND 10% more expensive. Closed.
* **d5-enabled is worse** (+0.0025, worse on 3 of 4) for 36% less cost. The ONLY difference from
  `dflt` is `escalation_cap=5` (budget 20 matches the default), so this measures the single-depth
  capped escalation directly: on this deck it costs quality.
* **d6-enabled ties** on the mean with a 2/2 seed split, at 20% less cost. A real tie, not identity
  — all four digests differ.

**Conclusion: ship no `value_play` block.** The built-in default is the best of the four on the
primary metric, and d6's tie is measured on 4 seeds only.

## Deferred: is d6 a free 20%?

d6-enabled is quality-neutral and 20% cheaper on 4 seeds — enough to be interesting, not enough to
adopt a policy that also LOCKS the deck's depth (an explicit `--depth` becomes an error). To settle
it, run the same 4 arms on held-out seeds (the heuristic-optimization rule: validate the winner on
seeds it was not selected on) and sum across ALL seed sets before calling a small delta a win —
this session already produced three single-seed reads that a wider run overturned.

```bash
PLAY_SEEDS="620000 621000 622000 623000 624000 625000" AB_SEEDS=" " \
  bash scripts/valueleaf.sh run decks/FiveColour     # after: rm logs/vlq_fivecolour/done/E_measure
```

## The bug this run found in the harness

Phase E's sweep wrote `target_depth` WITHOUT `enabled: true`. A `value_play` block steers play only
when `enabled == true` (`ValuePlay::drives()`); unenabled it is a pure recommendation and is
byte-identical. So the first sweep returned d4/d5/d6 all byte-identical and reported "+0.00000,
depth does not matter" having tested nothing at all.

A deck WITH a live enabled block never shows this — it only bites a deck's FIRST model, which is
exactly the case the sweep is most needed for. Fixed in `scripts/valueleaf.sh`: arms are written
enabled and carry `budget_ms` (an enabled block owns the budget too — omit it and it resolves to 0,
confounding depth with a resource change), plus a `dflt` arm so the baseline is what the deck
actually ships. Recorded as trap 3 in `.claude/skills/value-leaf.md`, since the root cause is that
sidecar-presence `enabled` and `value_play.enabled` mean opposite things.

## The matrix, for reference

```
heuristic:  H1=5.0981[540ms]  H2=5.0565[5.9s]  H3=5.0366[36s]  H4=5.0236[160s]  H5=5.0165[348s]
value-leaf: V1=5.4698[9ms] V2=5.2734[59ms] V3=5.1703[379ms] V4=5.0707[2.1s]
            V5=5.0279[5.8s] V6=5.0217[7.1s] V7=5.0217[7.0s] V8=5.0204[9.4s]
```

V6 matches H4 for 23x less work; V5 sits between H3 and H4 at 6x less than H3.
