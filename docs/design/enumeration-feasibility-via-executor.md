# Enumeration feasibility via the real executor (kill the per-deck cost/legality patching treadmill)

**Status (updated 2026-09-03): BUILT AND MEASURED** (see the reframe + experiment sections
below) — the reframe rig ships default-off (`MTG_COST_REFRAME`/`MTG_NO_COST_TRICKS`); the
successor gate is `MTG_EXEC_FEAS` (57b55bf6, 2026-08-30, default OFF). `MTG_ORDER_ORACLE` was
never built.

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

---

## Reframe (2026-07-23, MEASURED) — the leaf-apply gate is a DEAD END; the real change is much smaller

The "add a feasibility gate that applies each plan on a scratch state" design above (Layer 2 / the
`PlanFeasibleOnScratch` probe) was **built behind `MTG_FEASIBILITY_GATE` and measured. It is a dead end
for two independently fatal reasons:**

1. **Catastrophic perf — the probe was in the rollout leaf.** Placed in `Solve::consider` (the d0 greedy /
   rollout leaf), the full `ApplyPlanDirect` probe is called per-subset × per-turn × per-rollout × per-game.
   The smoke went from **143 s to wedged >270 s with only 10/21 cases done** — Dragonstorm, Hinata, and the
   d0 1000-game cases never finished. **Rule learned: NOTHING slow may live in the rollout.** (User,
   2026-07-23.)
2. **Zero value on top of the existing patches.** Every deck that *did* finish came back **byte-identical** —
   the probe found **no strands**, because the per-deck credit patches already prevent them. Measuring
   "no divergence" was measuring the *patches' output*, not the approach.

### What the measurement revealed — the change is tiny

Over-credit is **already** self-corrected everywhere by the **existing scoring apply**: `SolveWithLookahead`
applies every candidate via `ApplyPlanDirect` at 9446/9467, and this session's `MTG_AFFORD_AUDIT` proved an
over-credited infeasible line **strands on that apply** (~13 % cast-drop) and scores worse → is not picked.
So there is **no need for any new apply/probe** — not in the rollout, not anywhere. The asymmetry from the
top of this doc is the whole story: **over-credit is free-safe; only *under*-credit (dropping a feasible
line before it is scored) is a real gap.** Per-deck patches exist only to stop under-crediting.

**Refined plan (nothing added to the rollout; search-candidate-level only):**

1. **Make the enumeration pre-filter crudely OVER-optimistic** for interacting subsets (reducer / ritual /
   affinity / rock / any future cost mechanic): credit the *maximum conceivable* same-turn discount so it
   **never drops a feasible line**. It still rejects the genuinely hopeless (over-budget even at max
   discount), so candidate counts stay bounded.
2. **Delete the accurate per-deck credit patches** (`SameTurnReducerDiscount`,
   `SameTurnAffinityGenericCredit`) — the crude bound + the existing scoring apply replace them.
3. **Rely on the existing scoring apply** (9446/9467) as the real feasibility gate — it is already there,
   already runs, and already rejects over-credited infeasible lines. **No probe, nothing new in the rollout.**
4. **Lean on existing pruning** (`ManaPruneBound`, subset-reject filters, payoff-prune) to cull the extra
   optimistic candidates. The enumeration already **dedups near-identical candidates by end-of-phase state**
   (`BuildSimKey` / `seen_states`) — the "~99 % same result" collapse — so many optimistic extras vanish for
   free. **A hard total-candidate cap is a LAST RESORT** — add it only if measurement shows a blow-up (user:
   "only if absolutely necessary").
5. **DOMINANCE prune (the principled count-bounder, preferred over a hard cap).** An old optimization idea
   (discussed before, no home yet — this may be its home): keep a candidate only if it is **not strictly
   dominated** by another — where A dominates B when A reaches the **same resulting state** as B but is
   **≥ on every auxiliary axis** (more opponent damage, more mana/cards left) and **>** on at least one.
   This generalises the existing end-of-phase-state dedup (which already collapses *identical* results) and
   the scaled-cast "dominated level" drop (`DecisionProviders.cpp` ~1921). It caps the optimistic blow-up
   *without* discarding any genuinely-better line. Implement only if measurement shows the dedup alone is
   insufficient.

**The pattern for new decks (the actual treadmill-kill):** implement the cost mechanic in the executor
(needed anyway), let enumeration credit it *optimistically* (or simply not drop the interacting subset), and
**never** write an order-accurate validated hand-patch. The executor's scoring apply is the feasibility
oracle.

