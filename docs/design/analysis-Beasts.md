# Analysis ledger — Beasts

Started 2026-11-21. Worktree `.claude/worktrees/beasts`, branch `beasts-analysis`, based on
`origin/phase-1-2-deck-analyzer` @ c9a2e1ff6. Deck file `decks/Beasts/Beasts.cod` (copied from the
primary checkout's untracked `decks/Beasts/`, original left in place). Nothing pushed yet.

## Checkpoint (update after every milestone)

* **Last completed stage:** Stage 1 coverage.
* **Current stage:** Stage 2 per-card research fan-out (13 Opus agents, read-only drafts).
* **Running jobs:** none.
* **Next:** serial integration of the drafts → 2d review → 2d-bis audits → build → Stage 3.

## Deck

60 cards, G/r Beast tribal ramp: 23 lands (incl. 2 Contested Cliffs, Nykthos, 2 Cavern of Souls,
1 Gruul Turf, 7 fetches), 8 land Auras (Utopia Sprawl, Wild Growth), 2 Urza's Incubator, 4 Krosan
Warchief, 4 Herald of Ilharg, payoffs (Elder Gargaroth, Thragtusk, Questing Beast, Craterhoof,
Garruk, Packleader, …). Sideboard: 1 Kaheera (companion residue — see open questions).

Every creature is a Beast EXCEPT **Bloated Contaminator** (Phyrexian Rat). That matters for
Cavern of Souls (colour restricted to the chosen type), Urza's Incubator (chosen type), Krosan
Warchief (Beast spells), Kaheera's lord clause, and Kaheera's companion condition.

## Stage 1 — coverage (2026-11-21)

Missing (13): Contested Cliffs, Thragtusk, Krosan Warchief, Elder Gargaroth, Questing Beast,
Woodland Bellower, Cragplate Baloth, Garruk's Packleader, Garruk's Harbinger, Migloz Maze Crusher,
Nykthos Shrine to Nyx, Bloated Contaminator, Kaheera the Orphanguard.

Already implemented (13): Forest, Mountain, Gruul Turf, Stomping Ground, Wooded Foothills,
Windswept Heath, Garruk Primal Hunter, Craterhoof Behemoth, Herald of Ilharg, Urza's Incubator,
Utopia Sprawl, Wild Growth, Cavern of Souls.

Bracket notes to re-check for THIS deck:
* **Cavern of Souls** — "ETB creature-type choice simplified to 'any creature' — exact for a
  tribal list whose creatures share the named type". NOT exact here: Bloated Contaminator is not a
  Beast, so Cavern's coloured mana must not pay for it. → Stage 2 gap.
* Fetch-hold review (Wooded Foothills, Windswept Heath): hold is modelled; the deck has
  landfall-free fetch use, so hold mostly matters for shuffle/Garruk's Harbinger dig timing. To
  review at Stage 5.

## Stage 2 — cards

| card | tier | status | notes |
|---|---|---|---|
| (filled in as drafts land) | | | |

## Viewer classifications (2c-ter)

(pending)

## Verification verdicts (Stage 5)

(pending)

## Open USER questions (surfaced, not blocking)

1. **Kaheera companion (sideboard).** Kaheera's companion condition ("each creature card in your
   starting deck is a Cat, Elemental, Nightmare, Dinosaur, or Beast card") is NOT met: Bloated
   Contaminator is a Phyrexian Rat. So the sideboard Kaheera cannot be a legal companion and is
   left unmodelled (the sideboard is unreachable). Default taken: ignore the sideboard. If the
   intent was Kaheera as companion, Contaminator would have to go.

## Approved deferrals

(none yet)
