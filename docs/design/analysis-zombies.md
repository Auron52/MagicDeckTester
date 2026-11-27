# Zombies analysis ledger

Worktree: `.claude/worktrees/zombies-analysis` (`wip/zombies-analysis-1126`).
Started 2026-11-26 from shared commit `9e566000b`. The deck file was copied
from the primary checkout and landed on the shared branch at `1c5ab04be`.
The original untracked copy is backed up under
`/home/vscode/.claude/mdt-deck-backups/Zombies-original-20261126.cod`.
Rebased onto the pushed overnight checkpoint `ffee59231` after its landing gate
passed; implementation work starts from this shared source.

## Integration handoff (2026-11-27)

An earlier implementation and audited Zombies ground truth were pushed to
`origin/wip/zombies-analysis-1126` at `416158f44`, based on shared
`695797f95`. The accepted overnight snapshot ran from
`46271e67b`, and the ledger below records the changed same-score games, so
remote movement does not erase the measurement. The two Windows unit failures
seen on earlier work-branch runs were stale assertions introduced by the new
deck: the provider census omitted Zombies, and Haven's subtype-aware graveyard
return now sets `GyR_AllNames`. Both assertions were corrected in `416158f44`;
the focused post-rebase tests passed (routing census 137 assertions, Haven 8,
Zombies discard 39). Windows CI for this exact head is run `45965488850`.

Post-rebase Release validation: the four Zombies smoke keys matched their
accepted averages and play digests exactly (1,175 games). The seven Zombies
regression keys also matched exactly (1,435 games); 964 strict viewer
references had zero play drift, shuffle dead, board divergence, enum gaps,
mulligan drift, or contract failures. Scenarios and suboptimal references
passed. `test/check_gt_logs.py` reported 1,483 consistent records with none
stale or missing, and `scripts/check_references_safe.py` passed.

The final integration gate rebased onto shared `f2e307fe3` and built the
Release engine and test binary. The full unit suite passed 4,333/4,333 cases
in 48 shards. The full smoke tier passed 255/255 cases with exact averages and
digests, zero per-game play changes, and zero slowdowns; its batch took 87m29s
on this 12-core host. Zombies-scoped regression then passed all seven Zombies
and Zombies2HG cases exactly (1,435 games). Its concurrent side stages passed
678 scenarios, 970 strict viewer references (zero play drift, shuffle dead,
board divergence, enum gaps, mulligan drift, or contract failures), and both
suboptimal require-beat references. Regression took about 10m26s. The full
suite regression was not needed for the scoped Zombies change; its manifest
would have run 409 cases and roughly 2.3 times the smoke tier's expected work.

The earlier stopped smoke run reached 33 exact rows of 255 in 71 minutes and
used 8.7 CPU hours. It was stopped because this handoff incorrectly applied an
obsolete two-hour queue limit. The current `CLAUDE.md` removes that fixed
limit. The full unit build also ran concurrently for part of that attempt,
wasting scarce CPU and memory. The final build, unit, and smoke gates ran in
sequence. No swap growth was observed during smoke.

The completed Zombies overnight run need not be repeated solely because the
shared head moved; its source hash and same-score audit are recorded in this
ledger. The shared ground-truth header can reflect another deck's later accept.

The primary play viewer was started from the landed shared checkout with the
validated Release binary. The user saved ten Zombies reference games, committed
and pushed as `0fa58ab5a`. At committed depth 5/budget 20, the search matched
all ten win turns exactly (human 4.400, search 4.400; zero hand mismatches or
shortfalls). A live Scryfall cost audit compared all 13 costed Zombies cards;
all 13 mana costs matched.

## Post-reference mana follow-up (2026-11-27)

The first 5,000-game mana oracle gate on source `58e7b5aea94b` reported
7,870 `APPLY-DROP-ORDER` misses, with no other miss class and identical
oracle-off play. Most were Culling the Weak ordered after the Warchief it could
fund. A sequential payable-order search now covers sacrifice rituals and
ritual-plus-reducer lines at both enumeration and apply. The scratch walk puts
same-line sacrifice creatures onto the board with their physical IDs and pays
the sacrifice cost; it also models a Fleshbag Marauder choice that removes a
live reducer before the following spell. The focused 100-game oracle block
went from 187 misses to zero. A 500-game block on the rebased shared source
also has zero misses, zero oracle gaps and zero unmodeled cases.

All ten user references were replayed after these changes at committed play
depth 5/budget 20: human and search both average 4.400 turns, zero hand
mismatches, zero shortfalls. The first full 5,000-game rerun on source
`075cbb267890` reduced the failure count to 56 `APPLY-DROP-ORDER` misses,
all from two deterministic games (seed 30004321/game 4321 and seed
30004488/game 4488). In both, Culling the Weak sacrificed Fleshbag
Marauder's selected victim before Fleshbag entered, so its ETB sacrificed
Undead Warchief instead and removed the cost reducer required by a later
spell. The payable-order walk now resolves Fleshbag's sacrifice in every
sacrifice-ritual line. Both exact games pass with zero misses, oracle gaps,
unmodeled cases, or capped searches. After rebasing onto shared
`c4d5de01f`, the Release build again matched all ten references exactly
and passed both mana reproductions. The final full 5,000-game gate requires
a fresh PGO binary stamped to the integrated `HEAD:src`. The fresh PGO+LTO
build completed its 320 training jobs (8,562 games) and stamped source
`a9090dff06e5`. The final 5,000-game gate on games 0-4999 passed clean:
zero issues, `MISS=0`, `ORACLE-GAP=0`, `UNMODELED=0`, `CAPPED=0`, and
oracle-off play was byte-identical. The detailed report is
`logs/mana_clean/20261127-215946/report.md`; the committed gate record is
`test/mana_clean.json`.

