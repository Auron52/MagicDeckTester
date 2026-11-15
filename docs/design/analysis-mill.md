# Mill deck analysis

Status: analyzed (2026-11-14); opponent land policy updated 2026-11-15.

## Deck and opponent model

Source list: `decks/Mill/Mill.cod` (60 cards, no sideboard). The user requested
50 real decklists as passive-opponent library bases, selected by `seed % 50`.
The opponent draws and follows a fixed land policy; the chosen library composition matters for
Tasha's Hideous Laughter, graveyard thresholds, and deck-out timing.

## Coverage and implementation

The initial coverage scan found 15 missing cards: Glimpse the Unthinkable,
Archive Trap, Visions of Beyond, Fractured Sanity, Hedron Crab, Ruin Crab,
Surgical Extraction, Counterspell, Drown in the Loch, Tasha's Hideous Laughter,
Darkslick Shores, Drowned Catacomb, Fatal Push, Oboro, Palace in the Clouds,
and Polluted Delta. Island, Swamp, Watery Grave, and Scalding Tarn were present.
Scalding Tarn and Polluted Delta keep the battlefield fetch hold in Mill:
Hedron and Ruin Crab can use the fetched land's second landfall entry later.
The search offers both hold and an immediate crack with a pinned target; the
viewer offers the hold and activated search. Autonomous Mill fetches pick the
first available target in Watery Grave, Island, Swamp order. Outside Mill, both retain their
prior immediate-on-play behavior. Their `produces` colors remain hand-fixing
proxies; held copies have no mana ability.

The 50-deck pool is generated from a frozen manifest of current primary
mainboards; each has exactly 60 cards. Its generated header passes `--check`.
All 15 missing card entries are now in `cards.json`, including Drown in the Loch
and Fatal Push. Cost and field audits pass. The engine effects and choice wiring
are implemented. Final verification measurements are recorded below.

The passive opponent pool is an explicit manifest of all 50 top-level `.cod`
mainboards (each 60 cards), embedded in the binary. Alias directories and the
three text-only lists are outside this pool. Every pool name has a cards.json
definition. The game uses the actual seed modulo 50 to select one list, then
shuffles its library with a separate deterministic salt and draws seven cards.
The opponent draws on its turns and plays the leftmost land in hand. It cracks a
zero-mana fetch immediately, selects the first eligible land in its library,
and shuffles after searching. This opens an opponent-turn window to cast each
Archive Trap already in our hand for {0}; the viewer asks whether to cast or
hold it, while autonomous play casts it. The opponent still casts no spells
and makes no combat decisions. References recorded before this land policy
replay under the old passive land model; new recordings carry
`opponent_land_model: 1` and use the current model.

The 2026-11-15 smoke and regression batches measured this policy on Mill and
Mill2HG. Mill's searched smoke means stayed at 4.6 turns; its depth-3
regression mean improved from 5.0 to 4.6, and its shipped depth-5 mean stayed
at 4.6. Depth-0 smoke moved from 6.80 to 6.82; depth-0 regression improved
from 6.66 to 6.41. Mill2HG's regression mean improved from 4.8 to 4.6. Two
searched games at seed 2003 moved from turn 5 to turn 6 at the usual budget;
both recovered turn 5 when given more search budget. All 843 saved reference games replayed
without drift, including the six older Mill recordings under their old model.

A fresh 50-seed sweep (`9001..9050`, one pass across the opponent pool) under
the land policy scored 6.64 turns at depth 0 with nine unwon games, then 5.08
at depth 3/b10 and depth 5/b20 with all 50 won. The two searched depths had
identical win turns in every game of this sample. Logs are
`logs/mill_land_sanity_d{0,3,5}_seed9001.{out,err}`. The smoke depth-5
job cost is 39,013 units per game, about 0.45 times Hinata's 86,019 under
the same opponent-land update.

The land policy is shared with decks whose cards observe opponent lands. The
deck-derived trait enables it for Price of Progress and for Hinata paired with
permanent-targeting spells, using the same 50-deck pool and legacy-reference
replay path. Other decks keep their previous opponent setup.

