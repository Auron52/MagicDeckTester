# Heath fetchland-hint landing branch: wip/land-heath (2026-11-25)

Prepared for landing, NOT landed. The coordinator lands it through the push queue once the USER decides on adoption.

## Commits (on origin/phase-1-2-deck-analyzer 05e2e0030)

| commit | what |
|---|---|
| f8d6c3cb1 | the play change, squashed from wip/heath-wf (68b6d7bea..ffe5e3339, src/test/scripts only, no WIP or handoff commits): Heath + Wooded Foothills `produces` W/B/R/G restored (HAND-only), Arid Mesa / Flooded Strand stay `[]`; `hand_fixing_produces` key removed (CardDatabase, audit script, LandAbilityClasses.h); deck-derived `HandFixingProduces` / `FetchReachColourMask` + load-time `fh_*` bytes; `MTG_FETCH_HINT_TAPPED` report-only arm; `MTG_ORCHARD_BEFORE_FETCH` (default ON); **`MTG_ROLLOUT_FETCH_STATIC_HINT` flipped to DEFAULT ON** (`EnvOn(..., true)`, comment + HeuristicArm.h slot text updated); **`MTG_FETCH_AFTER_DUAL` deleted** (flag, slot, both code sites, unit test) |
| a80088979 | docs: `docs/design/heath-produces-2026-11-22.md` (+ landing note) and the r8 / heath-wf A/B generators under `logs/heath/` |
| 1323f0ac9 | GT: smoke + regression accept (deck-scoped to the 25 fetchland deck keys) |

HeuristicArm.h conflict: origin's slots kept as they are, then `FETCH_HINT_TAPPED` and `ROLLOUT_FETCH_STATIC_HINT` added at the end (enum and name table in step).

## Validation

* Build: `./build.sh` Release. Units: **4169/4170**. The one failure is `INT-9 free casts: a revealed Thrumming Stone is cast and the legend rule keeps the copy the cast names` (test_shadowborn_int9.cpp:184, `casts.size() == 2`). It **fails identically on origin 05e2e0030's own mtg-test** (clean uo-cpu worktree build), so it was already broken on origin and is not caused by this change. Not fixed here.
* Tiers: `MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --both` under the box lock. One pooled batch, 26m35s makespan. Smoke 200 pass / 47 fail, regression 321 pass / 74 fail.
* Scenarios 677/677. References: 364 ok, 585 repaired, 1 beaten, 0 play-drift, 0 shuffle-dead, 0 board-diverged, 0 enum-gap, 0 mull-drift. Require-beat 2/2.
* **Moved keys:** 121 (47 smoke + 74 regression). Every one belongs to a fetchland deck (antilife, beasts, boars, creature_giving, cyclone, fivecolour, minotaur, modernrwburn, morph, sea, spirits, stuffy, worldfire and their 2hg variants). No other deck moved. Mill (Scalding Tarn, Polluted Delta) did not move. Cyclone moved its digest only (net 0). All b0 backstop rows net 0.
* `test/check_gt_logs.py`: 1435 consistent, 0 stale, 0 missing. The GT diff is exactly the 121 failed keys. Existing smoke and regression notes are kept, and one new note per tier is added.

## Net turn-games vs the prior GT (loss-penalized; + = slower)

| deck key | play rows | d3 | d0 |
|---|---|---|---|
| antilife | +1 | +3 | -92 |
| antilife2hg | +0 | +0 | +0 |
| beasts | +1 | +0 | -129 |
| beasts2hg | +2 | +0 | +0 |
| boars | +0 | -1 | +5 |
| boars2hg | +0 | +0 | +0 |
| creature_giving | +2 | -1 | +8 |
| creature_giving2hg | +0 | +0 | +0 |
| cyclone | +0 | +0 | +0 |
| fivecolour | +0 | -1 | -69 |
| fivecolour2hg | +0 | +0 | +0 |
| minotaur | +0 | +0 | +2 |
| minotaur2hg | +0 | +0 | +0 |
| modernrwburn | -1 | +2 | +8 |
| modernrwburn2hg | +0 | +0 | +0 |
| morph | +0 | +0 | +1 |
| morph2hg | +2 | +0 | +0 |
| sea | +0 | +0 | -22 |
| sea2hg | +0 | +0 | +0 |
| spirits | +0 | +0 | +11 |
| spirits2hg | +0 | +1 | +0 |
| stuffy | +0 | +0 | +2 |
| stuffy2hg | +0 | +0 | +0 |
| worldfire | +1 | +0 | +36 |
| worldfire2hg | +0 | +0 | +0 |
| **total** | **+8** | **+3** | **-239** |

