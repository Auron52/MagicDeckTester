# Exact mana enumeration (deferred): replace the flat `wild` pool with an achievable-pool frontier

**Status:** DEFERRED (2026-07-29). Measured, scoped, not built. Parked here rather than in agent
memory per the repo's deferred-work rule. Split out of the odometer-factorisation work in
[plan-odometer-factorization.md](plan-odometer-factorization.md), where the *performance* half of the
same idea was built and measured.

## The idea

Enumerate, once per decision, the mana the board can actually produce — then answer each candidate
plan's affordability by looking that up, instead of approximating it. Concretely: precompute the
**Pareto frontier of achievable `ManaPool` vectors** over the untapped sources' tap choices, and test
a plan with "does some frontier entry dominate this cost?".

## Why it is NOT a performance lever (measured)

`perf record -F 999`, Release, single-thread, seed 1001. Share of total runtime spent in the
*per-plan affordability lookup* (`ManaPool::CanPay` + `SubsetPayable`):

| deck / depth | games | per-plan lookup | all mana code |
|---|---|---|---|
| treasure_hunt d3 | 150 | **0.44 %** | 8.2 % |
| Hinata2 d5 | 75 | **1.36 %** | 9.1 % |
| Dragonstorm d5 | 75 | **0.52 %** | 3.0 % |

The lookup is already O(1) against state that is already computed once per enumeration call —
`BuildPool` ([TurnSolver.cpp](../../src/ai/TurnSolver.cpp)), `ComputeAvailableColors`, `ManaPruneBound`.
Roughly twenty integer ops per plan. **No richer precomputation can beat 0.5 %.** Anyone revisiting
this for speed should stop here; the real enumeration cost was the plan cross-product, not the mana
(see the sibling doc).

## Why it is still worth doing — correctness and deleted code

`ManaPool` stores every multi-colour source as one `wild` unit that satisfies any single pip
([ManaPool.h](../../src/core/ManaPool.h)). That over-approximates, and two patches exist purely to
paper over it:

- **`SubsetPayable`** — a colour-producibility gate that rejects a subset needing a pip no untapped
  source can make at all. Deliberately does not model count/contention/filter yield, so it catches the
  "needs {U}, zero blue sources" phantom and nothing finer. Found by the Knights claude-play sweep,
  after the enumerator offered hard-casts the executor silently no-opped.
- **`SubsetPayableWithFilters`** — the opposite failure: a filter land (Cascade Bluffs: feed {U} →
  {R}{R}) is stored as one `wild`, which *cannot express colour conversion*, so genuinely payable
  filter lines fail the flat `CanPay`. The fallback **copies the whole `GameState`** and re-pays by
  tapping real sources. Only reached when the flat check already failed and such a land is untapped.

An exact frontier makes both unnecessary: colour identity, count, contention and filter conversion are
all properties of the achievable set. Expected net LOC: negative.

## Sketch

- Frontier size is small in practice. Mono-colour: one entry. Two colours with duals: the W/U split
  frontier, ~#duals + 1. Filters add conversion edges, not combinatorial blow-up, because the frontier
  is Pareto-reduced after each source is folded in.
- Build by folding one source at a time into a set of pool vectors, dropping dominated entries
  (`a` dominates `b` iff `a.c >= b.c` for every colour and `a.wild >= b.wild`).
- Key it on the untapped-source multiset so it can be cached per decision; the battlefield changes
  between nodes, so a cache key that is cheaper than the rebuild is itself a design question.
- Reuse `SourceMaxNet` ([SpellEffects.h](../../src/core/SpellEffects.h)) for the per-source yield bound
  and `ProducesForPayment` for the `colored_creature_only` (Unclaimed Territory / Cavern of Souls)
  creature-vs-noncreature split — the frontier must be built per `for_creature` value, or carry it.

## Cost / risk

- **GT-affecting for most decks.** Any deck with a dual, a filter or a restricted-colour land changes
  which plans are enumerated (both directions: phantoms removed, filter lines added). Needs a
  `MTG_LEGACY_WILD_POOL` hatch, a full rebaseline and a per-game audit — not a free win.
- **Two greedies must stay in lockstep** (`AIEngine::TapForCost` and `TurnSolver::TapForCostDirect`) —
  the frontier is an *enumeration* model and must not drift from what the payment path can realise.
- **`TapForCostBacktrack` stays the complete fallback.** Completeness by construction is not negotiable;
  the frontier only decides *which* legal payment is offered, never *whether* one exists.

## Maintenance note carried over

The total-mana bounds in `ManaPruneBound` / `BuildManaGateIndex` assume filters convert colour without
adding total mana. That holds today (Ferrous Lake and Izzet Signet are 2-colour ramp filters, net +1,
matching what `AddSourceToPool` credits) and would break for a 3+-colour ramp filter. An exact frontier
would make the assumption unnecessary — one more reason to do this eventually.
