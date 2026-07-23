# Within-turn ordering in the main-phase enumerator (a.k.a. "sequential plan evaluation")

**Status (2026-07-23):**
- **Increment 1 — DONE** (commit `63f7ffd`): human-play-gated aura legality-ordering for the *viewer*.
- **Scoping spike — DONE** (2026-07-23): settled what is actually missing (see *Spike findings*).
  Nuance corrected after review: affordability is currently *approximated* by an order-free scalar
  aggregate (`BuildPool` float + `SameTurnReducerDiscount`), which is **not exact** — a scalar sum
  cannot see that mana is typed and that a credit is only realizable if a payment *sequence* exists
  (the stranding case). So affordability needs real ordering too, same as legality; the aggregate is
  demoted to an optimistic pre-filter + a source of role tags.
- **Increment 2 — IN PROGRESS** (GT-affecting):
  1. **Port the aura legality-ordering into the autonomous search** (topological, K=1) — **DONE**
     (`SeqAuraOrderingEnabled()`, default on, `MTG_LEGACY_NO_SEQ_AURA` hatch). Auras d5 s700001
     `995ca5e30c33f51d → 1c7117c8a6f2f66c`, avg **4.4750 → 4.4650 (−0.010t faster)**; legacy hatch
     byte-identical; every other deck byte-identical (smoke 21/21, 0 play-changed).
  2. **Affordability exact check** (NEXT, conditional): an optimistic aggregate pre-filter, then a cheap
     canonical order (reducer-as-early-as-feasible → acceleration cheapest-first → payoffs) whose only
     search is a **linear reducer-insertion scan** (O(#accelerants), not n!). Build **only if** the
     oracle (3) finds the aggregate is actually wrong — measure first.
  3. **`MTG_ORDER_ORACLE` exhaustive reference** (the ordering analog of `MTG_UNPRUNED`): measures how
     often the aggregate is wrong and confirms any exact check matches full-permutation truth. Build
     BEFORE (2) so it drives whether (2) is needed.
- **Deferred (future, user-requested):** a per-deck **ordering-analysis step** that generates the
  candidate orderings a deck needs. See *Deferred: per-deck ordering-analysis step*.

---

## The plan model, and the real question

A **plan** is a *set* of actions (a land drop + N casts), built by `CollectActions` and enumerated by
`EnumeratePlans` in `src/ai/TurnSolver.cpp`; `ApplyPlan` / `ApplyPlanDirect` then execute it. The set is
legality/affordability-checked, and the dedup signature keys on cast *names* plus a few per-action
sub-tokens — so **ordering context inside a plan is largely erased**.

The real question this doc is about: **within-turn dependencies**, where a later cast's cost, castability,
legal targets, or value depends on an *earlier cast this turn having resolved*. In real Magic you cast one
thing at a time and state updates after each; a set-based plan checked against one snapshot can mis-model
that. There are **three distinct kinds**, and they are NOT the same problem.

### The three kinds of within-turn ordering dependency

1. **Legality / restriction ordering.** Daybreak Coronet enchants "a creature with **another Aura**";
   Lion Umbra needs a **modified** creature. Casting `Ethereal Armor → Bogle` then `Daybreak Coronet →
   Bogle` is legal (Coronet sees the just-attached Armor), but only in that order. **A canonical order
   always exists: enabler before payoff (topological). K = 1** — no order search needed; the reverse
   order is simply illegal.

2. **Affordability ordering.** Ruby Medallion (a red-spell cost reducer) + rituals in a storm turn. The
   order changes whether you can *pay*, and it is **not locally decidable**: you might be able to drop
   Ruby Medallion with 2 mana but thereby strand the ritual that would have netted you *more* mana, so
   Medallion-first is sometimes right (it even discounts the red rituals) and sometimes strands your
   acceleration. Deciding it requires whole-line feasibility, not a per-spell affordability check.

3. **Trigger / ETB value ordering.** Lathliss → Scourge → other Dragons. Permanents with a "whenever
   another X enters" trigger want to enter *first* so they see every later X. **This rule is
   value-sign-aware**: it holds only when the trigger *helps you*. A harmful on-other-ETB permanent
   flips it — you want it last, or you sequence the triggerers to minimise exposure. So the rule is
   "**beneficial** on-other-ETB sources lead; harmful ones trail," never pure syntax.

---

## What the engine ALREADY does (spike-verified)

### Affordability — currently an ORDER-FREE APPROXIMATION (not exact)

The autonomous enumerator does not enumerate cast orderings for affordability. It uses a **scalar
aggregate** that credits same-turn ramp and reduction into the budget without trying any order:

- [`BuildPool` (TurnSolver.cpp:256)](../../src/ai/TurnSolver.cpp#L256) credits **ritual float + retained
  over-production** — "spendable on later same-phase casts, so it counts toward affordability."
- [`SameTurnReducerDiscount` (~TurnSolver.cpp:429)](../../src/ai/TurnSolver.cpp#L429) credits a **reducer
  cast in the same subset** (Ruby Medallion) against the rest of that subset's combined cost.
- Ritual accelerants are powerset-ed as a subset dimension
  ([~TurnSolver.cpp:1133](../../src/ai/TurnSolver.cpp#L1133)).

All of this lives in the **shared** `Solve` / `EnumeratePlans` path (not human-play-gated), and Dragonstorm
goes off autonomously (regression smoke d5 ≈ 4.81). But the aggregate `subset_cost − reducer_discount −
ritual_float ≤ budget` is a **scalar relaxation of a typed + sequenced feasibility problem, and cannot be
exact**: a credit is only realizable if a payment *order* exists that pays each cost from then-available
mana of the right colours. Casting the reducer can tap the exact mana the ritual needed (the **stranding
case**) — no order realizes both credits, but the scalar sum grants them anyway. It bites even in mono-colour
decks when upfront mana is tight. The tell that this is an *unprincipled* relaxation: `SameTurnReducerDiscount`
exists **because the plain aggregate was dropping affordable lines** — a per-symptom hand-patch nudging it
optimistic. Dragonstorm winning only means the relaxation is optimistic enough that enough *truly-feasible*
lines survive; the latent hazard is the search selecting an **over-credited, infeasible** line (scores
lethal, isn't) that `ApplyPlan` then can't sequence. So affordability needs real ordering too — but cheaply
(see the canonical order below). The multiple orderings in `--claude-play` dumps are a viewer artifact.

### The canonical execution order (deterministic backbone; ONE movable point)

The full order is fixed by simple rules, with a **single** searched degree of freedom — the reducer's slot:

1. **Lands / mana sources first** — play, tap for mana. *Exception:* a **bounce land** (returns a land to
   hand) — float the outgoing land's mana *before* playing it, so it can go early without losing the mana.
   Deterministic, not a search axis. (Whether to *defer* a land to later in the turn is a separate
   plan/fragment decision — expressed as `land=none` in this fragment — not a reorder within the plan.)
2. **Enablers** (legality) — an aura/permanent that satisfies a later restriction goes before the payoff
   that needs it (topological; K = 1). Deterministic.
3. **Acceleration** (rituals / rocks) — **cheapest-first** (minimises upfront cost at each step, so running
   mana is maximised). Deterministic.
4. **Cost reduction — the ONLY movable point.** Insert the reducer at its **earliest feasible position** in
   the acceleration sequence (earliest = most discount realized; slide later only when doing it sooner
   strands the acceleration). A **linear insertion scan** — O(#accelerants), not n!.
5. **Payoffs** — Coronet / Dragonstorm / lethal, at the now-reduced costs.

Worked: 2-mana stranding case → reducer can't sit at position 0, slides past the ritual (`ritual →
Medallion → payoffs`); 4-mana case → reducer sits at position 0 and also discounts the red ritual
(`Medallion → ritual → payoffs`). Same rule, both outcomes. The residuals it can't express (multiple
reducers, colour-specific float conflicts, an accelerant that is also a payoff) are what the exhaustive
oracle exists to catch.

**Future case — count-scaling mana sources (currently UNMODELED).** Priest of Titania / Elvish Archdruid
(tap for #Elves), Overgrown Battlement / Axebane Guardian (tap for #Defenders): the source's yield grows
with the board, so the general principle *"realize the yield-boosting board actions before you draw on the
boosted mana"* applies — deploy the cheap subtype creatures early, **pay for them from non-scaling sources
so the scaling creatures stay untapped ("as much as possible"), then tap the scaling sources last at the
grown count** (fall back to tapping one at the lower count only when forced). The cast-order stays the
canonical sequence; the new part is a **payment/tap-order reservation** — a natural extension of
[mana-source-reservation.md](mana-source-reservation.md) (reserve the yield-sensitive sources, prefer fixed
sources for early payments), not a new subsystem. Note (i) **summoning sickness** bounds it — the scaling
source must be pre-existing to tap this turn; this-turn boosters raise its count but can't tap themselves —
and (ii) the order-free aggregate would tap a scaling source at the *start-of-phase* count, blind to
same-turn board growth → it **under-credits** and drops affordable lines: one more reason affordability
needs the sequential/reservation-aware check. Not implemented (no Elves/Defenders deck, no count-scaling
mana param); folded in when such a deck is added.

### Trigger / ETB value — hand-coded per deck where it matters

Dragonstorm's put order is bespoke: `DragonstormProvider::TutorToBattlefieldPutOrder` (Lathliss →
Scourges → Utvara → haste). Correct, but hand-written per deck — exactly what the deferred analysis step
should *derive* instead.

### Legality / restriction — NOT handled autonomously (the gap)

Aura target-legality/restriction is checked against the **frozen start-of-phase snapshot**. Increment 1
widened enumeration to include the enabler-first line **only under `HumanPlayActive()`** (the viewer). The
autonomous search still cannot enumerate `Ethereal-Armor-then-Coronet`.

---

## Increment 2 (this pass) — three pieces on one sequential-apply core

All three share a single primitive: **apply a plan's casts in a *given* order, updating a working
`GameState` after each** (deriving cost / castability / target-legality from the running state). Given
that, (a) walks the topological order, (b) walks the canonical order with the linear reducer-insertion
scan, and (c) walks all orders.

### (a) Port aura legality-ordering into the autonomous search — the capability win

Give the autonomous enumerator the same enabler-first widening increment 1 gave the viewer, applied in a
single **canonical topological order** (enabler → payoff; K = 1, no permutation search). This is
GT-affecting (more legal lines become enumerable → the search may play a faster line), so:
- **measure + rebaseline** the affected decks (Auras at minimum; it is not currently in the suite, so add
  it or measure the d5 s700001 digest directly), and
- keep an `MTG_LEGACY_*` gate so the frozen-snapshot behaviour stays A/B-comparable, per the repo's
  byte-identical-hatch convention.

Increment 1's helpers (`AppendSequencedAuraCandidates`, `SubsetHasUnenabledRestrictedAura`, the
enabler-first `stable_sort`) already encode the detection; the port lifts the `HumanPlayActive()` gate for
these and folds them behind the legacy gate instead.

### (b) Affordability exact check — canonical order + linear reducer-insertion scan

Demote the scalar aggregate to an **optimistic pre-filter** (drop obviously-unaffordable subsets fast; it
over-credits, so few false negatives) *and* the **source of role tags** (it already knows which cast is the
reducer / the accelerants and their float). On the survivors, run the exact check: build the **canonical
execution order** above and, for the reducer's single movable slot, do the **linear insertion scan** —
keep the plan iff a feasible order exists, and stamp that order onto the plan so `ApplyPlan` executes what
enumeration approved (no enumerate/execute mismatch). Also GT-affecting (fixes over-credited infeasible
lines and recovers under-credited feasible ones) → measure + rebaseline + legacy gate, same as (a).

### (c) `MTG_ORDER_ORACLE` — the exhaustive-order reference (the ordering `MTG_UNPRUNED`)

The pre-filter + canonical order is a **prune** of true sequential feasibility. It can still fall short on
the residuals the linear scan can't express: multiple reducers, colour-specific float conflicts, an
accelerant that is also a payoff. Keep an unpruned A/B reference to measure it, exactly as the codebase
validates every prune:

- **`MTG_ORDER_ORACLE=exhaustive`**: for each plan, actually try cast **orders** with running sequential
  state, and keep the best *feasible* (and best-scoring) one. Default (unset) = the canonical-order exact
  check from (b).
- **A/B via the regression harness** over train (regression) seeds, validate the winner on overnight
  seeds. **Lossless iff `exhaustive` never beats the canonical-order check on avg-turn-to-win** (the
  metric); any flagged game names the residual the linear scan missed.
- **Offline-only.** The oracle is a *validation harness*, never an in-play mechanism, so the factorial
  cost is paid once per validation run, not per production game.

**Taming the factorial** (lever order, biggest first):
1. **Outcome-dedup by resulting-state digest** — symmetric casts (three ritual floats of the same
   colorless) collapse from n! to a handful of distinct outcomes.
2. **Only decisions where order can matter** — ≥2 *interacting* casts (a reducer / ritual / enabler /
   restriction present). Most decisions are 0–1 casts and skip instantly.
3. **Cap plan size ≤ N casts, and `log()` every skip** — no silent truncation; coverage stays honest.
4. **Sampled-K-permutation fallback** for over-cap plans — deterministic (seed + decision-index, no RNG).

The oracle is "enumerate all orders, take best"; the shipped path (b) is "canonical order + linear
reducer-insertion scan." Same primitive, so build them together and keep the oracle as the permanent
ordering A/B.

---

## Deferred: per-deck ordering-analysis step (future, user-requested)

Hand-coding per-deck order rules (Lathliss-first) does not scale, and generic *syntactic* rules are wrong
(the value-sign point above). The future direction is an **offline analysis step that generates the
orderings a deck needs** — in the spirit of the analyze-deck / heuristic-optimization skills. Per deck it
would:

- **classify each card's order role(s):** mana / cost-reducer / enabler (satisfies a later restriction) /
  on-other-ETB source (**with a measured value sign**) / payoff / resource-scarce. Most of this derives
  from existing params (`produces`, `reduces_spell_color`, `ritual_floating_mana`, `aura_enchant_requires`,
  targeting); tricky cards get an explicit tag/override.
- **derive the per-deck ordering rules and the affordability "frontier"** — the small set of casts whose
  order actually changes affordability or value — by simulating the going-off turns and **measuring**
  where a naive order loses a line or value versus the `MTG_ORDER_ORACLE` exhaustive run.
- **emit the candidate orderings the in-play search should try** ("generate orderings **as needed**"), so
  the search evaluates a small precomputed set instead of searching orders live.

This keeps the **search-primary** bar: the analysis *narrows* the ordering space to a small candidate set
(a prune); the search still decides among them; the oracle stays as the lossless A/B. "As needed" is
frequency-driven — only generate/keep orderings for the decks and turns where the oracle shows order
actually matters.

---

## Two consumers, two ordering policies (unchanged from increment 1)

- **Viewer (human play):** respect the user's literal cast order; apply one at a time against the running
  state; accept iff each step is legal. Done for auras in increment 1.
- **Search (autonomous):** use a canonical / bounded-candidate order (do not enumerate every permutation).
  The legality case is a single topological order; the affordability case is the order-free aggregate,
  validated by the oracle.

---

## Spike findings (evidence, 2026-07-23)

Method: a parallelised `--claude-play` driver, 24 seeds per deck, driven greedily through the going-off
window. **Caveat:** `--claude-play` runs viewer-widened (`MTG_HUMAN_PLAY`) enumeration, so the plan dumps
show the *viewer* enumeration; the autonomous-vs-viewer question was settled by reading the enumerator, not
from the dumps.

| | Auras | Dragonstorm |
|---|---|---|
| main-phase decisions | 121 | 221 |
| avg plans / decision | 11.9 | 17.3 |
| **legality-ordering** (restricted-aura plan present) | **22 (18.2%)** | 0 |
| multi-aura plan (≥2 auras) | 28 (23.1%) | 0 |
| **affordability-ordering** (ritual+reducer plan) | 0 | **32 (14.5%)** |
| ritual + ≥2 payoff plan | 0 | 25 (11.3%) |

- **Auras:** the enumerator emitted *both* orderings of the same aura set (`Coronet → X, Hyena Umbra → X`
  **and** the reverse); only enabler-first is sequentially legal → **legality ordering is K = 1** and a
  genuine *autonomous* gap (increment 1 only fixed the viewer).
- **Dragonstorm:** full go-off lines (`Ruby Medallion, Pyretic Ritual, Desperate Ritual, Irencrag Feat,
  Dragonstorm`) are enumerated intact → affordability is already sequenced. The enumerator read confirmed
  this is the **order-free aggregate** (`BuildPool` float + `SameTurnReducerDiscount`), autonomous.

The two kinds separate cleanly by deck (legality → Auras, affordability → Dragonstorm), and both are
frequent (~15–18% of going-off decisions).

---

## Scope / risk

Increment 2(a) touches the hot enumeration path and shifts GT — measure against the search-perf budget,
rebaseline the affected decks, keep a legacy gate. Increment 2(b) is offline. The band-aids the aura
`enchant_target` chooser/dedup added (`plan_signature` + `CheckLine`, and the Light-Paws / crackle /
soulfire / splice subs) are partial compensations for the frozen snapshot on the *legality/target* axis;
under the topological legality port several could collapse into the general mechanism. MDFC land-face
selection is separable (a single land-play choice, not an intra-turn cast dependency) and already shipped.
