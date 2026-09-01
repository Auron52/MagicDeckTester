# Analysis ledger — EldraziDisplacerFlicker

Per-deck ledger for the `analyze-deck` skill run started **2026-09-01**. This file is the durable
state for the run (survives compaction and hand-off); update it as stages complete.

Deck: [decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod](../../decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod)

## What the deck is

An **infinite-mana flicker combo** deck. The engine is a flicker outlet plus a creature whose ETB
untaps lands, so each activation nets mana:

| piece | role |
|---|---|
| Eldrazi Displacer `{2}{W}` 3/3 | outlet — `{2}{C}: Exile another target creature, then return it to the battlefield **tapped**` |
| Emiel the Blessed `{2}{W}{W}` 4/4 | outlet — `{3}: Exile another target creature **you control**, then return it` (untapped); also a `{G/W}` +1/+1-counter trigger on each other creature ETB |
| Peregrine Drake `{4}{U}` 2/3 flying | payload — ETB **untap up to five lands** |
| Cloud of Faeries `{1}{U}` 1/1 flying | payload — ETB **untap up to two lands**; Cycling `{2}` |
| Training Grounds `{U}` | activated abilities of your creatures cost `{2}` less (floor: one mana) → Emiel `{3}`→`{1}`, Displacer `{2}{C}`→`{C}` |
| Wild Growth / Fertile Ground / Overgrowth / Trace of Abundance | land Auras: enchanted land taps for **+1 / +1 any colour / +{G}{G} / +1 any colour** |
| Shivan Gorge (Legendary Land) | the **kill**: `{2}{R}, {T}: deals 1 damage to each opponent` — re-untapped by the payload every iteration |
| Stroke of Genius `{X}{2}{U}` / Mariposa / Conservatory / Kitchen | draw / Clue outlets |
| Eladamri's Call `{G}{W}` | tutor a creature to hand (finds the missing combo piece) |

Neither outlet's ability has `{T}` in its cost, so **summoning sickness does not gate the combo** —
an outlet can go off the turn it lands.

## Stage 1 — coverage (2026-09-01)

`missing` (17): Eldrazi Displacer, Emiel the Blessed, Cloud of Faeries, Peregrine Drake, Wild
Growth, Overgrowth, Fertile Ground, Yavimaya Coast, Adarkar Wastes, Mariposa Military Base, Trace
of Abundance, Conservatory, Kitchen, Shivan Gorge, Eladamri's Call, Stroke of Genius, Training
Grounds.

`full` with bracket notes (2): Brushland, Azorius Chancery.

### Reclassified bracket note — Brushland's `{C}` mode is NOT inert for this deck

Brushland ships as a G/W painland with the note *"The free `{C}` mode is not modelled — our life
loss is inert for the goldfish clock"*. That reasoning does not hold here: **Eldrazi Displacer's
activation costs `{2}{C}`**, and `{C}` is a colourless *pip* that coloured mana cannot pay. A
painland's `{T}: Add {C}` mode is therefore a live, combo-relevant mode in this deck. Same for
Yavimaya Coast and Adarkar Wastes (both new). → treated as a **gap**, not a deferral.

## Open items / provisional decisions

(updated as the run proceeds)

## Approved deferrals

(none yet — every proposed deferral is PROVISIONAL until the user signs it off)
