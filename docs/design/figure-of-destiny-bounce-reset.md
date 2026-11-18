# Figure of Destiny keeps its levelled size when it leaves the battlefield (DEFERRED)

Status: **deferred.** USER 2026-11-18: *"Let's handle this Figure of Destiny issue when we have time."*

## The defect

Figure of Destiny's abilities set its **base** power and toughness (CR 613.4b, layer 7b) to 4/4 and later 8/8.
The engine stores that as an override on the card. When the card leaves the battlefield it becomes a new object
(CR 400.7), so the override must go. A Figure returned to hand (or put into a library or the graveyard) should be
a printed 1/1 again. Today it keeps the override, so a re-cast Figure enters already levelled for free.

Trench Gorger (Sea Creatures) has the same shape: its ETB sets base P/T to the number of lands exiled. Gorger was fixed
in its own change: `RevealedCard` drops the override, gated on Gorger's param `etb_exile_library_lands_set_base_pt`.
Figure was left alone on purpose: fixing it can move play and ground truth for decks that run Figure (today: Kithkin).
See the Sea Creatures ledger, `docs/design/analysis-Sea Creatures.md`, Open USER question 11 and I6 (e).

## The fix

- Make `RevealedCard` (and any other zone-change path that copies a card out of a permanent) drop **every**
  base-P/T override when a card leaves the battlefield, not just Gorger's. It must hold in both worlds, the executor
  and the rollout.
- Unit test: level a Figure to 4/4, bounce it, then check the card in hand is 1/1, and that a re-cast Figure is 1/1.
- Measure on Kithkin (and any other deck that runs Figure at that time). Look for a byte-identity break and its
  cause, then do the smoke + regression accept for the decks that move. A slower game is expected only where the old
  free level-up was being used. That was a rules bug, so losing it is not a regression to recover.