The full Release unit suite initially found three stale Dragonstorm expectations:
the ritual-plus-reducer payable-order domain now finds a paying generic line
that the old tests expected to strand. The assertions now verify that both
ordering arms cast the spells and that both exact-order settings find the
nearest paying order. The rebuilt full unit suite passed 4,342/4,342 cases
in 48 shards on 12 cores.

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
itself. The Zombies provider is owned by this deck and inherits the generic
play search policy; its cleanup discard rule is described below. The Stage 2
review, cost/field audits, build, and game checks completed. Lord of the Undead's
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
of at least 150 games per arm. The 200-game-per-arm depth-5 bound at seed 55001
finished with identical per-game win digest `d949eedaf4710bd0` and average
4.4700 in both arms. The flag changed work units (1,688,356 vs 1,704,410),
so the A/B was live, but shed ranking did not change any finished line in this
sample. A deck-specific bucket rule remains to be authored for user review
under the discard-policy workflow; the measured upside is currently zero.

The smoke tier's Zombies rows passed all 677 shared scenarios and were
accepted: d0 1,000 games avg 4.7780; d3 50 avg 4.4400; d5 100 avg 4.4500.
Depth 3 and 5 had identical outcomes on the 50 paired seeds. The 2HG smoke
batch played 25 games at avg 4.9200; its scenario gate passed and its new GT
was accepted. The pooled smoke/regression run checked 950 reference
replays (364 exact, 585 repaired, one beaten) and found zero play drift,
shuffle dead, board divergence, enum gaps, mulligan drift, or contract failures.
Its seven new regression keys (six main, one 2HG) were accepted. Main depth-5
regression blocks averaged 4.4267 and 4.3733 over 150 games each; the 2HG
75-game block averaged 4.9600. The overnight tier also passed and its 15 new
keys were accepted: four main depth-5 300-game blocks averaged 4.4333,
4.4233, 4.4033, and 4.3567; four 2HG 75-game blocks averaged 4.8933, 4.8933,
4.9200, and 4.9200. The overnight run used the Release binary built from
`76e7314cb`; its suite inputs and accepted smoke/regression GT were pushed
in `b33927915`. No unrelated GT rows were changed.

The same-run suite cost gate measured Zombies and Hinata together on this
12-core host. Zombies' worst budgeted searched case was 4,538.720 core-ms per
game (`zombies2hg_regression_d5_b20`), versus Hinata's 29,846.877 core-ms:
Zombies is 0.15x the pinned yardstick and passes the 3x admission limit.

The mandatory horizon tie-break screen ran 400, then 1,600, then 6,400
paired games at play settings (depth 5, budget 20). The largest sample changed
one game: leaf grading lost one turn, with zero improved games, an observed
0.000156 turn/game difference. The script reports `NO SIGN AT THIS SAMPLE`
because just one game bound. Its suggested decisive extension is roughly 321
blocks of 200 games per arm, which is disproportionate on this host. The one
case (seed 2600191, game index 1) reproduced as turn 6 without the tie-break
and turn 7 with it at budget 20; with the tie-break at unlimited budget it won
turn 6. The faster line is recoverable and no structural branch was removed.
Keep the default tie-break on pending stronger evidence.

The Stage 5g earliest-win miner used the `99395cd62` source build at depth 5,
budget 3000, seeds 26011 and 37021, requesting 300 games each. It emitted 2,673 decision records across both
seed blocks. All seed indices were reached, but one final game remained active
on one CPU for over eight minutes after the other workers finished, so the
miner was stopped at ten minutes in the second block. The partial report is
`logs/heuristics/Zombies.report.txt` (local, untracked run evidence). Its most
consistent order proposal was Dark Ritual before Undead Warchief: 113 decisions
favored that order, zero favored the reverse, and 26 tied. Dark Ritual before
other lords was strong but had one to four conflicts per pair. Fleshbag Marauder
was slower in 99 of 207 inclusion comparisons, but helped four, so it remains
available to search. No land-choice rule exists for this mono-Swamp deck. A
Zombies-only Warchief-order proposal was paired against the generic policy on
400 games at shipped depth 5 and budget 20 (seed 58001). The arms had identical
per-game win turns in all 400 games and the same 4.3925 average; the proposed
override was removed. The miner's candidate score did not translate into a
live-game improvement, so Zombies retains the generic cast order.

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

## Provisional cleanup discard buckets

The current evidence contains zero real cleanup decisions in 400 games and a
zero win-turn bound from inverting rollout cleanup ranking over 200 paired
depth-5 games. The analyze-deck workflow requires an authored policy and says
to keep working while user review is pending. The following default is being
implemented behind `MTG_ZOMBIES_BUCKET_DISCARD` (`=0` restores generic):

1. **Mana:** keep Swamps up to four total counting the battlefield, or five
   when Gray Merchant is in hand without Dark Ritual. Keep one Dark Ritual
   with a creature costing at least three; keep Culling the Weak in that slot
   only if a battlefield creature or a one-drop in hand can feed it.
2. **Threat floor:** keep two creatures counting the battlefield. Choose hand
   threats by land drops until castable next turn, then Gravecrawler, Dread
   Wanderer, Diregraf Ghoul, Warchief, the three-mana lords, Blood Scrivener,
   Gray Merchant, and Fleshbag Marauder.
3. **Overflow:** shed unneeded rituals, lands beyond the quota, then spare
   creatures; an unneeded Fleshbag with no opponent creature sheds first.
   Quota members shed last, with the ritual before lands and threats if the
   hand cannot retain every quota member. The rule returns one index and is
   re-evaluated after every shed. Distance uses printed mana value and the
   next land drop; it does not yet credit Warchief's conditional reduction.

