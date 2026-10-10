# Fetchland colours should come from the library, not a static `produces` (DEFERRED)

Status: DEFERRED -- found 2026-10-09 by the review of the Creature Giving discard policy, verified
2026-10-10. A play-affecting modelling gap; not fixed because the right fix is deck-aware, not a data edit.

## The issue (verified)

A fetchland never taps for mana: `PerformFetch` sacrifices it and puts a chosen library land onto the
battlefield. Its `produces` in `src/cards/data/cards.json` exists ONLY so a fetchland held in HAND
counts as a flexible colour source for the fixing heuristics (its own oracle note says so). That list is
static, but the colours a fetch can really reach depend on which Forest / Plains / Island ... typed lands
the DECK runs:

| Fetchland | static `produces` | Deck | Fetchable typed lands in the list | Reachable colours |
|---|---|---|---|---|
| Windswept Heath (Forest or Plains) | W B R G | Creature Giving | Forest, Temple Garden, Overgrown Tomb, Stomping Ground, **Breeding Pool** | W B R G **U** |
| Windswept Heath | W B R G | FiveColour | Overgrown Tomb, Stomping Ground, Godless Shrine, **Breeding Pool** | W B R G **U** |
| Windswept Heath | W B R G | Anti-Lifegain | Forest, Temple Garden, Overgrown Tomb, Stomping Ground, Godless Shrine | W B R G (correct) |
| Misty Rainforest (Forest or Island) | W U B R G | (its note: "this deck's ... targets reach all five colours") | | per deck |

So a Heath in hand is under-counted for blue in Creature Giving and FiveColour, and correct in
Anti-Lifegain. No single static value is right for all three decks; adding U would over-count for
Anti-Lifegain.

## Fix sketch (not implemented)

Compute a hand fetchland's colours as the union of `produces` over the lands still in OUR LIBRARY whose
types match its `fetch_land_types` (counts only -- the same non-clairvoyance standard as
`DiscardProtectScope::LastInDeck`), at the heuristic sites that read a hand fetchland's colours (the
fixing heuristics, the discard buckets' colour cover, `discard_dist::LandDropOf`). Keep the static
`produces` as the fallback when the library is unavailable. It changes play wherever a hand fetchland's
colours are read, so it needs the usual A/B and GT verdicts for Creature Giving, FiveColour and
Anti-Lifegain.
