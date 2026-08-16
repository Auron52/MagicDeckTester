# Classify-stack adoption status (re-measured at HEAD, 2026-08-16)

**Verdict: NOT adoptable yet.** Overall ~neutral, with two decks clearly harmed.

## Why the old number was withdrawn

The `-0.1443` held-out figure (`2047871`) was measured BEFORE the Oko (`c63f969`)
and Bolas (`881b517`) fixes landed in base. Its biggest winner was FiveColour --
precisely the deck those two base fixes improved. Re-measuring at HEAD was
necessary, not bookkeeping.

## The measurement

Regression tier, six levers on (`MTG_PHASE_CLASSIFY`, `MTG_SEARCH_SECOND_MAIN`,
`MTG_MAIN2_DROP`, `MTG_ACQ_RESOLVE`, `MTG_BP_SITES=63`, `MTG_DOUBT_MAIN2`) vs the
freshly-accepted levers-off GT. 60 keys, 26,300 games, per-GAME weighted:

| deck | better | worse | net turns | mean | win->loss | loss->win |
|---|---|---|---|---|---|---|
| hinata | 43 | 58 | +21 | **+0.0131** | 8 | 5 |
| antilife | 7 | 15 | +15 | **+0.0071** | **3** | **0** |
| fivecolour | 48 | 26 | -20 | -0.0125 | 0 | 0 |
| burn | 20 | 0 | -20 | -0.0067 | 0 | 0 |
| creature_giving | 19 | 1 | -18 | -0.0086 | 0 | 0 |
| 7 other decks | 0 | 0 | 0 | 0.0000 | 0 | 0 |
| **TOTAL** | **137** | **100** | **-22** | **-0.0008** | 11 | 5 |

## Two traps this measurement walked into (both previously documented)

1. **Per-key vs per-game weighting flips the SIGN.** Averaging per key gives
   `+0.00128` (worse); weighting per game gives `-0.0008` (better), because a
   100-game d5 key was counted equally with a 500-game d0 key. An effect whose sign
   depends on the weighting is an effect of zero -- report it as such.
2. **The suite's detail list prints SLOWER games only.** Reading per-deck counts off
   it produced "0 better" for every deck, which is selection bias, not a finding
   (the aggregate said faster=126). Always recount from the per-game logs
   (`test/logs/<mode>/wins` vs `test/gt_logs`). This is the same loser-only-list
   error that once made `MTG_MAIN2_DROP` look one-sided.

## The blockers, in priority order

1. **antilife: 3 win->loss, 0 loss->win.** Perfectly one-sided = a defect signature
   under the symmetry rule, and the smallest/cleanest lead. Start here.
2. **hinata: +0.0131, the largest single regression** (43 better / 58 worse, 8
   win->loss vs 5 loss->win). Not one-sided, so partly churn -- but the magnitude is
   the biggest thing standing between the stack and adoption.

Three decks (fivecolour, burn, creature_giving) are cleanly positive; burn is 20/0
with no games worse at all. So the stack's *idea* is sound; two decks are paying for
it. Fix those two and the stack should be clearly adoptable.
