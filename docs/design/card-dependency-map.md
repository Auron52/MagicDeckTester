# Card dependency map — analysis-derived, driving classification AND cast order

**USER doctrine (2026-08-15):** the doubt-flip residual (gi=28 / gi=149,
`midphase-ordering-audit.md`) "is entirely a dependency problem. If we can map those out and
preferably create them properly during the analysis process it should help with this problem
and cast order. These two problems are very very similar." Earlier form (session 5g): "bake in
the dependencies between cards (which needs to be part of the ordering anyway)".

Also USER: **unbounded should be 0** — an unbounded-budget search with correct emission never
loses to base. The doubt gap measured +0.067 at budget 10 / 40 / 1,000,000, so the unbounded
gap IS the blocked-line meter. Acceptance bar for this work: unbounded doubt gap → ~0 on the
antilife 300g apparatus; only then do bounded budgets measure practical cost.

## The insight

A card's main-phase class and its cast position are not intrinsic properties — both are
consequences of the same dependency graph. The m1/m2 boundary used to carry these semantics
implicitly (enabler cast in m1 is live by m2); the collapse must carry them explicitly.

Worked example (antilife): Invigorate is Main1 (pump). Its alt cost makes the opponent gain
life → it depends on a lifegain→loss enabler → **Tainted Remedy is pulled to Main1** (an
enabler must be considerable in the phase of its payloads). Aria of Flame's ETB ("opponent
gains 10") is itself a Remedy payload, and its verse trigger is a payoff for instant/sorcery
casts → **Aria is pulled to Main1** (payoff resolves before the casts that feed it). That
closure alone fixes gi=149 (Aria castable off the exalted Hierarchs BEFORE they attack —
no creature-mana special case needed) and orders gi=28's T3 line correctly (Aria before
Silence; Silence last).

## Edge types (derived mechanically from CardParams — no per-deck hand code)

1. **ENABLES (A → B):** `A.lifegain_to_loss` and B gives the opponent life
   (`alt_lifegain_cost > 0`, `opponent_lifegain > 0`, `etb_opponent_lifegain > 0`,
   `tap_opponent_lifegain > 0` (Grove), `controller_lifegain_equals_power` (Swords)).
   B's payload pays off only if A resolved first (or is live).
2. **CAST-PAYOFF (A ⇐ casts of class C):** `A.verse_damage` ⇒ A benefits from every
   instant/sorcery cast AFTER it resolves. (Prowess is the board-resident sibling, already
   handled as the pump/attack-feeding class.) A wants to precede C-class casts.
3. **DESTROYS / CONFLICTS (A ✗ B):** `A.destroy_all_enchantments` kills enchantment enablers
   and payoffs (Remedy, Aria). A orders LAST within a subset, and a subset containing A is
   valid only with a surviving (creature) enabler or subset-level lethality.

## Consumers

* **Phase classification closure (`ClassifyMainPhase`):** start from the explicit classes
  (attack-helping → Main1, damage templates → Main2, ...). Propagate to fixpoint:
  - if any payload B of enabler A classifies Main1/Both → A pulls to Main1 (or Both);
  - if cast-payoff A's trigger class C has any Main1 member → A pulls to Main1;
  - a DESTROYS card never pulls anything forward.
  The graph is per-deck and tiny; the fixpoint is trivial.
* **Cast order:** generalized enabler-first = topological order on ENABLES edges (Remedy
  before Invigorate-alt / Fiery Justice / Aria-ETB), CAST-PAYOFF nodes before their trigger
  casts (Aria before instants/sorceries), DESTROYS last. This subsumes the per-provider
  `CastEnablerFirst` / `CastOrderRank` hand rules for these classes and is exactly the
  "dependencies are part of the ordering anyway" doctrine.
* **Subset validity / emission:** a payload is backed iff its enabler is live or in-subset
  (exists: `SubsetHasUnbackedAltPayload`); still needed from the audit: the wipe-lethality
  test must be SUBSET-level (sum of in-subset converted damage + attack), not per-card.
  **The tight provider gate (`ShouldEmitRiskyAltPayload`, built to protect the greedy m2 —
  gi=36/84): USER 2026-08-15 — try DROPPING it outright first** (one emission rule for both
  consumers; the greedy m2 is slated for removal anyway, so a heuristic protecting it is
  polish on a path to delete). Be careful: measure the greedy-exposed configs (d0 + rollout
  leaves) for re-bricking; ONLY if dropping causes real issues, FIX the heuristic (give it
  the subset-level lethality) rather than resurrect the stale per-card gate.

## Where derived

At deck load (provider/CardDatabase init), from params — automatically correct for new decks.
The **analyzer** (`scripts/analyze_deck.py` / the analyze-deck skill flow) should PRINT the
derived map as part of coverage/review output so a human sees the edges the engine inferred
(a missing edge is an implementation-review item, same as a coverage gap).

## Status

Design agreed with USER 2026-08-15. Not yet implemented. Exemplar repros:
gi=28 `--seed 2030 --game-index 28`, gi=149 `--seed 2151 --game-index 149`, antilife d3
budget 10, doubt stack (`MTG_PHASE_CLASSIFY=1 MTG_SEARCH_SECOND_MAIN=1 MTG_MAIN2_DROP=1
MTG_ACQ_RESOLVE=1 MTG_BP_SITES=63 MTG_DOUBT_MAIN2=1`); targets 4→3 and 5→4 respectively,
then unbounded 300g gap → ~0.
