# Mana-source reservation ("leaving sources up")

Handoff for the follow-on to the shipped **scarcity-first tap ordering**. Self-contained:
everything needed is below or in the referenced code — no external notes required.

## Background: what already shipped

The greedy mana solver pays each pip from the **least-flexible qualifying source**, so
flexible sources stay untapped and the exponential `TapForCostBacktrack` fallback rarely
fires.

- Ranking: `GenericProvider::ManaSourceRank` — [`src/ai/DecisionProviders.cpp`](../../src/ai/DecisionProviders.cpp)
  (Hook 24, declared in [`src/ai/DecisionProvider.h`](../../src/ai/DecisionProvider.h); per-deck
  overridable). Ranks, spend-lowest-first: fixed/bounce 10, dual 20, filter/ramp-filter 25,
  tri 30, rainbow/any 50, **{C}-only manland (Mutavault) 60 = saved to attack**.
- **The two greedies MUST stay lockstep:** the scarcity selection is duplicated in
  `AIEngine::TapForCost` ([`src/ai/AIEngine.cpp`](../../src/ai/AIEngine.cpp)) and
  `TurnSolver::TapForCostDirect` ([`src/ai/TurnSolver.cpp`](../../src/ai/TurnSolver.cpp)).
  Change one, change both, or the rollout and the executor desync (a divergence bug).
- Toggles: `MTG_TAP_LEGACY` reverts to battlefield-array order (A/B lever); `MTG_TAP_STATS`
  counts top-level `TapForCostBacktrack` entries (backtracker pressure); `MTG_UNPRUNED`
  (`DecisionUnpruned()`) is the standing unpruned-vs-pruned audit switch.
- Completeness stays **by construction**: `TapForCostBacktrack`
  ([`src/core/SpellEffects.h`](../../src/core/SpellEffects.h)) remains the complete fallback,
  so the heuristic only ever picks *which* legal payment, never *whether* one exists.

Commits: `8cfebf1` lossless backtracker micro-opts · `2c98c88` scarcity default-on + GT ·
`efd5758` overnight GT.

## The problem this doc is about

Scarcity ordering decides *which legal payment to make*. But the mana decision with real
search value is a different one: **which sources to leave UNTAPPED.** And the search
cannot currently see it, because **tapping is not a search branch** — the planner
enumerates cast *plans* and delegates tapping to the greedy. Consequences:

- The search **does** recover cast-*sequence* value at depth (a different order of the same
  casts is a plan it can explore).
- The search **cannot** recover pure **reservation** value: leaving a dork untapped to
  attack, holding an Island for next phase's Ponder, or not spending a depletion counter.
  No amount of depth finds these, because the choice never becomes a branch.

## Scope — bounded, NOT the exponential tap search

Reservation only matters for a **few special sources**. Do not turn general tapping into a
search space; enumerate hold-vs-tap only for:

- a mana **dork** (hold to attack),
- a `can_animate` **manland** (hold to attack — already why Mutavault ranks 60),
- a **depletion** land (hold to avoid wasting a counter).

Two drivers, treated differently:

1. **Hold-for-future-cast** — clairvoyance-driven and deterministic (e.g. keep {U} up for a
   Ponder next phase). This is also the real fix for the Treasure-Hunt d0
   sequential-execution strand.
2. **Hold-to-attack** — a combat-value judgment.

## Recommended plan (oracle-first)

Add hold-vs-tap **branches under `MTG_UNPRUNED`** — 2^k over the k≈0–3 special sources on
board — as an audit, *before* writing any heuristic. That reveals, per deck, whether
reserving actually wins. Then build a per-deck provider hold-hook **only where the audit
shows it pays**. The rollout machinery already values an untapped dork's attack / reserved
mana; the only new machinery is enumerating the reserve subset.

This is also the concrete answer to "use `MTG_UNPRUNED` to audit tapping decisions."

## Hard constraints (do not violate)

- **`MTG_UNPRUNED` must NOT fall back to the default heuristic.** Unpruned means genuinely
  explore the reservation branches — falling back to the heuristic defeats its whole purpose.
- **Keep `TapForCostBacktrack` as the complete fallback.** Completeness stays by construction.
- **Search-primary:** heuristics only prune; keep the unpruned-vs-pruned A/B byte-identical
  when the heuristic is off.

## Gotchas / learnings

- **Tapping is irreducibly a decision (Mode A).** The perf/behaviour effect *is* the
  different resource state; you cannot "canonicalize a tapping choice to be lossless."
- **Manland ranking is subtle.** Rank **{C}-only** manlands last (reserve to attack);
  **colored/dual** manlands and **depletion** lands rank *by color* and are deliberately NOT
  reserved by default — they are mana you normally spend. An early version reserved *all*
  manlands and regressed Slivers via forfeited Mutavault attacks.
- The current regression suite barely exercises reservation (the only mana-creature in it is
  Hinata's Ornithopter — 0 power, rainbow, always held), so it "doesn't bite" today.
  **Validate on decks where a held dork/manland attack or an unspent depletion counter
  actually changes the outcome.**

## Validation

- `MTG_UNPRUNED` off must stay **byte-identical** (the guarantee that the heuristic only
  prunes).
- Use `MTG_TAP_STATS` to watch backtracker pressure.
- Run `test/regression.sh` (smoke → regression → overnight) and use the `--accept` flow with
  a **per-game audit** — never hand-edit ground truth.

## One nearby change, so the code doesn't surprise you

`9229b25` added a branch-and-bound **total-mana gate inside `TapForCostBacktrack`**
(`MTG_NO_MAXMANA_GATE` disables it; lossless, ~15–19× faster on Hinata). That is about
*pruning the backtracker's search*, not tap ordering — but it is why the backtracker looks
different from the `8cfebf1`-era code.
