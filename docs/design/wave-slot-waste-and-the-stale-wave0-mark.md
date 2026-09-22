# Three pre-existing defects in the deferred continuation waves

Status: **all three fixed, 2026-09-22.** Levers `MTG_BP_WAVE_NSKIP`, `MTG_BP_AXIS_W0_CLEAR` and
`MTG_BP_WAVE_NOBP` (all default 1); `=0` on any restores the previous behaviour for an A/B.

**Read this first, because it reframes what "fixing the cost" can mean here.** At play settings the
search is **budget-bound**: `budget_ms` is converted to virtual work units and the node spends them
until they run out, so wall time is set by the budget and not by how much of the work was useful.
Deleting 65.6% of the wave slots therefore did **not** make the suite faster (measured: −0.9%,
inside a 13% run-to-run spread on a shared box). It bought *better search inside the same budget*.
Anything here that reads like a cost saving should be read as a quality gain at constant wall.

Background: `docs/design/post-breakpoint-search.md` (the wave design),
`docs/design/canon-default-reachability.md` (the canon-default audit that reads `bp_wave0`),
`docs/design/greedy-continuation-deletion-route.md` (the deletion that invalidated the assumption
underneath both of these).

---

## The common root cause: the greedy deletion changed what an overrun MEANS

Both defects come from the same lapsed assumption, and it lapsed silently because it lives in
comments rather than in a test.

A wave slot hands out ranks `k = k0, k0+1, ...` into a breakpoint's ranked continuation list.
When `k >= cands.size()` the rank **overruns** the list. What happens then used to be:

> "Fewer continuations than variants -> fall back to greedy, making this variant a duplicate of its
> base plan (a wasted node, never a wrong answer)."

That comment is still in `bp_searched_plan`, and it is **no longer true**. Since the greedy
continuation was deleted (2026-09-17) an unresolved continuation is **EMPTY** -- byte-for-byte what
`kBpEmptyChoice` builds, "I am done acting in this phase". So an overrun is not a duplicate of the
base plan; it is the EMPTY line, which no in-list rank produces.

Everything downstream inherited the stale reading:

* `BpWaveWalker::Report` still says a past-end rank makes "a copy of its own base plan".
* `BpProbe::overrun` was documented as counting "provably WASTED NODES ... deletable outright".
* `MTG_BP_WAVE_NSKIP` was justified as "LOSSLESS by construction", on a 133-improvements A/B that
  **predates the deletion**.

One consequence is worth stating on its own, because it corrects a claim made elsewhere: on a
continuation list shorter than `W`, wave 0's own trailing ranks already score EMPTY. The
`MTG_BP_EMPTY_ARM` lever is default OFF, but the empty option is **not** absent from those slots --
it arrives through the overrun. Any argument of the form "a len-1 slot has no genuine alternative
because EMPTY_ARM is off" is wrong for exactly this reason.

---

## Defect 1 -- the stillborn skip was lossy, and fenced out of play for the wrong reason

`AddSlots` declined to open a slot when `known_n <= k0`: every rank from `k0` up is past the end, so
"the slot has nothing to hand out". Post-deletion that is false in two cases:

| case | what wave 0 scored | what rank `k0` produces | old test | correct |
|---|---|---|---|---|
| `n < k0` | ranks `0..W-1`, which **includes** `n` -> EMPTY was scored | EMPTY again | skip | skip |
| `n == k0` | ranks `0..W-1`, **all in-list** -- no EMPTY | the slot's **only** EMPTY | skip | **keep** |
| past-end wave-0 variant beam-cut | nothing past the end applied | the only EMPTY | skip | **keep** |

The fix adds the second half of the question. `W0Len` now carries `max_k` -- the highest rank wave 0
**actually applied** -- and the skip requires

```
n <= k0  &&  max_k >= n
```

i.e. *every continuation this slot could hand out is an EMPTY this node has already scored*. Both
lossy cases fail `max_k >= n` and open normally.

**That is what removes the scope.** The lever was gated to `UnbudgetedWorkScopeActive()`, whose
stated reason was that ungated it churned 5 GT keys at play. But a content-lossless skip cannot lose
a line; what the scope really bought was the budget being spent the same way, which is a GT-churn
argument, not a loss argument -- and churn is settled by measurement, not by fencing the lever out
of the only regime anybody ships. The measurement is below.

The cross-node memo (`MTG_BP_NSKIP_GLOBAL`, still default 0) gets the off-by-one fixed too (`<`
rather than `<=`). It cannot carry `max_k` -- it answers from any node, and `max_k` is a fact about
*this* node -- so the beam-cut case stays uncovered there. One more reason it stays default 0.

### The memo has to reach the loop that does the work

FSLineWin has kept a `bp_known_n` since 2026-09-15. `SolveWithLookahead`'s per-pass candidate loop --
which runs **every rollout turn**, and which the wave design's own measurement names as where the
searched-breakpoint gain actually comes from -- never had one, so its walker opened every stillborn
slot blind. Wiring the same memo there took the skip from 14,890 of stompy's 130,904 redundant slots
to 36,619.

