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

## Every deck (USER 2026-11-17)

The USER, on a stolen "Opponent Nonbasic Land" producing no mana (Sea Creatures, Kiora Bests the
Sea God chapter III): *"I think the obvious answer is to change it to use the approach that mill
does where the decks are randomized and the left land is played from hand. That way, they all
generate something."* and *"Ideally, I would like to do that for all decks."*

So `GoldFishRunner::SetupGame` (and the `--scenario` harness) now deal **every** deck's passive
opponent a seed-selected list from the fifty-deck pool (`opponentdeck::Selected`, the same list and
shuffle Mill's opponent gets for that seed), a seven-card hand and one draw per turn, and play the
leftmost land from its hand each notional turn (`ResolvePassiveOpponentLandTurn`; on the draw it
also plays one before our first turn). `GoldFishRunner::DeckNeedsOpponentLandPlay` is gone. A stolen,
targeted or counted opponent land is a real card with its own mana ability, types and subtypes.

What did NOT change:
- Player 0's shuffle: the opponent deal uses a salted derived seed, so our draws are unchanged.
  A deck that never reads the opponent's board should therefore play byte-identical games; any such
  deck whose games move points at an engine site that reads the battlefield without checking the
  controller.
- `StampDeckTraits`' opponent cost-reducer / spell-tax scans still read
  `opponentdeck::List(seed, DeckUsesDiverseOpponentPool(deck))`. The passive opponent never casts,
  so that scan only widens a performance gate.
- References recorded before this model carry no `opponent_land_model` stamp and replay under
  `--legacy-passive-opponent-lands` (the three-placeholder board, no library unless the deck
  touches opponent zones), exactly as Hinata / Midrange Burn references did from 2026-11-15.

Stale notes this makes: several `cards.json` bracket notes (Garth One-Eye's Stroke conjure, Unite
the Coalition) say the opponent target is dropped "because FiveColour deals the passive opponent NO
library". The opponent now always has a library; modelling those targets is a separate card change.

## 2HG: two opponent heads (USER 2026-11-17)

The USER: *"you are correct that there should technically be two and they should both play. Ideally we
would adjust it that way, but mill should just target one opponent, probably the one that has cheaper
cards in deck. (lower total MV)"*

- `opponentdeck::Deal(..., heads)` deals 2HG a second, distinct pool list (`SecondSlot`: a salted slot
  other than `seed % 50`). `players[1]` holds whichever list has the lower total printed mana value
  (`ListTotalManaValue`, lands 0); that head is the one every library/hand effect addresses (mill,
  exile-from-top, forced draws, Surgical Extraction, Tasha, Archive Trap's mill). The chosen slots are
  `GameState::opponent_slot` / `opponent2_slot`, so the viewer and Surgical's printed-list read follow
  the actual deal.
- The second head (`GameState::opponent2_hand` / `opponent2_library`, numbers from 110000) draws at
  the end of each of our turns, plays its leftmost land on the opponent's turn (cracking a fetch for
  the team's life, shuffling its own library on its own ordinal), and discards to seven. Its lands
  enter under controller 1 beside head 1's: no card in the pool counts lands per opponent (landwalk,
  Price of Progress and Sunspine Lynx read the team or the per-player sum). Running its library out
  is a team loss (CR 810.8a), on the same next-draw rule.
- Either head's fetch opens Archive Trap.
- Not modelled for the second head: draw caps, Howling Mine, hand-reading upkeep/end-step effects
  (Asylum Visitor, Sire of Insanity), and its graveyard. Nothing in the suite's 2HG decks reaches them.
- `heads == 1` is exactly the single-opponent deal; a legacy replay never deals a second head.

## Search cost

Every deck now copies an opponent library and hand with each search state (a `Card` is 136 bytes, so
53 library cards is ~7 KB per copy; 2HG adds the second head). To keep the dominance key cheap, a deck
with no card that reads the opponent library (`GameState::opponent_library_read`, a deck constant set
from `DeckTouchesOpponentZones || DeckUsesDiverseOpponentPool`) folds only that library's size and
shuffle ordinal: nothing else can change it. The copy cost is measured in the suite run that lands
this change; a copy-on-write library is the follow-up if it shows.
