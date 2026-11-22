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

