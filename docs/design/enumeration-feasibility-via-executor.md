# Enumeration feasibility via the real executor (kill the per-deck cost/legality patching treadmill)

**Status:** DESIGN (2026-07-23, user-driven). Not implemented. Written for review before touching the
hot enumeration path. Supersedes the "increment 2(b)" sketch in
[sequential-plan-evaluation.md](sequential-plan-evaluation.md) with a more general framing.

## The problem: a per-deck patching treadmill that does not scale

The main-phase enumerator (`EnumeratePlans` / `Solve::consider` in `src/ai/TurnSolver.cpp`) decides
**which candidate plans (action sets) to offer** the search. To decide whether a plan is *affordable*, it
uses an **order-free scalar aggregate**: `subset_cost − discounts − ritual_float ≤ budget`, where the
discount/float terms are:

- `BuildPool` (TurnSolver.cpp:256) — ritual float + retained over-production credited to the budget.
- `SameTurnReducerDiscount` (~429) — a same-turn Ruby Medallion crediting the rest of the subset.
- `SameTurnAffinityGenericCredit` (389–425) — a same-turn affinity card (Thrumming Hivepool) crediting
  its per-Sliver generic reduction, counting only creatures cast *before* it (lower `CastOrderRank`).

Each of these is a **per-symptom hand-patch** that exists because the plain aggregate was *dropping
affordable lines*. Legality has the same shape: the frozen start-of-phase snapshot plus the increment-2(a)
aura widening (`aura_enchant_requires` enabler-first sequencing) is a per-mechanic patch.

**The cost the user actually cares about (deck 8 of a 100+ backlog):** every new deck whose cards interact
with cost or legality in a new way risks *another* patch — agent + user time, per deck, for arguable
benefit. Measuring "no divergence today" does **not** prove this scales; it just confirms the patches we
already paid for stuck. The goal is to **remove the treadmill**, not to measure its output.

## The lever: we already own a correct, deck-agnostic per-step evaluator

The executor — `apply_one` / `TapForCostDirect` — casts one spell at a time against a running `GameState`,
computing each cost from the *then-current* board (so same-turn reducer/affinity/ritual-float/count-scaling
effects are seen as they actually resolve), paying via greedy → backtrack → filter-retry → atomic rollback
with mana-source reservation, and checking target legality at cast time. And the load-bearing fact
established earlier: **`ApplyPlan == ApplyPlanDirect == the same `apply_one` the search scores with`.** So
when the search *scores* a plan, it already runs correct per-step feasibility.

The gap is **only** at *enumeration*: the approximate aggregate decides which plans exist *before* scoring.
Two directions, asymmetric:

- **Over-credit** (offer an infeasible line): harmless in the *search* — the rollout applies it, the
  unpayable cast strands (`apply_one` no-ops it), and the line scores worse → discarded.
- **Under-credit** (drop a feasible line): the real gap — a dropped line is never enumerated → never
  scored → unreachable. This is the per-deck patches' whole reason for existing (keep the aggregate from
  under-crediting).

So the treadmill is the maintenance of an *approximation* whose only job is to agree with an evaluator we
**already have exactly**.

## Design: demote the aggregate; gate feasibility with the executor

### Layer 1 — cheap, deliberately *over*-optimistic pre-filter (never drops a feasible line)

Replace the accurate per-deck credit terms with a **loose upper bound on same-turn discount + float** (credit
the maximum conceivable reduction). Being loose, it **over**-credits → it can never drop a feasible line.
The finicky per-deck terms (`SameTurnReducerDiscount`, `SameTurnAffinityGenericCredit`, and future ones)
are **deleted** — they no longer need to be *accurate*, only non-dropping. This layer is O(1) per subset and
exists purely to kill obviously-hopeless subsets fast.

### Layer 2 — one authoritative, deck-agnostic feasibility gate (the real executor)

On the survivors that contain **≥1 interacting cast** (reducer / ritual / affinity / enabler / restricted
target — the cases a scalar cannot decide; trivial 0–1-cast plans skip this entirely), run a
**scratch-state plan-apply**: apply the plan on a *copy* of the state via the executor's real sequencing
(`CastOrderRank` canonical order + `TapForCostDirect` + reservation + target legality), and keep the plan
**iff every intended cast resolves (nothing strands).** Stamp the resolved order onto the plan
(`searched_order`) so `ApplyPlan` replays exactly what enumeration approved (no enumerate/execute mismatch).

