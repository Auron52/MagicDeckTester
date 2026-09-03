# Truncate enumeration at the first information point — via an explicit reservation axis

**Status: DEFERRED DESIGN (2026-09-03). Not scheduled.** Emerged from the recoverability audit's
Irencrag discussion with the USER; recorded per the deferred-work rule. Prerequisite ordering
matters: the reservation axis must exist BEFORE any tail truncation, or payment planning
regresses (the USER's work-saver constraint).

## The finding this formalizes

The base-plan enumerator runs PAST the first information point (a cantrip's draw breakpoint), so
it emits many subsets differing only in the post-breakpoint tail. The audit + USER discussion
established what that tail actually does in the shipped engine:

1. **It is NOT a casting decision.** The continuation machinery re-enumerates on the post-draw
   hand (canonically at hosted sites), the apply validates tail casts live, and the executor
   replays the recorded continuation, not the raw tail. USER: *"The only way we could have that
   line is if we didn't re-evaluate after drawing"* — which the engine does. This is also why
   the Irencrag waste gate is structurally sound (see search-recoverability-audit.md 4b).
2. **Its real function is INFORMATION for the heuristic layers** — USER: *"Information for
   heuristic parts of the implementation seems to be the only real use (tapping for mana being
   the most obvious case)."* The scarcity tap ordering, reserve masks and joint-affordability
   gates read the whole subset to pay early casts while keeping later ones payable.
3. **Therefore each distinct tail is a RESERVATION HYPOTHESIS**: "pay for the prefix as if the
   future is X." The USER's example: {Ponder, Irencrag} vs {Ponder} are identical up to the
   breakpoint *except perhaps in how they tap mana*. Tails implying the same reservation are
   pure duplicates the current encoding cannot see until after (part of) the apply — the
   recorded "cross dupes" root cause ("the base-plan enumerator runs past the breakpoint"),
   whose in-node fingerprint dedup lever is BLOCKED on the prefix-resume cache.

Convergence today is four-layered (plan-signature dedup, node prefix/child dedup, the bp-enum
memo, EOT dominance) and absorbs most of the overlap; the residual is the enumeration cost of
the tails themselves plus partial applies before a dedup point bites.

## The design

Replace tail materialization with an explicit per-plan **reservation choice**:

* Enumerate subsets only up to the first information point (the first draw/reveal breakpoint the
  plan's casts open).
* Attach a small reservation axis to each truncated plan: which mana (colour/amount, possibly
  which specific sources per the scarcity rules) to HOLD through the prefix's payments. The
  candidate reservations are derived from what the old tails would have implied — the distinct
  {cost of a would-be tail cast} set, deduped — so the axis is a handful of options, not a
  combinatorial space. "Reserve nothing" is always a candidate and reproduces the {Ponder}-alone
  plan.
* The tap layer consumes the reservation exactly where it consumes the tail today (the
  spanning-subset read becomes a reservation read). `UnprunedGate::TapReserve` and
  docs/design/mana-source-reservation.md are the standing machinery/doc this composes with.
* The continuation at the information point then decides the actual casts on the post-draw
  state, as it already does — but the enumerator no longer pays for tail combinatorics, and
  same-reservation duplicates never exist to be deduped.

## Why not just delete the tails?

The USER's restoration constraint: never drop a work-carrying structure without an alternative.
The tails carry the reservation information; deleting them without the axis regresses payment
planning (casts stranded, colour-tight lines lost). The axis IS the alternative — same
information, explicit, far cheaper.

## Acceptance shape (when picked up)

* Reachability: byte-identical-or-better on the suite; the d8 b0 spot checks on any mover
  (the audit's two-stage protocol).
* Perf: the win is enumeration volume on breakpoint-heavy decks (hinata/dragonstorm) —
  measure units + wall on the tail games; the cross-dupe counters (`MTG_BP_DUPE_TRACE`) should
  collapse.
* The blocked in-node fingerprint dedup becomes unnecessary if this lands (the duplicates are
  never generated), which also unwinds its prefix-resume-cache blocker.

## Context

search-recoverability-audit.md (the Irencrag closure + the USER quotes), mana-source-reservation.md
(the reservation machinery's standing design), bpnode-equal-quality-and-dupe-rootcause memory
(cross-dupe root cause), in-tree-greedy-reachability-hole (why continuations are trustworthy now).
