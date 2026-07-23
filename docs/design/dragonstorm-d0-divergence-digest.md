# Divergence digest: an automated loop for finding d0/rollout-quality fixes

**Status: method proven 2026-07-23 (worked example #1 shipped).** This documents the repeatable,
mostly-automated loop we used to find and validate the Dragonstorm go-off recognizer
(`dragonstorm-goff-lethal-recognition.md`), stated generally so it can be applied to any rollout-based
deck. It is the concrete answer to the standing ask: *"an automated approach to fix d0/rollout quality"*
that is neither full hand-authoring nor a learned model (both previously explored and set aside — see
`learned-d0-policy.md` and the memory notes on the `learned-d0-evaluator` / `d0-dynamic-model` branches).

## Why d0 quality matters at every depth

`d0` is the **rollout policy**: the search finishes each leaf greedily with `TurnSolver::Solve` →
`PlayOut`. A weak d0 therefore poisons the evaluation of *every* leaf, so the whole search inherits its
blind spots. For a rollout-only deck (no `value_leaf`/`value_play` model in its profile), d0 IS the
evaluator. Dragonstorm's d0 loss-penalized avg-win-turn was ~7.13 vs the d5 search's ~4.79 — a ~2.3-turn
gap that no amount of depth closed, because the leaves themselves couldn't see the win.

## The loop

1. **Instrument the divergence** — `MTG_DIVERGENCE_LOG=<file>` (`src/ai/AIEngine.cpp`, gated on the env
   var; inert and byte-identical when unset). On the real search-driven path, at each pre-combat main
   decision it ALSO computes the greedy d0 plan (`TurnSolver::Solve`) for the SAME untouched state and
   appends one JSONL record: `{seed, turn, diverge, search_land, search, greedy, feat[]}`. `diverge=1`
   marks a state where the greedy rollout would play a *different* line than the (better-informed)
   search — i.e. a state the rollout misplays. `feat[]` is the non-clairvoyant `ExtractMidGameFeatures`
   vector, for clustering. Run single-threaded for readable output.

2. **Digest** — cluster the divergences (`logs/divergence/digest.py`): divergence rate, by-turn, cards
   the search casts that greedy never does (and vice-versa), and the feature shape of divergent states.
   This turns thousands of records into a handful of candidate rule-shaped patterns.

3. **KEY LESSON — count ≠ cost.** The raw divergence *count* overstates the opportunity: many
   divergences are goldfish-indifferent (the search wastes a free ritual it won't be punished for; two
   lethal lines that both kill on the same turn). The count digest once said "greedy wastes Lotus Bloom
   504×"; extending the payoff guard to cover it was a **measured NO-OP on d0** (7.142 = 7.142 — the
   plays were free suspends, not cracks). **Always weight a candidate by its win-turn impact, and
   validate on the blind metric below, never on the count.**

4. **Hypothesize a provider hook** — map the surviving pattern to one of the deck's
   `DecisionProvider` hooks: `ExtraLethalDamage`/`HasExtraLethalModel` (win-now recognition — the
   go-off), `ArchetypeCardValue` (board-dev valuation), `CastOrderRank` (sequencing), etc. This keeps
   the fix archetype-scoped, never in the shared `GenericProvider`.

