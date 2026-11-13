# Graveyard-cast search holes — deferred (found during the Spirits analysis, 2026-11-13)

Status: **DEFERRED** (not being worked on). Found by the Spirits Lingering Souls integration (commit
8097213a2), which fixed the flashback instances of the same two hole shapes. These are the siblings that
remain, in decks other than Spirits.

## 1. Graveyard-return cards are invisible to the graveyard state key (soundness)

`tutor_from_graveyard` cards (Mystic Retrieval's hand cast, Reborn Hope, Auroral Procession) read the
graveyard, but no graveyard-reader bit covers them. 8097213a2 added flashback to the three sites that
already covered retrace:
- `DeckGraveyardReaders` (src/runner/GoldFishRunner.cpp)
- the graveyard fold in TurnSolver's state key
- the named-card check in `Dominance.h`

Graveyard-return cards are still missing from all three. Two states that differ only in graveyard contents
can therefore be merged or dominance-pruned even though a regrowth target differs.

**Decks that may move:** Gods (Mystic Retrieval) and Bruna (Reborn Hope, Auroral Procession: sideboard
wish targets).

**Fix:** treat a deck carrying `tutor_from_graveyard` as a graveyard reader at the same three sites. Then
A/B the affected decks per game, as a play change through the normal accept flow.

## 2. Retrace is enumerated once per NAME (completeness)

The graveyard block offers one retrace cast per card name present at the root frame. So "retrace Throes of
Chaos twice in one main phase" (two copies in the graveyard, enough lands to discard) cannot be found at
any budget. 8097213a2 fixed the flashback version of this:
- one candidate per graveyard copy;
- `SubsetHasNonCanonicalGyCopies` keeps the first k of identical copies, so the fold stays lossless.

**Fix:** the same per-copy enumeration for retrace, in both enumerators.

**Prove it first** with a `--budget-ms 0` scenario that reads the slower win at HEAD.

**Deck that moves:** TreasureHunt.

## Adoption

Both are correctness fixes, not heuristics, so they need no quality bar beyond correctness. They still go
through the normal smoke/regression accept flow, with each slower searched game explained.
