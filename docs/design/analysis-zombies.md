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
cards. All eleven now have card entries and shared engine paths. The new paths
cover devotion drain, lord deathtouch, empty-hand draw replacement, repeatable
graveyard casting, conditional graveyard return, graveyard exile for a token,
and simultaneous each-player sacrifice. The Warchief's lord bonus includes
itself. The Zombies provider is owned by this deck and currently inherits the
generic search policy. These entries still need Stage 2 review, cost/field
audits, a completed build, and game checks. Lord of the Undead's
opposing-Zombie buff, Lord of the Accursed's menace activation, and
Gravecrawler's can't-block clause are provisionally omitted as goldfish inert;
user sign-off remains pending under the analyze-deck workflow.

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

No proposed Oracle deferrals have been approved. Cemetery Reaper's target
wording is an advisory regex match; the dynamic viewer protocol exposes the
graveyard card targets as distinct plans.

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

## Validation progress, 2026-11-26

All 11 card definitions and their engine paths were committed and pushed in
`10d771d5a`; the baseline profile was generated with analyzer seed 51001 and
pushed in `f1af594ec`. The optimized `mtg` and `mtg-analyze` binaries built.
Coverage reports zero missing cards; the cost and hard-field Scryfall audits
pass. `audit_viewer_decisions.py` passed dynamically on seeds 52001 onward,
including the sacrifice choice and a forced Lord of the Undead return choice.
Cemetery Reaper's graveyard card target is encoded in distinct plan labels;
the static Oracle-text regex treats it as an advisory target mention.

The analyzer completed 1,000 card-score games at depth 5, a 400-game cost
diagnostic (`COST_NEUTRAL`), and a 400-game discard evidence run. The latter
found no real-game discard decisions but 2,641 rollout-only sheds; its verdict
is `DISCARD_UNLABELLED`, requiring a paired default-vs-worst shed-ranking bound
of at least 150 games per arm. This bound remains to run.

The smoke tier's Zombies rows passed all 677 shared scenarios and were
accepted: d0 1,000 games avg 4.7780; d3 50 avg 4.4400; d5 100 avg 4.4500.
Depth 3 and 5 had identical outcomes on the 50 paired seeds. The 2HG smoke
batch played 25 games at avg 4.9200; its scenario gate passed and its new GT
was accepted. The first main-deck regression pass checked 950 reference
replays (364 exact, 585 repaired, one beaten) and found zero play drift,
shuffle dead, board divergence, enum gaps, mulligan drift, or contract failures.
Its six new keys await acceptance after a viewer-label correction. A pooled
smoke/regression rerun for both main and 2HG Zombies is in progress.

External Codex-play seeds 54001-54017 found no illegal or missing playable
plans and no wrong state deltas. They exercised Gravecrawler's graveyard cast
after Culling, tapped entries, Cemetery Reaper's graveyard exile and token,
Blood Scrivener's doubled empty-hand draw and life loss, Fleshbag's distinct
sacrifice options, and lord effects. Seed 54008 found a viewer-label error:
the Cemetery Reaper plan appended `(target: opponent)` even though its target
was a graveyard card. `src/main.cpp` now omits that suffix for graveyard exile;
seed 54005's targeted replay confirms the corrected label. Seed 54004 found a
possible mulligan improvement: the baseline kept five lands and won turn 6,
while an alternate opening won turn 4 when handed to the same play policy.
That is a lead for the later mulligan-profile stage, not a play-search defect.
Dread Wanderer's graveyard return passed a targeted Codex-play replay on a
60-card Zombies-mechanics fixture at seed 54020 (20 each Dread Wanderer,
Culling the Weak, Swamp). After Culling sacrificed Dread with one card in hand,
the viewer offered its graveyard return; paying {2}{B} left one black floating
mana and put Dread onto the battlefield tapped and sick. It remained there and
untapped normally on the next turn. Choices through return:
`0,0,0,0,1,6,5,4,3,1,0,0,1,1`.

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
