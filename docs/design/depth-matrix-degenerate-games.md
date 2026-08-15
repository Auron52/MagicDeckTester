# Depth matrix: per-game storage, degenerate-game skipping, and quality-based condemnation

Design agreed with the user 2026-08-13/14, from the FiveColour value-leaf run. Four changes with one
enabler.

**STATUS 2026-08-15 — every piece SHIPPED, and the whole pipeline has now been run end to end
(`valueleaf.sh run decks/burn`, 4m36s: 10,808 rows -> model -> 52 matrix cells -> metadata -> A/B).
That run found the quality rule's zero-variance hole; see §"Quality-based rung condemnation".**

| piece | state |
|---|---|
| 1. per-game storage | SHIPPED (`b579db0`, then pairs). Chunks carry `g` = [(offset, win turn)], read from the `.wins` file; retention is per GAME (`_chunk_restrict`), legacy mean-only chunks stay atomic. **It was silently failing on most chunks until 2026-08-15 — see the race below** |
| 2. skip + backfill | SHIPPED. `<out>.skipped.json` per (deck, seed); `target` grows by its size so cells backfill; `apply_skiplist` excludes the union from every cell; the table discloses it as `~~ FILTERED`. Backfill verified end-to-end 2026-08-15 |
| 3. a real abort | SHIPPED. `ai/GameWorkMeter.h` + `GameEngine::kAbandoned` + `abandon_units` per manifest job. Deterministic, in work units, disarmed by default |
| 3b. THRESHOLD POLICY | SHIPPED 2026-08-15 — `abandon_k` x the median of a cell's first `abandon_calib` games, frozen and recorded. See below |
| 4. median condemnation | SHIPPED 2026-08-15. Row-wide aggregation was `230765d`; the statistic is now a MEDIAN, the limit is `median_sec_per_game` (30 s, down from a 60 s mean), and both old spellings REFUSE rather than being reinterpreted |
| 5. ladder tops at H5 | SHIPPED (`cbb7876`), H6 dropped |
| 6. quality-based rung condemnation | SHIPPED 2026-08-15. Paired equivalence test at 0.0075 turns, one-sided 95% upper bound, driven into the live pool through `condemn.control_file` |
| 7. skip list applied CONTINUOUSLY | SHIPPED 2026-08-15 — it was only applied at the start of a run, so a run that finished never applied it at all. See below |

## The threshold policy: k x the cell's own CALIBRATED median

An absolute ceiling cannot serve a matrix. Measured on **burn**, the cheapest deck in the repo, six
cells alone span 150x in median units (V1 50, V2 341, V3 1,269, H1 777, H2 3,326, H3 7,506) -- and
FiveColour's H5 is orders further out. One number is either inert at the top of the ladder or shreds
the bottom. At k=3 on burn the same single knob produced ceilings of 168 (V1) through 50,343 (H3).

**The sample is a FIXED set of games, not a running median.** A running median depends on which games
happen to have finished when the next one starts -- i.e. on thread interleave -- so the abandoned set
would differ run to run and machine to machine, destroying the one property the unit currency was
chosen for. Instead:

* the calibration sample is exactly the cell's games with GLOBAL index < `abandon_calib`;
* those games run with no relative ceiling, and everything above that index is HELD BACK (a per-cell
  dependency in the existing drip/park machinery, **not** a barrier -- every other cell keeps running);
* on the last calibration game the ceiling freezes at `k x median`, and the pool reports
  `[batch] CEILING cell=... calib=... median=... k=... units=...`;
* the driver STORES it per cell and hands it straight back on the next resume, where those
  calibration games no longer appear and could never be re-measured. A ceiling recomputed from a
  different sample would abandon a different set -- reproducibility would break halfway through a run.

Verified on burn (k=3, calib=10): the abandoned set is reproduced exactly by an independent
prediction from the unbounded run's unit counts, on all six cells; survivors are byte-identical to
the unbounded baseline in win turn, play digest AND unit count; the frozen ceilings round-trip
through a resume.

**Sample-size caveat.** At `calib=10` the calibration median ran 1.3-2.2x above the cell's true
median on burn (H3: 16,781 vs 7,506), so the effective k varies by about that much between cells. It
errs LOOSE, which is the safe direction, and it is irrelevant against monsters at 100-1000x the
median -- but "k=20" is not precisely 20x the typical game. A larger sample tightens it at the cost
of a longer window in which the cell's worst game is still unbounded, which is the expensive
trade at H5.

