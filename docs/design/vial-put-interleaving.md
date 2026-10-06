# Vial-put ORDER -- SUPERSEDED 2026-10-06 by the USER's ruling (puts are sequenced like casts)

**Status: closed.** The before/after-the-casts axis this file describes (`Plan::vial_after_casts`,
`AppendVialOrderVariants`, `VialOrderMatters` / `VialOrderChangesOutcome`, CheckLine's "Aether Vial
timing" sub) was DELETED on 2026-10-06. The user's ruling, verbatim:

> "The before the casts stuff should not be a standard decision. Aether vial deployments should be
> done in the same order as casting." / "But also, it should be done in the order the user
> specified." / "So, either way, I should not be asked for an order. The order is apparent." /
> "The search should use the order specified for deployment and the viewer should use my order."

## What replaced it

A Vial put (`Action::Kind::ActivateVial`) is a MEMBER of the ordered cast sequence in both apply
worlds (`ApplyPlanDirect`'s `apply_plan_actions` and `AIEngine::TakeTurn`, top-level plan and every
breakpoint continuation):

* **Canonical route** (search, no pin): the put is stable-sorted with the non-sacrifice hand casts by
  `CastOrderLess` -- its CREATURE's `CastOrderRank`, ties to plan-vector position -- and then goes
  through the same range ladder / enabler recheck / payable-order fallback as a cast
  (`IsEtbTreasureMakerCast` covers a Vial-put Corsair Captain, so the Treasure hoist moves it ahead of
  the casts it funds).
* **Explicit route** (`searched_order`: the human's `--cast-order` pin, or a searched cast ordering):
  the put resolves at its VECTOR position. `ReorderPlanCasts` treats a put as a slot in both pin forms
  (the viewer names it by its creature: a queued Vial entry, and `cast_order_canonical` lists it at its
  realised position). A cast-only pin that does not name the put (every reference saved before
  2026-10-06) places it FIRST, which is the order those games were played in. The cast-ordering
  expansion merges each ordering's puts by rank, so its vector order is its realised order.
* **Pricing**: `SameSubsetRevealSurcharge` walks the put at its sequenced position (a Pirate put that
  ranks after Daring Buccaneer is still in hand for the reveal -- the puts-last pricing RETRY is gone
  with the axis). The rescue-only sequential walk keeps the put's ETB Treasure up front.
* **Display**: `SummarizePlan` lists puts and casts in the realised order; `RealisedNonSacCastOrder` /
  `CanonicalNonSacCastOrder` include the puts.

Interleavings ("put A, cast B, put C"), order among the puts, and continuations are all expressed by
the same rule -- the three gaps this file used to list are closed by construction.

## What this means per deck

The Vial put position is now decided by the deck's cast order. Where a deck's provider ties its
creatures (Soldiers' generic order: every creature rank 10), ties fall to plan-vector position. The
user-proposed Soldiers order (`MTG_SOLDIERS_ORDER`, Champion of the Parish rank 1 -- default OFF, the
USER's pending decision, `cast-order-rankings.md`) puts every Human after the Champion; the scenario
`soldiers_vial_after_tutor_continuation` pins that flag to test continuation sequencing (T5 with it,
T6 under the generic tie).
