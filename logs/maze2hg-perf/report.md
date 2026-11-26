# Maze's End 2HG slow games (g1380, g1647): report

Written by the parent session from the agent's final message (its harness blocks report files).
Code: c5c8877d9. Handoff: logs/maze2hg-perf/maze2hg-perf-handoff.md (c5deef4e7). Branch: wip/maze2hg-perf.

## Root cause, g1380 (seed 31301380 gi1380)

Four decisions take 97% of the game's time:

| decision | units | wall | fan crossings |
|---|---|---|---|
| T5 | 224 | 305 s | 8.2M |
| T3 | 238 | 153 s | 8.8M |
| T4 | 628 | 179 s | 6.1M |
| T5 | 154 | 347 s | 9.9M |

- Every large fan is FSLineTail's single-main boundary fan at T6, depth 0, with cutoff equal to its own
  turn (cutoff = 6, because the root already holds a T7 win). Each fan makes 65k-284k crossings, and
  every child FSLineWin is refused at entry. None of about 2,000 fans found a win.
- Refused children charge no units, so each decision used only 154-628 units. The priced ceiling
  (`MTG_DECISION_PRICED_X`=100, 45 s priced) does trip at about 18M payments, but it priced a crossing
  only by its payments (~5.6 us) against 20-60 us of real work, so it allowed several times the real cost.
- The g803 dead-tail skip does not cover these tails: here `turn == cutoff`, not `turn > cutoff`. A win
  during the opponent's turn could still be adopted, so skipping the fan would not be sound.
- A memo would not help: the 50 fans in one decision had 50 different starting states.

g1647 (seed 31301647) needed no fix. Its 20-33 min came from the hold-up A/B binary, which predates the
g803 fixes. With those two fixes turned off, its T6 decision ran past 370 s (12M crossings).

## What changed (all default ON)

1. Three byte-identical per-node cuts in `src/ai/DecisionProviders.cpp`:
   - `MazesEndProvider::EtbTapSubtypeOrder` computes each sort key once instead of twice per comparison.
   - ManaSourceRank calls `ScaledManaFeederMana` only after the cheap devotion-land tests pass.
   - `LandEntryPinCandidates` stops its colour scan once every candidate colour is accounted for.
2. A dead-crossing price (`workctr::kDeadCross`, `MTG_DEAD_CROSS_PRICE`) in `src/core/WorkCounters.h` and
   `src/ai/TurnSolver.cpp`, at all four fan sites. It applies to a fan crossing whose child is refused at
   entry or whose crossing fails, priced at 21,426 ns (the fitted turn-step price). It only adds to priced
   work, so play can move only where the priced ceiling binds, and it is inert in an unlimited search.
   The census gets a `w_dead_x` column and `scripts/census_priced.py` knows the new kind.
3. Two unit tests in `test/unit/test_mazes_end_i3.cpp`: a refused crossing is billed, and the gate tap
   order stays tiered and stable.

## Identity batch

712 games: Maze's End standard and 2HG on three seeds each, plus Blood Sun standard and 2HG. Every job
digest, per-game win line and unit count is identical between before and after, with the price on. CPU
fell from 7,131 s to 6,794 s (-4.7%, busy box). No decision in this sample reached the priced ceiling, so
no game moved and there are no slower games to check at d8 b0.

## Wall and CPU (box at load 36-55 on 32 cores, so seconds are noisy; counts are exact)

| game | arm | digest | units | wall | CPU |
|---|---|---|---|---|---|
| g1380 | e430cc585 | 833668b2aae5ae1f | 172,253 | 990 s | 962 s |
| g1380 | tip | 833668b2aae5ae1f | 171,522 | 410 s | 356 s |
| g1647 | e430cc585 | 6b119926d7b287cb | 63,114 | 27.5 s | 23.8 s |
| g1647 | tip | 6b119926d7b287cb | 63,114 | 25.0 s | 21.7 s |

- In g1380, each slow decision now stops at about 100 s, and the first T5 makes 1.48M crossings
  instead of 8.2M.
- In a concurrent run, that first T5 took 544 s before, 384 s with only the per-node cuts, and 100 s
  with everything.
- g1380 plays the same game with 731 fewer units, so it is a play change under the GT rule.

## Unit tests

All "Maze's End*" tests pass. The full suite had 2 failures out of 4,195, both Windswept Heath
card-data tests (`test_bloodsun_suppression`, `test_boars_cards`). This change touches neither the card
data nor those tests. The agent suspects origin's c1206ef6b; the parent is checking the landed binary.

## Open

- GT: smoke and regression should be byte-identical unless a row hits the priced ceiling.
- Getting g1380 down to a normal 10-30 s: each ceiling-bound decision still costs about 45 s of priced
  work. Going lower means charging dead crossings to the UNIT budget, which changes Maze 2HG play
  wherever these fans occur, so it needs a held-out A/B. The only alternative is a deck-specific proof
  that no crossing can win at the boundary, which would allow a sound skip.
