# Condemn on the ROW average, not one seed's cell (SHIPPED 2026-08-13)

User directive, 2026-08-13: *"I don't think we should condemn based on one seed. That's a design flaw.
... It should be condemned based on the average."*

**SHIPPED 2026-08-13 (engine + driver), between matrix generations as the Gate requires.** The
manifest job gained a `row` key (`<deck>_<arm><depth>` -- the cell minus its seed); the MEAN rule
accumulates and judges on it, in both the finished-game rule and the heartbeat's in-flight fold.
`max_game_sec` stays per-cell on purpose (one game is one observation; widening its blast radius to
the row would remove four seeds on it instead of one -- see "Also worth fixing", still open). A
manifest with no `row` judges per cell exactly as before, reported with the old `cell=` wording, so
every non-matrix manifest is byte-identical. The driver stamps `row` on every chunk and mirrors
`CONDEMNED row=` lines into every cell of the row (still matching `cell=` for max_game_sec verdicts).
Verified: synthetic three-scenario check (row survives / row trips row-wide with both seeds' cells
capped at the reference sample / no-row legacy fallback) plus smoke 33/33 byte-identical.
Shipped together with the drip-as-cap release (`batch-drip-release.md`), which the row rule
composes with: a row is judged tractable after `reference_games` ROW-WIDE, so release from
metering comes ~4x sooner.

It was deferred only because it is an engine change and a matrix generation was 30% through; the
history and analysis below are kept as written.

**Partly mitigated already, driver-side.** A condemned cell no longer votes on what a play change
banks, and retention is chunk-atomic (`valueleaf_depth_matrix.py`, 2026-08-13). That stops a
condemned row from holding its seed hostage and stops ragged condemned chunks from fabricating `lp`
in every other cell of the group. It does NOT stop the false condemnation itself -- the row still
loses 350 of its 400 games -- which is what the change below is for.

## What happened

FiveColour's matrix condemned three value-arm cells, all on the same seed:

| cell | games | condemned at | limit |
|---|---|---|---|
| V6@8008 | 50 | 60.9 s/game | 60.0 |
| V7@8008 | 50 | 60.6 s/game | 60.0 |
| V8@8008 | 50 | 61.7 s/game | 60.0 |

Over the line by 1-3%. The same depths on the other three seeds ran 5.8-16.7 s/game and completed
all 400 games. One game did it: `gi=17` on seed 8008 took ~0.52 h in each of the three cells and is
1,908 s of V6@8008's 2,865 s total (67%). Excluding it the cell runs ~19.5 s/game -- a 3x margin
under the guard.

## The two defects

`BatchRunner.cpp` accumulates against `job.cell_id`, which is `(deck, arm, depth, seed)`:

```cpp
const int   n  = cell_games[cid].fetch_add(1) + 1;
const long long tot = cell_ms[cid].fetch_add(g_ms) + g_ms;
if (n >= condemn.reference_games && double(tot)/n > condemn.sec_per_game*1000.0 && ...)
```

**1. The unit is the seed.** Tractability is a property of the DEPTH -- whether searching this deck
that deep is affordable. A single seed is a sample of that property, not the property itself. Judging
each seed separately means the row's fate is decided four times independently, and any one of them
going over removes that seed from the row.

**2. The statistic is a raw mean over 50 games.** A mean is not robust, and cost per game in this
engine is heavy-tailed -- a handful of games carry most of a cell's cost. The `max_game_sec` guard
already exists to catch a single runaway game; letting one also tip the *mean* guard double-counts
the same outlier and conflates "one hard hand" with "this depth is unaffordable".

## Why it matters more than the lost games

The table's row mean is an unweighted mean of per-seed means (`valueleaf_depth_matrix.emit_table`):

```python
lp = sum(cell_mean(c) for c in cs)/len(cs)
```

so a 50-game cell carries the same 25% as a 400-game cell. On FiveColour:

```
V8:  8008 = 4.8200 (50g)   9009 = 4.9850   10010 = 5.0225   11011 = 4.9725
     unweighted row = 4.9500        game-weighted row = 4.9864
```

**0.036 between those two readings, against the 0.019 H5-vs-V8 gap the run was launched to settle.**
Seed 8008 scores ~0.17 below the other three, so which seeds a row carries moves that row further
than the effect under study. Condemnation therefore does not merely thin a row -- it makes rows
NON-COMPARABLE, because V6-V8 are missing 350 games of a systematically-easy-scoring seed that
H1-H5 will carry in full. Any cross-arm ordering read off such a table is partly an artifact of which
cells happened to trip a wall-clock guard.

Neither weighting rescues it. Equal-weight-per-seed is right in principle (the seed is the
replication unit) but then one seed's estimate rests on 50 games; game-weighting silently
underweights the hard seed. The only real fix is for every row to carry every seed.

## The change

Accumulate and judge on the ROW, `(deck, arm, depth)`:

- Key the counters on a row id instead of `cell_id` (cells keep their own counters for reporting).
- Trip when `row_ms / row_games > sec_per_game` with `row_games >= reference_games`.
- On condemnation, cap EVERY cell of the row at `reference_games`, which is what the driver's
  `target()` already assumes for an intractable cell.

Effects: V6/V7/V8 read ~23 s/game row-wide and survive. H6 -- genuinely explosive on all four seeds
(102.6 s/game where it got a mean at all, two seeds tripping `max_game_sec` on a single in-flight
game over an hour) -- still trips, and trips SOONER, because 50 reference games now accrue across the
row rather than 50 per seed. The reference cost of judging an explosive row falls ~4x.

Seed balance needs no extra machinery: the pool admits condemnable work at `drip` games per CELL, so
a row's games arrive roughly evenly across its seeds and the aggregate is not dominated by whichever
seed the scheduler reached first.