This is a provisional default for user amendment. The first 400-game paired
screen at seed 68001 was superseded: its build began compiling the provider
before the hand-placeholder lookup fix, so the screen measured an earlier
classification. A clean rebuild of the corrected source passed all three
focused hand-choice fixtures. The corrected 400-game A/B at depth 5, budget 20
(same seed 68001) again had identical per-game win turns and a 4.4550 average
on both arms. The policy changed the first rollout shed in 8,202 of 16,253
calls (50.5%); there were zero real cleanup discards. This establishes a live,
non-inferior policy on that sample, with no measured win-turn gain. Suite
validation found the three main smoke keys unchanged. Of six main regression
keys, five were unchanged; the sixth had one same-score play change (seed 3041,
game index 8): Lord of the Accursed on turn 3 became Cemetery Reaper, and both
lines won on turn 5. The changed digest was accepted after a paired replay;
there were no slower games, reference failures, or scenario failures.
The 2HG regression key was unchanged. The 2HG smoke key moved only one play
digest (seed 1015, game index 14), again with a turn-5 win in both arms: the
bucket policy chose a turn-1 creature/ritual development line and a turn-3
Gray Merchant instead of the fallback's slower board development followed by
a turn-4 Reaper. The two settings reproduced on a single-game replay, and the
same-score 2HG smoke digest was accepted. Its 677 scenarios and 950 reference
replays passed; no score regressions were observed.

The filtered overnight run on committed source `46271e67b` completed on
2026-11-27 in about six minutes. All 15 Zombies and Zombies 2HG rows kept
their win-turn averages. Eleven digests were exact; four searched rows and
one depth-0 row had same-score play changes. The changes came from two seeds:
4031 (2HG depth 5, game index 27, turn-4 win) and 4067 (2HG depth 5 and
single-player depths 0, 3, and 5, game index 63, turn-7 win). Kept hands and
draws were unchanged. The differing lines were legal: seed 4031 changed the
turn-1 creature/Ritual development, while seed 4067 changed a later
Culling/Lord/Death sequence. No game became faster or slower. The five
same-score play digests were accepted with `regression.sh --overnight
--deck=zombies,zombies2hg --accept`; the source hash is recorded in this
ledger. This snapshot is retained even if the shared branch
moves before integration, so a later sweep can measure the remaining drift.

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

## Codex-play sweep

commit: `76e7314cb`
seeds: 54001-54017, plus targeted 54020
games: 17 full-deck games and one targeted card fixture
flags: 0 unresolved

The 17 full-deck games found no illegal or missing plays and no wrong state
transitions. Seed 54008 exposed a viewer label error for Cemetery Reaper;
`76e7314cb` corrected it and seed 54005's replay confirmed the label.
The targeted Dread Wanderer fixture confirmed its return ability.

## Approved deferrals

None.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-27)

