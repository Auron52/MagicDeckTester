# Zombies analysis ledger

Worktree: `.claude/worktrees/zombies-analysis` (`wip/zombies-analysis-1126`).
Started 2026-11-26 from shared commit `9e566000b`. The deck file was copied
from the primary checkout, where it is still untracked; keep that original.
Rebased onto the pushed overnight checkpoint `ffee59231` after its landing gate
passed; implementation work starts from this shared source.

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

### Shared-path design notes

* Blood Scrivener: `AttemptDrawIntoHand` is the shared draw entry point. Its
  result currently holds one drawn card, but one Scrivener changes an empty-hand
  draw into two draws then one life loss; two copies yield three draws and two
  life loss in replacement order. Preserve every actual draw's hand-entry,
  draw-cap, dredge, miracle, empty-library, trigger, and log behavior in both
  executor and rollout. Direct callers, including draw steps and dig, must
  consume all actual draw records; a later card may enable a land drop.
* Gravecrawler: existing graveyard-cast action paths can carry a self-permission
  with a reserved source ID, but the permission must recheck a controlled Zombie
  before each payment. The search's activation-family and breakpoint logic
  assumes Lurrus-style once-per-turn permission; it must reopen a same-copy
  cast after Culling sacrifices Gravecrawler, while preventing infinite loops.
  The viewer's graveyard cast labels must be generalized from Lurrus.
* Fleshbag Marauder: opponent creatures can exist in the goldfish schedule.
  Snapshot all legal sacrifice victims before changing either board; the own
  victim is a searched and human-surfaced choice, including Fleshbag itself.
  Shared death watchers must observe the simultaneous sacrifice correctly.
The full overnight checkpoint landed on the shared branch as `ffee59231` before
this deck's implementation began.

### Implementation order

1. Add the existing-schema bodies and static lords first (Diregraf Ghoul,
   Undead Warchief, Death Baron, and the static parts of the other lords). Check
   that each lord's self-exclusion and affected subtypes match Oracle.
2. Add the graveyard action paths together (Gravecrawler, Dread Wanderer, Lord
   of the Undead, Cemetery Reaper). Keep every target or cast choice visible to
   both autonomous search and the play viewer.
3. Add the shared draw replacement, devotion life swing, and simultaneous ETB
   sacrifice (Blood Scrivener, Gray Merchant, Fleshbag Marauder). Exercise each
   in executor and rollout before producing a baseline profile.
4. Run the analyzer's convergence checks and record all resulting decisions in
   this ledger before adding the deck to the regression matrix.

## Approved deferrals

None.
