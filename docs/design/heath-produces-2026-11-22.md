# Fetchland hand-colour hint restored (Windswept Heath, Wooded Foothills, Arid Mesa, Flooded Strand), 2026-11-22

## Defect

`9344fa594` (Boars I1) made Windswept Heath a real battlefield fetchland and changed its `produces` from
`[W,B,R,G]` to `[]`, moving the colours to a new `hand_fixing_produces` key. Only Creature Giving's
cleanup-discard cover read that key. Every other land-drop and colour-fixing reader reads `produces`, so a Heath
in hand counted as a land that fixes no colour. The greedy land rank (`LandPlay.cpp`, the multi-colour pass) and
the provider fixing heuristics then played a dual or basic and held the Heath. The 2026-11-20 overnight accept
bisected it (`docs/design/overnight-accept-2026-11-20.md`, Five Colour section): Five Colour d0 games, Antilife
s6006 gi117 (1HG and 2HG), and 10 Creature Giving games.

Scalding Tarn and Misty Rainforest got the same battlefield conversion but kept a `produces` hand proxy. That is
safe on the battlefield: `EffectiveProducesFor` returns no colours for any permanent with `fetch_land_types`, and
the real tap (`tap_source`) goes through it, so a held fetchland never makes mana.

Wooded Foothills (since 2026-11-06, Modern RW Burn), Arid Mesa and Flooded Strand had the same inconsistency:
battlefield fetchlands with `produces: []`.

## Fix

* Windswept Heath: `produces` back to `[W,B,R,G]` (its pre-Boars record). `hand_fixing_produces` is removed from
  the card, the loader, `CardParams`, the Creature Giving fallback and the viewer-decision audit. No card carries
  it, so Creature Giving's cover reads `produces` exactly as it did when the USER settled that rule.
* Wooded Foothills: `[W,B,R,G]`, its pre-2026-11-06 legacy hint.
* Arid Mesa `[W,R]`, Flooded Strand `[W,U]`: their basic types' colours (they never had a hint; the Polluted
  Delta `[U,B]` convention).
* `test_boars_cards.cpp`: the Heath test now checks the hint and that a Heath or Foothills on the battlefield
  produces nothing through `EffectiveProducesFor`.

The battlefield model (separate entry and searched activation, the hold) is unchanged; it keys on
`fetch_land_types` and the `land_search_activation_*` fields, not on `produces`.

## Measurement

Held-out pooled A/B, 2026-11-22 (seed base 960000, game_index = offset; one pooled batch per arm because the arms
need different binaries and cards.json; base = `42d819bc0` built in a detached worktree, new = `9c61ad6e8`;
MTG_PGO=0). d0 = 5000 games per deck; play = the deck's play setting (profile depth, overnight-row budget), sizes
below. `net` = summed turn delta over games both arms won (positive = slower); lost/gain = wins lost/gained.

| case | n | changed | faster | slower | lost | gained | net | avg base | avg new |
|---|---|---|---|---|---|---|---|---|---|
| antilife d0 | 5000 | 801 | 439 | 311 | 25 | 26 | -98 | 4.7349 | 4.7200 |
| antilife play (b20) | 4000 | 3 | 1 | 2 | 0 | 0 | +1 | 4.1689 | 4.1692 |
| antilife2hg play (b20) | 2000 | 2 | 1 | 1 | 0 | 0 | 0 | 4.1102 | 4.1102 |
| boars d0 | 5000 | 628 | 334 | 287 | 4 | 3 | -33 | 5.1968 | 5.1908 |
| boars play (b40) | 3000 | 5 | 4 | 1 | 0 | 0 | -3 | 4.7516 | 4.7506 |
| creature_giving d0 | 5000 | 679 | 333 | 323 | 12 | 11 | -6 | 5.4701 | 5.4696 |
| creature_giving play (b30) | 3000 | 6 | 1 | 5 | 0 | 0 | +4 | 4.6110 | 4.6123 |
| fivecolour d0 | 5000 | 630 | 399 | 209 | 13 | 9 | -197 | 5.4628 | 5.4217 |
| fivecolour play (b40) | 2000 | 3 | 2 | 1 | 0 | 0 | -1 | 4.7861 | 4.7856 |
| modernrwburn d0 | 5000 | 397 | 190 | 203 | 1 | 3 | +26 | 4.0429 | 4.0486 |
| modernrwburn play (b20) | 3000 | 2 | 0 | 2 | 0 | 0 | +2 | 3.8046 | 3.8053 |
| morph d0 | 5000 | 405 | 219 | 176 | 4 | 6 | -9 | 4.6882 | 4.6873 |
| morph play (b20) | 3000 | 4 | 1 | 3 | 0 | 0 | +2 | 4.0227 | 4.0233 |
| sea d0 | 5000 | 493 | 228 | 171 | 43 | 51 | -43 | 6.0503 | 6.0511 |
| sea play (b20) | 1000 | 0 | 0 | 0 | 0 | 0 | 0 | 5.7042 | 5.7042 |
| spirits d0 | 5000 | 493 | 268 | 223 | 2 | 0 | -42 | 4.8735 | 4.8649 |
| spirits play (b20) | 3000 | 1 | 0 | 1 | 0 | 0 | +1 | 4.5367 | 4.5370 |
| stuffy d0 | 5000 | 730 | 276 | 271 | 97 | 86 | +40 | 5.5390 | 5.5444 |
| stuffy play (b20) | 1000 | 5 | 3 | 2 | 0 | 0 | 0 | 4.5302 | 4.5302 |
| worldfire d0 | 5000 | 200 | 23 | 18 | 87 | 72 | -3 | 6.9364 | 6.9293 |
| worldfire play (b20) | 1000 | 40 | 24 | 16 | 0 | 0 | -8 | 5.6121 | 5.6040 |