**Cost/GT:** GT-affecting only in the *recover* direction (feasible lines the old patches missed become
reachable); measure candidate-count/makespan + audit; rebaseline + `MTG_LEGACY_*` gate. The `d0` greedy
config (no scoring apply of its own committed plan) is the one place over-credit is not self-corrected — but
it is a non-primary baseline, so it keeps the current conservative aggregate (or a cheap once-per-turn check)
rather than the loosening. The `MTG_ORDER_ORACLE` remains the offline guard rail for the residual
non-canonical-order cases; it is **not** shipped in-play.

---

## Experiment result (2026-07-23): reframe built + MEASURED — partial, well-characterized

The reframe was **built behind flags and measured** with the user's test method: strip an existing deck of
its manual cost tricks (`MTG_NO_COST_TRICKS`) and see whether the general over-optimistic enumeration
(`MTG_COST_REFRAME`) recovers the same quality — a proxy for onboarding a new deck patch-free. Flags,
default off, are **byte-identical** (smoke 21/21 across every round). Three variants were tried: (1) crude
"assume all generic covered" bound; (2) + a dominance/resulting-state dedup in the `SolveWithLookahead`
scoring loop; (3) a tighter "max real discount (reducer + affinity)" bound. **All three converged.**

| case | tricks on (GT) | tricks off | tricks off + reframe |
|---|---|---|---|
| Slivers d5 (affinity) | 4.1933 | 4.2267 | **4.1933 — full recover** |
| Slivers d3 | 4.2560 | 4.2800 | 4.2640 — ~full |
| Dragonstorm d5 (reducer+ritual) | 4.8133 | 4.8400 | 4.8267 — partial |
| Dragonstorm d3 | 4.7067 | 4.7533 | 4.7600 — partial |
| makespan | ~150 s | ~185 s | ~290 s |

**Verdict — a partial, characterized treadmill-kill:**
- **Full patch-replacement for NARROW / self-discount mechanics** (affinity-class): Slivers recovered
  *exactly*, cheaply. Most decks fall here → no patch needed.
- **NOT for HIGH-DISCOUNT combos** (reducer + ritual / storm). **Root cause (triangulated 3 ways):** the
  reframe (offer-then-validate) offers a *superset* of the accurate aggregate — the affordable set *plus*
  over-optimistic extras. On a **fixed node budget** those extra distinct candidates dilute the search
  (worse play + higher makespan). The tighter bound didn't help because combo discounts are *large*
  (`generic − discount ≈ 0` → still broad); the dominance dedup didn't help because the extras reach
  *distinct* states, not strand-equivalent ones. The **accurate aggregate is simply more budget-efficient**
  on combo, so it keeps a ~0.01–0.05 t edge.

### The tool for the combo case (so we do NOT re-invent per deck)

The combo handling is already a **tool, not a per-deck patch**, on two levels:

1. **Generic, param-driven accurate credits (the FIX).** `SameTurnReducerGenericCredit` (keyed on
   `reduces_spell_color`), `SameTurnAffinityGenericCredit` (`affinity_for_subtype`), and ritual float
   (`ritual_floating_mana`) are **per-mechanic**, not per-card/per-deck. A new combo deck reusing any of
   these mechanics is handled with **zero new code**. A *novel* high-discount mechanic needs **one** generic
   credit function — written once, reusable for every future deck with that mechanic. And because the
   reframe still *offers* the line, the deck **works** (just sub-optimally on a tight budget) even before
   that credit exists — it never silently breaks.
2. **The A/B rig (the DIAGNOSTIC).** `MTG_NO_COST_TRICKS` + `MTG_COST_REFRAME` is a permanent, deck-agnostic
   probe: run the deck with tricks off ± reframe and read the avg-turn delta. If reframe-alone recovers the
   tricks (narrow-discount deck) → ship patch-free. If a gap remains (high-discount combo) → that deck wants
   the accurate aggregate, and you know *which mechanic* to check/add. This belongs in the analyze process
   as the "does this deck need an accurate cost aggregate?" check.

**So the analyze rule:** default new decks to the reframe (patch-free); run the A/B diagnostic; only for a
deck the diagnostic flags as a high-discount combo do you confirm/add the (generic, per-mechanic) accurate
credit. The rig stays committed behind flags (`MTG_COST_REFRAME` default off) as both the tool and the
diagnostic.
