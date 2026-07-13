# Non-clairvoyant mulligan-table generation (deferred)

**Status:** deferred idea, not being worked on. Not crucial now — captured so it isn't lost.

## The problem

The exhaustive keep/bottom mulligan table (`docs/design/exhaustive-keep-policy.md`) is generated with
**clairvoyant** rollouts. For each bucket-hand `H`, `ExhaustiveKeep.cpp` evaluates
`V[H][comp][pd]` as the mean win turn over `R` reshuffled library continuations
([ExhaustiveKeep.cpp](../../src/analyzer/ExhaustiveKeep.cpp) step 3, ~line 346; the leftover is
reshuffled per continuation to fix the burn 0-land sampling bias, ~line 709). The reshuffling makes the
*sampling* of futures unbiased, **but each individual continuation is still played CLAIRVOYANTLY** —
`RolloutWinTurn` runs a full-depth search over that shuffle's *known* library and plays it optimally.

So the stored value is `E_shuffle[ clairvoyant-optimal win turn ]`, not `E_shuffle[ win turn under a
blind, non-clairvoyant play policy ]`. The two differ by the **value of perfect information (EVPI)**,
which is *not uniform across cards*:

- **Fetchlands** — a clairvoyant playout "knows" the post-shuffle library, so it fetches/shuffles into
  exactly the right land and future; a blind player does not. Clairvoyance loves fetches.
- **Ponder / scry / Treasure-Hunt-adjacent selection** — their whole value is *learning/arranging the
  future*; clairvoyance already has that information for free, so it under-credits the selection but
  over-credits the *outcome* the selection was chasing. Net: hands built around future-manipulation
  read as stronger under clairvoyant generation than a blind player can realise.

Because the keep/bottom **policy is consumed in real, non-clairvoyant play**, a clairvoyance-biased table
mis-ranks hands: it will keep (and bottom toward) fetch/Ponder-heavy hands somewhat more than a
non-clairvoyant optimum would.

## Prior art in this repo (what makes this tractable)

This is the same clairvoyance axis already explored on the play side:

- **`TurnSolver::ReshuffleAvgChoosePlan`** — the non-clairvoyant *play* policy: at each decision it
  reshuffles the unseen library and averages the plan value over K samples, so the choice is judged on
  expected outcome over *unknown* draws (`docs/design/learned-d0-policy.md`).
- **`MTG_NC_BLIND_BOTTOM`** (`AIEngine::BottomCards`, ~line 987) — the *bottoming* analog: each removal
  candidate is scored over K reshuffled unseen futures (`shuffle_salt_search`, executor-order
  independent) instead of the one true post-bottom draw. This already corrects clairvoyance in the
  *runtime* bottomer; the KEEP side and the *generation* side do not have it.
- **`ShuffleEvalGuard` / `shuffle_salt_search`** — the decoupling instrument that makes a rollout's
  mid-game shuffles fold a per-sample salt, so a rollout can be made non-clairvoyant deterministically.
- **Clairvoyance-isolation finding (2026-07-11):** the exhaustive keep is a *real* non-clairvoyant gain
  (≈ −0.031t), and the "keep looks worse vs static" artifact was a clairvoyant→blind **bottoming**
  correction, not a regression. That isolation method (confounded A/B) is exactly the gate for judging
  an NC-generated table.

## Proposed change (when picked up)

Generate `V[H]` under a **non-clairvoyant playout** rather than a clairvoyant one: within each
continuation, play the reshuffle-averaged NC policy (or at minimum decouple the rollout's future via
`shuffle_salt_search` so the search cannot exploit the drawn order), matching how the deck is actually
played. Concretely, one of:

1. **NC rollout in generation** — swap the generation's clairvoyant `RolloutWinTurn` for an NC evaluation
   (ReshuffleAvgChoosePlan-style inner averaging, or a `MTG_NC_BLIND_*`-gated playout). The keep value
   becomes `E[ blind-play win turn ]`.
2. **Keep-side blind correction** — mirror `MTG_NC_BLIND_BOTTOM` for the keep decision (already partly
   there for bottoming), so at least the *policy extraction* is blind even if the raw V stays clairvoyant.

## Caveats / cost

- **Expensive:** NC evaluation needs inner reshuffle-averaging (K samples per continuation), so it
  multiplies the already-heavy generation cost. Trade against R; may force lower R or fewer buckets.
- **Commit-bound, full regen:** changing the generation rollout changes every sidecar's play-logic
  identity — all profiles must be regenerated on the new commit (see Rule 0 in
  `.claude/skills/mulligan-profile.md`). Do it as a deliberate re-baseline, not piecemeal.
- **Validate with the confounded A/B** (`MTG_CONFOUND_BOTTOM`, `keepmodel_exhaustive_ab.sh`): confirm the
  NC-generated table ≥ the clairvoyant one *in non-clairvoyant play*, and spot-check that fetch/Ponder-
  heavy hands actually get de-prioritised (the motivating symptom).
- **May be small:** clairvoyance isolation suggested the keep gain is genuine and modest; the fetch/Ponder
  over-valuation could be a second-order effect. Measure before committing the regen hours.

## Relationship to current state

The runtime bottomer already has the blind correction available (`MTG_NC_BLIND_BOTTOM`, default off). The
gap is (a) **generation** is clairvoyant, and (b) the **keep** decision has no blind analog. This doc is
about closing those two for a cleaner non-clairvoyant mulligan policy.
