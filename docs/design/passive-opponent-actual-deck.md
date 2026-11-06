# Passive opponent with an actual deck

User direction, 2026-11-05: "We could defer Goblin Guide's trigger for now, but I
think going forward I would like to have the opponent have cards from an actual
deck Maybe once we have 50 in our list we can just order all of those and have
them play a land a turn from hand until none remain. The idea would be to have
it be very low-overhead, but still provide a library and hand that we can
interact with."

## Current model

`src/core/OpponentDeck.h` already creates a deterministic 60-card library from
real entries in `cards.json` (24 lands, 20 creatures, 16 other spells), deals a
seven-card hand, and draws one card at the end of each active-player turn. It is
enabled only when the tested deck touches the opponent's zones. The opponent
does not play cards. `GoldFishRunner::SetupGame` separately starts the opponent
with three inert basic/nonbasic land placeholders for land-targeting effects.
Those battlefield lands are unrelated to the dealt hand and library.

## Intended follow-up

Use a selected actual decklist as the passive opponent's library. Keep the
opponent's seeded shuffle, opening hand, and one draw per notional turn. On each
notional turn, move one land from its hand onto the battlefield while one is
available, with no spell casting or strategic opponent search. Keep card names,
types, subtypes, and zone transitions available to Goblin Guide, discard,
mill, reveal, and land-targeting effects. Cache the source list and keep the
turn step O(hand size), with no opponent decision tree.

The user has not yet specified which actual decklist supplies the opponent or
what "50 in our list" refers to. Resolve those choices before replacing the
current fixed deck or the three starting land placeholders. The two-headed
abstraction currently has one opponent library and hand for both targetable
heads; decide whether this follow-up should retain that abstraction or give
each head its own zones.

This is future work separate from the Modern RW Burn card analysis. Goblin
Guide's trigger may be deferred for that analysis if needed; the user has
explicitly allowed it, but an already working implementation should be kept
and verified rather than discarded merely to create a deferral.

## Deck onboarding dependency

User clarification, 2026-11-05: some decks interact too heavily with the
opponent's library or hand to be added under the current model. Mill is one of
them and is deliberately not onboarded yet. Implement and validate this actual
deck/land-play model before analyzing those decks. The current fixed library
supports limited card interactions but is not the accepted foundation for
heavy library or hand strategies.
