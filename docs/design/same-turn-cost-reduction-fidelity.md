# Same-turn cost-reduction fidelity gap (Hinata / future reducers)

Status: **open, deferred** — a pre-existing fidelity gap, low frequency, not a
correctness/crash bug. Surfaced while fixing the enumeration mana-total prune
(commit 14103da); recorded here so it isn't lost.

## The gap

Cost reducers whose discount is **baked into `Action.cost` at action-build time**
are only credited when the reducer is **already on the battlefield** at the moment
the turn's plan is enumerated. Concretely, in `TurnSolver::CollectActions` the
Hinata discount is applied as

```cpp
cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
```

`HinataGenericDiscount` reads `state` — i.e. it only discounts when Hinata is
**in play now**. There is **no** per-subset "same-turn Hinata" credit inside
`consider()` (unlike rituals via `ritual_float`, mana rocks via `rock_mana`, or
affinity via `SameTurnAffinityGenericCredit`).

Consequence: a single whole-turn plan that **casts Hinata and then "goes off" the
same turn** (e.g. cast Hinata, then Reality Spasm ramp into discounted X-spells)
evaluates every follow-up spell at **full cost**. The enumerator therefore cannot
see the same-turn combo line — it under-rates or misses that turn.

Frequency is low: Hinata costs 4, so casting her *and* having enough extra mana to
exploit her discount the same turn is uncommon — but it is a real line (ritual
ramp like Reality Spasm can enable it).

## Why it is NOT a prune-correctness problem

The mana-total prune (`ManaPruneBound`) is byte-identical here: both the prune's
`sum(cost.ManaValue())` and `consider()`'s affordability read the **same** baked
`a.cost`. Since neither applies a same-turn-Hinata discount, they agree — the prune
prunes exactly what `consider()` would reject. (Empirically, hinata smoke is
byte-identical PASS with the prune on.) The gap is purely in **what lines the
engine can model**, not in prune consistency.

Affinity is the one reducer credited **per-subset in `consider()`** today, and it
*does* break prune byte-identity — handled separately by bailing the prune out on
affinity decks (14103da). See the maintenance breadcrumb in `ManaPruneBound`.

## Fix sketch (when picked up)

Mirror the affinity/ritual/rock same-turn credit for reducers:

1. Add a `SameTurnHinataGenericCredit(state, cands, sel)` analogous to
   `SameTurnAffinityGenericCredit`: when the subset itself casts the reducer
   (Hinata), credit the discount to the *other* spells in the subset (subtract
   from `combined.generic`, gated on the discounted spells being cast **after**
   the reducer resolves — which, at sorcery-speed main phase, they are).
2. Because that makes `consider()`'s real cost drop below `sum(cost.ManaValue())`,
   the mana-total prune must treat same-turn reducers like affinity: **extend the
   `ManaPruneBound` bail-out** to disable the prune when a same-turn reducer is in
   the candidate set (or model the max credit into the bound).
3. Validate byte-identical on non-Hinata decks; A/B the Hinata combo turns for the
   expected win-turn improvement; rebaseline hinata GT via the accept flow.

## Future cost reducers (the general rule)

"Instants and sorceries cost {1} less", "Dragon spells cost {2} less", etc. split
two ways:

- **Static / already in play** → discount bakes into `a.cost` → prune stays
  byte-identical, no special-casing needed (but the same-turn-cast case has the
  same fidelity gap as Hinata above).
- **Credited per-subset in `consider()` for same-turn casts** → behaves like
  affinity → **must** be added to the `ManaPruneBound` bail-out, or it silently
  breaks byte-identity.

Whoever adds the first such reducer should handle both the fidelity credit and the
prune bail-out together, and add a regression deck that exercises the same-turn
combo turn.