The six saved Mill references still replay under their recorded opponent model.
Their benchmark is search 5.333 versus human 6.000 turns, with one remaining
shortfall: seed 1 wins on turn 4 in the reference and turn 5 in autonomous play.
The reference holds Polluted Delta until turn 3; search cracks it on turn 2.
Forcing the reference's five-card opening hand and raising the search budget
from 20 to 80 and 320 ms still gives a turn-5 win. This is a decision difference
in the legacy model, not evidence of drift in the current land model.

Card effects implemented so far: literal mill into the appropriate graveyard;
Tasha's ordered top-of-library exile until cumulative printed mana value reaches
20; Visions' any-graveyard threshold; Fractured Sanity's cycling trigger;
Hedron and Ruin Crab landfall; Surgical's graveyard target and name-based
exile; Archive Trap's conditional free cost; Oboro's paid self-bounce; and
Drown/Fatal's target caps. The dedicated Mill provider explicitly preserves
the fetch hold and owns the Hedron/Surgical search policies. Since the passive
opponent casts no spells and its creatures do not threaten the Mill goldfish,
autonomous Mill play omits Counterspell, Drown in the Loch, and Fatal Push.
Human play retains their
legal casts; `MTG_UNPRUNE=millcontrol` restores them for measurement.

Oboro's self-bounce remains in the full search menu even before a Crab enters.
An overnight outlier (seed 4004) replayed Oboro repeatedly without a payoff,
but a proposed filter that removed this choice before a Crab entered slowed
two regression depth-0 games, including a lost turn-eight win: bouncing Oboro
early can preserve a later landfall entry for a Crab that arrives afterward.
The filter was rejected after the per-game A/B audit.

The static viewer auditor now passes. Glimpse and Hedron have player-target
choices; Surgical, Drown's modes, Oboro's activation, and Archive Trap's cost
choice ride the main-phase plan surface. Drown's Oracle-text modal regex still
prints an advisory because that scan does not recognize the new modal parameter.

Hedron target search uses a MillProvider-owned set of 16 periodic four-trigger
patterns. It represents every target sequence for up to four triggers; a unit
test enumerates all 16 four-trigger sequences. From the fifth trigger onward
the pattern repeats, so nonperiodic longer sequences are a known search-width
narrowing, not a rules restriction. The human viewer asks for every trigger
independently. The four-trigger mask is covered by tests and the multi-depth
sweep below.

Surgical's optional any-number selection has a large case in the opponent
pool: Shadowborn Apostle appears 18 times in one list. Exhaustive physical-card
subsets are impractical there (2^18). MillProvider's current autonomous policy
chooses every matching library copy for an opponent target and no additional
copies for a self target. `MTG_UNPRUNE=surgicalsubset` restores the full subset
search, and the human chooser still sees every legal subset. The plan signature
includes the Surgical hand slot, target, and subset; otherwise distinct
two-Surgical plans collapse during deduplication. A resolution audit also found
that the targeted graveyard card had been left in place for some optional
selections; the resolver now exiles that card unconditionally.

Focused tests cover Tasha's ordered printed-MV walk; Glimpse and Hedron target
player, including self-mill; Surgical target availability after earlier mill
in the same plan; Archive Trap's opponent-search condition; Counterspell and
Drown's own-spell target and stack behavior; Fatal Push's revolt cap; and
Oboro's searched repeatable self-bounce. The final focused suite passed 31 Mill
cases with 1,300 assertions. The full unit suite passed 3,115 cases and
3,600,257 assertions.

## Verification

Coverage: clean (19/19 mainboard names; 2026-11-14). The full mana-cost audit
compared 631 costed cards with Scryfall and reported zero mismatches. The static
viewer decision audit passes; Drown's modal text yields a known regex advisory.
The field snapshot was refreshed for all 15 new card entries; the offline audit
reports 795 checked, no hard mismatches, no unfetched cards (five custom/token
definitions are outside Scryfall scope).

The baseline profile was regenerated with analyzer seed 1 after the final
Surgical search changes and has `mulligan.min_playable: 1`. The analyzer found
`NO_COST_INTERACTIONS`; its 400-game depth-3 discard evidence pass observed 16
cleanup choices and returned `STATUS_QUO_OK`. No discard ordering was adopted.
The full unit suite passed 3,115 cases and 3,600,257 assertions.

