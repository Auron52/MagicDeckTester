# Analysis ledger: Enchanted Evening

Worktree `.claude/worktrees/enchanted-evening`, branch `ee-analysis` (from 05e2e0030). Nothing pushed yet.
Decklist: `decks/Enchanted Evening/Enchanted Evening.cod` (60 cards, GW enchantress / lock).

## USER rulings (verbatim, 2026-11-25)

1. *"Note that the deck locks down the opponent so, like Worldfire, we will need to take that properly into account. In
   particular Calming Verse with Enchanted Evening out removes everything and temporarily locks their lands. Enchanted
   Evening and Opalescence permanently locks the lands, though we should consider cards they can play prior to being
   locked to see if it is a true lock. One Parallax Wave is also this is considered a full lock because any creature can
   be exiled using the Parallax Wave + Opalescence combo and all permanents except Opalescence are creatures."*
2. *"(the lock idea is all about getting free turns in particular)"*
3. *"But with the full combo out here, we can attack with Enchanted Evening and Parallax Wave, ending the game pretty
   quickly."* / *"(while the opponent can do nothing)"*
4. *"It should be one 5/5 and one 4/4."* / *"(only enchanted evening costs 5, parallax wave is 4)"*

Reading: the lock is credited through the EXISTING free-turn rule (`MTG_FREE_TURNS`, default ON,
`src/core/FreeTurns.h`, `docs/design/opponent-lockdown-free-turns.md`): a locked opponent turn does not advance the
round. The deck's lock cards must join `GoldFishRunner::DeckMayLockOpponent`. Under EE + Opalescence the attackers are
Enchanted Evening 5/5 (MV 5) and Parallax Wave 4/4 (MV 4), plus the other non-Aura enchantments at P/T = MV.

## Stage 1 coverage (2026-11-25)

Missing (9): Enchanted Evening, Calming Verse, Sterling Grove, Enchantress's Presence, Opalescence, Force of Vigor,
Parallax Wave, Sunpetal Grove, Oblivion Ring. Implemented already: Utopia Sprawl, Wild Growth, Forest, Temple Garden,
Exploration, Windswept Heath, Plains, Idyllic Tutor, Wooded Foothills. Fetch-hold review owed (Heath, Foothills).

## Stage 2 work split

| group | cards | owner | status |
|---|---|---|---|
| simple | Sunpetal Grove, Oblivion Ring, Enchantress's Presence, Sterling Grove | implementer agent A | in progress |
| lock cluster | Enchanted Evening, Opalescence, Calming Verse, Parallax Wave, Force of Vigor | design agent B -> `docs/design/enchanted-evening-lock-design.md`, then an implementer | design in progress |

## Cards (tier, viewer bucket)

_(filled in as each card lands)_

## Open USER questions

_(none yet)_

## Approved deferrals

_none_
