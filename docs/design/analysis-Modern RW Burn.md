# Modern RW Burn analysis

Deck: `decks/Modern RW Burn/Modern RW Burn.cod` (60 mainboard cards).
Started 2026-11-05. This is the user's untracked deck file; the unrelated
`.vscode/settings.json` edit predates this analysis.

## Initial coverage

`python3 scripts/analyze_deck.py 'decks/Modern RW Burn/Modern RW Burn.cod' --coverage-only`
found seven missing mainboard cards: Arid Mesa, Sacred Foundry, Boros Charm,
Lightning Helix, Sunbaked Canyon, Inspiring Vantage, and Boltwave. The sideboard
is unreachable because the deck has no wish effect and the simulator has no
sideboarding between games.

Existing bracket notes to review: Goblin Guide's defending-player library
trigger; Skullcrack's life-gain and damage-prevention restrictions; and Wooded
Foothills' fetch resolution and hand-only colour-source approximation.

## Card implementation and viewer decisions

Missing-card research was fanned out; one integrator applied changes to shared
card data and C++. The final coverage check reports no missing or partial
cards. Live Scryfall cost audit compared all 542 costed entries with zero
mismatches.

The old fetch path immediately sacrificed a fetchland on play. This lost
the legal line of holding Arid Mesa or Wooded Foothills across turns and cracking
it later to enable Searing Blaze landfall without using that turn's land drop.
The same-turn crack does not affect landfall because the fetchland's entry
already counted. `LandPlay.cpp` and the `SacLandSearch` activation path now hold
the fetchland until a separate activation, pay life, search either subtype,
and prevent it from producing battlefield mana. Directed scenarios confirm a
held Arid Mesa enables Searing Blaze landfall and that it can find Sacred
Foundry, pay the fetch plus shock life, and fund lethal Boros Charm.

User direction (2026-11-05): Goblin Guide's attack trigger may be deferred for
this analysis, alongside a future move to an actual passive-opponent deck that
plays one land per turn from hand. The trigger had already been integrated in
the shared combat path before this direction arrived; retain it if verification
passes. The future opponent model is recorded separately in
`docs/design/passive-opponent-actual-deck.md`.

Play-protocol replays verify both Guide outcomes: a revealed nonland stays on
top, while a revealed Mountain leaves the opponent library and enters its
hand. Arid Mesa's activation emits a `tutor_etb` choice with Mountain and
Sacred Foundry as legal targets. A human-menu probe on the Swiftspear board
shows all four relevant Boros Charm variants (opponent, self,
indestructibility, and target creature double strike). With two own creatures,
the menu keeps separate double-strike targets for each creature.

Viewer follow-up, seed 2: queuing Wooded Foothills followed by Rift Bolt's
suspend yielded `legal_not_enumerated`. The fetchland cannot itself pay the
suspend cost, but its same-turn activation can find a Mountain that does. The
viewer now offers a "Crack Wooded Foothills, then suspend" route that commits
the land, activation, and suspend as consecutive main-phase segments, with the
fetched-land choice between them. This preserves the legal line and avoids a
misleading `cannot pay` verdict.
The viewer now takes that route automatically when the queued line needs the
fetch's mana. The same payment-triggered route handles an ordinary cast and
a single fetch already on the battlefield. The engine marks only untapped
fetch activations as same-turn mana; Evolving Wilds-style tapped searches do
not trigger the automatic route. The fetched land remains a player choice.

## Provider routing

The provider audit showed that fetchland search sends this list to
`AntiLifegainProvider` before the Burn branch. `ModernRwBurnProvider` therefore
derives from `AntiLifegainProvider` and is routed immediately above that branch.
Its fetch target choice is now Burn-specific: fetch Sacred Foundry until two
white-producing lands are on the battlefield, then Mountain, falling back to
the other legal target if the preferred one is absent. Human play and the
unpruned audit still expose every legal target.
Detection requires one of Boros Charm, Boltwave, Sunbaked Canyon, or Inspiring
Vantage, all unique to this repository decklist. Its certificate is
`NotAssessed`. A Claude game found that the inherited opaque-cast policy omits
a legal Bolt-then-spectacle-Skewer T3 kill; a provisional, flaggable
Modern-RW-specific cast order now places spectacle after burn and Eidolon last.
Its default-on adoption requires user review. Paired baseline/variant samples
found one faster and zero slower game in 30 d3 seeds, and one faster and zero
slower game in 20 d5 seeds. The remaining inherited AntiLifegain tutor, phase, attack, and
cleanup-discard policies also need explicit disclosure and user review.

