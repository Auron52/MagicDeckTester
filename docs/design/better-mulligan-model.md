# A mulligan model that can actually beat static

Goal: a keep decision at least as good as a strong human — better on the **marginal hands** where
humans slip and where the static keep is provably wrong. "Ties static in aggregate" is NOT the
bar; static is a weak floor, and accepting "we can't beat it" means accepting a model that keeps
hands a human would mulligan.

## Why static and every model so far are inadequate

The static keep is an **additive sum of per-card scores** (clamped ≥ 0) plus land/curve gates. Two
structural failures, both demonstrated on slivers:

- **It cannot penalize redundancy.** The profile *learned* that a 2nd Aether Vial is bad
  (2nd-copy marginal −0.3423), but the runtime **clamps negatives to 0**
  ([`AIEngine.cpp`](../../src/ai/AIEngine.cpp), "selection-bias artifacts"), so **4 Vials score the
  same as 1**. Concretely, `Unclaimed Territory + Ancient Ziggurat + 4×Aether Vial + one lord` is a
  **keep** (curve passes on the MV1 Vials; score = 0.052 + 0.167 ≈ 0.22 ≥ 0.195) — a hand a human
  instantly mulligans.
- **It cannot see composition.** An additive sum can't express "these cards are only good together"
  or "this hand is all dead cards + one threat."

The learned models (regret-tree / score / hybrid) didn't fix this because they trained on only
**aggregate features** (`key_piece_count`, subtype density, per-CMC counts) — **no per-card
identity** — and were **selected by held-out regret**, which
[does not transfer to in-game win-turn](keep-model-selection-by-runner.md). So they made the *same*
class of error as static → the A/B washed out → we mis-read "wash" as "static is good." It isn't;
both were equally blind on the hard hands.

## The plan

1. **Representation — per-card identity/redundancy. [DONE]** New `FeatureKind::CardCount` (`s` = card
   name → # copies in hand), emitted per distinct deck card in `BuildCandidateSpecs`. A model can now
   split on `count_Aether Vial >= 2`. Byte-neutral until a fit selects it (smoke 18/18). This is the
   input the aggregate features lacked.
2. **Clean labels.** Fit at `MTG_KEEP_ROLLOUTS >= 8` — single-rollout (R=1, the default all prior
   models used) label noise swamps the redundancy signal, so the fit won't pick `count_` splits from
   noise. R=8 de-noises (held-out regret drops ~50–75%).
3. **Capacity (if needed).** If a single greedy CART can't capture the interactions with per-card
   features, move to a boosted tree ensemble (extends the existing CART; a path = a conjunction of
   card conditions) — keep it interpretable (disclosure bar).
4. **Evaluation — sensitive + honest.** Judge on the hands where policies **disagree** / near the
   keep-mull boundary (aggregate win-turn over all hands is too blunt — rare bad keeps barely move it,
   and resilient decks recover), and **select in-game** via `test/keepmodel_select.sh` (real runner),
   never held-out regret.
5. **Acceptance check.** Confirm the model now **mulligans the hands static wrongly keeps** (the
   4-Vial / no-lord-all-dead cases), not just that an aggregate number moved.

## Status

- Phase 1 shipped (per-card `CardCount` features). Phases 2–5 open.
- Note: the label (`kv` rollout) already captures redundancy + composition — it plays the hand out.
  The whole effort is giving the *model* the inputs + capacity to fit that label, and *selecting* by
  the real objective. See also [keep-model-selection-by-runner.md](keep-model-selection-by-runner.md).
