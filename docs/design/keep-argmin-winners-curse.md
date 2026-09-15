# The KEEP side carries the same winner's curse — unfiltered, 3x larger, and unexamined

**Status: diagnosed offline, NOT yet measured in play (2026-09-14).** This is a lead with strong
evidence behind it, not an adopted result. Everything below comes from the raw sidecar and the
source; no A/B has been run.

Companion to [keepgen-bottoming-winners-curse.md](keepgen-bottoming-winners-curse.md), which fixed
the **bottoming** argmin and explicitly set the keep half aside:

> `KeepVal`, `ComputeDopt` and the keep flags are **untouched** — the keep A/B already passed, so it
> is not disturbed.

That reasoning does not hold. "The keep A/B passed" establishes that exhaustive keep beats the
**static** profile; it says nothing about whether the keep estimator is *unbiased*. It is not.

## 1. The two argmins are the same operation, and only one is filtered

```cpp
// ExhaustiveKeep.cpp:307 -- KeepVal: min over EVERY present subcomposition. No cnt filter.
for (const std::vector<int>& s : subs)
{ auto it = t.index.find(s); if (it != t.index.end()) { best = std::min(best, t.V[it->second][pd]); } }

// ExhaustiveKeep.cpp:624 -- best_sub: the SAME argmin, restricted to refined cells.
if (!(bottom_floor < 0 || (!t.cnt.empty() && t.cnt[idx][pd] > bottom_floor))) { continue; }
```

`best_sub`'s filter exists precisely because "a floor-R sub-cell can win this argmin only by being
lucky". `KeepVal` faces the identical hazard with the identical cells and no filter, so it ranks a
strictly noisier candidate set.

## 2. Measured: the keep side's curse is ~3x the bottoming side's

`test/keepraw_crosspd_prior.py` (same Monte Carlo as `keepraw_structural_prior.py`; the
`bottom_floor` argument selects which candidate set is scored). FiveColour, R=30, K=27:

| candidate set | plain | EB-model | EB-xpd | recovered |
|---|---|---|---|---|
| **bottoming** (refined-only, `floor=2`) | 0.0593 | 0.0451 | 0.0348 | 41.2 % |
| **keep** (unfiltered, `floor=1`) | **0.1766** | 0.0658 | 0.0440 | **75.1 %** |

Per-decision regret on the keep side is **0.1766 t against bottoming's 0.0593 t**. The noise-share
diagnostic agrees: `keepraw_argmin_noise.py` puts the keep argmin at **48.3 %** noise share at m=1
(bottoming: 50.0 %) over a candidate set that still contains 2-rollout cells.

## 3. Why this matters more than the bottoming case

`KeepVal` is not a reported statistic — it is load-bearing in two places:

* `ComputeDopt` (`:363`, `:366`) — the mulligan thresholds, via `Dopt[m] += P·min(KeepVal, Dopt[m+1])`.
* the keep flag itself (`:656`) — `keep = (KeepVal(h,m) <= Dopt[m+1])`.

**Predicted direction of the distortion.** The argmin selects the *lowest* estimate and win turns
are lower-is-better, so `KeepVal` is biased **optimistically low** for `m >= 1`. At `m = 0` there is
no argmin at all — `KeepVal(h,0) = V[7][h]`, a plain mean (`:2616`) — so the `m=0` decision compares
an **unbiased** hand value against an **optimistically-valued** mulligan alternative `Dopt[1]`.
Mulliganing therefore looks better than it is, and the policy should **mulligan too readily from 7**.

That is a concrete, falsifiable prediction and it is the cheapest way to test this claim: compare
the shipped profile's mull-from-7 rate against the rate implied by a debiased rebuild.

## 4. The fix is directive-compliant and needs no new information

Unlike the bottoming gate — which buys its improvement by deferring decisions to the lookahead
bottomer, prohibited by [no-lookahead-bottoming.md](no-lookahead-bottoming.md) — the estimator route
keeps every decision inside the table. Two levers, both offline, both already measured above:

1. **Structural prior** (already implemented for bottoming as `MTG_KEEP_BOTTOM_SHRINK`): recovers
   **62.7 %** of the keep-side curse, far more than the 23.9 % it recovers on the bottoming side,
   because the unfiltered set is noisier and therefore has more to gain from shrinkage.
2. **Cross-pd conditioning** (documented as unused in the bottoming doc's §8): a further 12.4 %.
   The doc estimated the play/draw correlation at 0.885; **measured on the structural residuals it is
   0.990 at size 6 and 0.969 at size 5** — each pd's rollouts are very nearly a second independent
   sample of the same cell. The correlation collapses at small sizes (0.261 at size 2), so the term
   must be weighted by a per-size measured rho, not a global constant.

Together: **0.1766 → 0.0440 t, 75.1 % of the curse removed, with no deferral and no new rollouts.**

## 5. What is NOT established

* **No in-play measurement.** Every number here is offline. The bottoming analogue *was* validated
  in game (offline gate sweep predicted a 0.0171 t gap; the confounded A/B measured 0.0142 t — a
  good calibration), which is why the offline scale is trustworthy enough to act on. It is not a
  substitute for an A/B.
* **The mapping from per-decision regret to win turns is not 1:1.** For bottoming the doc's
  `delta = f · (regret_table − regret_lookahead)` with `f = P(mulligan > 0) = 0.586` reproduced the
  measured A/B. The keep decision fires on **every** hand, so its `f` is 1.0 and the conversion is
  different; do not multiply the 0.1766 by anything without deriving the right factor.
* **Correcting `KeepVal` moves `D_opt`, and `D_opt` is the mulligan policy.** This is a larger blast
  radius than the bottoming fix, which provably left `D_opt` identical to 6 figures. Any keep-side
  correction must be validated with the full keep A/B, not just a bottoming A/B.

## 6. Suggested order of work

1. Implement the shrinkage + cross-pd posterior for `KeepVal`/`ArgminSub` behind the merge path.
2. Rebuild FiveColour and Melira offline; diff `D_opt` and the mull-from-7 rate against the
   incumbents — §3's prediction says the debiased build should mulligan **less**.
3. `KM_MODE=keep` A/B, exhaustive-vs-exhaustive (debiased vs incumbent).
4. Only then consider whether it closes the +0.0142 t that deferral was buying on FiveColour.