### Also worth fixing at the same site (SHIPPED 2026-08-15)

`max_game_sec` condemned the whole cell from ONE in-flight game (it is what took H6@8008 and
H6@9009). As a liveness guard it is right to fire -- nothing should wait an hour on one game -- but
the response should be to abandon THAT GAME, not the cell: the same one-observation-kills-a-cell
shape as above, one level down.

**It is fixed by SUBTRACTION, not by teaching this rule to abandon.** The abandon-that-game response
now exists and is the per-game work ceiling (`ai/GameWorkMeter.h`): it stops the game itself, counted
in work UNITS, so the set it drops is a deterministic function of `(deck, seed, depth, arm, limit)`
and is what the table-wide skip list is built from. `max_game_sec` cannot do that job, because it is
keyed on WALL CLOCK -- an abort here would drop a different game on a different box, or on the same
box under different load, and a wall-clock entry in a shared skip list makes the whole matrix
quietly unreproducible. So the rule was made to YIELD instead: **a cell whose per-game work ceiling
is armed (a frozen `abandon_k` ceiling, or an absolute `abandon_units`) is never condemned by
`max_game_sec`.** It prints one `[batch] OVER max_game_sec cell=...` line per cell and leaves the
cell alone.

Where the ceiling is in force, condemning was not merely disproportionate (one observation against
350 games, and the row loses a whole seed) but wrong on its own terms: the game is already bounded,
so an overrun says the box is loaded or `k` is too loose -- neither of which is a property of this
depth's tractability.

Verified on a 3-cell synthetic (burn d6, `max_game_sec` 0.02 s so every game trips):

| cell | ceiling | before | after |
|---|---|---|---|
| `c_units` | `abandon_units` | condemned at 5 games | **40/40 played**, one OVER note |
| `c_k` | `abandon_k` (frozen) | condemned at 5 games | **35/40 played**, 5 abandoned at the ceiling |
| `c_none` | none | condemned at 5 games | condemned at 5 games (unchanged) |

### The calibration window: the PREFIX ceiling (SHIPPED 2026-08-15)

That left one residual -- a cell using `abandon_k` has no ceiling for its first `abandon_calib`
games, so a monster INSIDE the window ran unbounded and `max_game_sec` was still the only thing that
could react, i.e. still condemned a cell on one observation. The user asked the right question of it:
*"don't we use the median now?"* We do -- just not soon enough. The fix is to **start using a median
earlier**, not to add another wall-clock rule.

**PROVISIONAL CEILING.** Once a cell's first `kCalibPrefix` = 3 games *by global index* are in, arm
`kProvisionalSlack (4) x k x median(those three)` for the rest of the window, published to games
already running through the same late-limit channel the freeze uses. The prefix is fixed BY INDEX, so
the value is a deterministic function of the data exactly like the frozen ceiling -- "the first three
to finish" would follow the thread interleave and make it machine-dependent.

It is deliberately SLACK. A 3-game median is a noisy estimate of a 10-game one, and this ceiling is
not trying to be the real one: it only has to stop the pathology (100-1000x the median) without
touching the natural spread (2.1-7.1x on burn, 11.2x on a FiveColour sample). At slack 4 and k=25 it
cuts at ~100x the prefix median, normally LOOSER than the frozen ceiling that follows -- which makes
the change **result-neutral**: a game it stops was already going to be dropped by `cost >= frozen` at
reduce time, so only the wall clock spent running it goes away.

Two details it needed:

* **A game CUT at a ceiling enters the calibration sample as THE CEILING**, not as the units it
  happened to hold when the abort landed. That number is timing-dependent (the limit is read on
  another thread, on a 4096-unit publish stride -- measured overshoots of ~2,800 units), and feeding
  it to a median would make the frozen ceiling machine-dependent. The substitution is exact rather
  than approximate: a cut game is at or above the limit and every uncut game is below it, so the
  ORDER is preserved and the median is identical to what true costs would give.
* **Both limits are re-derived at reduce time and OR'd.** A game cut at the provisional ceiling
  records a truncated cost that can sit BELOW the frozen ceiling (when the prefix median ran under
  the full one), so the frozen test alone would silently keep it on one machine and drop it on
  another. `recorded >= provisional` is true for every cut game everywhere.

Verified on burn (25 games, `abandon_calib` 10, `k` forced to 0.5 so the window actually cuts):
the abandoned set is exactly `{cost >= frozen} u {i >= 3 : cost >= provisional}` predicted from the
UNBOUNDED run's per-game costs, byte-identical across three runs at 2-7 threads while the freeze
POINT moved (9 completed/1 running vs 8/2). The window's worst game was cut at 24,584 units against
an unbounded cost of 95,493 -- 3.9x less work, in the one place nothing could previously stop it.

**What is left is one game per cell.** Games 0-2 have no prefix and still run unbounded, so
`max_game_sec` remains the backstop there. Shrinking that further would mean deriving a ceiling from
fewer than three games, which is a noise problem, not a mechanism problem.

## Gate

Engine change, so the standing gate applies -- smoke + regression -- plus a check that a condemned
row caps every cell at `reference_games` and that `[batch] metering ...` still reports.

It must land BEFORE or AFTER a matrix generation, never during, and for a sharper reason than the
usual `HEAD:src` fingerprint rule (`.claude/skills/value-leaf.md` Rule 0): condemnation decisions are
made from WALL-CLOCK s/game, so running the gate on a loaded box would inflate the live run's timings
and cause the very false condemnations this change removes. The rebuild also cannot replace
`build/Release/mtg` while that binary is executing.

Related: `docs/design/batch-drip-release.md` (the other deferred change to this same pool), and
`docs/design/fivecolour-depth-monotonicity.md` (the open V8-vs-H5 question this defect contaminates).
