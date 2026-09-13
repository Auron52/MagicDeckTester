# The bottoming argmin's winner's curse — diagnosis and offline fix

**Status: FIXED and measured (2026-09-12).** FiveColour's confounded bottoming A/B went from
**+0.018375 t (reject, 0/16 seeds)** to **−0.0021 t (table wins, 9/16 seeds)** with **no
regeneration, no engine change and no schema change** — only a change to how a policy is built from
an existing raw sidecar.

This document exists because the failure mode is **generic to every deck that ships an exhaustive
bottoming table**, and because the repair route (re-policy an existing raw offline) was not
previously known to be exact.

---

## 1. The symptom

FiveColour's 20-day generation (55.66 M rollouts, frozen `2f7822a2`) produced a keep table that
*won* decisively and a bottoming table that *lost*:

```
keep      -0.125000t  ok      (16/16 seeds, mean/se -23.4)
bottoming +0.018375t  REJECT  (0/16 seeds,  mean/se  +6.09)
```

Per `mulligan-profile.md` the response to a bad confounded bottoming A/B is "raise R or fix the
bottoming heuristic, **not** ship bottoming off" (there is no off switch). Raising R meant
regenerating. The user's constraint: *"I'm not regenerating something that took 3 weeks."*

## 2. The cause, measured rather than assumed

