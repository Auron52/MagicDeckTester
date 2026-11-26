# Zombies analysis ledger

Worktree: `.claude/worktrees/zombies-analysis` (`wip/zombies-analysis-1126`).
Started 2026-11-26 from shared commit `9e566000b`. The deck file was copied
from the primary checkout, where it is still untracked; keep that original.

## Coverage

`python3 scripts/analyze_deck.py decks/Zombies/Zombies.cod --coverage-only`
found 11 missing cards: Blood Scrivener, Gray Merchant of Asphodel, Undead
Warchief, Death Baron, Dread Wanderer, Cemetery Reaper, Lord of the Undead,
Gravecrawler, Lord of the Accursed, Diregraf Ghoul, and Fleshbag Marauder.
Culling the Weak, Dark Ritual, and Swamp are covered. There is no sideboard or
fetch-hold question.

## Investigation

Live Scryfall Oracle and engine-pattern research completed for the 11 missing
cards. No implementation has been made yet:

| Card | Likely implementation | Choice / risk |
| --- | --- | --- |
| Diregraf Ghoul | Existing enters-tapped vanilla creature | None |
| Undead Warchief | Existing Zombie lord plus subtype cost reduction | Confirm self receives +2/+1 |
| Death Baron | Existing other-Zombie/Skeleton lord; add deathtouch grant | Deathtouch inert against passive blocker-free opponent |
| Lord of the Undead | Existing other-Zombie lord and graveyard return activation | Graveyard target must be surfaced; Oracle affects all Zombies |
| Lord of the Accursed | Existing other-Zombie lord; add temporary menace activation | Oracle affects all Zombies; menace inert without blockers |
| Gray Merchant of Asphodel | New devotion-to-black ETB life loss and life gain | 2HG loses life per opponent, gains their total loss |
| Blood Scrivener | New empty-hand draw replacement in shared draw path | Multiple copies, draw counters, dredge, empty library, and logs |
| Dread Wanderer | Existing enters-tapped body plus new conditional self-return from graveyard | Sorcery timing and hand-size check |
| Gravecrawler | New repeatable self-cast permission from graveyard | Requires a Zombie on board at each cast; Culling chains |
| Cemetery Reaper | Existing other-Zombie lord and token activation extended with graveyard creature exile target | Exile is an effect; chosen target changes recursion options |
| Fleshbag Marauder | New ETB each-player creature sacrifice | All victims chosen from pre-sacrifice state; own choice searched and surfaced |

Integration, build, baseline profile, viewer verification, and all three
regression tiers remain. No proposed Oracle deferrals have been approved.
The full overnight checkpoint is being pushed separately from the primary
checkout before this deck analysis joins the shared branch.

## Approved deferrals

None.
