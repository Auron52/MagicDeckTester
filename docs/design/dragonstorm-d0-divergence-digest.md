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

5. **Cost-test on the blind d0 LP** — the clairvoyant d5 search is only a *hypothesis generator*: it is
   messy (it takes suboptimal lines it knows it won't be punished for because it can see the draws), so
   its suggested "holds" can't all be replicated by a blind player. The clean **validator** is the blind
   d0 loss-penalized avg-win-turn: build the hook behind a temporary env gate, measure d0 LP on/off over
   train seeds. This is self-correcting — it rejected the Lotus guard (NO-OP) and confirmed the go-off
   recognizer (−0.33). d0 must not get materially more expensive (it runs in every rollout); prefer
   integer math with no `GameState` copy (`GoOffSim` is pure integer recursion).

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

- **Slow-dragon rule** (confirmed ~14% of divergences): greedy burns rituals to hard-cast a *fair*
  dragon while the search holds/develops, because the ritual-payoff guard (`SubsetWastesAccelerant`,
  `TurnSolver.cpp`) counts ANY creature as a storm payoff. Candidate: a fair hard-cast dragon must NOT
  justify rituals (only Dragonstorm/Apex do). Cost-test on d0 LP per step 5.
- **Non-clairvoyant reference (blocked)** — a blind-search reference would be a *cleaner* hypothesis
  generator than the clairvoyant d5 (step 5). The attempt to get one via `MTG_SHUFFLE_SALT_SEARCH=N`
  was inconclusive: the salt gave LP identical to clairvoyant in the measured config (it did not
  decouple the main d5 search there). Needs the correct rollout-path plumbing
  (`TurnSolver.cpp` rollout entry points) or a reshuffle-averaged search before it can answer "how does
  blind play differ."
- **Codify into `analyze-deck`** — once 2–3 rules are proven by this loop (go-off = #1), add a
  "rollout-quality diagnosis" step to the analyze-deck workflow that runs the divergence log, digests
  it, and surfaces the top cost-weighted patterns as candidate provider hooks for review.
