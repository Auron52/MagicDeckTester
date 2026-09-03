# Making main-1 mana choices with main 2 in mind

**Status (updated 2026-09-03): SUPERSEDED** — the mechanism ships as `MTG_M2_PAYLOAD_RESERVE`
(default ON since 89076b85, 2026-08-26): while a pre-combat plan apply is paying, a source set is
reserved that keeps the best post-combat payload payable. It reserves for the single best uncast
hand card rather than this doc's `ClassifyMainPhase` want-set.

**Status:** designed, not built.
**Origin:** USER, 2026-08-16 — *"it would be nice for the mana choices to be made
with main 2 in mind (since often the human player would know what they need in
main 2)"*.

## The gap

A turn with a second main is solved as **two independent mana problems**:

1. `EnumeratePlansWithLand` picks the main-1 plan; `BatchPrepayMainCasts`
   ([`TurnSolver.cpp:8766`](../../src/ai/TurnSolver.cpp)) solves that plan's combined
   cost in ONE backtracking tap solve and pre-loads `state.floating_mana`.
2. Combat happens.
3. `SolveSecondMainInSearch` ([`TurnSolver.cpp:486`](../../src/ai/TurnSolver.cpp))
   solves main 2 **on the resulting state** — whatever main 1 left untapped.

Step 1 has no knowledge of step 3. Batch-pay is *whole-phase* joint, not
*whole-turn* joint, so main 1 can spend exactly the coloured source main 2 needed
and nothing prices that. This is the cause behind fivecolour gi=41 (two of three
mana unused on T3) that I had filed as "architectural".

A human does not play this way, which is the USER's point: they drop the land and
tap knowing what the turn's *second* half wants.

## Why this is tractable — the pattern already shipped

Do **not** reach for a full joint m1+m2 solver. The correct shape already exists in
this file, twice, and the second instance is the exact template:

`BatchPrepayMainCasts` accepts a `reserved_mask`, and depletion-land reservation
(shipped 2026-07-02, `DepletionReserveEnabled`, default ON) uses it like this:

* prescan the sources worth holding → `reserved`
* if `reserved == 0`, skip entirely — **byte-identical, no extra solve**
* solve the combined cost ONCE with all of them held
* if it pays wild-free, keep the hold; if the turn becomes unaffordable or forces
  an ambiguous/wild tap, **restore and solve unrestricted**

It is **all-or-nothing rather than per-source-maximal** on purpose: the per-source
greedy cost *k* extra full backtrack solves per call (~80% rejected) and measured
~15% slower on treasure_hunt.

Crucially it is *sound*: "reserved" only means *don't pre-tap*. The source stays on
the battlefield, so a later re-solve that genuinely needs it can still tap it —
nothing is ever stranded. That soundness is what killed the **earlier** per-payment
reservation scheme, which judged "payable without S" against a single payment rather
than the whole turn and stranded a later same-turn cast (treasure_hunt seed 3044:
reservation ON → T4 vs OFF → T3). Read that history in
[[mana-source-reservation]] before touching this.

## Proposed change

Extend the same all-or-nothing hold to cover **main 2's** needs:

1. Only when `state.uses_second_main && is_pre_combat` (else `reserved = 0` and the
   path is inert — every other deck stays byte-identical for free).
2. Build the main-2 want set. The classifier already knows this: the Main2-classified
   cards in hand (`ClassifyMainPhase`) that are plausibly affordable this turn. Their
   combined colour requirement gives the sources to hold.
3. One extra solve of main 1's combined cost with those sources reserved. Keep the
   hold if it pays wild-free; otherwise restore and solve unrestricted — main 1's
   own casts always win the tie, which is the USER's other rule
   (*"deferring it when you have main 1 plays is probably not a good idea"*).

## Relationship to the defer-the-drop rule

Deferring the land drop was a **crude proxy** for this: rather than predict what main
2 needs, it postponed the whole decision until the information arrived. Measured as
churn overall (net −0.0001 / 42,800 games) with a one-sided 4→5 loss bucket, then
refined to fire only when main 1 casts nothing at all.

Reservation is the better-targeted mechanism, and the two compose: reservation fixes
*which sources get tapped* on a turn that does play a land, while the refined defer
handles *whether to play the land at all* on a turn with no main-1 play. Neither
subsumes the other.

## What to watch

* The land tie-break (four exemplars: antilife gi=519 / gi=367 / gi=38, 5c gi=97,
  hinata d0 gi=1061/1987) is the same information problem one level up — *which*
  land to play, not which source to tap. A fetchland produces nothing directly, so
  any colour test must look through `FetchCandidates`.
* Do not let the want-set grow into a predictor. If it starts guessing which main-2
  cast will be chosen, it will be wrong in exactly the cases that matter and will
  strand main-1 mana. Hold only what is unambiguously wanted, and always fall back.