**UPDATE 2026-08-15: the window is now bounded from game 3 onward** by a PROVISIONAL ceiling --
`4 x k x` the median of the cell's first THREE games by index, published to running games through the
same late-limit channel. The section below describes the exposure as it stood before that; what
remains of it is games 0-2 rather than all ten. Full rationale, the two determinism details it needed
(a cut game enters the sample as its CEILING; both limits are re-derived and OR'd at reduce time) and
the verification are in `condemnation-row-average.md` §"The calibration window".

**What the calibration window costs, measured on FiveColour.** Ten games per cell still run
unbounded, and on this deck that window is enough to catch a monster: V3's own 10-game sample spanned
2,380 to **2,359,567** units (81x its median) and H2's spanned 10,555 to 1,438,186. The hold-back
then makes that particular game worse rather than better -- the cell's other 15 games sit parked
behind it instead of running alongside. That is the deliberate price of reproducibility: releasing
them early would mean which games ran unbounded depended on timing, and the abandoned set would stop
being a function of (deck, arm, depth, seed, k, calib). It is bounded and it is concentrated where it
does least harm -- `calib` games instead of `target` games (10 vs 400, a 40x cut in unbounded
exposure), all of it at the START of a run when 52 other cells are competing for the box.

**Why one number could never have worked, on the real deck.** FiveColour medians, seed 8008, from the
calibration samples: V1 266, V2 3,752, V3 28,975, H1 11,603, H2 127,533, H3 508,841 units. That is
1,900x across six cells of ONE deck at shallow depths, with H growing roughly an order of magnitude
per rung -- and the matrix runs to H5 and V8.

### How concentrated the cost actually is (FiveColour, seed 8008, 25 games/cell, unbounded)

```
cell    median u        max u   max/med   worst game as % of the CELL's total work
V1           213        3,159       15x    19.6%
V2         3,473       89,707       26x    34.4%
V3        23,462    2,359,567      101x    59.4%
V4        97,643  168,381,673    1,724x    92.2%
V5        92,711  614,043,981    6,623x    82.3%
H1        11,558      155,806       13x    28.9%
H2        88,943    1,671,831       19x    28.1%
H3       448,783   11,821,796       26x    27.9%
```

The doc's original estimate -- "1-2% of games carry roughly half an arm's cost" -- understates the
deep cells. At V4 and V5 a SINGLE game out of 25 is 82-92% of everything the cell costs, and the
concentration grows monotonically with depth on both arms. This is the whole case for a per-game
ceiling in one table: no per-cell condemnation rule can reach it (the cell's other 24 games are
ordinary), and no micro-optimisation matters against 6,623x.

### Freezing as soon as the median is DETERMINED — SHIPPED 2026-08-15

**The calibration window is exempt, and on this deck that is exactly where the worst game landed.**
V5's 614,043,981-unit game is game 6 -- inside the first 10 -- so the armed run paid it in full,
25 minutes and counting, with the cell's other 15 games parked behind it. V4's game 6 (168,381,673
units, 12.6 minutes) was the same hand one rung shallower. At k=5 the V5 ceiling would have been
463,555 units, so that one game ran ~1,300x past what the ceiling would have allowed, purely because
of where it sat in the ordering. This is the one place the mechanism still cannot help, and there is
a clean way to close it that keeps determinism:

The median of an N-sample is DETERMINED before all N finish. With N=10 it is the mean of the 5th and
6th smallest, so once 6 games have completed and every still-running calibration game has already
consumed more units than the 6th smallest completed value, the final median is known -- the
unfinished games can only land above it, and their exact values cannot move it. Freeze there. Two
things follow, and the second is the point:

* the cell's held games start ~immediately rather than waiting on its slowest sample game, which is
  the utilisation half;
* the frozen ceiling can then be applied to the still-running CALIBRATION games as well. That is
  sound precisely because they are known to sit above the median: abandoning them cannot change the
  number that was frozen, so the abandoned set stays a function of (deck, arm, depth, seed, k, calib)
  and nothing depends on when the check happened to run.

A running game's consumed units are `thread_local`, so the meter now PUBLISHES them to a per-slot
atomic -- strided (every 4,096 units) and only while the game belongs to a calibration sample, so
every other game and every non-batch caller pays one predictable null check. A second pointer at the
cell's ceiling lets a calibration game stop when the ceiling freezes underneath it.

**Determinism is restored at REDUCE time, not by the abort.** Whether a given sample game was still
running at the instant of the freeze is a race, so the verdict is re-derived from cost alone when the
job is reduced: a calibration game is abandoned iff its work reached the ceiling its own sample
produced, whether it was stopped mid-flight or finished first. The abandoned set is then exactly
`{g : cost(g) >= ceiling}` on every machine, and the mid-flight abort is a pure cost saving with no
bearing on which games the table keeps. (Safe against the reduce racing the freeze: a job holding
calibration games also holds that cell's HELD games, and those cannot run until the freeze, so the
job cannot complete before the ceiling exists.)

Measured, FiveColour seed 8008, H1-H3 + V1-V5 at 25 games/cell:

```
                                    wall     V5's worst game
  no ceiling                     46 min      614,043,981 units, ran to completion
  ceiling, calibration exempt   >32 min      614,043,981 units, ran to completion (game 6, in-window)
  ceiling + early freeze           35 s      abandoned at 625,175 units
```

Same 8 cells. The abandoned set is still reproduced exactly by an independent prediction from the
unbounded run's unit counts, on all 8 cells, and the 170 surviving games are byte-identical to the
unbounded baseline in win turn and play digest. Across three repeats the freeze POINT moved (H2 froze
at 7 completed / 3 running, then 6/4, then 7/3 -- the schedule is genuinely non-deterministic) while
the frozen ceilings and the skip list came out identical every time, which is the property that
matters.

**Graceful degradation (`--max-skip-frac`, default 0.10).** Past some rate the dropped games are not
a tail but the distribution. It is also a runaway: `target` grows by the skip count, and a ceiling
below a cell's own median abandons every backfilled game too -- demonstrated with a deliberately low
absolute ceiling on burn, where H3 went 19 games, then 31, climbing with no end. Over the cap, the
ceiling is DISARMED for that deck+seed and the table says so. The relative ceiling makes this
unlikely by construction, so it is a backstop for a bad k, not the primary guard.

## The skip list was never applied WITHIN a run (fixed 2026-08-15)

§2 says it plainly -- "Detection is per-cell, application is global" -- and `apply_skiplist` does
exactly that. It was just only ever called ONCE, at the start of a run, while the list it applies
GROWS throughout. A run that finishes therefore never applies its own abandonments at all: the cells
that already played a game keep its result, and only the cell that abandoned it loses it.

Measured on burn (user caught it, 2026-08-15). Seed 8008 abandoned 28 games. At the end of the run
H1 still held all 28 of them and H4 held 7 -- so H4 was missing 21 games that H3 had, and those 21
were precisely the expensive ones, because the expensive ones are what gets abandoned. The row means
that came out:

```
                       H1      H2      H3      H4      H5     <- each over its OWN game set
  before the fix    4.3387  4.3362  4.3350  4.2589  4.2815
  after the fix     4.2354  4.2327  4.2327  4.2327  4.2327    <- all over ONE set of 357 games
```

H3 vs H4 read as a 0.076-turn gain from one extra rung (0.105 on seed 9009). Paired on the games
both cells held, they were identical to four decimals -- and after the fix they simply *are*
identical, because the row means are now over the same games. Every apparent H-rung improvement on
this deck was the artifact.

Two parts to the fix, and the second one matters as much as the first:

* sweep on EVERY chunk, not only on the ones that grow the list -- a chunk landing after the last
  abandonment would otherwise re-introduce an offset the other cells have dropped, and with no later
  growth nothing would sweep it out again;
* sweep through `_chunk_restrict`, not by filtering the incoming rows. Filtering by hand left the
  chunk's `g` disagreeing with its own `n` and `lp`, so the table reported 400-game counts over
  372-game data -- a different wrong answer that looked righter.

The table now also CHECKS the invariant instead of assuming it: comparable cells (not reference-
capped, not rung-capped) must hold identical game sets, and it says `!! UNEQUAL GAME SETS` if they
ever do not. An earlier attempt at this compensated inside `emit_table` by averaging over the
intersection; that was removed. A second mechanism silently papering over the first is how the
divergence stays invisible -- the guard reports, the skip list fixes.

## Quality-based rung condemnation, and the channel it needed

The rule is §6's: condemn a rung when the extra depth buys nothing, on a PAIRED equivalence test
(one-sided 95% upper bound below 0.0075 turns), never on a point estimate. Gated on a minimum pair
count and on 90% coverage of the shallower rung so a fluke early sample cannot condemn a live one.

Two things it needed that were not obvious:

* **The driver has to own the test, and needed a way to say so mid-run.** The comparison is against
  games this process may never have seen -- on a resume half the sample lives only in the state file
  -- so the engine cannot reach the verdict itself. `condemn.control_file` is a path the driver
  appends row names to and the pool re-reads on each heartbeat tick. Without it the rule could only
  fire at the NEXT resume, i.e. never for a single long phase-C invocation, which is exactly the run
  paying for the dead rung.
* **These verdicts IGNORE `never_condemn_depth`, deliberately.** The d<=5 floor protects the
  crossover rungs from the COST guards, which are wall-clock and can fire because the box was busy --
  an accident that would leave a hole in the answer. A quality verdict is not an accident: it has
  MEASURED equivalence on a sample large enough to bound the difference, and the rung keeps the games
  that proved it. Since every H4->H5 ever measured is dead, the floor would otherwise make the rule
  unreachable exactly where it pays.

* **A ZERO-VARIANCE SAMPLE IS NOT ZERO UNCERTAINTY** -- found by the first end-to-end pipeline run
  (burn, 2026-08-15), which condemned all NINE of its rungs at the 201-pair minimum, every one
  reporting `improvement +0.0000, se 0.0000`. When the two rungs agree on every game the sample se is
  0, so the bound is 0, so the rung is certified against ANY threshold from ANY sample size -- the
  test degenerates into "these 201 games were identical", which on a depth-insensitive deck is the
  normal case rather than evidence. The fix is a RESOLUTION FLOOR: with k=0 differing games in n
  pairs the rule of three bounds the rate of a differing game at 3/n, and a game that does differ
  moves this score by at least a whole turn, so the effect cannot be bounded below `3*step/n` --
  0.0149 at n=201, twice the threshold the sample claimed to clear. Floored at every k, not only 0,
  because the normal bound understates a handful-of-events sample too (k=1 gives ~2.6/n against a
  ~4.7/n Poisson bound). Its practical effect is a minimum of ~400 paired games before a perfectly
  flat rung can be condemned (`3/0.0075 = 400`), which is simply the sample a 0.0075-turn claim
  needs; a rung with real variance is untouched (FiveColour's H4->H5 floor at n=1600 is 0.0019).
  Re-measured on burn: the same nine rungs now condemn at 401-424 pairs instead of 201, at bounds of
  0.00748-0.00752 -- i.e. the floor, not the sample, is what they finally clear.

A quality-capped cell is marked `=` in the table, distinct from `*` (intractable / reference-only),
and is excluded from the equal-game-set check: it stopped early on purpose, so it holds a subset.

**So the metadata deriver reads PER-GAME DATA, not the table's row means.** Everything it produces --
`value_trust_depth`, `value_no_fallback`, the crossover -- is a COMPARISON between one depth's LP and
another's, so the two have to be over the same games or part of the difference is just which hands
each cell drew. `valueleaf_table_to_metadata.py` now recomputes every row over one game set per seed
from `<log>.cells.json` (intersecting the cells that are not reference-capped; a 50-game reference
cell would otherwise drag the whole deck down to 50). Verified against the burn matrix where the
rungs were genuinely unequal: the deriver moved H1 4.3387->4.2354, H2 4.3362->4.2327, H3
4.3350->4.2327, H4 4.2589->4.2327, recovering exactly the values a separately-fixed run produced --
two independent paths landing on the same numbers, and H2 = H3 = H4 as the paired truth. A legacy
table with no per-game records still derives, with a note saying the rows are only comparable if
every cell held the same games.

## The race that was silently disabling per-game storage (fixed 2026-08-15)

`--batch` printed a job's result line BEFORE writing its `.wins`/`.units` files, so a driver waiting
on that line could open the file before it existed. Measured on a 12-chunk burn matrix: **9 of 12
chunks** lost their per-game rows and were stored mean-only. The damage is worse than losing detail:

* a chunk SHORTENED by abandonment then records n survivors with no offsets, and `_chunk_offsets`
  falls back to `off..off+n` -- a contiguous range it does not hold. One measured chunk was stored as
  covering "0..7" while actually holding {1,2,4,7,12,17,19,22}, so every paired comparison against it
  would intersect on the wrong game identities. That is exactly the defect per-game storage exists to
  end (§1), reintroduced underneath it.
* the abandoned games never reached the skip list, so every resume re-ran and re-abandoned them at
  full cost -- the failure §2's skip list is built to prevent.

Two fixes: the engine writes the files before announcing the job (the line is now a promise that they
are on disk), and the driver REFUSES a short chunk that has no per-game rows rather than storing it
with a fabricated range.

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
complete in reasonable time, is the leaf as good as H5?" rather than "on all games". A reader
comparing against an unfiltered table would otherwise be comparing different populations, so the skip
list goes into the table.

But do not read the filter as a regrettable compromise -- **it is the correct population** (user,
2026-08-15). Shipped play is BUDGETED; it will never process one of these positions at depth, because
the budget truncates the search long before. So the filtered games are not cases production reaches
and then handles badly, they are cases production never reaches at all. Measuring their unbounded
quality would be characterising a regime that does not occur, and letting them into the average moves
the number the policy is derived from. The question the table exists to answer is: *on the cases
where we do escalate, and the cost is not insane, does the leaf match H5?*

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

## 4. Condemn on the MEDIAN, row-wide — SHIPPED

Supersedes the statistic in `condemnation-row-average.md` (the row-wide aggregation there still
holds; this refines what is aggregated).

**The question the rule has to answer is "can we escalate to this depth SOME of the time, at a cost
that is not insane?"** (user, 2026-08-15) -- because that is the only regime the derived policy is
applied in. A row is therefore usable when a TYPICAL game is affordable, however ugly its tail (§2's
ceiling truncates the tail), and unusable only when the MAJORITY of its games are impractical. That
sentence *is* a median guard.

What was there before: a raw MEAN over 50 games. One 32-minute game carried V6/V7/V8@8008 over a
60 s/game limit while the other 49 games ran ~19.5 s/game. (The final means below sit just under 60;
condemnation fired earlier, when n was small enough for the one monster to dominate -- which is its
own defect, a verdict that depends on when you look.)

```
cell            games   >60s   % over    mean s/game   mean rule   median rule
V6@8008            50     12    24.0%          57.3s     CONDEMN          keep
V7@8008            50     12    24.0%          57.6s     CONDEMN          keep
V8@8008            50     12    24.0%          58.5s     CONDEMN          keep
H6@9009             4      4   100.0%        1319.1s     CONDEMN       CONDEMN
```

**Two reasons the mean had to go, and the second is fatal:**

1. It answers the wrong question. A mean conflates "many moderately slow games" -- condemn, there is
   nothing to salvage -- with "a few catastrophic ones" -- abandon those and keep the row. Opposite
   responses, same statistic.
2. With the §2 ceiling armed it is not well defined. A ceiling'd game contributes its TRUNCATED cost,
   so a row with 10% abandoned games reads `0.9m + 0.1(km)` = **3.4x its median at k=25 and 1.9x at
   k=10**. The verdict would follow an arbitrary constant, and 74% of the statistic would come from
   games the table discards. A median does not move at all.

**The H5 objection that used to sit here was wrong, and it was blocking this.** It read: "H5 has
30-60% of its games over the limit, so a median guard would condemn H5 -- the reference trust is
defined against." H5 is *structurally exempt*: `never_condemn_at_or_below` is clamped to >=5 (the
driver exits below that) and `HDEPTHS` tops out at 5, so no H cell is condemnable whatever the
statistic is. The only condemnable rows in a real run are V6/V7/V8.

**The limit came DOWN with the statistic, and had to.** A mean carried tail inflation a median does
not, so reusing 60 would have loosened the guard by roughly 3x. `median_sec_per_game` is 30: healthy
V6-V8 cells measured ~19.5 s/game typical (~1.5x headroom), and a row sitting at the limit costs
400 games x 4 seeds x 30 s = 13.3 core-hours. Both old spellings -- the `--intractable-sec-per-game`
flag and the `condemn.sec_per_game` manifest key -- REFUSE rather than being reinterpreted, because
the same number means something stricter under a median and a silently-carried-over limit would
condemn on a threshold nobody chose.

**In-flight soundness survives the change.** The heartbeat rule folds running games in at their
elapsed time, a LOWER bound on their final cost. A median is monotone in every element, so raising
those partial values can only raise it: the in-flight median can only UNDERSTATE the final one, and
condemning on it cannot be a false positive from the partial term. Same argument the mean version
had, intact.

**Graceful degradation** is `--max-skip-frac` (default 0.10): past that skip rate the ceiling is
disarmed for the deck+seed, because filtering is then reshaping the population rather than trimming
it. That matters most for the decks we most want -- dragonstorm's deep rungs were all condemned.

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
