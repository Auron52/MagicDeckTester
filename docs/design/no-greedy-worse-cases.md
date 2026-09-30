# No-greedy purge: every game that got WORSE -- revisit list

USER 2026-09-30: *"we should record any cases that are worse in particular and revisit them, even if
it is budget churn."* Quality first, then performance. This is that list, standalone.

**Measurement.** Regression tier, commit `cd2908d9` (purge through step 19) vs ground truth (= the
committed tree `3753dcf3`), searched depths. Net: 100 faster / 66 slower, **-33 turns**. Classified
with `bash test/classify_turn_later.sh regression` (re-run at 4x and 16x budget); persisting games
diffed with `test/explain_game.py` against the committed-tree binary. Full context:
`docs/design/no-greedy-in-search-window.md` (step log, cost/churn ledger, checkpoint 3).

**RECOVERABILITY -- PASSED (definitive test).** USER 2026-09-30: *"d8 b0 (unbounded) is the definitive
test. There is no purpose to doing the bounded tests"* -- a bounded re-run either succeeds when d8 b0
would or fails and proves nothing -- and *"Diverging draws is no excuse. It should still recover."* All
66 games re-run with the purge binary at `--depth 8 --budget-ms 0` (one pooled batch,
`logs/purge/d8b0_worse.json`): **66 / 66 reach at least their committed-tree win turn**, including the
three draw-diverged Hinata games and th gi276. The 4x/16x verdicts in the table below are kept only as
the record of which games churn at PLAY settings (the mitigation phase's input).

**Standing bar.** Nothing below blocks: every game either recovers with more budget or depth, or is a
different physical game (draws diverge). But churn is a cost to MINIMIZE, not excuse -- the mitigation
phase works this list (exact-duplicate prunes first, then the budget share of the new options).

**How to reproduce one:** `python3 test/explain_game.py regression <case> <gi> --old-bin <committed-tree
binary> --new-bin build/Release/mtg [--budget-ms B]`.

## Per deck (searched depths, net turns vs ground truth)

| deck | slower games | net turns / games |
|---|---|---|
| hinata | 23 | net <= 0 (deck better overall) |
| hinata2hg | 8 | net <= 0 (deck better overall) |
| th | 8 | +5 / 1600 |
| fivecolour | 6 | net <= 0 (deck better overall) |
| antilife | 4 | +4 / 1100 |
| fungus | 4 | +4 / 600 |
| melira | 4 | +3 / 230 |
| goblins | 2 | +2 / 1100 |
| slivers | 2 | +2 / 1400 |
| antilife2hg | 1 | net <= 0 (deck better overall) |
| auras | 1 | +1 / 2000 |
| creature_giving | 1 | net <= 0 (deck better overall) |
| fivecolour2hg | 1 | net <= 0 (deck better overall) |
| kitty | 1 | net <= 0 (deck better overall) |

## Every slower game

| case | gi | old turn | new turn | verdict |
|---|---|---|---|---|
| antilife_regression_d3_s2002 | 1 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| antilife_regression_d3_s2002 | 137 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| antilife_regression_d3_s3003 | 76 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| antilife_regression_d5_s3003 | 39 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| antilife2hg_regression_d3_s2002 | 54 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| auras_regression_d5_s3003 | 301 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| creature_giving_regression_d3_s2002 | 114 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| fivecolour_regression_d3_s2002 | 101 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| fivecolour_regression_d3_s3003 | 100 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| fivecolour_regression_d3_s3003 | 174 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| fivecolour_regression_d3_s3003 | 181 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| fivecolour_regression_d3_s3003 | 36 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| fivecolour_regression_d3_s3003 | 57 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| fivecolour2hg_regression_d3_s2002 | 43 | 5 | 6 | churn (recovers to 5: 4x=6 16x=5) |
| fungus_regression_d3_s2002 | 78 | 7 | 8 | churn (recovers to 7: 4x=7 16x=7) |
| fungus_regression_d5_s2002 | 30 | 7 | 8 | churn (recovers to 7: 4x=7 16x=7) |
| fungus_regression_d5_s3003 | 74 | 6 | 7 | churn (recovers to 6: 4x=7 16x=6) |
| fungus_regression_d5_s3003 | 99 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| goblins_regression_d3_s2002 | 153 | 3 | 4 | churn (recovers to 3: 4x=3 16x=3) |
| goblins_regression_d3_s3003 | 166 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| hinata_regression_d3_s2002 | 129 | 6 | 7 | churn (recovers to 6: 4x=7 16x=6) |
| hinata_regression_d3_s2002 | 143 | 6 | 7 | variance: draws diverge T3 (persists 4x/16x) |
| hinata_regression_d3_s2002 | 159 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d3_s2002 | 48 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| hinata_regression_d3_s2002 | 57 | 7 | 8 | variance: draws diverge T6 (persists 4x/16x) |
| hinata_regression_d3_s2002 | 77 | 7 | 8 | variance: draws diverge T3 (persists 4x/16x) |
| hinata_regression_d3_s3003 | 112 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| hinata_regression_d3_s3003 | 169 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d3_s3003 | 26 | 8 | loss | churn (recovers to 8: 4x=8 16x=8) |
| hinata_regression_d3_s3003 | 43 | 5 | 6 | churn (recovers to 5: 4x=6 16x=5) |
| hinata_regression_d3_s3003 | 70 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| hinata_regression_d3_s3003 | 82 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| hinata_regression_d5_s2002 | 11 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d5_s2002 | 21 | 6 | 7 | churn (recovers to 6: 4x=7 16x=6) |
| hinata_regression_d5_s2002 | 35 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d5_s2002 | 36 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| hinata_regression_d5_s2002 | 37 | 5 | 6 | churn (recovers to 5: 4x=6 16x=5) |
| hinata_regression_d5_s2002 | 73 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d5_s3003 | 11 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata_regression_d5_s3003 | 27 | 7 | loss | churn (recovers to 7: 4x=7 16x=7) |
| hinata_regression_d5_s3003 | 4 | 5 | 6 | churn (recovers to 5: 4x=6 16x=5) |
| hinata_regression_d5_s3003 | 52 | 7 | 8 | churn (recovers to 7: 4x=8 16x=7) |
| hinata_regression_d5_s3003 | 56 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata2hg_regression_d3_s2002 | 2 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| hinata2hg_regression_d3_s2002 | 48 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| hinata2hg_regression_d5_s2002 | 11 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata2hg_regression_d5_s2002 | 21 | 6 | 7 | churn -- and BETTER than baseline 6 at higher budget (4x=5 16x=5) |
| hinata2hg_regression_d5_s2002 | 35 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| hinata2hg_regression_d5_s2002 | 36 | 4 | 6 | churn (recovers to 4: 4x=4 16x=4) |
| hinata2hg_regression_d5_s2002 | 37 | 5 | 6 | churn (recovers to 5: 4x=6 16x=5) |
| hinata2hg_regression_d5_s2002 | 6 | 7 | 8 | churn (recovers to 7: 4x=7 16x=7) |
| kitty_regression_d5_s3003 | 210 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| melira_regression_d3_s2002 | 1 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| melira_regression_d3_s2002 | 14 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| melira_regression_d3_s3003 | 0 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| melira_regression_d3_s3003 | 44 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| slivers_regression_d3_s2002 | 226 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| slivers_regression_d3_s2002 | 354 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| th_regression_d3_s2002 | 147 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| th_regression_d3_s2002 | 198 | 6 | 7 | churn (recovers to 6: 4x=6 16x=6) |
| th_regression_d3_s2002 | 276 | 6 | 7 | recovers with DEPTH (T6 at d5 and d7 on both binaries); T7 at d3 for every budget to 64x |
| th_regression_d3_s3003 | 16 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| th_regression_d3_s3003 | 163 | 4 | 5 | churn (recovers to 4: 4x=4 16x=4) |
| th_regression_d3_s3003 | 457 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |
| th_regression_d5_s2002 | 167 | 7 | loss | churn (recovers to 7: 4x=8 16x=7) |
| th_regression_d5_s3003 | 293 | 5 | 6 | churn (recovers to 5: 4x=5 16x=5) |

## Greedy-runner (d0) losses -- separate, lighter bar

slivers d0 s2002: 5 faster / 13 slower, +10 turns / 1000 (the runner lost the deleted post-cast
mana sinks; see "Deferred: slivers d0 regression" in the purge doc and step 20). No other deck's d0
cell moved.


## Step 23 (beam refund), full overnight tier vs step 22 -- 2026-09-30

34 faster / 2 slower (+1 hinata). Each slower game, step 23 binary:

| game | step 22 -> 23 | 4x budget | d8 b0 | verdict |
|---|---|---|---|---|
| stompy d3 s6006 gi60 | 5 -> 6 | 5 | 5 | churn; recovers |
| hinata d3 s7007 gi220 | 5 -> 6 | 6 | 5 | recovers at d8 b0 |
| th d3 s5005 gi211 | 5 -> never | 5 | **never** | recovers at 4x; the d8 b0 failure is PRE-EXISTING (committed tree 3753dcf3 and step 22 also never win it at d8 b0, while winning T5 at d3 b80) |

**OPEN (pre-existing, not the purge): th d3 s5005 gi211 is non-monotone in depth on the committed
tree** -- T5 at d3 b80, no win at `--depth 8 --budget-ms 0`. A deeper unbounded search should never
lose a win a shallower one finds; root-cause it before relying on d8 b0 as the recoverability test
for treasure_hunt.