Because layer 2 *is* the real executor, it handles reducer discounts, affinity, ritual float, count-scaling
mana, and target-legality restrictions **by construction** — a novel interaction on deck #47 needs **zero
new enumeration code** (only the card's params/effect, which the executor already consumes). The aura
legality widening (2a) is **subsumed**: legality is just another per-step check the executor performs.

### Why layer 2 is required (not just layer 1)

It is tempting to ship layer 1 alone and let the *rollout* strand-score infeasibility. That is unsafe for
the **d0 greedy leaf** (`Solve::consider`), which commits a plan with **no rollout to validate it** — the
code already refuses to apply the reducer credit there for exactly this reason (see the NOTE at
~TurnSolver.cpp:2188: crediting an unrealisable Medallion+Apex line let the greedy commit a line the
executor then stranded on, smoke gi523 8→loss). A blanket over-optimistic pre-filter would re-open that
hole. So d0 needs an **exact** feasibility answer → layer 2. Using layer 2 everywhere (d0 leaf *and*
search branch list) is the clean unification: exact where d0 needs it, and in the search it also prunes
infeasible over-credited lines *before* their wasted rollout.

## Cost / risk (the "without significant costs" question)

- **Perf.** Layer 2 is **one plan-apply (O(k) casts) on a scratch state — not a rollout** — run only on
  interacting survivors, and it *gates* the far-more-expensive rollout. In the search it can be
  perf-**neutral-or-better** (it removes infeasible lines that today get rolled out to a loss). The real
  risk is the **d0 leaf**, evaluated at every search node: an O(k) scratch-apply per interacting leaf. Must
  be **measured** (regression makespan + per-deck node counts), gated on interaction so non-interacting
  leaves keep the O(1) path, and — if hot — memoised by plan signature within a decision.
- **GT.** Affecting only in the **recover** direction (feasible lines the aggregate under-credited become
  reachable → faster lines). Infeasible drops were scored out anyway → no change. → rebaseline affected
  decks + `MTG_LEGACY_*` gate for byte-identical A/B, per repo convention.
- **Correctness residual.** The canonical-order apply can strand where a *non-canonical* order would not
  (reducer-slot, multiple reducers, colour-specific float). **`MTG_ORDER_ORACLE`** (full-permutation apply,
  offline) is the guard rail: A/B it vs the canonical-order gate; any flagged game names the residual, and
  the fix is a **bounded, measured** extension of the canonical order's searched slots (the reducer-insertion
  scan) — not a per-deck patch.
- **Net LOC.** Plausibly **negative** — the per-deck credit patches are deleted.

## The scratch-apply feasibility primitive (new)

A function `PlanFeasibleOnScratch(state, plan) -> {feasible, order}` that:

1. Copies `state`.
2. Applies the plan via the executor path (the same `apply_one` sequence `ApplyPlanDirect` uses), in
   `CastOrderRank` order, running real payment + reservation + target legality + effect resolution (a
   ritual's float, an ETB that enables a later restriction, an affinity discount all materialise as they
   would in play).
3. Returns `feasible = (every intended CastFromHand actually resolved)` and the realised cast order.

This is nearly `ApplyPlanDirect`, instrumented to report strands and run on a throwaway copy. Re-solve
breakpoints (draw/cascade) keep the existing behaviour (plan/breakpoint order, search owns the ambiguous
ordering) — the gate checks feasibility in the executor's order, exactly as scoring already does.

## Rollout plan

1. **Doc review** (this file) — agree the approach + the perf gates.
2. `PlanFeasibleOnScratch` + `MTG_FEASIBILITY_GATE` (default off) — build, byte-identical when off.
3. Turn it on behind the flag; **measure** regression makespan + node counts (perf gate) and the
   recover-direction GT delta (audit per-game before any accept).
4. `MTG_ORDER_ORACLE` A/B — confirm the canonical-order gate misses no residual; extend searched slots
   only where the oracle flags a game.
5. Delete `SameTurnReducerDiscount` / `SameTurnAffinityGenericCredit`; loosen the pre-filter. Re-measure.
6. Flip default-on + `MTG_LEGACY_*` hatch; rebaseline all modes; accept per the regression flow.

The win condition: onboarding decks 9…100+ needs **no** new cost/legality enumeration patch — the executor
already knows the rules.