5. **Cost-test on the blind d0 LP — AND the shipped-config LP.** The clairvoyant d5 search is only a
   *hypothesis generator*: it is messy (it takes suboptimal lines it knows it won't be punished for
   because it can see the draws), so its suggested "holds" can't all be replicated by a blind player. The
   blind d0 loss-penalized avg-win-turn is a cheap first **validator**: build the hook behind a temporary
   env gate, measure d0 LP on/off over train seeds. It is self-correcting — it rejected the Lotus guard
   (NO-OP) and confirmed the go-off recognizer (−0.33). d0 must not get materially more expensive (it
   runs in every rollout); prefer integer math with no `GameState` copy (`GoOffSim` is pure integer
   recursion).

   **CAVEAT (learned the hard way, 2026-07-23): blind d0 LP validates only INFORMATION-ADDING rules, not
   OPTION-PRUNING ones — always confirm on the SHIPPED config too.** The rollout policy's real job is
   *faithful simulation of a plausible continuation*, not optimal standalone play. An information-adding
   rule (the go-off recognizer: recognize a lethal the leaf would actually execute) is monotone — it
   helps the leaf both as a standalone player and as a search evaluator, so d0 LP and search move
   together. An option-pruning rule can *split* them: the **slow-dragon guard** (don't let a fair
   hard-cast Dragon justify a ritual; hold it for the storm) improved blind d0 LP by ~−0.73 turns but
   WORSENED the shipped d5-value search by ~+0.37 and d3 by ~+0.36 — a clean reject. Why: a real d0 game
   runs many turns and eventually draws Dragonstorm and spends the saved ritual, so holding helps; but a
   blind, short-horizon rollout LEAF that holds the ritual just **durdles** (it can't foresee the future
   Dragonstorm draw), never reaches the storm, and returns worse/flatter leaf win-turns — so the search
   reads the pruned leaves as worse and picks worse lines. The takeaway for the loop: a candidate that
   only *removes* a greedy option must be measured on the shipped search config, because "better blind
   d0 play" ≠ "better rollout evaluator." Rules that *add lethality/board information* are the safe,
   monotone class; prefer them.

   **How to RESCUE an option-prune (worked example #2, the storm-hold rule): gate the prune on the held
   resource's payoff being VISIBLE.** The slow-dragon prune durdled because a blind leaf held a ritual
   for a storm it couldn't foresee. The fix (from the deck's human pilot): only hold when a storm payoff
   (Dragonstorm/Apex) is ALREADY IN HAND — then the leaf *can* see the payoff it is saving for, so it
   doesn't durdle, and the search's leaf win-turns don't degrade. Same code (a fair Dragon stops being a
   payoff in `SubsetWastesAccelerant`), now gated on `storm_in_hand`. Result: blind d0 −0.60 AND shipped
   d5 −0.005 (NEUTRAL, no regression) — adopted. The gate converts an unsafe option-prune into a safe
   one by removing the blind-durdle failure mode. General rule: an option-prune is safe when the leaf can
   *observe* the reason for it; unsafe when it holds on faith. Apply it only to the greedy/rollout policy
   (Solve's `consider`), not the search's root branch list, so the search can still arbitrate the pruned
   line at the root.

6. **Validate + adopt** — sweep the regression suite (train seeds) for the searched-depth deltas,
   confirm no other deck moves and zero searched-depth slowdowns, then adopt default-on with an
   off-switch and rebaseline GT (per the regression-testing + heuristic-optimization skills). Report the
   decision to the user before adopting.

## Worked example #1 — the go-off recognizer

The digest showed the search STORMS OFF (rituals → Dragonstorm → N dragons → Scourge ETB pings) at
states where greedy casts Dragonstorm late as a board-dev spell. Root cause: `DragonstormProvider` had
no `ExtraLethalDamage` override, so `GenericProvider` returned 0 and the greedy/rollout `wins` check
never saw the go-off (Dragonstorm the spell has `direct_damage == 0`; its damage is the fetched dragons'
later ETB). The hook (`GoOffSim`, mirroring `SpellEffects.h OnDragonEnters`) projects that burst so
`wins` fires for real go-offs. Blind d0 LP: 7.14 → 6.81. Adopted. Full detail:
`dragonstorm-goff-lethal-recognition.md`.

## Open threads (next applications of the loop)

- **Slow-dragon rule → storm-hold rule (REJECTED then RESCUED + ADOPTED 2026-07-23)**: the
  *unconditional* slow-dragon prune (a fair Dragon never justifies a ritual) improved blind d0 ~−0.73t
  but worsened the shipped d5 search ~+0.37t — an option-prune that durdled (step-5 CAVEAT). Gating it on
  a storm payoff being IN HAND (`storm_in_hand`) removed the durdle: blind d0 −0.60, shipped d5 −0.005
  (neutral). Adopted default-on (off-switch `MTG_NO_STORM_HOLD`), regression + smoke rebaselined,
  containment held (Dragonstorm-only, no other deck moved). This is worked example #2 — how to make an
  option-prune safe.
- **Storm-hold reachability refinement (REJECTED 2026-07-23, worked example #3 — a CORRECT human
  heuristic the engine doesn't reward)**: the pilot noted the unconditional storm-hold over-holds — with
  a storm in hand but mana-light, a human deploys an early dragon instead, *if* the dragon is >=3 turns
  earlier than the storm turn (2 turns early "probably not," 1 turn "certainly not"). We built a faithful
  turns-to-storm estimator (board lands at real yield incl. Sandstone=2 / storage-counter ramp, hand
  lands one-drop-per-turn, ritual burst, Lotus timing incl. suspend arrival, Medallion discount) and held
  only when the storm was <=2 turns out. **Result: shipped d5 IDENTICAL to unconditional (4.8533/4.6367,
  both seeds) and blind d0 slightly WORSE (+0.003..+0.04).** Reverted. Why the (correct) heuristic doesn't
  help the engine: (1) *the d5 search already makes this call* — it rollout-scores every land/dragon/hold
  plan, so when an early dragon genuinely beats waiting it already finds that line; encoding it in greedy
  changes nothing the search does (d5 byte-identical proves the search fully absorbs the greedy change).
  (2) *the blind rollout can't capitalize on an early dragon* — it plays the rest greedily and can't
  leverage the early board into a faster storm, so "deploy more" just re-introduces ritual waste → d0
  slightly worse. **The sharpened lesson: the rollout policy benefits from anti-WASTE rules (hold rituals
  = stop wasting) but NOT from smart-FOLLOW-UP rules (deploy early + capitalize) — the latter needs
  lookahead the rollout doesn't have, and the search already covers it. A heuristic that is correct for
  optimal play still fails adoption when the decision lives inside the search's horizon.**
- **Non-clairvoyant reference (blocked)** — a blind-search reference would be a *cleaner* hypothesis
  generator than the clairvoyant d5 (step 5). The attempt to get one via `MTG_SHUFFLE_SALT_SEARCH=N`
  was inconclusive: the salt gave LP identical to clairvoyant in the measured config (it did not
  decouple the main d5 search there). Needs the correct rollout-path plumbing
  (`TurnSolver.cpp` rollout entry points) or a reshuffle-averaged search before it can answer "how does
  blind play differ."
- **Codify into `analyze-deck`** — DONE (Stage 5i + `scripts/rollout_divergence_digest.py`). The loop
  runs the divergence log, digests it, and surfaces candidate patterns for review.
- **Residual gap after go-off + storm-hold (analyzed 2026-07-23; d0 ~6.25 vs d5 ~4.6 ≈ 1.6t)** — a fresh
  digest (post-storm-hold binary) shows the remaining gap is now **lookahead-bound, not rule-shaped**.
  Of the go-off turns greedy under-builds: ~76% are multi-turn / optimistic-mana assembly greedy can't
  replicate without a rollout; ~24% are the **same-turn Ruby Medallion discount** — which greedy
  DELIBERATELY omits (`SameTurnReducerCredit` is `EnumeratePlans`-only; crediting a same-turn reducer in
  `Solve` stranded greedy on an unrealizable Apex line, `smoke gi523 8→loss`). An in-play Medallion IS
  credited (shared cost fn). The raw "greedy wastes" count is still dominated by Lotus Bloom (~300 free
  suspends = non-cost noise). **Conclusion: the cheap information-adding wins are taken; the remaining
  candidates are all the optimistic-same-turn-mana class that backfires in a rollout-less greedy — this
  is where the value-leaf / lookahead earns its keep, not more d0 rules.** d0 ~6.25 is the honest floor
  for a non-stranding rollout policy on this deck. (A scoped same-turn-Medallion credit COULD be
  re-tested now that the go-off recognizer + executor arbitration exist — the gi523 strand predates them
  — but it is a small slice with a documented regression risk; low priority.)