`verify_deck.py decks/Zombies/Zombies.cod --no-network --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 14 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 937 cards match snapshot (cost/PT/types/keywords); 20 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (zombies) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 1 type(s) wired (emitter + GUI): sacrifice |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/144 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Codex-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Zombies is regression key `zombies` |
| provider_policy | PASS | yes | every expected provider policy is implemented (or none applies) |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Culling the Weak: ADDITIONAL COST (sac_additional_creature, any creature; CR 601.2h): paid at CAST in both worlds through the shared PerformSacrificeCreatureCost (death cascade, morbid); not castable with no creature. WHICH creature is a SEARCHED cast variant (battlefield classes folded by identity + readiness, plus same-plan hand creatures -- see Diabolic Intent's note); human play: the `sacrifice` board click. RITUAL: ritual_floating_mana:4 + ritual_float_color:B -- GROSS 4 BLACK into the turn-scoped reserve, net +3 mana for one body. [PARTIAL, PROVISIONAL: instant timing collapsed to the main phase. Every goldfish instant window was checked: in response to Abhorrent Overlord's upkeep sacrifice or Reaper's end-step destroy the {B}{B}{B}{B} empties with nothing to cast and the body is lost either way; in response to Sire of Insanity's end-step discard it costs an extra body for unspendable mana; during combat the mana empties before main 2. USER sign-off pending.
- coverage deferral -- Lord of the Undead: PARTIAL, PROVISIONAL: the static bonus to opposing Zombies is omitted. The fixed passive opponent's spawned creatures are not Zombies, so the omission is inert in current goldfish games. User sign-off pending.
- coverage deferral -- Gravecrawler: PARTIAL, PROVISIONAL: can't block is inert against the fixed passive opponent, which never attacks. User sign-off pending.
- coverage deferral -- Lord of the Accursed: PARTIAL, PROVISIONAL: the menace activation has no effect against the fixed passive opponent, which never blocks; no line in this goldfish can benefit from activating it. User sign-off pending.
- coverage deferral -- Dark Ritual: Burst-mana RITUAL: pay {B} (1), add {B}{B}{B} = GROSS 3 BLACK to the turn-scoped reserve (state.floating_mana.black), net +2. Modelled via ritual_floating_mana:3 + ritual_float_color:B (the coloured-float variant of Seething Song / Rite of Flame), so the black can never pay an off-colour pip (Sire of Insanity's {R}). Executor (EffectHandler ApplyRitualFloat), rollout (apply_one) and planner (the subset float credit, colour-exact) are the shared ritual path. Instant speed is inert in the single-player goldfish (no opponent turn to respond in; every main-phase line an instant reaches, a sorcery-speed cast reaches too). No drawback, so no max_casts_after. A free cast (ripple / cascade) floats its own {B}{B}{B} through the same resolution.
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Wanderwine Prophets [keywords]: Champion is modeled by champion_merfolk with linked exile, return, sacrifice if no champion, and searched human choice. The engine has no generic cham
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- allowlisted divergence -- Garruk's Harbinger [keywords]: 'Hexproof from black' is a real keyword but INERT here (PROVISIONAL, user sign-off pending; disclosed in the card's bracket): the passive goldfish opp
- allowlisted divergence -- Bloated Contaminator [keywords]: Toxic and Proliferate are not engine keywords (the loader has no tag for either): toxic 1 is the toxic param (ApplyToxicPoison, both combat damage-ste
- oracle_text advisory -- Lord of the Undead: oracle_text diverges (similarity 0.50); scryfall='Other Zombie creatures get +1/+1.
{1}{B}, {T}: Return target Zombie card from your graveyard to your hand.'
- oracle_text advisory -- Lord of the Accursed: oracle_text diverges (similarity 0.48); scryfall='Other Zombies you control get +1/+1.
{1}{B}, {T}: All Zombies gain menace until end of turn.'
- oracle_text advisory -- Gravecrawler: oracle_text diverges (similarity 0.57); scryfall="This creature can't block.
You may cast this card from your graveyard as long as you control a Zombie."
- oracle_text advisory -- Deeproot Pilgrimage: oracle_text diverges (similarity 0.55); scryfall='Whenever one or more nontoken Merfolk you control become tapped, create a 1/1 blue Merfolk creature token with
- oracle_text advisory -- Svyelun of Sea and Sky: oracle_text diverges (similarity 0.73); scryfall='Svyelun has indestructible as long as you control at least two other Merfolk.
Whenever Svyelun attacks, draw
- oracle_text advisory -- Wanderwine Prophets: oracle_text diverges (similarity 0.11); scryfall='Champion a Merfolk (When this enters, sacrifice it unless you exile another Merfolk you control. When this lea
- oracle_text advisory -- Arid Mesa: oracle_text diverges (similarity 0.43); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Plains card, put it onto the battl
- oracle_text advisory -- Decimator of the Provinces: oracle_text diverges (similarity 0.22); scryfall="Emerge {6}{G}{G}{G} (You may cast this spell by sacrificing a creature and paying the emerge cost reduced by t
- oracle_text advisory -- End-Raze Forerunners: oracle_text diverges (similarity 0.15); scryfall='Vigilance, trample, haste
When this creature enters, other creatures you control get +2/+2 and gain vigilance
- oracle_text advisory -- Flinthoof Boar: oracle_text diverges (similarity 0.17); scryfall='This creature gets +1/+1 as long as you control a Mountain.
{R}: This creature gains haste until end of turn.
- oracle_text advisory -- Herald of Ilharg: oracle_text diverges (similarity 0.24); scryfall='Trample
Whenever you cast a creature spell, put two +1/+1 counters on this creature. If that spell has mana v
- oracle_text advisory -- Herald's Horn: oracle_text diverges (similarity 0.26); scryfall="As this artifact enters, choose a creature type.
Creature spells you cast of the chosen type cost {1} less to
- oracle_text advisory -- Ilharg, the Raze-Boar: oracle_text diverges (similarity 0.21); scryfall="Trample
Whenever Ilharg attacks, you may put a creature card from your hand onto the battlefield tapped and a
- oracle_text advisory -- Raggadragga, Goreguts Boss: oracle_text diverges (similarity 0.23); scryfall='Each creature you control with a mana ability gets +2/+2.
Whenever a creature you control with a mana ability
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste
Whenever this creature attacks, defending player reveals the top card of their library. If it's a land
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.
Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
Wh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
Whenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.18); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.27); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has first strike.
Umbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.13); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment,
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.15); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.16); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B},
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.34); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.03); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.21); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."
Equip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.12); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand,
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token:
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample
The Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)
Create two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.
If one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.
Evoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.17); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.10); scryfall='Other Elemental creatures you control get +1/+1.
{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike
When this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.
{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.
Storm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest
As this Aura enters, choose a color.
Whenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
{2}{U}: Untap enchanted creature.
Umbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.
{2}, {T}: You gain 1 life.
{3}, {T}: Untap target creature.
{4}, {T}: Tap target
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —
• Put a +1/+1 counter on this creature.
• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.
Landfall — If you had a land enter the battlefield under your control this turn,
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink
Adelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.
Whenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature
Enchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature
Enchanted creature has flying.
Aura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.
Channel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance
Whenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature
When this Aura enters, tap enchanted creature.
Enchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature
Enchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature
Enchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature
Enchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.
{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying
At the beginning of combat on your turn, if you have a full party, choose target nonland permanent an
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature
As long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample
Landfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
This crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample
Landfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.
Landfall — Whenever a land you
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.
Landfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.
Whenever
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.
Landfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.
You may play lands from the top of your library.
Landfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.
Remove three
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste
This creature can't be blocked by creatures with power 2 or less.
Landfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.
Suspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.
{T}: Add {C}.
{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample
Whenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.
{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.
{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)
{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.
{T}: Add {C}.
{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance
When this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.
{T}: Add {C}.
{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.
Whene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)
Vigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike
Equipped creature has double strike.
Reconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.
During you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.
Whenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)
Look at target player's hand.
Draw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
Lifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.
{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.
{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.
This creature has haste as long as an opponent has 10 or less life.
Landfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike
Other Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike
Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)
When this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.
Tap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying
{T}: Create a 2/2 black Vampire creature token with flying.
{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Vampire creatures you control get +2/+2.
{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.
{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.
Creatures of the chosen color get +1/+1.
Whenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.
+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample
This creature enters with four +1/+1 counters on it.
Whenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.
−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.
At the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample
This creature enters with X +1/+1 counters on it.
When this creature enters, choose one —
• Double
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.
{2}{G}, Remove a +1/+1 counter from this creature: Destroy
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance
Hydra spells you cast cost {4} less to cast.
Whenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.
{4}: Put a +1/+1 counter on this creature.
Remove a +1/+1
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —
• You gain 1 life.
• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.
At the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)
Cycling—Pay 2 life.
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you cycle or discard another card, scry 1.
Cycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.
When you cast this spell, take an extra turn after this one.
Flying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.
Indestructible
Whenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.
Indestructible
Annihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.09); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.
All creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash
Other Merfolk you control get +1/+1.
Sacrifice a Merfolk: Counter target noncreature spell unless its
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Search your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Add {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.
{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying
When this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying
Morbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.
Creatures
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library.
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.
Equip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample
Other creatures you control have trample.
Whenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible
As long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.
Whenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying
When this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.
{2}, Sacrifice this creature: Destroy up to one target artifact or
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink
Whenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.
Flashback {2}{R} (You may cast this card from your graveyard for its
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible
As long as your devotion to red is less than five, Purphoros isn't a creature.
Whenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.
Flashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.
Threshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.
{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying
Whenever this creature deals combat damage to a creature, destroy that creature.
Dredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.
Dredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible
As long as your devotion to blue and red is less than seven, Keranos isn't a creature.
Reveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible
As long as your devotion to black and red is less than seven, Mogis isn't a creature.
At the
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible
As long as your devotion to red and green is less than seven, Klothys isn't a creature.
At th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible
As long as your devotion to white and blue is less than seven, Ephara isn't a creature.
At th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible
As long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.
Crea
- oracle_text advisory -- Wild Defiance: oracle_text diverges (similarity 0.21); scryfall='Whenever a creature you control becomes the target of an instant or sorcery spell, that creature gets +3/+3 un
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.25); scryfall='Cast this spell only before the combat damage step.
Target creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.
Ferocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)
Target creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.11); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste
At the beginning of the end step, sacrifice this creature.
Morph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample
Whenever this creature deals combat damage to a player, you may draw a card.
Morph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.
Megamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.
{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- oracle_text advisory -- Drogskol Captain: oracle_text diverges (similarity 0.25); scryfall="Flying
Other Spirit creatures you control get +1/+1 and have hexproof. (They can't be the targets of spells o
- oracle_text advisory -- Supreme Phantom: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Spirits you control get +1/+1.'
- oracle_text advisory -- Shacklegeist: oracle_text diverges (similarity 0.17); scryfall="Flying
This creature can block only creatures with flying.
Tap two untapped Spirits you control: Tap target
- oracle_text advisory -- Spirit of the Labyrinth: oracle_text diverges (similarity 0.08); scryfall="Each player can't draw more than one card each turn."
- oracle_text advisory -- Flooded Strand: oracle_text diverges (similarity 0.12); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island card, put it onto the battlef
- oracle_text advisory -- Geist of Saint Traft: oracle_text diverges (similarity 0.23); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)
Whenever Geist of
- oracle_text advisory -- Lingering Souls: oracle_text diverges (similarity 0.16); scryfall='Create two 1/1 white Spirit creature tokens with flying.
Flashback {1}{B} (You may cast this card from your g
- oracle_text advisory -- Patrician Geist: oracle_text diverges (similarity 0.14); scryfall='Flying
Other Spirits you control get +1/+1.
Spells you cast from your graveyard cost {1} less to cast.'
- oracle_text advisory -- Mausoleum Wanderer: oracle_text diverges (similarity 0.19); scryfall="Flying
Whenever another Spirit you control enters, this creature gets +1/+1 until end of turn.
Sacrifice thi
- oracle_text advisory -- Skyclave Apparition: oracle_text diverges (similarity 0.28); scryfall="When this creature enters, exile up to one target nonland, nontoken permanent you don't control with mana valu
- oracle_text advisory -- White Orchid Phantom: oracle_text diverges (similarity 0.21); scryfall='Flying, first strike
When this creature enters, destroy up to one target nonbasic land. Its controller may se
- oracle_text advisory -- Usher of the Fallen: oracle_text diverges (similarity 0.09); scryfall='Boast — {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this
- oracle_text advisory -- Millicent, Restless Revenant: oracle_text diverges (similarity 0.16); scryfall='Affinity for Spirits (This spell costs {1} less to cast for each Spirit you control.)
Flying
Whenever Millic
- oracle_text advisory -- Zhur-Taa Druid: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {G}.
Whenever you tap this creature for mana, it deals 1 damage to each opponent.'
- oracle_text advisory -- Spark Elemental: oracle_text diverges (similarity 0.46); scryfall="Trample, haste (This creature can deal excess combat damage to the player or planeswalker it's attacking. This
- oracle_text advisory -- Ball Lightning: oracle_text diverges (similarity 0.53); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
Haste (Th
- oracle_text advisory -- Flamekin Bladewhirl: oracle_text diverges (similarity 0.12); scryfall='As an additional cost to cast this spell, reveal an Elemental card from your hand or pay {3}.'
- oracle_text advisory -- Sunspine Lynx: oracle_text diverges (similarity 0.19); scryfall="Players can't gain life.
Damage can't be prevented.
When this creature enters, it deals damage to each playe
- oracle_text advisory -- Hellspark Elemental: oracle_text diverges (similarity 0.13); scryfall='Trample, haste
At the beginning of the end step, sacrifice this creature.
Unearth {1}{R} ({1}{R}: Return thi
- oracle_text advisory -- Nova Chaser: oracle_text diverges (similarity 0.12); scryfall='Trample
Champion an Elemental (When this enters, sacrifice it unless you exile another Elemental you control.
- oracle_text advisory -- Thunderkin Awakener: oracle_text diverges (similarity 0.17); scryfall="Haste
Whenever this creature attacks, choose target Elemental creature card in your graveyard with toughness
- oracle_text advisory -- Mogg Maniac: oracle_text diverges (similarity 0.31); scryfall='Whenever this creature is dealt damage, it deals that much damage to target opponent or planeswalker.'
- oracle_text advisory -- Spiteful Sliver: oracle_text diverges (similarity 0.17); scryfall='Sliver creatures you control have "Whenever this creature is dealt damage, it deals that much damage to target
- oracle_text advisory -- Nomads en-Kor: oracle_text diverges (similarity 0.11); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control
- oracle_text advisory -- Shaman en-Kor: oracle_text diverges (similarity 0.13); scryfall='{0}: The next 1 damage that would be dealt to this creature this turn is dealt to target creature you control
- oracle_text advisory -- Stuffy Doll: oracle_text diverges (similarity 0.14); scryfall='Indestructible
As this creature enters, choose a player.
Whenever this creature is dealt damage, it deals th
- oracle_text advisory -- Guilty Conscience: oracle_text diverges (similarity 0.13); scryfall='Enchant creature
Whenever enchanted creature deals damage, this Aura deals that much damage to that creature.
- oracle_text advisory -- Screaming Nemesis: oracle_text diverges (similarity 0.27); scryfall="Haste
Whenever this creature is dealt damage, it deals that much damage to any other target. If a player is d
- oracle_text advisory -- Blasphemous Act: oracle_text diverges (similarity 0.20); scryfall='This spell costs {1} less to cast for each creature on the battlefield.
Blasphemous Act deals 13 damage to ea
- oracle_text advisory -- Chain Reaction: oracle_text diverges (similarity 0.41); scryfall='Chain Reaction deals X damage to each creature, where X is the number of creatures on the battlefield.'
- oracle_text advisory -- Loran's Escape: oracle_text diverges (similarity 0.10); scryfall='Target artifact or creature gains hexproof and indestructible until end of turn. Scry 1.'
- oracle_text advisory -- Worn Powerstone: oracle_text diverges (similarity 0.15); scryfall='This artifact enters tapped.
{T}: Add {C}{C}.'
- oracle_text advisory -- Flooded Grove: oracle_text diverges (similarity 0.16); scryfall='{T}: Add {C}.
{G/U}, {T}: Add {G}{G}, {G}{U}, or {U}{U}.'
- oracle_text advisory -- Hedge Maze: oracle_text diverges (similarity 0.22); scryfall='({T}: Add {G} or {U}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Spawning Kraken: oracle_text diverges (similarity 0.20); scryfall='Whenever a Kraken, Leviathan, Octopus, or Serpent you control deals combat damage to a player, create a 9/9 bl
- oracle_text advisory -- Koma, World-Eater: oracle_text diverges (similarity 0.19); scryfall="This spell can't be countered.
Trample, ward {4}
Whenever Koma deals combat damage to a player, create four
- oracle_text advisory -- Serpent of Yawning Depths: oracle_text diverges (similarity 0.29); scryfall="Krakens, Leviathans, Octopuses, and Serpents you control can't be blocked except by Krakens, Leviathans, Octop
- oracle_text advisory -- Lorthos, the Tidemaker: oracle_text diverges (similarity 0.24); scryfall="Whenever Lorthos attacks, you may pay {8}. If you do, tap up to eight target permanents. Those permanents don'
- oracle_text advisory -- Whelming Wave: oracle_text diverges (similarity 0.13); scryfall="Return all creatures to their owners' hands except for Krakens, Leviathans, Octopuses, and Serpents."
- oracle_text advisory -- Kiora Bests the Sea God: oracle_text diverges (similarity 0.23); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — Create an 8/8 bl
- oracle_text advisory -- Summon: Leviathan: oracle_text diverges (similarity 0.22); scryfall="(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — Return each crea
- oracle_text advisory -- Hullbreaker Horror: oracle_text diverges (similarity 0.12); scryfall="Flash
This spell can't be countered.
Whenever you cast a spell, choose up to one —
• Return target spell yo
- oracle_text advisory -- Brinelin, the Moon Kraken: oracle_text diverges (similarity 0.14); scryfall="When Brinelin enters and whenever you cast a spell with mana value 6 or greater, you may return target nonland
- oracle_text advisory -- Sin, Unending Cataclysm: oracle_text diverges (similarity 0.15); scryfall="Flying, trample
As Sin enters, remove all counters from any number of artifacts, creatures, and enchantments.
- oracle_text advisory -- Trench Gorger: oracle_text diverges (similarity 0.18); scryfall='Trample
When this creature enters, you may search your library for any number of land cards, exile them, then
- oracle_text advisory -- Maze's End: oracle_text diverges (similarity 0.30); scryfall="This land enters tapped.
{T}: Add {C}.
{3}, {T}, Return this land to its owner's hand: Search your library f
- oracle_text advisory -- Azorius Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {W} or {U}.'
- oracle_text advisory -- Boros Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Dimir Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U} or {B}.'
- oracle_text advisory -- Golgari Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {B} or {G}.'
- oracle_text advisory -- Gruul Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Izzet Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Orzhov Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {W} or {B}.'
- oracle_text advisory -- Rakdos Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Selesnya Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Simic Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Planar Nexus: oracle_text diverges (similarity 0.39); scryfall="This land is every nonbasic land type. (Nonbasic land types include Cave, Desert, Gate, Lair, Locus, Mine, Pow
- oracle_text advisory -- Guild Summit: oracle_text diverges (similarity 0.35); scryfall='When this enchantment enters, you may tap any number of untapped Gates you control. Draw a card for each Gate
- oracle_text advisory -- Gates Ablaze: oracle_text diverges (similarity 0.28); scryfall='Gates Ablaze deals X damage to each creature, where X is the number of Gates you control.'
- oracle_text advisory -- Archdruid's Charm: oracle_text diverges (similarity 0.45); scryfall="Choose one —
• Search your library for a creature or land card and reveal it. Put it onto the battlefield tap
- oracle_text advisory -- Gond Gate: oracle_text diverges (similarity 0.19); scryfall='Gates you control enter untapped.
{T}: Add {C}.
{T}: Add one mana of any color that a Gate you control could
- oracle_text advisory -- Exploration: oracle_text diverges (similarity 0.12); scryfall='You may play an additional land on each of your turns.'
- oracle_text advisory -- Burgeoning: oracle_text diverges (similarity 0.20); scryfall='Whenever an opponent plays a land, you may put a land card from your hand onto the battlefield.'
- oracle_text advisory -- Manabond: oracle_text diverges (similarity 0.32); scryfall='At the beginning of your end step, you may reveal your hand and put all land cards from it onto the battlefiel
- oracle_text advisory -- Amulet of Vigor: oracle_text diverges (similarity 0.10); scryfall='Whenever a permanent you control enters tapped, untap it.'
- oracle_text advisory -- Thran Portal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
As this land enters, choose a basic land
- oracle_text advisory -- Gateway Plaza: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped.
When this land enters, sacrifice it unless you pay {1}.
{T}: Add one mana of any co
- oracle_text advisory -- Sea Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
As this land enters, choose a color other than blue.
{T}: Add {U} or one mana of th
- oracle_text advisory -- Manor Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
As this land enters, choose a color other than green.
{T}: Add {G} or one mana of t
- oracle_text advisory -- Cliffgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
As this land enters, choose a color other than red.
{T}: Add {R} or one mana of the
- oracle_text advisory -- Citadel Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
As this land enters, choose a color other than white.
{T}: Add {W} or one mana of t
- oracle_text advisory -- Black Dragon Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
As this land enters, choose a color other than black.
{T}: Add {B} or one mana of t
- oracle_text advisory -- The Black Gate: oracle_text diverges (similarity 0.37); scryfall="As The Black Gate enters, you may pay 3 life. If you don't, it enters tapped.
{T}: Add {B}.
{1}{B}, {T}: Cho
- oracle_text advisory -- Heap Gate: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
{1}, {T}, Tap an untapped Gate you control: Create a Trea
- oracle_text advisory -- Baldur's Gate: oracle_text diverges (similarity 0.22); scryfall='{T}: Add {C}.
{2}, {T}: Add X mana of any one color, where X is the number of other Gates you control.'
- oracle_text advisory -- Basilisk Gate: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}, {T}: Target creature gets +X/+X until end of turn, where X is the number of Gates you cont
- oracle_text advisory -- Talon Gates of Madara: oracle_text diverges (similarity 0.20); scryfall='When this land enters, up to one target creature phases out.
{T}: Add {C}.
{1}, {T}: Add one mana of any col
- oracle_text advisory -- Vexing Devil: oracle_text diverges (similarity 0.11); scryfall='When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this cr
- oracle_text advisory -- Golgari Rot Farm: oracle_text diverges (similarity 0.27); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Copperline Gorge: oracle_text diverges (similarity 0.34); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Graven Cairns: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{B/R}, {T}: Add {B}{B}, {B}{R}, or {R}{R}.'
- oracle_text advisory -- Lotus Vale: oracle_text diverges (similarity 0.25); scryfall="If this land would enter, sacrifice two untapped lands instead. If you do, put this land onto the battlefield.
- oracle_text advisory -- Scorched Ruins: oracle_text diverges (similarity 0.50); scryfall="If this land would enter, sacrifice two untapped lands instead. If you do, put this land onto the battlefield.
- oracle_text advisory -- Lotus Field: oracle_text diverges (similarity 0.14); scryfall='Hexproof
This land enters tapped.
When this land enters, sacrifice two lands.
{T}: Add three mana of any on
- oracle_text advisory -- Blood Sun: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, draw a card.
All lands lose all abilities except mana abilities.'
- oracle_text advisory -- Fastbond: oracle_text diverges (similarity 0.19); scryfall="You may play any number of lands on each of your turns.
Whenever you play a land, if it wasn't the first land
- oracle_text advisory -- Grim Tutor: oracle_text diverges (similarity 0.17); scryfall='Search your library for a card, put that card into your hand, then shuffle. You lose 3 life.'
- oracle_text advisory -- Diabolic Revelation: oracle_text diverges (similarity 0.16); scryfall='Search your library for up to X cards, put those cards into your hand, then shuffle.'
- oracle_text advisory -- Dimir Machinations: oracle_text diverges (similarity 0.33); scryfall="Look at the top three cards of target player's library. Exile any number of those cards, then put the rest bac
- oracle_text advisory -- Keldon Champion: oracle_text diverges (similarity 0.35); scryfall='Haste
Echo {2}{R}{R} (At the beginning of your upkeep, if this came under your control since the beginning of
- oracle_text advisory -- Keldon Marauders: oracle_text diverges (similarity 0.26); scryfall='Vanishing 2 (This creature enters with two time counters on it. At the beginning of your upkeep, remove a time
- oracle_text advisory -- Flame Javelin: oracle_text diverges (similarity 0.18); scryfall="({2/R} can be paid with any two mana or with {R}. This card's mana value is 6.)
Flame Javelin deals 4 damage
- oracle_text advisory -- Browbeat: oracle_text diverges (similarity 0.04); scryfall='Any player may have Browbeat deal 5 damage to them. If no one does, target player draws three cards.'
- oracle_text advisory -- Goblin Fireslinger: oracle_text diverges (similarity 0.07); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.'
- oracle_text advisory -- Vulshok Sorcerer: oracle_text diverges (similarity 0.06); scryfall='Haste
{T}: This creature deals 1 damage to any target.'
- oracle_text advisory -- Cinder Pyromancer: oracle_text diverges (similarity 0.10); scryfall='{T}: This creature deals 1 damage to target player or planeswalker.
Whenever you cast a red spell, you may un
- oracle_text advisory -- Teetering Peaks: oracle_text diverges (similarity 0.11); scryfall='This land enters tapped.
When this land enters, target creature gets +2/+0 until end of turn.
{T}: Add {R}.'
- oracle_text advisory -- Worldfire: oracle_text diverges (similarity 0.09); scryfall="Exile all permanents. Exile all cards from all hands and graveyards. Each player's life total becomes 1."
- oracle_text advisory -- Keldon Halberdier: oracle_text diverges (similarity 0.38); scryfall='First strike
Suspend 4—{R} (Rather than cast this card from your hand, you may pay {R} and exile it with four
- oracle_text advisory -- Mystical Tutor: oracle_text diverges (similarity 0.31); scryfall='Search your library for an instant or sorcery card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Personal Tutor: oracle_text diverges (similarity 0.40); scryfall='Search your library for a sorcery card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Overmaster: oracle_text diverges (similarity 0.36); scryfall="The next instant or sorcery spell you cast this turn can't be countered.
Draw a card."
- oracle_text advisory -- Mistrise Village: oracle_text diverges (similarity 0.40); scryfall="This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {U}.
{U}, {T}: The next spell yo
- oracle_text advisory -- Garruk's Packleader: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature you control with power 3 or greater enters, you may draw a card.'
- oracle_text advisory -- Krosan Warchief: oracle_text diverges (similarity 0.12); scryfall='Beast spells you cast cost {1} less to cast.
{1}{G}: Regenerate target Beast.'
- oracle_text advisory -- Questing Beast: oracle_text diverges (similarity 0.34); scryfall="Vigilance, deathtouch, haste
Questing Beast can't be blocked by creatures with power 2 or less.
Combat damag
- oracle_text advisory -- Thragtusk: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, you gain 5 life.
When this creature leaves the battlefield, create a 3/3 green Bea
- oracle_text advisory -- Cragplate Baloth: oracle_text diverges (similarity 0.15); scryfall="Kicker {2}{G}
This spell can't be countered.
Hexproof, haste
If this creature was kicked, it enters with fo
- oracle_text advisory -- Kaheera, the Orphanguard: oracle_text diverges (similarity 0.34); scryfall="Companion — Each creature card in your starting deck is a Cat, Elemental, Nightmare, Dinosaur, or Beast card.
- oracle_text advisory -- Contested Cliffs: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{R}{G}, {T}: Target Beast creature you control fights target creature an opponent controls. (Ea
- oracle_text advisory -- Elder Gargaroth: oracle_text diverges (similarity 0.14); scryfall='Reach, vigilance, trample
Whenever this creature attacks or blocks, choose one —
• Create a 3/3 green Beast
- oracle_text advisory -- Garruk's Harbinger: oracle_text diverges (similarity 0.19); scryfall='Hexproof from black
Whenever this creature deals combat damage to a player or planeswalker, look at that many
- oracle_text advisory -- Migloz, Maze Crusher: oracle_text diverges (similarity 0.18); scryfall='Migloz enters with five oil counters on it.
{1}, Remove an oil counter from Migloz: It gains vigilance and me
- oracle_text advisory -- Nykthos, Shrine to Nyx: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of mana of that color equal to your devotion to that co
- oracle_text advisory -- Bloated Contaminator: oracle_text diverges (similarity 0.18); scryfall='Trample
Toxic 1 (Players dealt combat damage by this creature also get a poison counter.)
Whenever this crea
- oracle_text advisory -- Woodland Bellower: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may search your library for a nonlegendary green creature card with mana value
- oracle_text advisory -- Sunpetal Grove: oracle_text diverges (similarity 0.34); scryfall='This land enters tapped unless you control a Forest or a Plains.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Oblivion Ring: oracle_text diverges (similarity 0.17); scryfall="When this enchantment enters, exile another target nonland permanent.
When this enchantment leaves the battle
- oracle_text advisory -- Enchantress's Presence: oracle_text diverges (similarity 0.10); scryfall='Whenever you cast an enchantment spell, draw a card.'
- oracle_text advisory -- Sterling Grove: oracle_text diverges (similarity 0.19); scryfall="Other enchantments you control have shroud. (They can't be the targets of spells or abilities.)
{1}, Sacrific
- oracle_text advisory -- Enchanted Evening: oracle_text diverges (similarity 0.10); scryfall='All permanents are enchantments in addition to their other types.'
- oracle_text advisory -- Opalescence: oracle_text diverges (similarity 0.18); scryfall='Each other non-Aura enchantment is a creature in addition to its other types and has base power and base tough
- oracle_text advisory -- Calming Verse: oracle_text diverges (similarity 0.15); scryfall="Destroy all enchantments you don't control. Then if you control an untapped land, destroy all enchantments you
- oracle_text advisory -- Force of Vigor: oracle_text diverges (similarity 0.21); scryfall="If it's not your turn, you may exile a green card from your hand rather than pay this spell's mana cost.
Dest
- oracle_text advisory -- Parallax Wave: oracle_text diverges (similarity 0.21); scryfall="Fading 5 (This enchantment enters with five fade counters on it. At the beginning of your upkeep, remove a fad
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 76e7314cb (HEAD 1a960a4f0b59); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