The initial repository-wide provider audit could not inspect this deck before
its profile existed. The next audit found the fetchland misroute described
above. It also found a pre-existing Slivers certificate inherited without an
override. A Slivers-specific `NotAssessed` declaration now preserves that
stance while satisfying the own-provider metadata rule. The provider audit
passes, including Modern RW Burn routing.

## Saved references and fetch regression (2026-11-06)

At the shipped play settings, search matched or beat all ten saved user games:
six ties, four one-turn improvements, zero opening-hand mismatches, and zero
shortfalls. The result remained identical after the Burn fetch target rule.

The first full smoke run after the fetch activation change found 14 moved keys
concentrated in Anti-Lifegain, FiveColour, Burn, and Modern RW Burn. Holding a
fetchland until activation cost Anti-Lifegain and FiveColour about a tenth of a
turn at depth 0 (4.999 and 5.566 versus 4.900 and 5.477 with immediate cracking).
Those two providers now explicitly crack the reviewed zero-mana, one-life
fetchlands on play in autonomous search. Human play and the unpruned audit keep
the activation choice. After this policy change, the affected smoke and
regression searched games had zero slower outcomes versus the committed GT;
strict replay passed 767 active references with zero play drift, board
divergence, or enumeration gaps. The affected smoke and regression rows were
accepted together with their per-game logs; `check_gt_logs.py` reported all
1,027 entries consistent. The affected overnight rows were subsequently
measured on the same source tree with PGO and accepted with their per-game
logs; the full 1,027-entry consistency check passed again. Its searched game
audit reported 27 slower and 202 faster games. Higher-budget replays recovered
all Anti-Lifegain and FiveColour slowdowns. Persistent Burn slowdowns had a
different kept hand or a different post-fetch draw, so they are not
like-for-like search regressions. Burn's four solo depth-5 overnight rows each
improved by roughly 0.04–0.07 turns.

## Verification

Coverage reports zero missing or partial cards. Static viewer decision audit
passes after classifying the fetch activation life payment and recognizing an
empty mana cost as a real land-search activation (`tutor_etb` expected). Baseline
profile generation finished with 18 card scores and no hand-score gate. Its
multicolor static mulligan rule was corrected to `min_playable=1` after the
generator wrote its default `0`. The cost-aggregate diagnostic returned
`NO_COST_INTERACTIONS`. The discard study found zero cleanup decisions in 400
real-play games, but its rollout census counted 4,279 sheds, all with fewer
than four lands. Its verdict is `DISCARD_UNLABELLED`. A paired 150-game d3
`MTG_SHED_WORST=1` bound had identical average win turn (4.0067) and identical
play digest to the baseline, so no provider-specific cleanup ranking is
justified by the measured outcomes. The suite cost calculation on the pooled
regression batch is 23,981.44 ms/game for RW Burn (2HG d5/b20) against
13,205.93 ms/game for pinned Hinata (2HG d5/b30), a 1.816x ratio under the
3x limit, and the official cost gate returned PASS. Its accompanying strict
saved-reference replay found 10 play drifts, three enumeration gaps, and seven
shuffle-dependent cases, all in pre-existing Anti-Lifegain/FiveColour games
that use Wooded Foothills. Those recordings used an immediate-fetch land drop.
A replay-only `--legacy-fetch-immediate` flag and a `fetch_activation` stamp for
new recordings restore their recorded line while live play retains the held
activation. Strict replay of all 43 affected Anti-Lifegain/FiveColour references
on the rebuilt binary found zero play drift, shuffle-dead cases, or enumeration
gaps (one exact replay and 42 repaired stale decisions).
Smoke and overnight tiers passed and their 4 and 16 new keys respectively
were accepted with the same filtered deck selector. The final Release regression
tier passed all 426 scenarios and all five new Burn keys, with zero changed-game
regressions; those five keys were accepted with the same selector. The horizon
tie-break quick check, eight-block extension, and 32-block check found zero
changed outcomes in 400, 1,600, and 6,400 paired games respectively. The
32-block result is unbindable: it gives no measured direction for the tie-break,
so its existing default remains enabled.

