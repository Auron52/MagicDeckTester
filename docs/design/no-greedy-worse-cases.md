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

## Small per-deck losers after step 23 -- recoverability (2026-09-30)

Every game step 23 plays worse than the committed tree on the overnight tier for mirrorwing (+3),
fluctuator (+2), fungus (+2), kitty (+2), pirates (+2), stompy (+1) and melira (+4): 32 games
(`logs/purge/small_slow`). **31/32 recover at d8 b0; 29/32 already at 4x budget** -- budget churn.
The one exception, **fluctuator d3 s4004 gi419** (base T6, step 23 T7, d8 b0 T7), is the same
PRE-EXISTING depth non-monotonicity as th gi211: the committed tree and step 22 are also T7 at d8 b0,
and step 23/24 recover T6 at 4x. Both d8-b0 anomalies are open against the committed tree.

**Update (step 25): fluctuator gi419 ROOT-CAUSED and FIXED** -- the go-off seed (unbounded-only) played an undecided land drop the executor did not reproduce, crediting a T6 kill play never made (`MTG_WINLESS_SEED=0` restored T6). Seeds now carry a decided land drop; gi419 is T6 at b0. **th gi211 is a different class**: it also never wins at d3 **b80** while winning T5 at its cell budget (b20), and the certificate is not involved (`MTG_WINLESS_CERT=0` still never wins) -- budget-non-monotone, the known th gi276 class, open.

## Step 26 (main-2 ordinary-plan dedup), overnight tier vs step 24 -- 2026-09-30

1 faster / 4 slower on hinata (antilife -5, antilife2hg -1, giants -1, goblins2hg -1 on the gain side).
All four slower hinata games recover -- churn:

| game | step 24 -> 26 | 4x budget | d8 b0 |
|---|---|---|---|
| hinata d3 s6006 gi390 | 6 -> 7 | 6 | 6 |
| hinata d5 s4004 gi158 | 7 -> 8 | 7 | 6 |
| hinata d5 s5005 gi224 | 7 -> 8 | 7 | 6 |
| hinata d5 s6006 gi144 | 5 -> 8 | 5 | 5 |

## Regression tier on step 27 (2026-09-30)

Scenarios 118/118; reference replay gate PASS (0 play-drift, 0 enum-gap; 35 ok vs 28 at step 19,
1 board-diverged vs 2, 0 shuffle-dead vs 1). Slower games not already in the 66-game set:

| game | GT -> step 27 | 4x budget | d8 b0 |
|---|---|---|---|
| auras d3 s2002 gi94 | 4 -> 5 | 4 | 4 |
| auras d3 s2002 gi428 | 5 -> 6 | 4 | 4 |
| auras d3 s3003 gi117 | 5 -> 6 | 6 | 5 |
| hinata d3 s2002 gi69 | 6 -> 7 | 6 | 6 |

All recover at d8 b0 (gi428 beats GT there). Budget churn.

## Checkpoint 4 (steps 20-29), all three tiers vs the previous GT -- 2026-10-01, ACCEPTED

Searched depths 722 faster / 269 slower, net -435 (smoke -42, regression -49, overnight -344); d0 +135,
all slivers d0 (greedy runner, deferred -- see the d0 section above). Every searched slower game was
re-run at `--depth 8 --budget-ms 0`: the non-hinata ones on the final binary (they are byte-identical
with or without step 30), the hinata ones on the purge-only binary. All recover except these nine,
each checked on the committed-tree binary and on the step-27 snapshot at the same settings:

| game | GT | d8 b0 now | committed tree | step 27 | verdict |
|---|---|---|---|---|---|
| dragonstorm ov d5 s4004 gi107 | 5 | 6 | 6 | 6 | pre-existing |
| hinata smoke d3 s1001 gi110 | 6 | 7 | 7 | 7 | pre-existing |
| hinata ov d3 s5005 gi308 | 4 | 5 | 5 | 5 | pre-existing |
| hinata ov d3 s4004 gi355 | 5 | 6 | 6 | 6 | pre-existing |
| hinata ov d5 s7007 gi75 | 5 | 6 | 6 | 6 | pre-existing |
| hinata ov d5 s7007 gi161 | 5 | 6 | 6 | 6 | pre-existing |
| hinata2hg ov d5 s7007 gi75 | 5 | 6 | 6 | 6 | pre-existing |
| hinata2hg ov d5 s6006 gi61 | 5 | 6 | 5 | 6 | lost at a step <= 27; OPEN |
| hinata ov d5 s5005 gi13 | 6 | 7 | 6 | 7 | Gamble lockstep: the executor's index pick discards a Reality Spasm the verified T5 line casts; step 30 (canonical pick) -> T5 |
| hinata reg d5 s2002 gi25 | 5 | 6 | 5 | 5 | **FIXED (step 31, `rollout-executor-lockstep.md` #10): an unpaid executor cast replayed the next breakpoint segment.** The T1 search (searched depth 5, verified) commits a T5 win; at T5 the executor casts Soulfire Eruption, takes the line's EMPTY continuation at its look, then pops a SECOND main-2 phase (Island + Reality Spasm x6 + Gamble, without the Expressive Iteration the line holds) and the kill does not happen -> T6. Not the Gamble discard: search and executor both shed Ornithopter on the same hand. Exposed, not caused, by step 29a (the full Ponder order changes the T1 line: Preordain where the committed tree casts Ponder). Under step 30's canonical pick the game realises T5 |

Per-deck searched cells still above GT -- all churn (their slower games recover at d8 b0): th reg +5,
antilife ov +4, antilife2hg ov +3 / reg +2, slivers reg +2, mirrorwing ov +1, stompy ov +1,
dragonstorm2hg smoke +1, slivers2hg smoke +1. Tracked for the mitigation phase.

**Step 30 split out (Gamble's canonical discard).** It re-deals which card a random discard takes, so a
Gamble game becomes a DIFFERENT game -- the d8-b0 recovery test does not apply to it. The 33 hinata games
that failed to recover on the first candidate (which carried step 30) all recovered on that binary with
only the pick reverted (32/33; the 33rd is gi61 above), and hinata_d0 matched GT exactly. So it is its own
commit with its own GT.

**Step 31 (2026-10-01): gi25 ROOT-CAUSED and FIXED.** The executor armed a committed-line breakpoint
replay after a cast it could not pay, which the rollout never does (`apply_one` returns first), so the
continuation recorded for the Gamble cast replayed at a failed Expressive Iteration. Now gated on a paid
cast, committed-line replay only (`rollout-executor-lockstep.md` #10). gi25 -> T5 under the index pick it
was found on. All three tiers turn-neutral (one regression cell's play digest moved at the same score).
The other eight open games are unchanged by it.
