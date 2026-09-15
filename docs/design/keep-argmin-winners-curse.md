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
| **bottoming** (refined-only, `floor=2`) | 0.0593 | 0.0451 | 0.0363 | 38.7 % |
| **keep** (unfiltered, `floor=1`) | **0.1766** | 0.0658 | 0.0474 | **73.2 %** |

(Cross-pd figures use the corrected per-size rho of §4. An earlier draft used an invalid
rho of 0.990 and overstated these as 0.0348/41.2 % and 0.0440/75.1 %.)

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
2. **Cross-pd conditioning** (documented as unused in the bottoming doc's §8): a further 10.4 %.
   The doc's **0.885 is correct** and an earlier draft of this document was wrong to "correct" it:
   computing rho on *structural residuals* with `tau_r` in the denominator gave **1.142**, an
   impossible value that silently hit a 0.99 clamp. Disattenuated on the raw cell values the
   correlation is 0.885 (size 6), 0.887 (5), 0.836 (4), 0.697 (3), 0.551 (2) — so it must be a
   per-size measured rho, not a global constant, and the gains below use those.

Together: **0.1766 → 0.0474 t, ~73 % of the curse removed, with no deferral and no new rollouts.**

But note §5b: most of that is available from a *filter*, not an estimator — and the filter is
one condition that `best_sub` already carries.

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

## 5b. WHY the table loses to a peek-nullified lookahead — the mechanism, measured

The puzzle: under `MTG_CONFOUND_BOTTOM=1` the lookahead bottomer's peek is destroyed by a reshuffle,
yet it still beats the ungated table. Bucket abstraction does **not** explain it — FiveColour's
discovery merged almost nothing (K=27 over ~27 distinct cards), so the table sees essentially real
cards. The mechanism is that **the generator's adaptive refinement makes the candidate pool
adversarial**:

```
size pd  floorN   refN  meanV(floor)  meanV(ref)    diff  corr(cnt,V)
  6  0  197700 319583        6.8500      5.7456  +1.1044       -0.724
  6  1  224285 292998        7.4606      6.1470  +1.3136       -0.766
  5  0   46306  69757        7.2651      6.1399  +1.1252       -0.739
  4  1   10161  11542        8.3284      7.1883  +1.1402       -0.829
```

Refinement targets contenders, so cells left at the R=2 floor are **1.1–1.3 turns worse** than
refined cells while carrying 2–4x the standard error (`corr(cnt,V)` = −0.72…−0.86 at every size).
A floor cell therefore needs only a ~2σ downward fluctuation to win an argmin, and when it does the
mistake costs **more than a full turn**.

This is the one failure mode that can be **worse than random selection**: a lookahead rollout, once
confounded, is an essentially unbiased draw among plausible candidates, whereas the table's argmin
systematically prefers whichever candidate is both bad and lucky. That is why *declining to answer*
bought so much — the gate skips the selection step, not the estimation step.

**And it is exactly what `best_sub`'s `cnt > bottom_floor` filter already prevents** — which is why
bottoming's regret is 0.0593 while the unfiltered keep argmin's is 0.1766. Measured on identical
candidate sets differing only by that filter:

| candidate set | regret |
|---|---|
| unfiltered (what `KeepVal` does today) | **0.1766 t** |
| filtered (what `best_sub` already does) | **0.0593 t** |

**So the first thing to try on the keep side is not a new estimator — it is giving `KeepVal` the
filter `best_sub` has had all along.** Worth ~3x, one condition, no rollouts, no deferral.

Caveat before implementing: `KeepVal` returns a *value*, not just a choice, so filtering shifts
`D_opt` — deliberately upward, since it removes the lucky-low floor cells. A genuinely-best cell
should already be refined (that is the generator's contract and `best_sub`'s stated rationale), but
the fallback `best_sub` uses when NO candidate is refined (6.3 % of m=1 decisions) must be carried
over too.

## 6. Suggested order of work

0. **Give `KeepVal` `best_sub`'s refined-only filter** (§5b) — by far the best ratio of value to
   risk, and it needs no estimator work at all.
1. Implement the shrinkage + cross-pd posterior for `KeepVal`/`ArgminSub` behind the merge path.
2. Rebuild FiveColour and Melira offline; diff `D_opt` and the mull-from-7 rate against the
   incumbents — §3's prediction says the debiased build should mulligan **less**.
3. `KM_MODE=keep` A/B, exhaustive-vs-exhaustive (debiased vs incumbent).
4. Only then consider whether it closes the +0.0142 t that deferral was buying on FiveColour.