Accepted overnight rows give 100% wins in 1,200 solo d5 games (mean win turn
4.0508), 100% in 1,600 solo d3 games (mean 4.0469), 7,967 wins in 8,000 solo
d0 games (mean winning turn 4.3766), and 100% in 400 2HG d5 games (mean 5.12).

The Scryfall field snapshot audits 695 cards with no hard-field mismatch and
no unfetched entries. Its network refresh added the seven new cards but
returned HTTP 429 for 175 existing fetches; those retained their committed
snapshot entries. The live cost audit found zero mismatches among 542 costed
entries.

A directed Boros Charm scenario found a plan deduplication bug: distinct mode
and creature-target variants collapsed to one cast. Both plan signature
builders now include its mode and target; the scenario wins on turn 4 with
double strike. The fast/legacy signature check reports zero mismatches.
Directed Lightning Helix life gain (2 to 5) and Boltwave 2HG team damage
(opponent 6 to 0) scenarios also pass. All eight directed cases pass on the
final no-diagnostic game binary.

The true Claude-play oracle found a viewer-label defect: Boros Charm's legal
self-damage target uses `enchant_target=-1`, which the generic plan summary
mistook for an opponent-creature trick sentinel. The menu now explicitly names
all three Charm modes and the self target. Its seed 891002 also exposed a real
play miss: Bolt followed by spectacle Skewer and two prowess triggers wins T3,
but the inherited order priced Skewer at its printed cost and chose a T4 line.
`modern_rw_burn_spectacle_t3.json` reproduces the missed kill with the ordering
flag off and passes T3 with the provisional default-on ordering. The full seed
891002 AI game likewise improves T4 to T3.

Claude seeds 891004 and 891005 exposed a second provider gate: with no opposing
creature, Searing Blaze is dead face damage, but the generic MV-only dig test
counts it as castable and suppresses Sunbaked Canyon's sacrifice-to-draw line.
`modern_rw_burn_canyon_dead_blaze.json` fails under the inherited gate and
passes with `MTG_UNPRUNE=dig`. A narrow Modern-RW-only A/B override now offers
the draw decision when the only cheap spells are dead Searing Blazes and at
least two lands would remain after sacrifice. The rebuilt candidate and paired
measurement are reported below; user review remains outstanding.

The rebuilt default-on gate passes the Canyon T5 fixture; setting
`MTG_RW_BURN_DEAD_BLAZE_DIG=0` restores the old failure. On exact Claude seeds
at d5/b200, AI seed 891004 improves from unwon within eight turns to T7
(Claude T8), and 891005 improves T6 to T5 (Claude T5). The original Claude
choices replay on the rebuilt binary without state drift. Shipped-budget
d5/b20 reruns confirm the same T7 and T5 wins. A 20-game paired d5/b20 sample
showed no changed win turns. Both provider changes remain provisional pending
user review.

A further directed depth-zero fixture found that the cheap lethal projection
undercredits Boros Charm's creature double-strike mode: a pumped Swiftspear
would deal 10, but the search chose Charm's 4-to-face mode and dealt only 9.
`modern_rw_burn_boros_double_strike_d0.json` reproduces this miss. A shared
conservative extra-strike credit has been added to both Solve and EnumeratePlans;
it counts an existing ready attacker only when no selected action might tap or
remove it. The final rebuild passes this d0 lethal fixture and the other seven
directed Burn scenarios.

The true Claude-play sweep reached 15 games (seeds 891001–891015). On the
final build, the AI at shipped d5/b20 won four games earlier than Claude and
tied eleven; Claude won earlier in none. The AI-side replay after the Boros
projection fix left all 15 win turns unchanged. Claude reported no illegal
plans or state-transition flags. The 40-game play-viewer runtime audit
passed: expected `land_entry`, `target`, and `tutor_etb` choices all surfaced;
it also observed `free_cast`, with zero missing decisions or driver failures.

## Claude-play sweep

commit: a705f3dea6003cda2be7747383a224312ef274e0 (plus this analysis working tree)
seeds: 891001–891015
games: 15
flags: 0 unresolved