`BuildPolicyFromTables::best_sub` picks a hand's bottoming target by `argmin` over its
subcompositions' estimated values. Those estimates are rollout means at finite R, so the argmin over
N noisy candidates systematically selects the **luckiest estimate, not the best hand**. That is a
bias at *any* finite R, and it is the mechanism
`keepgen-subtable-starvation-detection.md` already named ("a winner's curse picks the luckiest
estimate, not the best hand").

Everything below is measured from the raw sidecar by the `test/keepraw_*.py` tools.

**Sub-cell precision.** FiveColour's sub-table rollout counts are **trimodal: exactly R = 2, 18 or
30** (floor / one refine step / cap). 41–43 % of size-4…6 cell-sides sit at the floor. Per-rollout
sd is **0.74–0.80 t**, so se ranges 0.52 t (R=2) → 0.14 t (R=30).

**Exposure.** A `--gen-mulligan fast` run has `adaptive_bottom=true`, so `best_sub` receives
`bottom_floor = r0 = 2` and the argmin is already restricted to refined cells. That filter helps, but
is not sufficient — on the refined candidate set:

| depth m | hand size | candidates N | se | tau (true spread) | **noise share** | no-refined fallback |
|---|---|---|---|---|---|---|
| 1 | 6 | 4.2 | 0.200 | 0.296 | **50.0 %** | 6.3 % |
| 2 | 5 | 10.3 | 0.218 | 0.377 | 37.4 % | 1.9 % |
| 3 | 4 | 16.2 | 0.217 | 0.413 | 31.0 % | 0.7 % |

"Noise share" = `mean(se²)/var_observed` — the fraction of the apparent spread between candidates
that is pure sampling noise. At the depth that dominates play, **half of it**.

**Quantitative confirmation against the A/B.** Bottoming runs only when `mulligan_count > 0`
(`AIEngine.cpp:590`), so `delta_overall = f · (regret_table − regret_lookahead)`. Measured by Monte
Carlo backward induction over the real hypergeometric hand distribution:

* `f = P(mulligan > 0) = 0.586`, mix m1 62 % / m2 30 % / m3 7 %
* simulated table regret at that mix = **0.0495 t** → implied confounded-lookahead regret **0.0185 t**
* `0.586 × (0.0495 − 0.0185) = 0.0182` vs the **measured +0.0184**

The curse accounts for the entire deficit. Note `keep%_opt` in the gen report is **unweighted over
compositions** and is *not* the mulligan rate; do not reuse it as one.

## 3. What does NOT work (measured, so nobody re-tries it)

**Shrinking every candidate toward a common mean recovers ~6 % of the regret (0.0037 of 0.0609 t).**
This is not a tuning failure, it is arithmetic: with equal per-candidate precision, shrinking by a
common factor toward a common mean is a **monotone transform of the estimates, so the argmin is
unchanged**. All of its leverage comes from *differences* in precision, and among refined candidates
ours differ only R=18 vs R=30. **Re-weighting the same numbers adds no information.**

**A structural prior helps but is not enough on its own.** A quadratic (pairwise-interaction) fit of
the cell mean on bucket counts explains **50–89 %** of true between-cell variance (size-5 draw:
tau 0.689 → 0.224), because a cell can borrow strength from every other cell holding those cards.
It recovers 24 % of the regret — projected +0.0184 → +0.0133. Still a reject.

## 4. What works: decline to answer when the margin is noise

The table is **not uniformly worse** than the engine's lookahead bottomer — it is worse *on average*
only because near-ties are settled by noise. Where its margin is decisive it is near-perfect. So:

> emit a bottoming target only when the winner's margin clears the combined noise of the top two
> candidates; otherwise **emit nothing**.

```
Z_i    = Vhat_i + lam_i (V_i - Vhat_i)     posterior mean, lam_i = tau^2/(tau^2 + se_i^2)
sig_i  = se_i * sqrt(lam_i)                posterior sd
emit iff  Z_(2) - Z_(1)  >=  k * sqrt(sig_(1)^2 + sig_(2)^2)
```

**This required no engine change and no schema change.** `ExhaustiveKeepPolicy::DecideBottom`
already rejects a slot whose target vector fails its `size() != K` check, and
`AIEngine::BottomCards` then falls through to the lookahead bottomer — which is exactly arm A of the
A/B. An empty per-slot vector round-trips through both the JSON writer and the bincache. So "no
opinion" was already expressible; nothing needed to learn a new concept.

Simulated on the measured `(se, tau, candidate-set)` structure, weighted by the real mulligan mix:

| gate k | defer % | blended regret | predicted delta |
|---|---|---|---|
| 0.00 | 0 % | 0.0345 | +0.0094 |
| 0.50 | 33 % | 0.0166 | −0.0011 |
| **1.00** | **56 %** | **0.0131** | **−0.0032** |
| 2.00 | 82 % | 0.0153 | −0.0019 |
| 3.00 | 92 % | 0.0171 | −0.0008 |

Two properties worth keeping in mind:

* **The downside is bounded.** As k rises the policy converges to the lookahead it is being compared
  against, so a mis-specified gate degrades toward parity — it cannot regress to the old deficit.
* **There is an interior optimum**, which is the signature of a real effect rather than a degenerate
  "always defer".

**Measured outcome at k=1.0** (16 seeds × 1000 games, `MTG_CONFOUND_BOTTOM=1`): **−0.0021 t, 9/16
seeds, se 0.0018**, against a predicted −0.0032. Deferral was 67.9 % of all emitted slots (a larger
denominator than the simulation's 56 %, which weighted by decision frequency). The profile also
shrank 1.78 GB → 956 MB, since deferred slots serialize as `[]`.

**Caveat, stated plainly:** the lookahead's regret L = 0.0185 t is *derived* from this same A/B plus
the regret model, not independently measured, so absolute predictions inherit its error. The ranking
of gate values is robust to L; the absolute delta is not. The confounded A/B remains the gate.

## 5. Implementation

`src/analyzer/ExhaustiveKeep.cpp`, all **off by default**:

* `BottomRefineCfg { shrink, gate_k }`, passed to `BuildPolicyFromTables` as a nullable pointer —
  `nullptr` keeps the generation path and every other deck **byte-identical**.
* `BottomFeatSparse` / `FitBottomPrior` / `BottomSolve` — precision-weighted ridge least squares,
  quadratic → additive → none depending on how many cells the table has to support the design.
  The fit **refuses itself** if `tau >= tau_raw`, so a useless model cannot make things worse.
* Per-cell `Z`/`SIG` are precomputed once per table; predicting per decision would dominate the
  build (a size-7 table asks `best_sub` ~2 M × (max_mull+1) × 2 times).
* `KeepVal`, `ComputeDopt` and the keep flags are **untouched** — the keep A/B already passed, so it
  is not disturbed. Verified: rebuilt `D_opt` is identical to 6 figures.

Flags (merge path): `MTG_KEEP_BOTTOM_REFINE` (off), `MTG_KEEP_BOTTOM_SHRINK` (default on),
`MTG_KEEP_BOTTOM_GATE_K` (1.0), `MTG_MERGE_BOTTOM_FLOOR` (−1).

## 6. The offline rebuild is EXACT — and two merge bugs that hid it

**A 3-week generation can be re-policied in ~20 minutes with zero rollouts.** Verified: with
`MTG_KEEP_MERGE` + `MTG_MERGE_BOTTOM_FLOOR=2` over the single raw sidecar, the rebuilt profile's
`entries` section is **byte-identical** to the generated one and `D_opt` matches to 6 figures
(draw 4.77341 / play 5.01141). **The raw sidecar, not the profile, is the durable asset.**

Establishing that control exposed two merge-path defects that affect *every* merged profile:

1. **`t.cnt` was never populated and `t.se` was pushed as `{0,0}`.** Because `best_sub`'s filter
   short-circuits on `bottom_floor < 0`, a plain merge silently rebuilt an **unfiltered** bottoming
   policy — re-admitting exactly the floor-R cells an adaptive-bottom generation deliberately
   excluded. A merged profile was therefore *not* a faithful rebuild of the profile it replaced, and
   no bias correction could see a cell's standard error. Any pooled/merged bottoming profile built
   before this fix is suspect.
2. **`ek.play_digest` was dropped by the merge.** That field is the real pooling identity (`commit`
   over-approximates), and `RunKeepMerge` itself uses it to decide whether sidecars may pool. Every
   merged profile shipped without it, so it could not be pooled against or audited later.

## 7. Measurement tools

Pure-python, read only the raw sidecar, no engine and no rollouts:

| tool | answers |
|---|---|
| `test/keepraw_argmin_noise.py` | se / N / tau and the noise share, filtered vs unfiltered |
| `test/keepraw_shrinkage_sim.py` | what shrink-to-mean can recover (the null result) |
| `test/keepraw_structural_prior.py` | how much signal a structural prior carries |
| `test/keepraw_mullrate.py` | Dopt, mulligan mix, and `f = P(bottoming fires)` |
| `test/keepraw_hybrid_sim.py`, `..._quad.py` | the gate sweep and predicted A/B delta |

## 8. Open follow-ups

* **Apply the gate at generation time**, not just on the merge path, so a fresh deck never ships an
  ungated bottoming table. The correction currently lives only where a policy is rebuilt.
* **Fold the other pd's observation into the prior.** A cell's play and draw true values correlate
  at **0.885** (sizes 5–6), so each is strong evidence about the other — unused information that
  would tighten tau further. Not pursued because the gate, not the estimator, dominates the result.
* **Tune k per mulligan depth.** The optimum was taken globally; m=1 and m=3 have different N, se
  and tau.
* The producer-side barrier of `keepgen-producer-barrier-and-durability.md` remains OPEN at HEAD;
  it is unrelated to this defect.
