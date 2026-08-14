# Depth matrix: per-game storage, degenerate-game skipping, and quality-based condemnation

Design agreed with the user 2026-08-13/14, from the FiveColour value-leaf run. Four changes with one
enabler.

**STATUS 2026-08-14 — 1, 2, 3 and 5 have SHIPPED; 4 partly; 6 is open.**

| piece | state |
|---|---|
| 1. per-game storage | SHIPPED (`b579db0`, then pairs). Chunks carry `g` = [(offset, win turn)], read from the `.wins` file; retention is per GAME (`_chunk_restrict`), legacy mean-only chunks stay atomic |
| 2. skip + backfill | SHIPPED. `<out>.skipped.json` per (deck, seed); `target` grows by its size so cells backfill; `apply_skiplist` excludes the union from every cell; the table discloses it as `~~ FILTERED` |
| 3. a real abort | SHIPPED. `ai/GameWorkMeter.h` + `GameEngine::kAbandoned` + `abandon_units` per manifest job. Deterministic, in work units, disarmed by default |
| 4. median condemnation | ROW-WIDE half shipped on the other machine (`230765d`); the MEDIAN statistic is still open |
| 5. ladder tops at H5 | SHIPPED (`cbb7876`), H6 dropped |
| 6. quality-based rung condemnation | OPEN — the 0.0075 equivalence threshold is derived below but not implemented |

**What is left before this can drive a real run: the THRESHOLD POLICY.** The ceiling is currently
supplied by the caller as an absolute unit count; nothing computes "k x this cell's median" yet.
`MTG_DUMP_UNITS` now writes a `<job>.units` file so the distribution can be MEASURED rather than
guessed -- a first sample (FiveColour, d3 b10, 25 games) gives median 36,451 units and max 408,902,
i.e. 11.2x the median at a cheap depth. k, whether the median is per-cell or per-row, and the
graceful-degradation rate all want that data before being fixed in code.

The motivating run: 22,400 games, 88% complete after 28 hours, then stalled with **6 games running,
one of them for 26.5 hours**, holding 6 of 24 cores. Those six games could not change any conclusion
the table exists to produce.

## The evidence

**The tail is the rollout leaf, not the search.** Same deck, same board states, same depths:

| arm | leaf | slow-game total | worst game | top 1% of games |
|---|---|---|---|---|
| H | rollout to end-of-game (`FSLineWin`/`FSLineTail`) | 751 core-h | **21.36 h** | **28.6%** of the arm |
| V | O(1) learned model | 33 core-h | **0.53 h** | 9.1% |

23x total cost and 40x worst case from the leaf alone. `perf` on the live run showed a flat profile --
top symbol `memset` at 8.6%, nothing above 10%, mana payment the largest coherent cluster at 22.3%,
recursion BOUNDED at ~11 `FSLine` frames. Each stuck thread ground a different per-node function
(lord bonus 30%, mana backtracking 39%, state zeroing 34%, action enumeration 27%). Flat profile plus
per-thread concentration is combinatorial BREADTH: node count is the multiplier, and no micro-
optimisation touches it.

**It is unbounded by construction.** The matrix runs `budget_ms: 0` (the table measures unbounded
quality) and `never_condemn<=5` exempts d<=5 from every guard. An H5 game has no budget, no
condemnation and no cap. **Shipped play is unaffected** -- play is budgeted (`BuiltinDefaultPlay` =
d5 / 20 virtual-ms) and never enters this regime. This is an artifact of the measurement.

**No meaningful ETA exists once a game is in the tail.** ~60% of games past X also pass 2X through
most of the range (alpha ~0.74, INFINITE mean); only the far tail thins to alpha ~2, fitted on 50
points. Expected remaining for the 26.5 h game: ~26 h to unbounded, depending on threshold.

**The expensive games are not the informative ones.** Paired on identical 25-game blocks:

```
H4 vs H5:  61 blocks, 57 IDENTICAL (93%)   differing blocks median 469 s/game vs 550 agreeing (0.9x)
V6 vs H5:  48 blocks, 47 IDENTICAL (98%)   differing blocks median 635 s/game vs 481 agreeing (1.3x)
V5 vs H5:  61 blocks, 53 IDENTICAL (87%)   differing blocks median 825 s/game vs 481 agreeing (1.7x)
```