Claude drove each game through the true `--claude-play` oracle protocol. The
AI-side final-build replay won four earlier, tied eleven, and lost none of the
turn comparisons. There were no illegal choices or state-transition flags.

## Further verification

The final-profile nonconverged d3, full-divergence d5, and 2HG d5 mismatch
sweeps covered 240, 240, and 100 games respectively, all with zero flags. A
20-seed matched d0/d3 sample after
the Boros projection fix had four depth improvements, sixteen ties, and no
depth regressions.

The final `verify_deck.py` report passed coverage (18/18 full), Scryfall mana
costs, the 695-card field snapshot, all three regression tiers, viewer wiring
and runtime surface, both mismatch arms, eight play-invariant games (132
decisions), the recorded 15-game Claude sweep, and suite admission. Its only
skip was the optional dedicated per-clause ledger; the card clauses were
checked through coverage, bracket notes, and oracle comparison.

The official Hinata cost gate passed at 1.816 times the pinned benchmark,
below the 3 times admission limit. All three regression tiers have accepted
Burn rows.
The old recordings assumed that Wooded Foothills fetched immediately on being
played. New live games correctly hold fetchlands for activation. A replay-only
compatibility flag and recording stamp preserve both timelines.

An 800-seed matched d0/d3 comparison had 246 faster d3 wins, 553 ties, and
one slower d3 win. In the outlier, d3 cast Lava Spike and spectacle Skewer on
turn 2, then Searing Blaze on turn 3; d0 played Eidolon on turn 2 and a second
Eidolon on turn 3, letting Eidolon damage enable a turn 4 kill. The d3 line
won on turn 5. This is a horizon choice, not a rules or legality defect;
matched d3/d5 games had three improvements and no slower games.

## Approved deferrals

User decision (2026-11-05): defer instant-speed fetchland activation. Games can
have opposing creatures, but this simulator does not need a response window to
make Burn's relevant fetchland and Searing Blaze plays. Wooded Foothills and
Arid Mesa remain activatable in human-play main-phase slots, including a later
turn after being played; the autonomous Burn search now takes the provider's
immediate-crack policy. The user can also hold the land in hand until its
landfall turn. This timing simplification is approved for this analysis.
Goblin Guide's reveal and land-to-hand trigger is implemented.

Follow-up on delayed fetch activation: the only distinct Burn use identified
was playing a fetchland for Searing Blaze landfall, leaving it uncracked, then
cracking it for another Blaze on a later turn without another land drop. That
requires two Blazes, suitable opposing creatures, and no need for the fetched
mana or land in between; it is too narrow to justify a Burn-specific search
branch by itself. Terramorphic Expanse and Evolving Wilds in Landfall and
Landfall Elementals are separate cases. In a 100-game Burn probe at d3/b10,
the delayed path had 519,088 solve-memo misses versus 396,908 with the
replay-only immediate-fetch mode (+31%), while wall time was 52.8s versus
53.7s. This is evidence of extra search work, not a demonstrated wall-time
slowdown. The replay mode is not a production timing switch; any simplification
must preserve the fetch's life payment, legal plays, and saved references.
The autonomous Burn provider now resolves its zero-mana, one-life fetches on
play by default (`MTG_RW_BURN_FETCH_AUTOCRACK=0` restores the hold search).
At three life or less, it keeps the hold so a fetch can still be played for
landfall when cracking it or shocking in a target would be unsafe. Human play
and `MTG_UNPRUNED` still expose the legal later activation. This is
an explicit Burn heuristic, not a generic fetchland rule. A proposed refinement
is to allow hold only for a plan that does not spend the mana its immediate
fetch would have supplied; that needs plan-level payment evidence and is not
claimed by this coarse initial policy.
An isolated 100-game d3/b10 A/B on seed 1001, one worker in both arms to keep
wall time comparable, measured auto-crack at 54.0s / 3.97 mean win turn and
full hold (`MTG_RW_BURN_FETCH_AUTOCRACK=0`) at 60.7s / 4.03. This is a small
sample; the per-game regression audit remains the correctness gate.
The same fetchland timing model applies to other lists: Anti-Lifegain, Creature
Giving, FiveColour, Mill, Minotaur, and Unpredictable Cyclone also contain
fetchlands. A global timing change would therefore need a cross-deck audit.
Unpredictable Cyclone can usually get its desired post-Brainstorm shuffle by
holding Scalding Tarn in hand, playing it after Brainstorm, then cracking it.
The battlefield-hold line differs only when that turn's land drop is already
used, Tarn had to be played earlier to avoid losing a land drop, or multiple
fetch activations in one turn matter. These are possible but unmeasured; the
earlier Cyclone note (`docs/design/uncracked-fetchland.md`) overstated the
general need for battlefield hold. Mill's library-dependent analysis remains
deferred. No Burn-only result establishes a global timing policy.

