# Change-detection carry (whole-pool warm-start)

**Status: DESIGN (2026-07-07). The most powerful "cut subsequent runs" lever; supersedes confident-cell
carry. Not yet built.** Motivated by "never re-run Hinata for a week again" — and, per the user, useful
for *small deck changes* too.

## The idea

Confident-cell carry (built, `docs/design/exhaustive-profile-workflow-deferred.md` §2) reuses the cells a
prior run decided *confidently* and re-samples every near-threshold cell from scratch — even the ones the
change never touched. Change-detection carry goes further: carry the **entire prior pool** as a warm-start
prior, then on the new commit **re-sample every cell thinly** and spend real rollouts **only where the new
thin samples disagree with the prior** — i.e. exactly the cells the play/deck change actually moved. It
auto-scopes the re-run to what changed, without needing to know what changed.

## Core mechanism

Per cell (per pd), the prior pool gives `(V_prior, n_prior, var_prior)` (var via the pooled `sumsq`).
1. Draw a thin batch of `k` fresh rollouts on the **new commit** → `(V_new, var_new)`.
2. Two-sample test: could `V_new` and `V_prior` be the same distribution, given both counts/variances?
   - **Consistent → cell UNMOVED.** Keep the prior value `V_prior` (its high `n_prior` makes it the better
     estimate); stop. The `k` thin rollouts were only a detector.
   - **Inconsistent → cell MOVED.** Discard the prior; refine fully with fresh (new-commit) samples — a
     normal from-scratch cell, adaptive as usual.
3. Build the policy from the mix (prior values for unmoved cells, fresh values for moved cells).

Cost: unmoved cells cost only `k` (detection); moved cells cost full R. If the change moved few cells,
the re-run collapses to (all cells × k) + (moved cells × R) — potentially a week → a day.

## Why an unmoved cell's prior value is valid on the new commit (a Rule-0 refinement)

Rule 0 says a play-logic fix invalidates prior sidecars. It's *conservative*: a play change only alters
rollout outcomes for cells whose rollouts **execute the changed code path**. A cell whose rollouts never
hit the changed logic produces **identical** outcomes on both commits — its prior value is not "stale," it
is *the same number*. Change-detection is precisely an empirical way to identify the still-valid subset:
Rule 0 becomes "re-verify every cell cheaply, keep what didn't move" instead of "throw everything away."

## Fidelity: the false-negative is the only danger, and it's asymmetric

- **False positive** (declare a truly-unmoved cell "moved") → we refine it fully → wasted work, never
  wrong. Fine.
- **False negative** (miss a real move → keep a stale prior) → **wrong policy**. This is the risk to
  control. Bias the test toward "moved" (a loose consistency threshold): prefer wasted rollouts over a
  missed move.
- **Curse asymmetry.** Win-turn is right-skewed, so a thin fresh batch reads *lower* than the truth
  (misses the tail). A cell that got **better** (moved down) reads even lower → easily detected. A cell
  that got **worse** (moved up) is partly masked by the downward bias → the danger direction (same
  asymmetry as verify-mode keep→mull). Mitigate with `sumsq`-shrinkage on `var_new` and adequate `k`.
- **k-sizing.** Size `k` to detect the smallest *decision-relevant* shift `δ` (the win-turn move that
  could flip a keep/mull or a bottom target near its threshold) at high power, using `var_prior` as the
  known scale: `k ≈ (z_β·σ/δ)²`-ish. Cells far from any threshold tolerate a larger undetected move (their
  decision can't flip), so `δ` can be **distance-to-threshold-aware** — detect tightly only near
  thresholds, loosely for deep junk. This ties the detector to the same margin logic the confident-cell
  gate already uses.
- **Multiple testing.** ~0.7M cells × 2pd ≈ 1.4M tests. Control the *miss* rate, not the family-wise
  false-positive rate (false positives are just wasted work). A per-cell loose threshold + the
  distance-to-threshold weighting is the lever; a sequential test (keep adding to the thin batch until
  confidently moved-or-not) avoids a fixed-`k` power cliff.

## Relation to confident-cell carry (it supersedes it)

Confident-cell carry = "skip cells that were confidently decided." Change-detection = "reuse the prior for
every cell the change didn't move, confident or not." The latter also preserves the **expensive
near-threshold R investment** across a commit (the cells that cost the most to establish), which
confident-cell carry throws away. Build order: confident-cell carry (done) is the cheap 80%; change
detection is the thorough version when a re-run's near-threshold + bottoming cost dominates.

## Small deck changes (the user's extra motivation)

For a small decklist change (swap 1–2 cards) the same detector works — most hands' rollouts are nearly
identical (the swapped card just sits elsewhere in the library), so the detector finds most cells unmoved
and refines only hands whose outcomes actually shift. Two extra pieces vs the same-list case:
1. **Bucket-membership translation.** A card swap reruns equivalence discovery → the bucket set/order can
   change → prior comps no longer index the new bucket space. The prior pool must be translated by bucket
   *membership* (map old→new buckets by their card lists; a cell touching a changed bucket has no prior →
   sampled fresh). This is the same translation the modified-list feature needs.
2. **The global threshold shifts.** A deck change moves overall speed → `Dopt` shifts → a value-unmoved
   cell can still flip *near* the threshold. The detector already handles this: "unmoved" is about the
   cell's *value*; the keep/mull decision is recomputed against the fresh `Dopt`, so a value-stable cell
   near a shifted threshold is re-decided correctly (its prior value vs the new `Dopt`).

So change-detection + bucket translation = the small-deck-change tool; the user's "drop cells that don't
include the changes" falls out of it (those cells detect as unmoved).

## Interface sketch

- `MTG_KEEP_PRIOR_RAW=<pool.raw.json>` — the warm-start prior (must carry `sumsq`; fingerprints checked,
  with bucket translation when `bucket_fp` differs for a deck change).
- `MTG_KEEP_DETECT_K=<k>` — thin detection batch (or `0` = sequential/auto-sized from `var_prior` and δ).
- `MTG_KEEP_DETECT_DELTA=<turns>` — the decision-relevant shift to detect near thresholds (distance-aware).
- Output: same raw sidecar; unmoved cells emit the prior value with `count=0`-style provenance (or a small
  `k` with a "carried" tag) so a downstream merge treats them correctly. **Do NOT pool prior + fresh across
  the commit** (Rule 0) — the prior value is *reused*, not summed with new-commit samples.

## Open questions / risks (resolve before building)

- Exact test + `δ` schedule (fixed-k vs sequential; distance-to-threshold weighting).
- How to serialize "carried-from-prior" cells so the policy build and any later same-commit pooling stay
  correct (a carried cell must not be pooled with new-commit chunks; it's a fixed prior value).
- Validation: on a synthetic "commit change" (perturb a subset of cells' true means), confirm the detector
  refines the moved subset and keeps the rest, and that the final policy's decisions match a full
  from-scratch run on the new commit. Reuse the tiny-deck harness.
- Interaction with adaptive sampling and the confident-cell carry (change-detection likely *replaces* the
  confident-cell floor logic rather than layering on it).

## Build plan (phased)

1. **Same-list / new-commit** (no bucket translation): `MTG_KEEP_PRIOR_RAW` + fixed-k detector + keep-prior
   for unmoved. Validate on the tiny deck with a synthetic per-cell mean perturbation.
2. **Distance-to-threshold δ + sequential detection** (efficiency + fidelity).
3. **Bucket-membership translation** → small deck changes (shares code with the modified-list feature).