The signal lives in 1-4 blocks out of ~50-60. The single most expensive block in the run (4,908
s/game, holding the monsters) is in the AGREE set for both comparisons -- it cost the most and said
nothing. Caveat: 4, 8 and 1 differing blocks is a thin basis; the direction is clear, the ratio is not.

**Economics of skipping.** Against the banked H arm (8,036 games, 566 core-h, mean 253 s/game):

| trigger | games hit | % of H games | time saved |
|---|---|---|---|
| 0.5 h | 251 | 3.12% | ~81% |
| 1 h | 149 | 1.85% | ~66% |
| 2 h | 94 | 1.17% | ~46% |
| 4 h | 38 | 0.47% | ~24% |

(Approximate: `slow_games.log` spans passes later discarded, so it over-counts against the banked
total.) ~1-2% of games carry roughly half the arm's cost.

## 1. Per-game results instead of chunk means -- THE ENABLER

Today a chunk stores one mean over its `n` games. Consequences:

* **Nothing below a chunk can be dropped.** Skipping one game makes that chunk cover 24 games with a
  mean over a different game set than every other cell -- it silently leaves every paired comparison.
  Dropping the whole chunk everywhere instead is worse: 149 degenerate games spread over ~321 H blocks
  means **~37% of blocks would contain at least one**, so we would discard a third of the sample to
  save 2% of the games. With the signal living in 1-4 blocks, losing blocks is the one thing we cannot
  afford.
* It is also what let `_chunk_minus` FABRICATE an `lp` (fixed 2026-08-13 by making retention
  chunk-atomic) -- the same limitation, different symptom.

Per-game storage costs a few MB (22,400 x 56 values) and makes a skip an **analysis-time filter**: cells
that already played the game keep the result on disk and simply exclude it. Nothing is regenerated,
nothing is retroactively edited, and comparisons intersect on GAME SETS rather than counts.

## 2. Skip degenerate games and backfill

Abandon a game past a threshold, discard its result, and extend the cell's target so the game count
stays whole.