The follow-up fetch gate passed 429 directed scenarios, including a life-one
landfall line where cracking is unpayable, and the seed-2 viewer line remains
`legal_not_enumerated` rather than `cannot pay`. Strict replay of 758 saved
references found no play drift, board divergence, enumeration gap, or contract
failure. The 100-game solo d3/b10 A/B above favored auto-crack by 0.06 turns
and about 11% wall time. In the regression tier, searched games had 26 faster
and six slower outcomes; all six slower games persisted at 16x budget, with
four duplicate entries from two seeds whose fetch timing changed the next
draw, one changed bottom hand, and one further draw-divergent fetch game.
The aggregate win-turn mean improved in all four solo cells. The solo baseline
was accepted with those exceptions recorded. The 2HG smoke mean stayed 5.24;
17 same-turn play digests changed with identical kept hands and draws, and its
baseline was accepted.
The 75-game 2HG regression improved from 4.9733 to 4.9600 mean win turn,
with three faster and two slower games. Both slower games recovered their old
turn at 4x and 16x budget. One also drew a different card after a changed
fetch line; the other was a same-draw turn-one pass at the shipped budget.
The 2HG baseline was accepted with both cases recorded. The same Release
binary had already passed the 758-reference strict replay before this run.

## Opponent-model dependency

Mill and other decks that depend heavily on the opponent's hand or library are
deferred until the passive opponent uses an actual decklist and plays lands
from hand. This is a deck-onboarding dependency, not a deferred RW Burn card
clause. See `docs/design/passive-opponent-actual-deck.md`.

## Exhaustive mulligan profile

The adopted 12-bucket exhaustive profile contains the fully sampled keep and
bottoming policy (raw sidecar R=30). Its keep-only A/B improved mean win turn
by 0.1885; the confounded bottoming A/B improved it by 0.04125. Mountain is
grouped with Inspiring Vantage and Sacred Foundry in the measured bucket set:
the list has enough white sources that forcing a separate Mountain bucket did
not justify regenerating the table.

Before adoption, the no-table smoke and regression cells reproduced their
committed ground truth exactly. The overnight depth-five sample was increased
from 300 to 425 games per seed to meet the profile-adoption floor, and its
no-table baseline was accepted first. The current engine's Skewer fix moved
three searched overnight lines at the same win turn and improved one greedy
game by a turn; there were no slower no-table games.

With the table active, all smoke, regression, and overnight cells improved.
The overnight solo means moved from 4.3450–4.3645 to 4.0720–4.0945 at depth
zero, from 3.9375–4.0100 to 3.8250–3.8625 at depth three, and from
3.9294–4.0094 to 3.8141–3.8706 at depth five. The four overnight 2HG means
moved from 5.0500–5.1400 to 4.7400–5.0500. Search work also fell sharply.
The adoption replay reproduced both baselines exactly: 5,695 solo games changed
mulligan count, 924 changed the bottomed hand, 15 changed only bottom order,
and none were unexplained. The 2HG replay likewise had zero unexplained games.

## Value leaf

The completed value-leaf sweep selected the emulated ladder. Its paired
8,000-game A/B against live search improved mean win turn by 0.00100 at 0.999
times the work units. The direct staged model improved by 0.00112 but cost
1.442 times the units, so it was not selected. After installing the emulated
ladder on the exhaustive keep table, the smoke tier had no win-turn changes;
the regression tier improved one 2HG game by one turn and had no slower games.
The solo searched cells changed some same-turn play digests without changing
their mean win turns. Smoke and regression ground truth were accepted with
the leaf active; the overnight tier records the table-only adoption and is
left to the periodic full-suite run under the value-leaf workflow.