Reading (first pass, not yet root-caused):
* The proven Heath/Foothills defect is a **d0** effect: Five Colour -197, Anti-Lifegain -98, Boars -33 turn-games.
  Searched play barely moves (0-6 changed games in 1000-4000 per deck).
* **Arid Mesa's new W/R hint looks harmful at d0:** Stuffy Doll (Mesa is its only changed card) +40 and 11 net wins
  lost; Modern RW Burn (Mesa + Foothills) +26.
* Flooded Strand: Spirits d0 -42, Worldfire play -8, but Worldfire d0 loses 15 net wins (87 lost / 72 gained).
* Searched nets above 0: Creature Giving +4, Anti-Lifegain +1, Burn +2, Morph +2, Spirits +1. The d8 b0 replays of
  all 34 searched slower games are in `build/scratch/heath/ab/d8_*` (pending at the time of writing).

**Not yet adoptable:** the bar is net <= 0 on every deck, searched and d0. Next steps are in
`logs/heath-produces/handoff.md`.


## Rounds 2-4 (2026-11-22 / 23)

### USER rulings (verbatim)

* "I don't know what this is for, but W/U is understating the power of fetchlands. They can get any shock in the deck,
  so they can theoritically produce any colour those can get."
* "Yes, for 1 we should count shocks. I'm less certain about triomes because of the come-into-play-tapped, but usually
  shocks cover the colours we need. 2. Orchard first makes sense for the decks you mentioned." Orchard-before-fetch
  (`MTG_ORCHARD_BEFORE_FETCH`, Creature Giving + Stuffy Doll) is USER-APPROVED, default ON.
* "I'm more concerned about areas that help searched." Searched shapes are sized at 1000-10000 games and reported first.

### Round 2 (static hints, wip/heath-produces 088ecfbc6)

Heath + Foothills `[W,B,R,G]`, Mesa / Strand `[]`, Orchard before a fetch. d8 b0 on the 10 slower searched games: all
recover. A larger held-out searched A/B (seed 2000000, b20): Burn 30000 net +4, Morph 30000 net +3 (one lost), Sea 10000
net +9 (2 faster / 11 slower). Every changed game's first divergence is the T1 fetchland played earlier; all 33 slower
or lost games recover at d8 b0. Decoupling the search's shuffle from the real one (`shuffle_salt_search 1`): Burn net 0,
Morph -22, Sea +5 (13 / 16); Sea blind search -34. So the round-2 searched deficit is fetch-reshuffle timing under the
lockstep (clairvoyant) search, not a play mechanism.

### Round 3/4: the hint is DECK-DERIVED (wip/heath-derived)