**The threshold must be in WORK UNITS, not wall-clock.** `SearchBudget` exists precisely to make
results machine-independent ("identical seed + budget does an identical amount of work ... on every
machine"). Keyed on wall time, the same game is dropped on one box and kept on another -- breaking
reproducibility and cross-machine pooling. In units the skip list is a deterministic function of
(deck, seed, depth, arm), identical everywhere.

**It must also be RELATIVE.** Cells span 11 ms (V1) to 700 s (H5) per game, so no single absolute
number serves. Something like k x the cell's own running median, k ~ 20-50; at H5 on FiveColour that
lands near 1-3 h, where the tail actually begins.

**Detection is per-cell, application is global.** A game degenerate at H5 may be trivial at V3. Once
any cell abandons game g, g joins the (deck, seed) skip list and every cell excludes it from
comparisons. With per-game storage no re-running follows.

**Queue bookkeeping is small.** `target(c)` becomes `args.target + len(skiplist(deck, seed))`;
`build_queue` already computes missing offsets against a target, so it extends past 400 naturally.
The skip list is shared per (deck, seed), so all cells of a seed grow together and stay aligned.

**The estimand changes and MUST be recorded in the artifact.** The table then answers "on games that
complete in reasonable time, is the leaf as good as H5?" rather than "on all games". That is arguably
the better question -- budgeted play never searches a degenerate position to completion, so its
unbounded quality was never decision-relevant -- but a reader comparing against an unfiltered table
would otherwise be comparing different populations. Write the skip list into the table.

## 3. A real ABORT, not a repurposed budget — SHIPPED

There was no cancellation. `BatchRunner`'s in-flight condemnation only stops FUTURE dispatch; its
comment ("there is no safe way to abort a search mid-node") was stale, because
`SearchBudget::SetOverrunLimit`/`Overrun` is exactly a polled bail-out, built for a search whose cost
explodes past its estimate.

But it is **per-pass**: it makes one search roll back to its last completed pass, after which the game
continues playing with a cheaper decision. Re-arming it per decision still leaves the GAME unbounded.
What was needed, and what shipped:

* a unit counter accumulating across every search in the game -- `ai/GameWorkMeter.h`, a thread_local
  fed from `SearchBudget::Consume`. That is the one place that sees all of a game's work: budgets are
  per DECISION and a search builds sub-budgets for its probe passes, so no single `SearchBudget` can
  bound a game. Consuming from one budget per node means each turn-step counts exactly once.
* a sentinel return from `RunGame` -- `GameEngine::kAbandoned` (-2), mapped to the existing `kSkipped`
  in `BatchRunner` so `reduce_job` already drops it from the average. Scoring it as a loss would make
  skipping CHANGE the number being measured, which is the one thing it must not do.
* `Overrun()` additionally consults the meter, purely so the recursion unwinds promptly. The two
  signals stay distinct: a pass overrun leaves a playable line and is recorded as such; an
  abandonment means no result should be reported at all. Overloading one for the other would make
  them indistinguishable in the very telemetry used to judge tractability.
* `PlayOutFrom` checks the flag at the turn boundary, so a voided game stops instead of playing on
  over rolled-back lines -- cheap per turn, unbounded in count, all of it discarded.

Measured on FiveColour (25 games, d3 b10): ceiling 0 -> 25 played and the baseline digest exactly;
200k -> 20 played / 5 abandoned; 50k -> 14 / 11; 20k -> 11 / 14. Identical digest and identical
abandoned SET across three repeat runs (only the stderr ordering varies, which is thread interleave).
The suite is byte-identical with the meter disarmed: smoke 36/36, regression 60/60, 0 play-changed.

Note the selection effect visible in that table -- the average over survivors moves (4.84 -> 4.64 as
the ceiling tightens), because expensive games are systematically harder hands. That is the estimand
change, and it is why the filter has to be disclosed in the artifact rather than applied silently.

## 4. Condemn on the MEDIAN, row-wide

Supersedes the statistic in `condemnation-row-average.md` (the row-wide aggregation there still
holds; this refines what is aggregated).

Today: a raw MEAN over 50 games of one (deck, arm, depth, SEED) cell. One 32-minute game carried
V6/V7/V8@8008 1-3% over a 60 s/game limit while the other 49 games ran ~19.5 s/game.

"Condemn if the majority of games are slow" IS a median guard, and a median is immune to one or two
long tails by construction. Measured against what actually happened:

```
cell            games   >60s   % over    mean s/game   mean rule   median rule
V6@8008            50     12    24.0%          57.3s     CONDEMN          keep
V7@8008            50     12    24.0%          57.6s     CONDEMN          keep
V8@8008            50     12    24.0%          58.5s     CONDEMN          keep
H6@9009             4      4   100.0%        1319.1s     CONDEMN       CONDEMN
```

**The 60 s/game limit is calibrated for cheap decks.** H5 has 30-60% of its games over it, and over
half on two seeds -- a median guard at 60 s would condemn H5 on this deck. That is not a tail; that is
the whole distribution. Skipping cannot rescue a cell whose MEDIAN exceeds the limit, only one with a
heavy tail. So the limit must scale (per-depth, or relative to the arm's shallower rung) or H5 keeps
its protection -- otherwise we lose the reference the trust criterion is defined against.

**Graceful degradation:** if the skip rate in a cell exceeds some percentage, stop skipping and condemn
instead. That matters most for the decks we most want -- dragonstorm's H3-H6 were ALL condemned, so a
skip policy there would fire constantly.

Caveat on the percentages above: `slow_games.log` accumulates across resumes, so re-run games are
double-counted (`H6@8008` shows 11 records for 7 games). The V-cell result is robust (24% is nowhere
near 50%); treat the H numbers as approximate.

## 5. The ladder tops at H5; trust = closeness to H5

**Escalation caps at H5**, so H5 is the strongest fallback the runtime can ever take, so "trust the
leaf" is definitionally "the leaf matches H5". Nothing deeper can inform a decision the runtime cannot
make -- which removes any need to generate H6 or H7.

**H6 has never once been completed, on any deck, in any run.** Every H6 we hold is a small side-sample:
fivecolour 4g (`*`), antilife 50g (`*`), dragonstorm 100g (`*`), and the 5-deck tables' H6 was merged
in from a **150-game** `d68` pass (`_antilife_d68_avg.txt` and `_5deck_combined.txt` both carry
`H6=4.6933`, byte-identical) into tables declaring `games=500`. Those predate both the `*` marker and
the `# games/cell:` line, so nothing disclosed it. The numbers say the same: H4->H5 across all decks is
0.0000-0.0033 while H5->H6 ranges **-0.6048 to +0.1525**.

The rationale in `valueleaf.sh` for keeping H6 -- *"H6 came in ~11x CHEAPER than H5"* -- is contradicted
by this run: `H5=[822515.7ms]` vs `H6=[991283.1ms]`, i.e. 1.2x MORE expensive, and that measured on the
4-50 games that tripped the one-hour guard, so it is a floor.

Cost of keeping it in this run alone: **47.4 core-h** for 111 unusable games; it supplied the
`H6=4.8100` that flipped the derived fallback rule from "never fall back" to "fall back at H6" (fixed
2026-08-13); and its ragged condemned chunks (`n=4`, `n=7`) held seeds 8008/9009 hostage in the banking
intersection. Change: `HDEPTHS="1 2 3 4 5"`, and rewrite the stale rationale at `valueleaf.sh:112-121`.

**Do not drop to H4.** The two decks still improving at H3->H4 (dragonstorm +0.1100, hinata +0.0850)
are exactly the two whose H5 was never measured -- dragonstorm's H5 was ATTEMPTED AND CONDEMNED, hinata's
never attempted. Depth stays useful roughly as far as a deck's games run (antilife 4.09 dies at H2->H3;
fivecolour 4.98 dies at H4->H5; hinata's games run to turn 6.00). H5 is the conservative ceiling.

## 6. Quality-based condemnation of a heuristic depth

Condemn a rung when the extra depth does not buy anything, not merely when it is expensive.

Rung deltas across 8 decks are **bimodal with a real gap** -- nothing lands between 0.0044 and 0.0078:

```
dead rungs:  0.0000 ... 0.0044   (15 observations)
             ---- gap 0.0034 ----
live rungs:  0.0078 ... 0.1100   (14 observations)
```

Every H4->H5 ever measured is dead: burn/knights/slivers 0.0000, antilife 0.0003, TH 0.0017, and
fivecolour **+0.0033** (the largest anywhere).

**Use an equivalence test, not a point estimate.** "Point estimate < threshold" condemns live rungs on
noise; the correct rule is that the one-sided upper confidence bound falls below the threshold.

**Proposed threshold: 0.0075 turns.** Above the dead cluster (max 0.0044) with margin, below the live
cluster (min 0.0078), and -- decisively -- certifiable with ~27 blocks (667 games/arm, ~42% of a full
run), so the rule can fire EARLY, which is the point. A 0.005 threshold could never fire in time: it
needs 2.5x a FULL 400x4 run.

FiveColour worked: `H4-H5 = +0.0033, se 0.0017, one-sided 95% upper bound 0.0061` -> PASSES at 0.0075,
FAILS at 0.005/0.006. Completing the run does not change it (bound moves 0.0061 -> 0.0060).

Keep a minimum-coverage gate (the user suggested 90%) so a fluke early sample cannot condemn a live
rung, but the equivalence test is what does the work.

Caveat: other decks' deltas are row means from historical tables with no per-chunk data, so the
DISTRIBUTION is real but per-deck equivalence tests were not possible. The threshold is calibrated on
8 decks.

## Gate

`SearchBudget`/`RunGame`/`BatchRunner` changes are engine: standing smoke + regression gate, and they
must land BEFORE or AFTER a generation, never during -- condemnation and skipping decisions read
timings, so gating against a live matrix inflates them and causes the very false condemnations this
removes. The driver-side pieces (per-game storage, skip bookkeeping, queue targets) are Python and can
land any time; they take effect at the next resume.

Related: `condemnation-row-average.md` (row-wide aggregation; this refines the statistic),
`batch-drip-release.md` (same pool), `fivecolour-depth-monotonicity.md` (closed by this run: V8 is
IDENTICAL to H5 on all 48 common blocks -- no search bug, the apparent inversion was engine
contamination plus unequal seed sets).
