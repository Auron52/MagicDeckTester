# Vial-put ORDER beyond one puts-first / puts-last twin (DEFERRED)

Status: deferred 2026-10-04 (Soldiers 5d claude-play sweep). Self-contained; no prior reading needed.

## What exists

A plan's Aether Vial puts (`Action::Kind::ActivateVial`) resolve either all BEFORE its casts (the
default) or all AFTER them (`Plan::vial_after_casts`, the "twin"). Both apply worlds honour the flag:
`ApplyPlanDirect`'s `apply_plan_actions` (top-level plan via `vial_after_armed`, every breakpoint
continuation via `apply_continuation_plan`) and `AIEngine::TakeTurn` (top-level `vial_after`, the
breakpoint branch's `cont_vial_after`). A recorded continuation script replays in the order the rollout
applied it.

`AppendVialOrderVariants` emits the twin for a base plan when `TurnSolver::VialOrderMatters` (structural:
a put AND a cast) holds and `TurnSolver::VialOrderChangesOutcome` (apply both orders on copies, compare
the canonical sim key) says the two orders reach different positions. Continuation lists are twinned
too. The human / `MTG_SEARCH_ORDER` cast-ordering expansion builds a twin per ordering, gated the same
way.

## What is still inexpressible

1. **Interleavings.** A single split point (all puts first / all puts last) cannot express
   "put A, cast B, put C" or "cast A, put B, cast C". Example where it costs a counter: two Vials,
   Champion of the Parish X put + Champion Y cast + a non-watcher Human N put + a non-watcher M cast.
   Optimal is X, Y, N, M (both watchers first: X +3, Y +2 = 5). Puts-first gives X, N, Y, M (= 4);
   puts-last Y, M, X, N (= 4). Needs two Vials and two order-sensitive cards in different zones the same
   turn -- rare, but a legal line no budget reaches.
2. **Base plans that open a mid-turn breakpoint** (other than site 9, post-entry activation, which a
   base plan never opens) get no twin: the two worlds realise a breakpoint's continuation at different
   points of their cast loops, so deferred puts would have to be threaded through both loops at the
   breakpoint. The continuation itself IS twinned, which covers the common Soldiers shape (tutor first,
   then cast the tutored card and Vial-put after it); what remains is "Vial-put AFTER a cast that
   precedes the breakpoint card but BEFORE the breakpoint".
3. **Order among the puts themselves** follows plan action order (one put per Vial); it is never
   searched.

## Shape of a full fix

Replace the bool with a per-put split index (`vial_slot`: number of realised casts before this put),
flushed by a shared `after_cast(k)` hook in every cast loop of both worlds (clean / opaque / explicit
order, sac-land, graveyard, continuation). Enumerate slots only where `VialOrderChangesOutcome`-style
measurement shows distinct positions (dedupe twins by the canonical key, as the cast-ordering expansion
already does). Price: one apply per candidate slot vector; with <= 2 Vials and <= 4 casts that is at most
24 extra applies per qualifying plan, before dedupe.

## Measured context (2026-10-04)

The single-twin generalisation cost +1.6% (300 games) / +3.1% (60 games) units_total on Soldiers at
play settings (d5/b20), and changed no win turn in 360 games: ties keep the base plan, so the twin only
plays where it moves the kill turn. The scenarios `test/scenarios/soldiers_vial_after_*.json` pin the
lines that DO move it.