## Searched slower games: paired replays at --depth 8 --budget-ms 0

Both arms ran the same 1-game jobs through `scripts/box_lane.sh --slots 2`: origin = 05e2e0030's binary run from a clean origin worktree (`.claude/worktrees/land-heath-base`), new = this branch. The "case" column is a repro at the case's own settings. It shows that the pairing is valid: origin reproduces the GT turn and new reproduces the tier turn on every game.

```
case                                          gi   GT->tier | case-repro base/new | d8b0 base/new  digest
antilife_smoke_d5_s1001                       112  4->5   | 4/5 | 4/4 diff
beasts_smoke_d3_s1001                         19   4->5   | 4/5 | 4/4 same-digest
beasts_smoke_d5_s1001                         19   4->5   | 4/5 | 4/4 same-digest
creature_giving_smoke_d5_s1001                97   7->8   | 7/8 | 7/7 same-digest
modernrwburn_smoke_d3_s1001                   34   3->4   | 3/4 | 3/3 same-digest
spirits2hg_smoke_d3_s1001                     24   5->6   | 5/6 | 5/5 same-digest
antilife_regression_d3_s2002                  46   6->-1  | 6/-1 | 6/6 diff
antilife_regression_d5_s3003                  103  4->5   | 4/5 | 4/4 diff
beasts2hg_regression_d5_s3003                 17   5->6   | 5/6 | 5/5 diff
beasts2hg_regression_d5_s3003                 20   5->6   | 5/6 | 5/5 same-digest
creature_giving_regression_d5_s2002           184  5->6   | 5/6 | 5/5 same-digest
creature_giving_regression_d5_s3003           289  5->6   | 5/6 | 5/5 same-digest
creature_giving_regression_d5_s3003           312  4->5   | 4/5 | 4/4 same-digest
modernrwburn_regression_d3_s2002              6    3->4   | 3/4 | 3/3 diff
morph2hg_regression_d5_s2002                  24   4->5   | 4/5 | 4/4 same-digest
morph2hg_regression_d5_s2002                  57   4->5   | 4/5 | 4/4 same-digest
worldfire_regression_d5_s3003                 57   4->5   | 4/5 | 4/4 same-digest
```

**All 17 recover to the origin turn at d8 b0.** 12 of them play byte-identical lines (same digest). In the other 5 (antilife 112 / 46 / 103, beasts2hg 17, modernrwburn 6) the line differs but the win turn is the same. **No slower game stays unrecovered.** 12 are at play settings and 5 at d3 b10. Faster searched games: 8 (smoke 3, regression 5).

## Notes for the USER decision

* Play rows are net **+8** over these seeds, all of it in budget churn that recovers at d8 b0. The coordinator's fresh-seed A/B measured play-neutral.
* d0 is net **-239** (faster), mostly from Beasts -129, Anti-Lifegain -92 and Five Colour -69. Some d0 rows got slower: **Worldfire d0 +36**, Spirits +11, Creature Giving +8, Modern RW Burn +8, Boars +5. d0 has low priority under the depth rule, but Worldfire's +36 is worth knowing about. The lever is a playout-only reader, so Worldfire d0 still uses the deck-derived ranker, as the r4 Worldfire d0 +23 found.
* The overnight GT is left for the periodic overnight run.

Scratch: `build/scratch/land-heath/` (tier.log, nets.txt, slower.txt, d8/).
