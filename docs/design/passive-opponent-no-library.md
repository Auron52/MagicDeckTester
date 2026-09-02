# The passive opponent has no library, hand, or draw step

**Status: OPEN GAP.** Recorded 2026-09-02 from a user observation while diagnosing
[analysis-EldraziDisplacerFlicker.md](analysis-EldraziDisplacerFlicker.md).

## What is actually modelled

`GoldFishRunner::SetupGame` gives the opponent **exactly one thing**: a life total.

```cpp
state.players[0].life = start_life;
state.players[1].life = start_life;     // <- the only write to players[1] anywhere in src/
state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());
```

Grepping every non-`life` write to `players[1]` across `src/` returns **nothing**. So:

* `players[1].library` is **empty from turn 0** and never filled;
* the opponent never becomes the active player, so `GameEngine::DrawStep` never runs for them;
* `player_lost_on_draw` is set at exactly one site (`GameEngine.cpp`) and only for `ActivePlayer()`.

## Why it matters: it silently deletes a whole class of win condition

**Milling or decking the opponent cannot win, and cannot even be attempted.** There is no draw for
them to fail, so CR 104.3c never fires; a mill effect aimed at the opponent is a no-op against an
already-empty zone.

This is not hypothetical. `EldraziDisplacerFlicker`'s original decklist ran **Stroke of Genius**
(`{X}{2}{U}`, "Target player draws X cards") as its intended fast kill: with unbounded mana, point it
at the opponent with X = their library and they lose on the next draw. In this engine that line is
impossible twice over — the card was ALSO implemented as a no-op (`template: draw_x`, empty
`parameters`) — which left the deck with only Shivan Gorge's `{2}{R}, {T}` grind, needing a fresh
untap for every one of ~20 activations. Measured result: **zero wins before turn 5 in 800 games,
14% never winning, 7.20 avg** on a deck the user expects to kill around turn 4-5.

## The trap for whoever implements this

`players[1].library` being **empty rather than absent** is the dangerous shape. The obvious test for
"have I decked the opponent?" —

```cpp
if (state.Opponent().library.empty()) { /* we win */ }      // WRONG TODAY
```

— is **true on turn 0**, so it would hand out a spurious instant win in every game of every deck.
Any deck-out condition must be gated on the opponent having actually been given a library, not on
the zone being empty.

## The other lesson: do not write an engine gap down as a card fact

The Stroke of Genius entry justified targeting **self** with: *"'Target player' = self (handing the
passive opponent cards is strictly pointless -- the same collapse Braingeyser carries)."* That reads
as a statement about the card. It is really a statement about **this engine**: handing the opponent
cards is pointless *only because* they have no library to run out of. A real opponent makes that the
card's primary mode.

Same family as the shroud miss on Trace of Abundance (an "inert" clause that actually constrained our
own plays). Both come from reasoning about a card against the CURRENT opponent model and then
recording the conclusion as though it were a property of the card. When a clause is inert only
because of a modelling limitation, say **which limitation**, so the note self-destructs when the
limitation is lifted.

## Shape of a fix (not started)

Contained but **not cheap**, because it moves every deck's baseline:

1. Give `players[1]` a library at setup (a copy of the deck under test, or a fixed neutral list —
   this is a real choice: a mirror makes deck-out depth deck-dependent, a fixed list makes it a
   constant across decks and therefore comparable).
2. Run a draw step for them, or simulate one, so `player_lost_on_draw` can fire on their side.
3. Give them a hand, if any card ever reads opponent hand size.
4. Gate any deck-out win on "the opponent was actually dealt a library" (see the trap above).

**Cost:** this changes results for every deck that touches opponent draw/mill, and any change to
turn structure is a full GT rebaseline across all three tiers. It should be its own piece of work
with its own A/B, not a rider on a deck analysis.

**Not needed for the current deck.** `EldraziDisplacerFlicker2` kills with Essence Depleter
(`{1}{C}: Target opponent loses 1 life and you gain 1 life`) — life loss, which the opponent does
have. So this gap is recorded, not blocking.
