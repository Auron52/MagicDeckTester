# Model-driven performance levers for the NC policy / value leaf

**Status (updated 2026-09-03):** one of the five IS built — the value-guided escalation beam
(use-case 3) shipped as `MTG_ESC_BEAM` + per-deck `value_play.beam_width` (f517bb36; see
escalation-beam-verify.md). The other four remain unbuilt backlog.

**Status:** deferred backlog — a menu of *performance* techniques (same play quality, less compute) for the
learned-model use cases. None built yet; recorded so any agent/session can pick one up. Companion to
`learned-d0-policy.md` (the quality story) and `stable-shuffle.md` (the CRN reshuffle these compose with).

## Why performance is the open frontier (and why a quality-capped model still helps)

Model **quality** is capped: a static estimator can't beat the reshuffle-averaged rollout, because the residual
error is *forward-simulation* (what the hidden future draws yield), not missing features — see
`learned-d0-policy.md` (draw-wall; ~13 falsified quality levers, incl. data, capacity, DAgger, card-identity at
both bag-of-cards and isolated-enabler granularity).

But a quality-capped model is still a first-rate **compute allocator and variance reducer**, because those roles
need only *correlation* with the truth, not accuracy. The pattern for every lever below is the same: let the
cheap model *rank / prioritize / de-noise*, so the expensive exact computation (heuristic rollouts, K-reshuffle
NC search) runs on **fewer candidates or fewer samples — losslessly** (same decision, less work). The model
being a weak absolute predictor does not hurt: a wrong-by-a-constant leaf still orders candidates well enough to
allocate compute, and the exact computation still decides.

**Lossless bar.** Each lever must be validated to not change the decision (byte-identical play, or within-noise
LP) on the regression suite before adoption — the win is wall-clock, not win-turn. Report any coverage a lever
drops (top-N caps, sampling) per the heuristic-optimization skill. Adopt in the archetype provider, not the root.

## Use case 3 — search leaves (biggest opportunity)

The cost center is the **escalation fallback**: an unverified value-leaf line below `value_trust_depth` triggers
a full heuristic rollout (`FullSearchLineHybrid`, `TurnSolver.cpp`). Most of the value-leaf's speedup is lost
whenever it escalates. Levers, highest payoff first:

1. **Confidence-gated escalation** (most model-native). Train a second cheap model to predict the leaf's *own
   reliability* — target `|leaf_estimate − teacher_label|` from **observable** features (turn, board
   development, projected win-distance, candidate separation). Escalate only the *uncertain* leaves instead of
   every sub-trust-depth line. Reliability is learnable *without* predicting draws (it correlates with turn /
   board-dev), which is exactly why it can win where the value leaf can't. Expected: skip a large fraction of
   rollouts; needs a calibrated false-negative bound (never skip a rollout that would have flipped the pick).

2. **Racing / best-arm identification** for the escalation rollouts. When several candidates need escalation,
   don't roll them all to full depth — use the leaf as the bandit *prior*, sample rollouts adaptively, and drop
   dominated arms early (sequential elimination / LUCB). Subsumes the current top-M prune; lossless (same
   winner, high-confidence). Composes with CRN reshuffles (shared randomness tightens the gaps). Likely the
   single biggest lossless win.

3. **Model as a control variate.** The rollout estimate has draw-variance; use the leaf as a correlated baseline
   `rollout − (leaf(start) − E[leaf])` to cut that variance, so fewer rollout samples reach the same
   confidence. Pure variance reduction; the leaf's bias cancels.

4. **Cascade leaf.** A super-cheap screen (linear, or the GBDT's first few trees) handles the easy states; only
   ambiguous ones invoke the full GBDT or a rollout. Cheapest to prototype.

## Use case 1 — non-clairvoyant play

The NC teacher is reshuffle-averaged search (`MTG_NC_K`=16 reshuffles × `MTG_NC_DEPTH`=2); cost ≈ K rollouts per
decision. Levers:

1. **Model-prior candidate pruning.** NC-evaluate (the expensive K×depth) only the model's top-few candidates;
   let the cheap model screen the rest. Cutting the candidate count that gets the expensive treatment is the
   biggest NC lever. Guard: keep enough candidates that the true best is almost never pruned.

2. **Adaptive K + antithetic / common-random reshuffles.** Do fewer reshuffles when candidates are already
   well-separated (sequential stopping across K), more when close. Pair reshuffles antithetically, or evaluate
   *all* candidates under the *same* reshuffles (CRN) so the shuffle noise cancels in the comparison — fewer K
   for the same discrimination. Composes directly with `MTG_STABLE_SHUFFLE` (see `stable-shuffle.md`).

3. *(Already falsified — do not re-run):* model **as the rollout playout policy** (`MTG_MODEL_ROLLOUT`) beats a
   same-depth heuristic rollout but is dominated by cheap heuristic *depth* (better AND cheaper). Not a win.

## Use case 2 — d0 one-shot policy

Already O(1) per candidate; cost is candidate enumeration + featurization. Levers:

1. **Model-guided generation order + early stop**: generate candidates in model-predicted-best-first
   order and stop once the lead is safe, instead of enumerate-all-then-rank. (An early stop with a
   safety condition — NOT a rank cap; see heuristic-optimization.md Rule 0b before narrowing this
   into any fixed-width "beam".)
2. **Cheap pre-filter** of strictly-dominated candidates before featurizing.
3. For the **A/B card-swap screener** use case (validated in `learned-d0-policy.md`): compile / quantize the
   GBDT for the batch path (many variants × many seeds).

## Cross-cutting

- **CRN reshuffles** (`stable-shuffle.md`) are the multiplier under racing, adaptive-K, and control-variate —
  shared randomness is what makes the candidate *gaps* tight enough to stop early. Land that first if pursuing
  the NC / leaf racing levers.
- **Measure the right thing:** wall-clock at equal decision, not RMSE. Every lever here is orthogonal to the
  draw-wall (they reallocate/deduplicate compute, they don't try to out-predict the future).

## Related

- `docs/design/learned-d0-policy.md` — the quality story, draw-wall, falsified levers, the value-leaf hybrid.
- `docs/design/stable-shuffle.md` — the CRN reshuffle these compose with.
- `.claude/skills/heuristic-optimization.md` — the adopt-on-approval / archetype-provider discipline.
