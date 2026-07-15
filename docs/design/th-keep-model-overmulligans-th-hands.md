# TH keep model mildly over-mulligans Treasure-Hunt hands (refinement candidate)

**Status:** deferred refinement (not a bug, not a blocker). The mm6 TH exhaustive keep
profile is adopted and net-positive (avg9 −0.15 vs the prior profile, 8/8 seeds); this
note records a characterized soft spot for a future TH regen.

## Observation

Inspecting the adopted TH exhaustive keep table (`decks/treasure_hunt/*.keepmodel.exhaustive*`,
mm6, R=41, `MTG_EQUIV_DEPTH=5`), the model mulligans many hands that contain **Treasure Hunt**:

- Of 21,716 TH-containing size-7 comps, ~20% (4,307) have `keep[0]=0` (mull the opener).
- 97% of those are 5–6 land "lands + Treasure Hunt, no payoff" hands; it even mulls every one
  of the 145 ≤4-land TH hands (e.g. "3 lands + 4 Treasure Hunt").
- Keep rate by dig engine: **has TH 80% / Throes-only 25% / no dig 0%** (never keeps a no-dig
  hand — correct). Throes-keeps favor an accel/storage land (Sandstone Needle/Saprazzan Skerry)
  2494:118 — the model already encodes "Throes is slow without acceleration."

The user's expectation: with **only 7 nonlands in the 60-card deck** (4 Treasure Hunt, 2 Land's
Edge, 1 Throes), lands are the win-condition *fuel* (discarded to Land's Edge for 2 each), not
flood. A single Treasure Hunt into a near-all-land library draws a huge pile of lands → Land's
Edge burns them out. So a hand with a few lands + a dig spell should almost always be a keep.

## Why it is NOT a bug

- **Treasure Hunt is modeled correctly** (`EffectHandler::ResolveDrawUntilNonland`): reveals
  from the top until a nonland *or the library empties*, puts all revealed (incl. the nonland)
  into hand via a raw library-pop — **not** the game "draw" action — so emptying the library does
  **not** deck you (the `while(!library.empty())` loop just ends). Decking only happens on the
  next draw step from an empty library, which IS modeled as a loss (CR 104.3c, `Library.h` /
  `EffectHandler.cpp:361`). So multi-TH hands are not mis-scored by a phantom deck-out.
- The keep decision is a backward-induction over the hypergeometric hand distribution
  (`ComputeKeepPolicy` in `ExhaustiveKeep.cpp`): keep iff the hand's keep-value ≤ the *expected*
  value of mulliganing (averaged over all fresh hands). NB the raw sidecar's two per-hand numbers
  are `V[on_the_draw, on_the_play]` — BOTH keep-values — not keep-vs-mull; the mull-value is a
  distribution average, so you cannot read the decision off a single hand's two arms.
- The mulled TH hands have keep-value ≈ **4.1** (≈ deck average, a turn-4 goldfish). They mull
  because the average fresh hand scores marginally lower — genuine **near-ties**, not blunders.

## The refinement hypothesis

The keep-value is produced by the keep-rollout at **depth 5 / 20 ms** (`ExhaustiveKeepConfig`).
A shallow rollout may not find the optimal single-Treasure-Hunt → Land's Edge → discard-lands
burn sequence, so it likely **underrates the true ceiling** of "lands + TH" hands (nearer turn 3
with perfect play) → a mild, systematic over-mulligan of castable Treasure-Hunt hands.

## Candidate fixes (for a future TH regen, on a frozen commit)

1. **Regenerate TH at higher `MTG_EQUIV_DEPTH`** (e.g. 7–8) and compare the keep table + the
   NEW-vs-OLD avg9 A/B (`test/mm6_newvsold_ab.sh`). If deeper rollouts raise the keep rate on
   "has TH" hands AND improve avg9, the over-mulligan was a depth artifact.
2. **Keep-floor for "has TH" hands**: force `keep[0]=1` whenever the hand has ≥1 land and ≥1
   Treasure Hunt (castable), then A/B vs the current table. Cheap to test; if win-or-tie, it
   confirms the hands are keeps and gives a simple correction without a full regen.
3. **Decisive single-hand probe**: `mtg <deck> --profile <prof> --eval-hand "Island;Island;Treasure Hunt;..."`
   at depth 5 vs a higher depth to see whether the keep-value drops (better) with more search.

## Verification method used (for reproducibility)

Recover the OLD profile from the parent commit, run a game's `gi` under OLD vs NEW via
`--profile <exh> MTG_EXHAUSTIVE_PROFILE=none --seed base+gi --games 1 --log-dir`, and read
`mulliganSequence` (kept `attempt` / effective hand size). This is drift-proof; the inline
`explain_game` diff is blind to a profile-only change (old profile off disk).