`HandFixingProduces` (SpellEffects.h): a fetchland in hand fixes the union of the colours of the untapped lands in its
controller's LIBRARY whose land types match its `fetch_land_types`. Library contents are known information; its order is
not read. Wired into `EffectiveProduces(in_hand)`, the greedy land rank's multi-colour pass and its rollout mirror, and
Creature Giving's cleanup cover. `MTG_FETCH_HINT_TAPPED` (report-only) also counts always-tapped lands (triomes): Five
Colour d0 100000 games net 0 (18 changed), play 3000 games 0 changed, so the USER's untapped-only reading costs nothing.
The static `produces` stays as the fallback for the ~60 direct `params.produces` readers (mostly per-deck provider
heuristics; the fetch decks' FetchCandidates sites already skip fetchlands or read the targets).

r4 held-out A/B (base = 42d819bc0, derived = c7a354a63; play seed 3000000 at each deck's play setting, d0 seed 7000000
100000 games; net = summed turn delta on both-won games, + = slower):

| deck | play n | chg | fast/slow | lost/gain | play net | d0 net (100k) | d0 fast/slow | d0 lost/gain |
|---|---|---|---|---|---|---|---|---|
| Anti-Lifegain | 8000 | 9 | 4/4 | 1/0 | 0 | -2106 | 8719/6128 | 570/578 |
| Anti-Lifegain 2HG | 4000 | 3 | 0/2 | 1/0 | +2 | -2363 | 9226/6226 | 686/708 |
| Boars (b40) | 6000 | 7 | 4/3 | 0/0 | -1 | -870 | 6842/5802 | 68/86 |
| Creature Giving (b30) | 6000 | 18 | 10/8 | 0/0 | -2 | -611 | 5840/5035 | 279/329 |
| Five Colour (b40) | 3000 | 4 | 0/4 | 0/0 | +4 | -3649 | 7616/4224 | 182/186 |
| Mill | 1000 | 0 | - | - | 0 | +3 | 4/7 | 2/3 |
| Minotaur | 4000 | 0 | - | - | 0 | -31 | 101/73 | 2/0 |
| Modern RW Burn | 10000 | 21 | 5/16 | 0/0 | +11 | +33 | 3697/3667 | 43/43 |
| Morph | 10000 | 9 | 3/6 | 0/0 | +4 | -939 | 4212/3088 | 156/152 |
| Sea Creatures | 4000 | 6 | 2/4 | 0/0 | +2 | -1397 | 4941/3300 | 798/797 |
| Spirits | 4000 | 2 | 0/2 | 0/0 | +2 | -611 | 5117/4466 | 8/6 |
| Stuffy Doll | 2000 | 9 | 4/4 | 1/0 | -1 | -959 | 3868/2625 | 992/1051 |
| Unpredictable Cyclone | 2000 | 0 | - | - | 0 | +4 | 0/4 | 9/6 |
| Worldfire | 2000 | 73 | 34/36 | 2/1 | +6 | +23 | 291/281 | 1231/1229 |

**Slower searched games (94: 93 slower + lost on the play keys).** Every one was replayed on both arms at
`--depth 8 --budget-ms 0` (pre-rebase binaries; derived ran in heath-produces, same cards.json).
* The 56 outside Worldfire: 55 recover (derived d8 b0 <= the base A/B turn). The exception is **Boars gi5351**. At
  d8 b0, base wins T5 and derived T6: derived plays Wooded Foothills T1 where base plays Stomping Ground T1, and the
  real reshuffle diverges. With `MTG_NC_SEARCH=1` (non-clairvoyant search), derived wins T5, and so does base. So
  derived's honest search finds base's turn. The miss belongs to the lockstep search valuing a T1 fetch through a
  library order it does not get. This is the round-2/3 mechanism. Classified as clairvoyance.
* Worldfire (38): 19 recover at d8 b0. For the other 19, d8 b0 is intractable on this box (games ran 6-13 h before the
  coordinator stopped the run). They are UNRESOLVED at d8 b0. As an escalation, they were replayed at the play depth
  with b2000 and a 60M-unit cap: gi46 at T5 (= base), and 13 more recover (156, 1007, 1023, 1625, 1732, 1874, 613, 645,
  751, 844, 868, 906, 921). gi205 was abandoned at the cap. 103, 1363, 1410 and 1684 do not recover at b2000 (see below).

**CPU.** The deck-derived scan (library walk with a string subtype match) was made byte-identical-cheaper with
load-time bytes (`CardDefinition::fh_*`, 500bcdfc8). Digests matched on all 32 jobs vs the unfixed binary, and the
change is about -2% d0 CPU. r4's d0 CPU ratios (Worldfire +21%, Stuffy +12%) were run-time drift between the two arms.
They ran three hours apart, and the r4 tapped arm replays identical games at 0.93x. Measured concurrently on one lane,
base vs fix over 3 rounds: Worldfire d0 0.984, Stuffy d0 0.985, Mill d0 0.997. Searched work (units, deterministic):
within +-0.8% on every deck except Five Colour play +2.6% (4 changed games of 3000; the hint reorders its land menu).

**The four Worldfire games that did not recover at b2000, with base replayed at the same b2000** (play depth, 60M-unit cap):

| gi | A/B base / derived | b2000 base | b2000 derived | status |
|---|---|---|---|---|
| 103 | 5 / 6 | 5 | 6 | UNRESOLVED (derived d8 b0 intractable; NC run killed, not finished) |
| 1363 | 5 / 7 | 5 | 6 | UNRESOLVED (same) |
| 1410 | 7 / 8 | 8 | 8 | equal at b2000 (base's T7 was budget luck) |
| 1684 | 5 / 6 | 5 | 6 | UNRESOLVED (same) |

gi205 was abandoned at the 60M cap at b2000 and is UNRESOLVED too. So 4 Worldfire games remain open: 103, 1363, 1684, 205.
None is shown to be clairvoyance yet. The `MTG_NC_SEARCH=1` derived run of 103/1363/1410/1684 at b2000 had been running
about 16 h when the coordinator killed it to free the lane.

### Rebased tiers (wip/heath-derived 500bcdfc8 on origin/phase-1-2-deck-analyzer 138f60c83)

Units 4126/4126. `regression.sh --both` (MTG_PGO=0): smoke 44 failed / 199 passed, regression 69 failed / 320 passed.
Every failed case is a fetchland deck (Anti-Lifegain, Boars, Creature Giving, Cyclone, Five Colour, Minotaur, Burn, Morph,
Sea, Spirits, Stuffy, Worldfire, plus their 2HG rows). Searched audit: smoke 5 slower / 4 faster, regression 10 slower /
6 faster. Many more games show same-score play changes (the fetchland now gets played on a different turn). Reference
protocol: 0 play-drift / board-diverged / enum-gap. All **15 searched slower tier games recover at d8 b0** on the rebased
binary, each at its old GT turn. NOT YET ACCEPTED.

## Round 5 (2026-11-24): what the play-setting slower games are, the keep-the-fetch lever, the fresh-seed re-measure

### USER bar (verbatim, 2026-11-24)

"I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and then look for
ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason it is worse on
those other decks are the specific seeds we can just move on. d0 is not very important at all as I said previously. The
primary thing that matters is play settings, though we should also check whether any worse case is a result of
clairvoyant play. We can ignore those." / "Play settings are the most important, so if the change was specific to Maze's
end we shouldn't adopt." / "(until we can show it is clairvoyance, variance or fix it)" / "If play settings are super
marginally better, but other settings are much worse it might be worth holding back as well, but usually play settings
being better is enough."

### Method

Every changed r4 play game of Burn (21), Morph (9) and Five Colour (4), and 20 of Worldfire's 73, replayed as single
games on the r4 binaries (base 42d819bc0 in heath-base, derived c7a354a63 in heath-produces; --threads 1), with game
logs, first at the r4 setting (every game reproduces its r4 turn on both arms) and then with `MTG_NC_SEARCH=1`
(non-clairvoyant search) on both arms. Scratch: `build/scratch/heath/r6/tr/<deck>[nc]_gi<gi>/{base,derived}`;
`r6/tab.py <tag> <nctag>` prints the table.

### The one mechanism: the dual-vs-fetch tie flips, then the reshuffle deals a different game

Every changed game diverges at its turn-1 or turn-2 LAND DROP, a fetchland against a non-fetch land, and from the
fetch's reshuffle on the two games draw different cards. The reader of the hint that flips it is the GREEDY LAND RANKER
(LandPlay.cpp GreedyLandRank), which the search consults as its last-resort tie-break between land lines of equal value
(TurnSolver `greedy_land_name`) and which its playout mirrors (SimulateLandPlayOne). In base a fetch in hand had no
colour, so it sat in the mono pass behind every dual. With the deck-derived hint, Arid Mesa / Wooded Foothills reach W+R
in Burn (Sacred Foundry, Mountain) and R+G in Morph (Stomping Ground), so they TIE Sunbaked Canyon / Sacred Foundry /
Stomping Ground in the multi pass and hand order decides. In the Burn table below, 16 of the 21 changed games are "derived drops the fetch
(first in hand order) where base drops a non-fetch land", 14 of them a real dual, and they go both ways (11 slower, 5
faster). Four more are the reverse (base fetches, derived plays Inspiring Vantage / Mountain: a search choice moved by
the playout's new land order), and 2738 (a mulligan to 4) skips its T1 drop.

Honest value of that choice is ~0: the fetch and the dual make the same colours here, and the only honest differences are
one land thinned earlier (in the fetch's favour) and the reshuffle point. Under the LOCKSTEP search the reshuffle is not
random to the search (ShuffleByKey keyed on the shuffle ordinal: the search sees the very order the real game deals), so
a fetch held in hand is a clairvoyant option -- "crack it on the turn whose reshuffle deals the better cards". Base holds
the fetch more often, so base keeps that option more often; that is the 16:5 skew on Burn.

Burn (r4 play, b20; AB = r4 turns, base,derived; NC = both arms with MTG_NC_SEARCH=1):

| gi | AB | NC | base T1-T2 drops | derived T1-T2 drops | class |
|---|---|---|---|---|---|
| 629 | 3 / 4 | 4 / 4 | T1 Sunbaked Canyon; T2 Arid Mesa | T1 Arid Mesa; T2 Sunbaked Canyon | C: NC base misses its own T3 |
| 1641 | 4 / 5 | 6 / 6 | T1 Wooded Foothills; T2 Arid Mesa | T1 Inspiring Vantage; T2 Wooded Foothills | C: NC base misses its own T4 |
| 2185 | 4 / 5 | 5 / 5 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Wooded Foothills; T2 Sunbaked Canyon | C: NC base misses its own T4 |
| 2234 | 4 / 5 | 5 / 5 | T1 Sunbaked Canyon; T2 Arid Mesa | T1 Arid Mesa; T2 Sunbaked Canyon | C: NC base misses its own T4 |
| 2738 | 5 / 6 | 7 / 7 | T1 Sunbaked Canyon | T2 Sunbaked Canyon | C: NC base misses its own T5 |
| 4027 | 3 / 4 | 3 / 3 | T1 Sunbaked Canyon; T2 Wooded Foothills | T1 Wooded Foothills; T2 Sunbaked Canyon | recovers under NC |
| 4074 | 4 / 5 | 6 / 6 | T1 Arid Mesa; T2 Inspiring Vantage | T1 Inspiring Vantage; T2 Arid Mesa | C: NC base misses its own T4 |
| 4151 | 3 / 4 | 4 / 4 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Arid Mesa; T2 Sunbaked Canyon | C: NC base misses its own T3 |
| 5250 | 5 / 6 | 6 / 6 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Arid Mesa; T2 Sunbaked Canyon | C: NC base misses its own T5 |
| 5555 | 5 / 6 | 8 / 8 | T1 Arid Mesa; T2 Mountain | T1 Mountain; T2 Arid Mesa | C: NC base misses its own T5 |
| 6410 | 4 / 5 | 4 / 5 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Arid Mesa; T2 Sunbaked Canyon | V: same flip under NC, reshuffle luck |
| 6888 | 4 / 5 | 6 / 6 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Arid Mesa; T2 Wooded Foothills | C: NC base misses its own T4 |
| 8003 | 6 / 7 | 7 / 8 | T1 Sunbaked Canyon; T2 Sacred Foundry | T1 Sunbaked Canyon; T2 Arid Mesa | C: NC base misses its own T6 (NC derived 8) |
| 8192 | 3 / 4 | 3 / 4 | T1 Sunbaked Canyon; T2 Mountain | T1 Arid Mesa; T2 Sunbaked Canyon | V: same flip under NC, reshuffle luck |
| 8645 | 5 / 6 | 7 / lost | T1 Mountain; T2 Mountain | T1 Arid Mesa; T2 Sacred Foundry | C: NC base misses its own T5 (NC derived lost) |
| 8822 | 3 / 4 | 4 / 4 | T1 Arid Mesa; T2 Inspiring Vantage | T1 Inspiring Vantage; T2 Arid Mesa | C: NC base misses its own T3 |
| 2703 | 4 / 3 | 3 / 4 | T1 Mountain; T2 Arid Mesa | T1 Arid Mesa; T2 Mountain | (faster) |
| 2928 | 4 / 3 | 4 / 4 | T1 Sacred Foundry; T2 Wooded Foothills | T1 Wooded Foothills; T2 Sacred Foundry | (faster) |
| 5144 | 4 / 3 | 4 / 4 | T1 Sacred Foundry; T2 Sunbaked Canyon | T1 Wooded Foothills; T2 Sacred Foundry | (faster) |
| 5283 | 4 / 3 | 4 / 4 | T1 Sunbaked Canyon; T2 Arid Mesa | T1 Arid Mesa; T2 Sunbaked Canyon | (faster) |
| 9967 | 4 / 3 | 3 / 3 | T1 Sacred Foundry; T2 Mountain | T1 Wooded Foothills; T2 Arid Mesa | (faster) |

Morph: the same flip, Wooded Foothills against Stomping Ground / Fire-Lit Thicket (9 of 9 changed games). Slower 6:
1736 (NC 6/6 vs base 5), 2004 (6/6 vs 4), 6404 (5/5 vs 4), 7542 (both lose under NC vs 7) are C; 5381 recovers under NC
(derived 4 = base 4); 2276 is the flip under NC too (NC base T1 Stomping Ground + Birds, T2 Stomping Ground: 5; NC derived
T2 Foothills: 8) -- V. Faster 3 (707, 1907, 3911) all become equal under NC.

Five Colour: 4 slower, all C. Each first divergence is a fetch against a shock or another fetch (146 Misty / Foothills;
1096 Stomping Ground / Heath; 1621 Scalding Tarn / Breeding Pool; 1769 Breeding Pool / Foothills). NC: 146 both lose,
1096 5/5 (base 4), 1621 7/6 (base 5; derived FASTER under NC), 1769 5/5 (base 4).

Worldfire: a different shape. 73 of 2000 games change, 34 faster / 36 slower (net +6, z ~0.7), and of the 20 traced
(10 slower, 10 faster) the first divergence is scattered: a fetch against a non-fetch in 7 (141, 227, 361, 403, 448,
606, 635), a Saprazzan Skerry / Mercadian Bazaar / Sandstone Needle choice in 6, a cantrip choice (Ponder / Brainstorm /
Preordain / Halberdier) in 3, and a different draw at the same position in 4 (an earlier hidden reshuffle or put-back).
That is the hint moving the budget-bound search's valuations a little everywhere, both ways -- variance, not a
mechanism. Round 4 already showed 33 of its 38 slower games recover at d8 b0 or b2000 and 1410 equal at b2000; 103, 1363,
1684 and 205 stay UNRESOLVED (no capped NC run was needed for this classification and none was run).

### Classification of the play-row slower games

C = clairvoyance: under MTG_NC_SEARCH=1 base itself does not reach the turn it won on in r4, so its faster game came
from the lockstep search seeing the reshuffle. V = the dual/fetch flip repeats under NC and the reshuffle deals the two
games different cards (no honest difference in the choice itself; the lever below removes the flip). R = derived's NC
search reaches base's turn.

| deck | slower | C | R | V | note |
|---|---|---|---|---|---|
| Modern RW Burn | 16 | 13 | 1 (4027) | 2 (6410, 8192) | 8003 / 8645 are C but NC derived is also 1 slower / lost: same flip |
| Morph | 6 | 4 | 1 (5381) | 1 (2276) | |
| Five Colour | 4 | 4 | 0 | 0 | 1621 is faster under NC |
| Worldfire | 36 + 2 lost | -- | 33 at d8 b0 / b2000 | rest variance (34 faster) | 103, 1363, 1684, 205 UNRESOLVED |

Under NC the 21 changed Burn games read 5 derived-slower / 0 faster (6410, 8192, 8003, 8645 and the r4-faster 2703); the
set was selected under the clairvoyant search, so that is no estimate. The re-measure's decoupled rows are the unbiased
honest-reshuffle check.

### The mitigation: MTG_FETCH_AFTER_DUAL (keep the fetch behind a real dual)

`MTG_FETCH_AFTER_DUAL` (heurarm slot FETCH_AFTER_DUAL, shared reader `FetchAfterDualEnabled` in EngineFlags.h; **default
OFF** until the re-measure). In the greedy land ranker's MULTI-colour passes (LandPlay.cpp GreedyLandRank) and its
rollout mirror (TurnSolver.cpp SimulateLandPlayOne, pass 0), every non-fetch candidate is tried before a fetchland. A
fetch still comes before every mono-colour land, which is what the restored hint was for (the Heath held behind a basic).
ORDER only, at the permitted greedy sites (the depth-0 runner, the playout's drop, the search's last-resort land
tie-break); the search's own land menu is unchanged. Why it is sound: the fetch and the dual it ties make the same
colours here, a fetch kept in hand stays flexible (it can still become any colour it reaches) and keeps its reshuffle
for a later Brainstorm / Ponder, so "play the less flexible land first" is the ordinary sequencing rule; and it is
exactly base's order between the two, so the fix's change narrows to fetch-vs-mono and the other in-hand readers.
Unit test: `test_boars_cards.cpp` "MTG_FETCH_AFTER_DUAL: ..."; units 4127/4127 at a0773a63d.

Single games at the r4 setting (rebased binary a0773a63d, `MTG_FETCH_AFTER_DUAL=0` / `=1`; lever off reproduces the r4
derived turn in every game and is move-for-move identical to the pre-lever rebased binary on the 5 games checked):

| deck | r4 slower -> base's turn with the lever | r4 faster -> back to base's turn |
|---|---|---|
| Modern RW Burn | 15 of 16 (5555 stays 6: base fetches T1, derived plays Mountain) | 4 of 5 (2703 stays 3) |
| Morph | 5 of 6 (6404 stays 5: Foothills against a basic Forest) | 3 of 3 |
| Five Colour | 4 of 4 | (none) |

The rebased base (`mtg.base2`, 249db2aa7) reproduces r4 base's turn on all 21 Burn games, and the rebased derived (lever
off) r4 derived's, so the r4 picture holds on the current tree.

So on these decks the lever puts back base's play wherever the hint only re-broke a fetch/dual tie (and with it most of
the r4 faster games, which were the same coin flip). What it keeps of the fix is the fetch over a mono-colour land and
every other in-hand reader. Whether that keeps the fix's gains on Creature Giving / Boars / Stuffy / d0 is what the
re-measure's `fad` arm answers. Scratch: `build/scratch/heath/r6/tr/l{burn,morph,fc}_gi*/{off,on}`, `r6/tab2.py`.

### Fresh-seed re-measure (NOT RUN by the agent; the coordinator runs it under the box lock)

Generator `build/scratch/heath/r8/gen_manifest.py` (copy in `logs/heath/r8/`), runner `r8/run.sh`, report `r8/report.py`.
Seed base 8400000 (fresh: this work used 960000, 2000000, 3000000, 5000000-6047500, 7000000-7100000). Three arms paired
on (seed, game_index): `base` = 249db2aa7 (the branch's parent; binary `mtg.base2`, cwd `.claude/worktrees/heath-base2`),
`derived` = a0773a63d with the lever off (= r4's derived), `fad` = lever on. derived and fad are ONE pooled batch
(per-job `flags`); base needs its own binary and cards.json, so it is a second pooled batch (run.sh runs them back to
back under the box lock). Rows at each deck's play setting: Burn 10000, Morph 10000, Worldfire 3000, Five Colour 3000
(b40), Anti-Lifegain 2HG 4000, Sea 4000, Spirits 4000, Creature Giving 6000 (b30), Boars 6000 (b40), Stuffy 2000, plus
DECOUPLED rows (shuffle_salt_search 1: the honest-reshuffle check) for Burn and Morph, 4000 each, on their own seed block.
600 base jobs / 60000 games; 1200 fix jobs / 120000 games. CPU from r4's per-game cost: ~48k core-s per arm, ~145k
core-s total (Worldfire is ~half), so roughly 1.5-2 h of the 32-core box.

## Round 6 (2026-11-24): why the fix costs Worldfire, and the playout mitigation (wip/heath-wf)

### What was measured before this round

r8 (fresh seeds 8400000, coupled search) Worldfire play row base > derived: net +14 (38 fast / 49 slow, 1 lost) of 3000.
r9 (seeds 8600000, `shuffle_salt_search 1`: the search cannot see the real post-shuffle library) net +48 on both-won
games (75 fast / 104 slow, z ~2.5 on the changed pairs). Burn / Morph decoupled rows were ~0, so Worldfire's cost is
not the clairvoyant-reshuffle mechanism of Round 5; it is a play mechanism.

### What changed for Worldfire

Worldfire runs 4 Scalding Tarn and 2 Flooded Strand; its fetchable lands are 3 Steam Vents (U/R), 1 Island, 1 Mountain.
Static `produces`: Tarn W/U/B/R/G (the legacy proxy), Strand [] (empty). Deck-derived (HandFixingProduces): both U/R
while a Steam Vents is in the library. So the fix makes a Strand in hand MULTI-colour, where base had it colourless;
Tarn stays multi. The Worldfire provider's own land logic (WfLandOf) reads static params, so it is unaffected.

### Method

The 20 largest r9 slower games and the 6 largest faster ones that did not tie, replayed as single games (--threads 1,
MTG_SHUFFLE_SALT_SEARCH=1, seed 8600000+gi, b20 at the profile's d5): every one reproduces its r9 turn on both arms. A
diagnostic build (not committed) then put back the static `produces` at ONE reader of the hint at a time on the fix
binary: bit 1 = the greedy ranker (LandPlay.cpp GreedyLandRank: the d0 runner, the search's last-resort land
tie-break), bit 2 = ActivationColorLandIndex, bit 4 = the PLAYOUT's legacy land drop (TurnSolver.cpp
SimulateLandPlayOne, the default when MTG_ROLLOUT_LAND_RANKER is off), bit 8 = EffectiveProduces(in_hand) (the other
in-hand readers). All four bits together reproduce base move for move (checked on 973 and 1407). Scratch:
`build/scratch/heath/wf/` (arms.sh, tr/{s,f,at}_gi*, attab.txt).

### Result: the reader is the playout's land drop

| gi | base | fix | greedy static | actcolour static | PLAYOUT static | in-hand static |
|---|---|---|---|---|---|---|
| 114 | 7 | 4 | 4 | 4 | 4 | 4 |
| 350 | 7 | 4 | 4 | 4 | 7 | 4 |
| 624 | 5 | 8 | 8 | 8 | 5 | 8 |
| 794 | 7 | 5 | 7 | 5 | 5 | 5 |
| 973 | 4 | 8 | 4 | 8 | 8 | 8 |
| 1097 | 6 | L | L | L | 6 | L |
| 1348 | 7 | 4 | 4 | 4 | 7 | 4 |
| 1368 | 4 | 6 | 6 | 6 | 4 | 6 |
| 1385 | 8 | L | L | L | 8 | L |
| 1387 | 4 | 8 | 4 | 8 | 4 | 8 |
| 1407 | 4 | 7 | 7 | 7 | 4 | 7 |
| 1562 | 8 | L | L | L | 8 | L |
| 1594 | 5 | 7 | 7 | 7 | 5 | 7 |
| 1771 | 4 | 7 | 7 | 7 | 4 | 7 |
| 1905 | 5 | 7 | 6 | 7 | 5 | 7 |
| 2015 | 5 | 7 | 7 | 7 | 5 | 7 |
| 2256 | 4 | 6 | 4 | 6 | 4 | 6 |
| 2286 | 4 | 7 | 7 | 7 | 4 | 7 |
| 2331 | 5 | 7 | 7 | 7 | 6 | 7 |
| 2448 | 5 | 7 | 7 | 7 | 5 | 7 |
| 2567 | 8 | 4 | 8 | 4 | 4 | 4 |
| 2607 | 8 | 4 | 8 | 4 | 4 | 4 |
| 2761 | 6 | L | L | L | 6 | L |
| 2841 | 4 | 7 | 7 | 7 | 4 | 7 |
| 2889 | 6 | 8 | 6 | 8 | 8 | 8 |
| 2961 | 5 | 7 | 7 | 7 | 5 | 7 |

(L = not won by the turn limit. Rows 114-2607 with base slower are the faster games.)

Of the 20 slower games, putting back the static `produces` at the PLAYOUT's land drop alone returns 17 to base's turn
(624, 1097, 1368, 1385, 1387, 1407, 1562, 1594, 1771, 1905, 2015, 2256, 2286, 2448, 2761, 2841, 2961) and one more
gains a turn (2331: 7 -> 6, base 5). The other two (973, 2889) are the greedy ranker's (bit 1). Of the 6 faster games,
2 go back to base's turn (350, 1348) and 4 stay faster. The other two readers change nothing on these games.

The first divergence is scattered (the T1 land drop in 14 games, a cantrip choice in 4, T2-T6 in the rest; div_s.txt),
because the reader is not a choice the game makes: it is the search's LEAF. SimulateLandPlayOne's legacy two-pass
drop (pass 0 = the first multi-colour land in hand order, pass 1 = the first land in hand order) knows nothing of a
land's entry or its size. With the derived hint a Flooded Strand joins pass 0, so in every playout it now jumps ahead
of every mono land, and in Worldfire the mono lands are the deck's engine: Sandstone Needle (RR), Saprazzan Skerry
(UU), Mercadian Bazaar (storage), the lands its nine-mana Worldfire turn is built on. Playout drop census on gi1407
(MTG_LANDDROP_STATS): the fix's playouts play Flooded Strand on 19% of their drops (base reader: 10%), Sandstone
Needle 11.5% (19.4%), Saprazzan Skerry 21% (27.7%). The playouts are the values the budget-bound search compares its
early lines with, so the search misprices them, in both directions (the 2 faster games that revert are the same
mechanism), with a net cost. MTG_TRACE=search shows the shape: gi1407's fix search projects a T4 win at T1 and T2 on
the Skerry line and delivers T7; with the playout reverted it projects T5 for Tarn + Ponder and wins T4.

d0 is unaffected (r4 Worldfire d0 +23 over 100k games: the d0 runner uses the greedy ranker, not this loop), which is
consistent with a search-only (leaf) mechanism.

### The mitigation: MTG_ROLLOUT_FETCH_STATIC_HINT (wip/heath-wf)

The playout's legacy land drop reads a fetchland's STATIC `produces` again (PlayoutLandColours, LandPlay.cpp; heurarm
slot ROLLOUT_FETCH_STATIC_HINT; reader RolloutFetchStaticHintEnabled in EngineFlags.h). That is exactly what the loop
read before 2026-11-22, so it is the diagnostic's bit 4 and nothing else. The greedy ranker (the d0 runner's drop, the
search's last-resort land tie-break, the playout under MTG_ROLLOUT_LAND_RANKER), ActivationColorLandIndex and every
other in-hand reader keep the deck-derived reach. ORDER of a permitted greedy site (beyond the horizon and the depth-0
static plans), so it touches neither the search window nor completeness. **Default OFF** until the A/B below (the
FETCH_AFTER_DUAL precedent: a heuristic order default is the USER's call once the data is in). Unit test:
test_boars_cards.cpp "MTG_ROLLOUT_FETCH_STATIC_HINT: ...".

Why not FETCH_AFTER_DUAL: it orders a fetch behind a DUAL, but Worldfire's cost is a fetch ahead of MONO lands (Strand
vs Needle / Skerry / Bazaar), and r8 measured it +5 worse than derived on Worldfire (and +8 on Five Colour).

**Single games, lever on** (the diagnostic's bit 4 is the lever's code path exactly; derived = lever off):
* Worldfire r9 (decoupled): the table above, PLAYOUT column -- 17 of 20 slower back to base, 1 partial, 2 stay (the
  greedy ranker's); 4 of 6 faster stay faster.
* Modern RW Burn r8 (coupled, b20), all 18 changed games: 3 of 10 slower back to base (5689, 6170, 6742), 1 of 8
  faster reverts (4150); net -2 vs derived on these games.
* Creature Giving r8 (b30), its 6 faster games (the fix's gain there): all 6 stay faster. Five Colour r8 (b40), all 5
  changed games: unchanged. (Both decks' fetches keep a multi-colour static proxy or the Heath's restored W/B/R/G, so
  the lever barely moves their playouts.)
Scratch: `build/scratch/heath/wf/tr/{cg,fc,bu}_gi*/m{0,4}`.

### A/B (NOT RUN by the agent; the coordinator runs it under the box lock)

Generator `logs/heath/wf/gen_manifest.py` (copy + run.sh + report.py in `build/scratch/heath/rwf/`, manifests already
written there). Seeds 9930000 (decoupled rows +5000), fresh. Three arms paired on (seed, game_index): base (mtg.base2 in
heath-base2), derived (mtg.wf, lever off), wf (mtg.wf, lever on); derived + wf pooled in one batch (per-job flags).
Rows: Worldfire play 3000 + Worldfire play decoupled 3000, and Burn, Morph, Five Colour (b40), Anti-Lifegain 2HG, Sea,
Spirits, Creature Giving (b30), Boars (b40), Stuffy at 2000 each. 240 base jobs / 24000 games + 480 fix jobs / 48000
games; ~31k core-s per arm from r8's per-game cost, ~95k core-s total. `bash build/scratch/heath/rwf/run.sh`; markers
rwf/DONE_base, DONE_fix, DONE; report rwf/report.txt.

**Checks done at c13167c19** (mtg.wf = `build/scratch/heath/mtg.wf`): units 4128/4128. Lever off reproduces the
derived binary (mtg.fad, lever off) move for move on 5 Worldfire games (1407, 624, 350, 2015, 794); lever on
reproduces the diagnostic's bit 4 move for move on the same 5. Dry run of both manifests (one game per row of
Worldfire, Worldfire decoupled, Anti-Lifegain 2HG, Spirits) parses on both binaries; the flag reaches the job
(Worldfire decoupled: wf's digest equals base's, derived's differs).

### Decision this A/B feeds (USER-owned, not taken by the agent)

Lever default. Recommendation to bring once the report is in: default ON if wf <= derived on Worldfire's play rows and
does not give back the fix's gains elsewhere (Creature Giving, Five Colour, Anti-Lifegain), then re-run smoke +
regression on the new default and accept GT as Round 5 step 4 describes.
