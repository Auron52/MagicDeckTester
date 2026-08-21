# Stompy: top-of-library consumers vs library-writing tutors (deferred design)

**Status: DEFERRED — not being built yet.** Recorded per the deferred-work rule after the
StompySurprise cast-order review (USER, 2026-08-21; the order itself is implemented behind
`MTG_STOMPY_ORDER`, see `cast-order-rankings.md`). This doc holds the three modeling items the
cast-order rank *cannot* express, with the user's rulings verbatim.

## The problem

Worldly Tutor is not a draw — it is a **library write**: "search, shuffle, put on top". The USER:

> I just realized one problem with tutors like Worldly tutor. They search, but don't nicely draw
> the card. Instead they change the library. Arguably this ordering could be handled a different
> way. We could put the search cards above and trigger something like a breakpoint that re-enables
> cards that interact with the top of the library.

The deck's **top-consumers** are: Call of the Wild activations ({2}{G}{G}: reveal top; creature →
battlefield), Turntimber Symbiosis (look 7, put a creature), Vaultborn-style enters-draws (the
draw *is* the top card), and Mirri's Guile (upkeep arrange 3). A tutor mid-line changes what every
later consumer sees, so consumer decisions bound before the tutor resolves are stale — the exact
defect that hit the play viewer (seed 1 T4: Turntimber's put-candidates were collected pre-tutor,
so "put the tutored Craterhoof" was never offered; fixed for HUMAN play by moving the put to
resolution time off the real look).

## Item 1 — tutor as a breakpoint that re-enables top-consumers

The USER's [9] tier: "Worldly Tutor (resets Call of the Wild Activations and Turntimber when
cast)". Clarified rulings, verbatim: "We need to build the reset for my combo to be workable. To
be clear, it could also be done as a loop." / "Cast worldly tutor -> now activations and
Turntimber can be cast" / "I don't want the order to be static. It is a loop. If you play Tutor
you unlock Turntimber again as a possibility." / "You can think of it as a do-while loop. That
continues as long as we cast Tutors."

So the tier list is the order of ONE PASS, and a tutor cast opens the next pass with the
consumer tiers re-enabled — a do-while over passes whose condition is "this pass cast a tutor".
Natural Order sits OUTSIDE the loop (USER: "natural order does not need to be part of the loop.
It could go before the other effects except that it shuffles") — it never consumes the top and is
never re-enabled. Its POSITION was updated the next day (USER, 2026-08-21): late by default (just
before Craterhoof — "avoid us sacrificing creatures early and... drop a powerful Craterhoof with
it"), early only to keep its shuffle off a live tutor stack; and a new tier appeared between the
late NO and Craterhoof — "Worldly tutor loop one time (if searching for Craterhoof)" — a tutor
cast AFTER the late Natural Order whose stacked hoof the loop's consumers then drop. The static
rank cannot see a tutor's target, so that late-tutor-for-hoof placement rides on this Item's
re-solve, not on the order. Full verbatim + rank mapping in `cast-order-rankings.md`.
Intended shape: consume the unknown top first (Call activations, a first Turntimber),
then the tutor stacks a known creature, then the consumers go again (another activation round, a
second Turntimber copy). A static cast rank cannot express "A, then B, then A again" — two copies
of the same card share one rank. What can: the existing **breakpoint re-solve** machinery
(draw/staging/cascade cards already re-solve the rest of the turn from the post-resolution state;
`MTG_ACQ_RESOLVE` deliberately armed the same re-solve for tutor-to-HAND). Arming a re-solve after
`tutor_to_top` resolution would:

* let the search enumerate the post-tutor continuation (a fresh Call activation, the second
  Turntimber) against the *real* stacked top, and
* close the **autonomous composition gap**: Turntimber's clairvoyant named candidates are
  collected at action-collection time, pre-plan, so a plan casting Worldly Tutor(X) before
  Turntimber can never bind "put X" today (human play no longer cares — resolution-time modal —
  but the search still cannot compose the line).

Cost caution: breakpoints multiply rollout work; Turntimber was measured as this deck's #1
branching driver before the width cap. Any implementation must be measured on the suite like every
other lever.

## Item 2 — upkeep Call of the Wild activation (pre-draw window)

The USER, verbatim:

> Note that Call of the Wild Activation in Upkeep is a real play that we may need to model.
> If you have just the mana to cast Worldly Tutor at the end of turn this can be important.
> Since you often don't want to draw the card, but dump it into play for 4. (if you searched for
> it anyway)

The line: end-of-turn Worldly Tutor (cast-order rank 24) stacks a fatty; next turn's UPKEEP
activation puts it into play for 4 mana **before the draw step would pull it into hand** (where a
7–11 MV body must be hard-cast). Today Call activations exist only as main-phase plan actions —
by main phase the draw has already eaten the stacked card, so the engine cannot represent the
line's whole point.

**Gate — intentionally-stacked tops only.** The USER, verbatim:

> If you didn't stack the top I don't think it's as important to enable this, so we potentially
> could just allow it in those cases.
> I'm not particularly interested in having it take advantage of clairvoyance to do the same when
> it was not put there intentionally.
> Though I'm sure this will still happen from activations during the turn as well. (in that
> activations will look good when a bomb is coincidentally on top.

So the upkeep window should exist **only when the top was stacked by an intentional write** (a
`tutor_to_top` resolution since the last draw, or a Mirri's Guile arrangement) — never because the
clairvoyant search happens to know an unstacked top is a creature. (An unstacked upkeep
activation is the same blind gamble as the main-phase one and needs no new window; and yes,
in-turn main-phase activations already exploit clairvoyance — that is a known systemic property
of the search, acknowledged above, not something this window should add to.) Implementation
shape when picked up: an upkeep decision point in the vial-charge mold (heuristic default:
activate iff a stacked-known creature is on top and the mana doesn't strand the turn's plan;
human play surfaces it as a modal; state tracks "top stacked by <source> since last draw").

## Item 3 — Call-activation position within the turn (the [6] tier)

The USER's tier [6] places Call activations between the Call cast [5] and the cheat-out casts
[7]/[8]. The executor dispatches every activation AFTER every cast (trailing dispatch) — which is
exactly right for the tutor → activation compose (rank 14 tutor resolves before the trailing
activation reads the top), but cannot express "activate on the unknown top BEFORE the tutor
stacks it" or any cast-activation interleaving. If Item 1's breakpoint lands, the re-solve
subsumes this; a standalone fix (activations joining the ordered sequence like MTG_GARTH_ORDERED
did for Garth) is the fallback route.