Stage 5b used 50 consecutive games from seed 9001, one against each opponent
slot, at max turn 8. Depth 0 won 35/50 at a scored average of 7.16 turns; depth
3/budget 10 won 49/50 at 5.14; depth 5/budget 20 won 50/50 at 5.08. No game
slowed as depth increased. Depth 5 recovered depth 3's one unwon game (game
17) at turn 7 and improved game 22 from turn 5 to turn 4. The seven depth-5
turn-seven games are the slow tail. Seed 4004 in the overnight canary is a
separate poor opening: two Surgical Extraction, Drown in the Loch, Fractured
Sanity, and three lands with only two blue sources. It remains unwon by the
eight-turn cap, but wins by turn 11 when the cap is raised. Its repeated early
Oboro bounce looked suspicious; a proposed before-Crab filter was rejected
after it slowed two depth-0 regression games, including a lost turn-eight win.
Oboro must remain searchable before the Crab is on the battlefield because the
bounce can preserve a future landfall. Per-game sweep results are in
`logs/mill_final_sanity_d{0,3,5}_seed9001.{out,err}`.

The later hand-based Oboro rule omits its paid self-bounce when another land is
already in hand. The action remains available with no land in hand, including
before a Crab enters, and in human play. `MTG_UNPRUNE=oborobounce` opens it for
the paired audit.

Stage 5h's dynamic viewer audit drove 40 games from seed 9101 and passed:
all expected `land_entry`, `target`, and `tutor_etb` decisions surfaced, with
no hard miss, unverified card, or driver failure. A targeted `--verify-card`
run for Drown in the Loch confirmed its destroy-target decision at seed 9101,
game 1. Drown's modal Oracle-text advisory is a regex limitation; its modes
are present in the main-phase plans. Audit output is in
`logs/mill_viewer_audit_seed9101.out` and `logs/mill_viewer_verify_drown.out`.
The repository's strict reference viewer gate also passed on the final source:
837 references, zero play drift, shuffle failures, enumeration gaps, or
contract failures.

Surgical's first one-target ranking had a depth-3 slowdown in the 20-game
full-target A/B (seed 9015, game 14), so the initial analysis retained a
shortlist of opponent names. That shortlist used hidden library contents and
was replaced during the subsequent user-directed optimization. Autonomous Mill
now carries one deferred Surgical action: immediately before paying for the
cast, after any earlier same-plan mill, it considers only nonbasic cards in
graveyards. Without Tasha in hand, it picks an opponent printed four-of with
exactly one public copy in graveyard plus exile, breaking ties by highest
printed mana value. With Tasha in hand, it considers printed three- and
four-ofs and ranks them by `(printed copies - public copies) × mana value`.
This estimates the mana value Surgical can remove from the remaining deck;
the opponent's hidden hand and library order are not inspected. Without Tasha,
other four-ofs follow the one-public tier. It then considers another legal
opponent card, then a self target. A cast with no
legal target is refused. The chosen opponent target exiles its library copies;
the cast pays two life whenever that leaves the caster alive. Human play and
`MTG_UNPRUNE=surgicaltarget` retain the full target menu.

The Surgical rank A/B used identical 50-opponent seed slots for each arm
(training seed 9001, held-out seed 4004; 100 depth-0, 50 depth-3, and 100
depth-5 games per set). The previous one-public/decklist-order rule averaged
6.60/5.12/5.10 turns on training and 6.67/5.12/5.18 held out. Mana-value
tie breaking averaged 6.59/5.12/5.09 on training and 6.67/5.14/5.18 held
out. The Tasha-aware rank kept those training win turns and reduced held-out
depth-5 from 5.18 to 5.17 (one game, seed 4004 game 64, won on turn 4 rather
than 5). Its held-out depth-3 average stayed at 5.14. The paired outputs are
in `logs/mill_heuristic_ab/`. The final full unit suite passed 3,129 cases
and 3,600,425 assertions; focused Mill and both unpruned Surgical paths passed.

