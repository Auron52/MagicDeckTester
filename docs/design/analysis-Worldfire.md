# Analysis ledger -- Worldfire

Deck: `decks/Worldfire/Worldfire.cod` (60 cards, U/R). Started 2026-11-21.

Plan: suspend a finisher (Rift Bolt, Keldon Halberdier) and/or Lotus Bloom, ramp to {6}{R}{R}{R}
(Seething Song, Irencrag Feat, Lotus Bloom, Mercadian Bazaar storage, Sandstone Needle /
Saprazzan Skerry depletion lands), dig with Brainstorm / Ponder / Preordain / Overmaster and the
tutors (Mystical, Personal), cast Worldfire: every permanent, hand and graveyard is exiled and both
life totals become 1; the suspended finisher (in exile, untouched) kills next turn.

## Stage 1 -- coverage (2026-11-21)

Missing (6): Worldfire, Mystical Tutor, Keldon Halberdier, Personal Tutor, Overmaster,
Mistrise Village. Every other card `full`. Fetch-hold review required (Scalding Tarn, Flooded
Strand; hold modelled).

| card | Scryfall cost | proposed tier | notes |
|---|---|---|---|
| Worldfire | {6}{R}{R}{R} sorcery | 3 | new effect: exile all permanents, all hands, all graveyards; each player's life becomes 1. Suspended cards (exile) survive; floating mana survives. |
| Keldon Halberdier | {4}{R} 4/1 first strike | 2/3 | first creature with suspend (4 -- {R}); off-suspend creature has haste (CR 702.62a). First strike inert (no blockers). |
| Mystical Tutor | {U} instant | 1 | tutor_to_top Instant/Sorcery, shuffle; upkeep cast as Worldly Tutor. |
| Personal Tutor | {U} sorcery | 1 | tutor_to_top Sorcery, shuffle. |
| Overmaster | {R} sorcery | 1 | draw 1; "next instant/sorcery can't be countered" inert (passive opponent). |
| Mistrise Village | land | 1 | U, enters tapped unless you control a Mountain or a Forest (checkland_subtypes); {U},{T} uncounterable rider inert. |

Memory Lapse is a faithful dead card (no opponent spells).

## USER rulings this analysis

- 2026-11-21: opponent-lockdown FREE TURNS -- see `docs/design/opponent-lockdown-free-turns.md`
  (verbatim proposal + provisional defaults).

## Approved deferrals

(none yet)

## Provisional deferrals (await USER sign-off)

(none yet)