No `bp_self` remap is needed on that path (unlike FSLineWin's): `EnumeratePlansWithLandUncached`
stable_sorts `all` **before** it calls `AppendBreakpointVariants` and returns it untouched
afterwards, so `bp_base` is stamped against the final order. That is a property of where the sort
sits -- a reorder added after the append would give this memo FSLineWin's stale-index hazard.

---

## Defect 2 -- `bp_wave0` was inherited by clones that wave 0 never fanned out

`Plan::bp_wave0` is a record of a fact about **one plan**: "wave 0 emitted ranks `0..W-1` for this
plan". `AppendSubdecisionAxes` runs **after** `AppendBreakpointVariants` and every axis builds its
variants with `Plan v = p`, so each clone carried a mark earned by the plan it was copied from --
while no rank variant anywhere points at the clone.

Two consumers were misled:

* **`BpWaveWalker::AddSlots`** opens a marked plan's slot at `k0 = W`. For a clone that means the
  walker started at rank 2 and **ranks 0..W-1 were unreachable at any budget or depth** -- the exact
  ceiling the deferred waves exist to remove ("no rank is unreachable at an unbounded budget"),
  reintroduced by a copy constructor.
* **the unchallengeable-canon audit** counts `plan.bp_wave0` as proof the plan was offered to the
  variant machinery. For a clone that proof was borrowed.

Measured, stompy 300 games d3/b10: of 83,887 marked base plans reaching the walker, only 22,466 had
been fanned out. **35,534 were axis clones**; the remainder are clones further down the same chain.

The fix clears `bp_wave0` on everything the axes append. A cleared clone opens at rank 0 and the
walker covers it exactly as it covers any plan wave 0 skipped -- which is the design's own answer for
a class wave 0 does not fan out ("a cost prune precisely because its plans are picked up here at
rank 0"). It makes the walker do **more** work, not less; it is a reachability fix, not a saving.

**The audit still reports ZERO on all 23 decks with the borrowed mark removed**, so the canon-default
result did not rest on it. That was not obvious in advance and is the reason the audit was re-run
before this landed.

---

## Defect 3 -- the walker trusted a predicate the apply had already refuted

`PlanOpensBreakpoint` is an **over-approximation**: it asks whether a plan *looks* like it opens a
breakpoint, from the plan and the pre-apply board. The apply settles it -- and the node has already
run that apply, on the base plan itself, in the candidate loop immediately above the walker. Nothing
was reading the answer.

A variant cannot disagree with its base plan on this question. `Next()` builds every variant as
`out = plans[sl.base]` with only `bp_choice`/`bp_at` overwritten, so it carries the identical action
list; `bp_choice` decides what to do *at* a breakpoint and cannot create one. The apply is
deterministic in (state, actions). So if the base plan's apply reached **zero** breakpoint
occurrences, every variant of it reaches zero, resolves nothing, and is a byte-identical duplicate
of the base plan the node has already scored.

`MTG_BP_WAVE_NOBP` declines those slots. It is conservative by construction: the counter it reads
(`g_bp_any_last`) is incremented for **any** occurrence of **any** class, not just the searchable
ones, so `== 0` is strictly stronger than "reached no eligible breakpoint" and the gate declines
strictly less than it safely could. A base plan not in the set opens exactly as today.

This is the largest of the three by a wide margin: **87,365 slots declined** on stompy's 300 games.

---

## Measurement

stompy, 300 games, d3/b10, `MTG_BP_WAVE_PROBE=1`:

| | slots | stillborn | of those, pure rediscovery | memo misses | rolled | improved |
|---|---|---|---|---|---|---|
| all off (previous behaviour) | 139,423 | 137,230 | 130,904 | 102,758 | 1,165 | 0 |
| skip only | 109,711 | 107,533 | 100,571 | 102,758 | 1,204 | 0 |
| skip + reachability | 109,303 | 105,336 | **148** | **67** | 2,774 | 0 |
| **all three** | **48,028** | 44,104 | 150 | 69 | **2,842** | 0 |

Reading it: the redundant-rediscovery term is essentially gone (130,904 -> 150), the memo now
reaches almost every slot it should (misses 102,758 -> 69), real rollouts more than **doubled**
because clones that had been starting at rank 2 now get searched from rank 0, and total slots are
down **65.6%** -- all while covering strictly more of the rank space than before.

`nodes` drops 9,698 -> 5,107 as well: half the nodes that used to build a wave had no non-duplicate
slot to offer.

`improved=0` throughout is worth recording: on stompy the entire wave phase never beats its node's
incumbent, in 300 games, either before or after. That is a separate question (is the wave phase
earning its cost on this deck at all?) and is not addressed here.

Play on stompy is **digest-identical** across all four arms.

### Suite

Smoke, against committed GT:

* **reachability fix alone**: 4 keys changed, **every one avg-identical** -- pure play-shape change,
  no quality cost.
* **skip alone**: 5 keys changed; th `4.0400 -> 4.0267` (better), melira `4.7600 -> 4.8000` (worse),
  3 avg-identical.

Those two numbers are **one game each**: melira's smoke d5 case is 25 games and th's is 75, so
0.0400 and 0.0133 are exactly one turn apiece. Smoke cannot answer this; see the held-out sweep in
`logs/nskip_ab/` (8 deck x 4 fresh seeds x 300 games, seeds 40004/50005/60006/70007, disjoint from
smoke's 1001, regression's 2002/3003 and overnight's 4004..7007).

---

## What is NOT claimed

* **No wall-clock figure.** This box is shared and a previous A/B here had *both* arms rise ~25% on
  byte-identical work. Only the deterministic avg-win-turn numbers and the counter deltas above are
  quoted.
* **The wave phase's own value is untouched.** `improved=0` on stompy says the phase may not be
  earning its cost there, but nothing here tests that, and the slot counts above are not a
  cross-arm comparator for it.
* **Wave 0 still emits a fixed `W` ranks per marked plan.** Consulting the length memo at *emission*
  time (rather than at slot-opening time) would remove the remaining duplicate overruns inside wave 0
  itself -- 3,478 on stompy at site 10. Not done: wave 0 runs before any apply for that plan, so it
  would have to read the cross-node memo, which carries the position-keying hazard the append-only
  ordering currently avoids.