After the Mill fetch, control, Oboro, life-paid Surgical, and deferred-target
rules, the accepted smoke depth-3/depth-5 job costs fell from 82,702/84,378
to 24,946/34,339 units. The searched smoke games had no slower wins and six
faster wins across the Mill and 2HG rows. Regression's searched games likewise
had no slower wins and one faster win; seed 3003 depth 5 improved from 5.4
to 5.2 turns. The depth-0 rows had both faster and slower games, with net
average improvements from 6.90 to 6.83 (smoke) and 6.84 to 6.78 (regression).

The original full-target comparison exceeded the suite cost gate on the
depth-3 block alone (17,031 core-ms/game). A 20-game paired
depth-3 subset A/B (`MTG_UNPRUNE=surgicalsubset`, seeds 9001–9020) had no
slower game under the one-mode policy; its one changed game won on turn 4
instead of the full subset menu's turn 6 (seed 9003). The initial paired
suite gate measured Mill at 5,028 core-ms/game and Hinata at 3,601.307, a
1.40x ratio below the fixed 3x limit. The one-mode Surgical subset axis is
retained for the Shadowborn Apostle size case; the printed rules are fully
available in human play and through `MTG_UNPRUNE=surgicalsubset`.

The initial analysis accepted all three suite tiers and the 2HG canaries. Smoke
search rows average 5.2 turns; regression rows average 5.0 at depth 3 and
4.8/5.4 at depth 5; overnight rows average 5.6 at depths 3 and 5 on seed
4004 and 5.0 at depth 5 on seed 5005. Their per-game logs and play digests
are in `test/gt_logs/`; `test/check_gt_logs.py` reports no missing or stale
logs. Those values record the initial analysis; smoke and regression were
reaccepted after the heuristic tuning above, while overnight remains for the
next periodic full-tier run. The Codex-play sweep and final-build Surgical
replay are recorded below.

The final `verify_deck.py --no-network` gate passed: coverage 19/19, field
snapshot 795 checked, all regression tiers with GT, viewer and wiring, zero
`[nonconv]` or `[fd-diverge]` flags over seed bases 7001 and 8001 with 60
games each, and 8 play-invariant games with 242 decisions. The network cost
audit had already reported zero mismatches over 631 costed cards. The leaf
tie-break A/B changed zero of 1,600 paired play-setting games (3,200 runs);
Mill does not reduce opponent life, so its opponent-life horizon grade ties
throughout. The default remains enabled. The checker reports `NO SIGN` for
this finite sample, and no opt-out is justified by either the observed games
or this deck's zero-damage rules.

## Approved deferrals

None.

## Codex-play sweep

- commit: `9b071ffe8626922d2a7ab05819f87a9b008507bc` (the Release binary was built from the shared, uncommitted Mill working tree at this HEAD, before the prompt wording fixes below)
- seeds: `731000 + 13i`, `i = 0..17`; games: 18; opponent library slot: `seed % 50`; game index: 0; maximum turns: 12; reveal: 6
- flags: 0 unresolved gameplay correctness flags
- paired result: all 18 games were won by both players; Codex tied the shipped search in 8 and won later in 10; no earlier Codex win exposed a search misplay.

| Seed | Opponent slot | Search win turn | Codex win turn |
| ---: | ---: | ---: | ---: |
| 731000 | 0 | 4 | 4 |
| 731013 | 13 | 5 | 7 |
| 731026 | 26 | 8 | 11 |
| 731039 | 39 | 5 | 5 |
| 731052 | 2 | 6 | 7 |
| 731065 | 15 | 7 | 12 |
| 731078 | 28 | 5 | 6 |
| 731091 | 41 | 5 | 5 |
| 731104 | 4 | 5 | 6 |
| 731117 | 17 | 5 | 5 |
| 731130 | 30 | 5 | 5 |
| 731143 | 43 | 4 | 4 |
| 731156 | 6 | 5 | 7 |
| 731169 | 19 | 5 | 6 |
| 731182 | 32 | 8 | 11 |
| 731195 | 45 | 4 | 4 |
| 731208 | 8 | 5 | 5 |
| 731221 | 21 | 8 | 9 |

