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

## 3. A graveyard cast can never precede a hand cast (completeness; found by integrator C1, Millicent)

**The hole.** Both apply worlds resolve every graveyard cast (flashback, retrace) in the TRAILING pass, after
every hand cast and Vial put (ApplyPlanDirect's trailing pass; AIEngine::TakeTurn). So a line whose hand
cast needs something the graveyard cast produces cannot be realised in one main. Spirits example: Lingering
Souls in the graveyard, Millicent, Restless Revenant in hand. "Flashback Souls ({1}{B}, two Spirit tokens),
then Millicent two cheaper" is mana-neutral and yields two more tokens. The plan menu offers both casts, but
the realised order is Millicent first, at the higher price. The same-turn affinity credit therefore
deliberately does NOT credit a CastFromGraveyard (`AffinityFeedCount`, TurnSolver.cpp). If the credit
counted it, the plan would be priced at a cost that neither world can pay.

**Reach.** It needs Souls in the graveyard and Millicent (a 1-of) in hand in the same main, with the mana
tight enough that the order decides affordability. The line is reachable a turn later: flashback this turn,
Millicent next turn.

**Fix shape.** Sequence graveyard casts at their card's CastOrderRank, as Vial puts already are (USER
2026-10-06: puts ride the cast sequence). That change touches integrator B's Hole A/B machinery (the
B1 pending post-pass and SubsetHasNonCanonicalGyCopies) and moves Gods, so it needs its own change set.
Then credit a CastFromGraveyard token maker in `AffinityFeedCount` when its rank precedes the affinity
card.

**Prove it first:** a `--budget-ms 0` scenario with Souls in the graveyard, Millicent in hand and the
lands set so that only flashback-first pays both.

## Adoption

Both are correctness fixes, not heuristics, so they need no quality bar beyond correctness. They still go
through the normal smoke/regression accept flow, with each slower searched game explained.
