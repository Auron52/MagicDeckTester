# The passive opponent's library, hand, and draw step

**Status: CLOSED 2026-09-02** (`src/core/OpponentDeck.h`). Recorded 2026-09-02 from a user
observation while diagnosing [analysis-EldraziDisplacerFlicker.md](analysis-EldraziDisplacerFlicker.md);
implemented the same day. The history below is kept because the *shape* of the bug — and the two
traps it set for whoever fixed it — is the reusable part.

## What used to be modelled

`GoldFishRunner::SetupGame` gave the opponent **exactly one thing**: a life total.

```cpp
state.players[0].life = start_life;
state.players[1].life = start_life;     // <- the only write to players[1] anywhere in src/
state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());
```

Grepping every non-`life` write to `players[1]` across `src/` returned **nothing**. So:

* `players[1].library` was **empty from turn 0** and never filled;
* the opponent never becomes the active player, so `GameEngine::DrawStep` never runs for them;
* `player_lost_on_draw` is set at exactly one site (`GameEngine.cpp`) and only for `ActivePlayer()`.

## Why it mattered: it silently deleted a whole class of win condition

**Milling or decking the opponent could not win, and could not even be attempted.** There was no
draw for them to fail, so CR 104.3c never fired; a mill effect aimed at the opponent was a no-op
against an already-empty zone.

Not hypothetical. `EldraziDisplacerFlicker`'s original decklist ran **Stroke of Genius**
(`{X}{2}{U}`, "Target player draws X cards") as its intended fast kill: with unbounded mana, point
it at the opponent with X = their library and they lose on the next draw. That line was impossible
twice over — the card was ALSO implemented as a no-op (`template: draw_x`, empty `parameters`) —
which left the deck with only Shivan Gorge's `{2}{R}, {T}` grind, needing a fresh untap for every
one of ~20 activations. Measured: **zero wins before turn 5 in 800 games, 14% never winning, 7.20
avg** on a deck the user expects to kill around turn 4-5.

## What is modelled now

A **fixed** 60-card list (24 lands / 20 creatures / 16 spells), identical for every deck under test,
dealt with a 7-card opening hand. They still never cast, block, or decide anything.

Fixed rather than a mirror of the deck under test, deliberately: deck-out depth becomes a
**constant** (53 cards after the opening hand), so a mill measurement is comparable across decks
instead of being a function of the mill deck's own size.

"Realish" is not decoration — the distribution is **read**. Dimensional Infiltrator exiles the top
card and cares whether it is a **land**, so the 24/36 split sets a real hit rate; discard effects
need a hand to bite on, hence the opening seven.

**The draw is simulated at the end of each of OUR turns.** There is no opponent turn to hang it on.
That slot is faithful either way — on the play, their turn N follows ours; on the draw, their turn
N+1 follows our turn N and their turn 1 (where they are on the play) correctly draws nothing — and
it is what makes the win turn come out right with no special-casing anywhere. Per the user
(2026-09-02): *"the win turn for the case where the opponent decks out should be listed as your last
turn ... because the user doesn't get any main phases."* Firing before the turn increment sets
`opponent_decked` while `turn_number` is still ours, so the ordinary win check reports that turn.

**It is gated.** `GameState::opponent_library_dealt` is derived from the decklist by
`GoldFishRunner::DeckTouchesOpponentZones`; a deck with no way to touch those zones keeps the old
model and is byte-identical. The gate also keeps 53 extra card hashes per dominance key off the hot
path for every such deck.

**The gate scans BOTH boards**, which is load-bearing rather than incidental: this deck's only
library-toucher (Dimensional Infiltrator) sits in the **sideboard**, reachable off Living Wish. A
mainboard-only scan would leave the opponent with no library, so the Infiltrator would exile from an
empty zone forever and the deck's second win condition would silently not exist — the same defect
the coverage scan had (see `SideboardReachability` in `scripts/analyze_deck.py`).

## The two traps, for anyone touching this again

**1. Empty is not absent.** `players[1].library` being **empty rather than missing** is the
dangerous shape. The obvious test for "have I decked the opponent?" —

```cpp
if (state.Opponent().library.empty()) { /* we win */ }      // WRONG
```

— is **true on turn 0**, so it hands out a spurious instant win in every game of every deck. Any
deck-out condition must be gated on the opponent having actually been dealt a library. Use
`OpponentHasLost(state)` (GameState.h), which does it for you.

**2. One predicate, both worlds.** The win check was scattered: the executor asked
`GameEngine::CheckWinCondition` while the rollout open-coded `Opponent().life <= 0` at **30 sites**.
Add a non-damage win condition to only one side and you get the worst available failure — an
executor that *recognises* the win and a search that never *pursues* it, so the line is played only
by accident. That is exactly the Dragonstorm go-off that executed as a kill zero times. All 30 now
route through `OpponentHasLost`, and the opponent's draw fires in **both**
`GameEngine::RunTurnFrom` and `TurnSolver::SimulateEndAndStartNextTurn`.

## Cost: none, and the reason is worth knowing

Smoke came back **48/48 byte-identical** — same digests, zero play changes, zero searched
slowdowns — so this needed no GT rebaseline at all. That was not luck. `Library::Shuffle` takes an
**explicit seed** rather than drawing from a shared RNG stream, so dealing a second library off a
derived salt cannot perturb player 0's permutation by a single card. Had the engine used one shared
stream, giving the opponent a library would have re-permuted every deck's opening hand and cost a
full three-tier rebaseline. Worth checking for the shared-stream shape before assuming any new
random draw is free.

## The other lesson: do not write an engine gap down as a card fact

The Stroke of Genius entry justified targeting **self** with: *"'Target player' = self (handing the
passive opponent cards is strictly pointless -- the same collapse Braingeyser carries)."* That reads
as a statement about the card. It was really a statement about **this engine**: handing the opponent
cards was pointless *only because* they had no library to run out of. A real opponent makes that the
card's primary mode — and now that they have one, that note is simply wrong.

Same family as the shroud miss on Trace of Abundance (an "inert" clause that actually constrained
our own plays). Both come from reasoning about a card against the CURRENT opponent model and then
recording the conclusion as though it were a property of the card. When a clause is inert only
because of a modelling limitation, say **which limitation**, so the note self-destructs when the
limitation is lifted.

## Still not modelled

The opponent does not **play** anything they draw, does not attack or block, and takes no actions of
any kind. Only their *zones* exist. Extending them to actually play the deck is a much larger piece
of work (a real Phase 2 opponent) and is not implied by any of the above.
