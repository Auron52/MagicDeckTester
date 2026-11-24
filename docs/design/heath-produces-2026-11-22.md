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