The agents read the exact Mill definitions in `cards.json` and checked the
offered plans and state changes. Crab landfall, fetch entry and sacrifice,
Glimpse, Fractured Sanity cycling and cast, Archive Trap, Surgical Extraction,
and Tasha's ordered exile produced the expected library or zone changes on
their observed lines. The different opponent slots gave Tasha visibly different
exile depths: 10 cards at slot 2, 27 at slot 43, and 24 at slot 8.

The sweep found presentation defects, fixed in source after the Release binary
was frozen: Hedron Crab's target prompt called mill "damage"; Glimpse's human
target default chose self even though the usual mill target is the opponent;
Counterspell/Drown rider proposals inherited Remand's return-and-draw label;
Surgical's reused `dig` and `dragon` prompts described taking cards into hand
or putting them onto the battlefield. The game-state effects were correct in
the observed replays. After the final rebuild, protocol replays confirmed
Hedron's `mill_cards: 3`, Glimpse's `mill_cards: 10` and opponent default,
Surgical's graveyard-target and exile notes, and Counterspell's graveyard
counter label. The viewer JavaScript syntax check and direct render checks for
Hedron, Glimpse, and both Surgical panels passed.
In a targeted final-build replay of seed 731091, Surgical's mandatory target
was the same with explicit index 1 or the `-1` default. The optional prompts
offered two Ageless Entity copies in hand and one in the library. Selecting the
library copy reduced that library from 39 to 38; declining it left 39.

Full `--choices` replay streams, in seed order:

```text
731000: 1,0,1,-1,-1,3,0,0,0,-1,-1,3,0,-1,-1,0,0,0
731013: 0,1,2,0,-1,-1,3,0,-1,-1,0,0,-1,-1,0,0,0,-1,-1,0,0,-1,-1,11,13,0,-1,-1,33,0
731026: 1,0,-1,-1,1,0,-1,-1,2,-1,-1,2,0,-1,-1,7,-1,-1,-1,-1,13,0,0,-1,-1,19,-1,-1,-1,-1,1,20,0,0,-1,-1,4
731039: 1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,3,0,0
731052: 1,0,2,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0
731065: 0,0,1,4,5,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,2,2,3,2,2,3,2,2,3,0,0,1,1,0,0,0,0,0,0,0
731078: 0,1,0,0,2,0,-1,-1,0,0,-1,-1,0,0,1,-1,-1,3,0,-1,-1,0,-1,-1,0
731091: 1,0,0,0,0,1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0
731104: 0,0,1,4,4,1,-1,-1,2,0,2,1,0,0,-1,-1,0,0,-1,-1,0,-1,-1,0,0,0,-1,-1,0,0,0,0
731117: 1,0,1,-1,-1,3,-1,-1,0,0,0,-1,-1,0,-1,-1,120,0
731130: 1,0,-1,-1,7,0,2,1,0,0,-1,-1,0,0,-1,-1,1,-1,-1,0
731143: 1,0,1,-1,-1,3,0,0,0,-1,-1,0,0,-1,-1,0
731156: 1,2,-1,0,0,1,2,1,0,0,0,3,0,0,2,1,-1,-1,0,0,-1,-1,0,-1,-1,0
731169: 0,1,2,0,0,0,0,0,-1,-1,1,0,0,0,1,0,0,0,0,-1,-1,2
731182: 0,0,0,1,0,5,0,0,0,0,-1,-1,-1,-1,-1,-1,0,1,1,1,-1,-1,0,-1,-1,0,-1,-1,0,0,0,0,-1,-1,-1,-1,0,-1,-1,-1,-1,12,0
731195: 1,0,1,-1,-1,1,0,2,1,0,-1,-1,0,0,0,0,0,0,-1,-1,18,9,0,0,0
731208: 1,1,-1,-1,3,0,-1,-1,1,1,0,0,-1,-1,0,-1,-1,1,0,0
731221: 0,1,4,1,0,0,1,0,0,0,0,0,2,0,-1,-1,0,1,0,0,0,0,0,0,0,0,0,0,5,5,2
```
