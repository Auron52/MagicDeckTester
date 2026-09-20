# A per-game WALL-CLOCK backstop: the guard that was specified but never built

**Status: OPEN, not started. Found 2026-09-19 during the Fungus value-leaf phase C run.**

## The finding

**A game has no wall-clock bound of any kind.** The only thing that can stop a running game is the
per-game WORK ceiling, denominated in work units, and nothing in the abandonment path reads a clock:

```cpp
// src/ai/GameWorkMeter.h -- the whole abandonment mechanism
inline thread_local long long t_used, t_limit;      // UNITS. no deadline, no time_point.
inline void Add(long long n)
{
    t_used += n;
    if (t_limit > 0 && t_used >= t_limit) { t_abandoned = true; }   // units
    ...
    if (late > 0 && t_used >= late)       { t_abandoned = true; }   // also units (frozen cell ceiling)
}
```

Both `GameEngine::kAbandoned` sites (`GameEngine.cpp:110`, `:137`) read `gamework::Abandoned()`, and
that is the only route. There is no other.

**`max_game_sec` is NOT this guard, and mistaking it for one is the trap.** It is wall-clock, but:

* its response is to **condemn a CELL**, not to stop a game;
* it **stands down entirely** when the per-game work ceiling is armed. The log line says so in as
  many words -- *"a game has been running 3600.0 s (limit 3600.0) but the cell's per-game WORK
  ceiling is armed, so the game is bounded and the cell is NOT condemned."*

So on any cell with a work ceiling -- which is every cell the value-leaf matrix runs -- the wall-clock
limit is inert by construction, and "bounded" in that message means bounded in UNITS, which is not a
bound in time at all.

The runner's own design note records that a second wall-clock rule was CONSIDERED AND DECLINED:

> "the only thing that could react was `max_game_sec` -- a wall-clock rule whose response is to
> condemn the whole cell (one seed of the row, on one observation)... this closes the rest by
> starting to use a median EARLIER **instead of by adding another wall-clock rule**."

That reasoning is sound for the *calibration-window* hole it was written about. It does not cover the
case here, where the ceiling is armed and correct and the game still runs for hours.

## What it cost (Fungus, value-leaf phase C, 2026-09-19)

The four worst games of the run, all in the unbudgeted heuristic (H) cells:

| wall | units | ceiling | win turn | repro |
|---|---|---|---|---|
| **15,340,154 ms (4.26 h)** | 40,321,397 | 40,000,000 | `INT_MIN` | `--seed 10092 --game-index 82 --games 1` |
| 12,379,290 ms (3.44 h) | 40,017,268 | 40,000,000 | `INT_MIN` | `--seed 9049 --game-index 40 --games 1` |
| 9,508,956 ms (2.64 h) | 40,002,533 | 40,000,000 | `INT_MIN` | `--seed 8045 --game-index 37 --games 1` |
| 8,913,082 ms (2.48 h) | 40,002,541 | 40,000,000 | `INT_MIN` | `--seed 11089 --game-index 78 --games 1` |

Every one of them **ran to the unit ceiling and was then abandoned**, so `wt = INT_MIN` and the work
is discarded. The 4.26 h game produced nothing at all.

**The ceiling is not wrong in its own currency -- it is wrong in wall-clock terms.** USER 2026-09-19:
*"The ceiling is not meant to bound the game to 15 minutes. The goal was an hour."* At that intent,
40M units implies ~11k units/s. These games ran at ~2,600 units/s, so they overshot the intended
bound by **~4x in time while landing exactly on it in units**. The unit/wall ratio is not stable
across the H cells, and the ceiling has no way to notice.

## The requirement (USER, 2026-09-19)

> *"we didn't want to cap it at 1 hour in a non-deterministic way, but we did want to have a guard
> when it is clearly overshooting that target to prevent 4h games."*

Two bounds with different jobs, and the distinction is the whole design:

1. **The WORK ceiling stays the real bound.** Deterministic, reproducible, a function of the data --
   this is what decides which games are kept and which are dropped, and it must keep doing so.
2. **A LOOSE wall-clock backstop catches clear overshoot only.** Not a second opinion on the same
   question; a failsafe for when the unit/wall relationship has broken down badly enough that the
   first bound is no longer doing its job.

## The hazard that constrains the design

**An abandoned game enters the skip list that every other cell filters on**, and the depth matrix
depends on that list being a deterministic function of the data. From the matrix script: *"if H5
excludes game 380 and V6 keeps it, the row comparison breaks"* -- cross-cell and cross-arm
comparisons are per-game, so an exclusion has to apply identically everywhere.

A wall-clock trigger is **load-dependent by nature**: the same game may trip it on a loaded box and
not on an idle one, so the skip list stops being reproducible from the data alone. This is exactly
why the existing design refused a wall-clock rule, and any implementation has to answer it rather
than ignore it.

Three properties that together keep it acceptable:

* **Loose enough to be a non-event.** Sized so it only ever fires on games that no defensible
  ceiling would have kept -- the 4 h class above, not the 1-2 h class. If it fires with any
  regularity, the WORK ceiling is mis-set and that is the bug to fix instead.
* **Reported distinctly from a unit abandon**, so the two are auditable apart and a run that
  triggered it is obvious in the log rather than silently different from its rerun.
* **Never the primary.** If a game hits the wall backstop, the honest reading is *"the unit ceiling
  failed to bound this cell"*, and the backstop's log line should say so and name `abandon_k`.

## Sketch

`gamework::Add` is the natural site: it is already called once per simulated turn-step and already
carries a strided slow path (`kPublishStride = 4096`) for exactly this reason -- a clock read per
call would be far too expensive, and the worst game measured here would otherwise read it 614
million times. The existing stride is only armed for CALIBRATION games (`t_publish`), so the
deadline check needs its own counter to apply to every game.

```
Begin(limit_units, deadline)     // deadline 0 = disarmed, as limit 0 already means
Add(n): every kDeadlineStride units -> one steady_clock read; past deadline -> t_abandoned,
                                       with a flag distinguishing WALL from UNITS
```

Plumbing: a `max_game_wall_sec` alongside `abandon_units` in the manifest's `condemn` block, default
**off** so every existing manifest is byte-identical, and a distinct
`[goldfish] ABANDONED-WALL job=... elapsed=... units=... ceiling=...` line carrying the same
self-contained repro the unit path already emits.

## Open questions, deliberately not answered here

* **What value? USER 2026-09-19: *"I was personally thinking 3h+."*** That line catches the 4.26 h
  and 3.44 h games and leaves the 2.48-2.64 h class alone, which fits the observed distribution --
  it fires on the clear pathology and not on the merely slow. Confirm against
  `wall / cell-median-wall` on a second deck before hard-coding it.
* **Should it abandon, or downgrade?** An alternative is to let the game finish but mark the CELL as
  having a broken unit/wall ratio, which keeps the skip list deterministic at the cost of the hours.
  Abandoning is what the user asked for; this is recorded because it is the option that does not
  compromise reproducibility.
* **Is the real fix upstream?** These games reached a 40M-unit ceiling at ~2,600 units/s against a
  45-70k units/s norm elsewhere. A 20x collapse in the unit rate is itself a finding, and if it has
  a bounded cause then fixing THAT removes the need for a backstop. The unit rate should be
  characterised before the backstop's value is chosen -- see the FD_LEAF_DEPTH probe below.

## Related

* `MTG_MATRIX_FD_LEAF_DEPTH` (`scripts/attic/valueleaf_depth_matrix.py`) is the nearest precedent:
  on EDF, unbudgeted H cells were INFEASIBLE at the default leaf depth 1 (~98% of wall in the
  per-simulated-turn 1-ply lookahead, single H5 games >1.2 h), and depth 0 measured quality-neutral
  on 32 paired games. The script's comment is explicit that this is per-deck evidence only.
  **MEASURED AND REJECTED FOR FUNGUS 2026-09-19.** A paired single-thread probe on the 4.26 h game
  (`--seed 10092 --game-index 82`) ran both arms to 16 minutes with neither finishing, and `perf`
  profiles of the two arms are the SAME SHAPE -- so the leaf-depth lever does not transfer here and
  the EDF precedent should not be cited for this deck again.

## The profile: there is no single win to find (2026-09-19)

`perf record` on the 4.26 h game, both arms, ~155k samples each. The profile is FLAT -- the top
symbol is under 8%, and the cost is spread across state machinery rather than concentrated:

| symbol | leaf depth 1 | leaf depth 0 |
|---|---|---|
| `BuildSimKey` | 6.65% | 7.68% |
| `CardHasSubtype` | 3.10% | 2.86% |
| `CardDatabase::LookupCached` | 2.78% | 2.70% |
| `GameState::GameState` (copy) | 2.64% | 3.21% |
| `TapForCostSharedImpl` | 2.40% | 2.75% |
| `CollectActivationKeys` | **2.39%** | **2.97%** |
| `ApplySacForMana` | 2.33% | 2.11% |
| `FireEtbWatchers` + `FireCreatureEnterWatchers` + `OnCreatureDies` | ~6.4% | ~4.7% |
| `operator new` | -- | 2.11% |

So the honest answer to *"is there headroom on these crazy games?"* is **not in one place**. There is
no 10x sitting behind a single function; it is memo-key construction, whole-`GameState` copies,
string-keyed subtype tests in hot loops, watcher cascades, and allocation. Each is a few percent.

**`CollectActivationKeys` is a cost THIS SESSION added** (the breakpoint delta rule, 169bf491) and it
is already a top-3 symbol. The rule is still a clear net win -- 55% fewer breakpoint continuation
enums at identical play -- but the mechanism moved work from "arm a breakpoint" to "scan the
battlefield twice per apply", and that half is now measurable. The cheap fix is a param-level
pre-filter so a permanent with no activatable params is skipped before the per-ability tests, and a
`CardHasPostEntryActivation`-style superset predicate already exists to build it from.

The two watcher-cascade entries are the known Fungus signature (an ETB cascade walking the whole
board per enter, with DB-wide `CardDatabase::Has*()` gates that are always true), and `CardHasSubtype`
at ~3% is string comparison on a hot path -- both are pre-existing and both are worth more than the
leaf-depth lever that did not work.
* `docs/design/breakpoint-condemnation-status.md` -- the reach/yield/reinvestment ceiling analysis,
  same habit of bounding a fix's best case before building it.

## DEGENERATE-GAME CENSUS (Fungus, phase C partial @ 54%, 2026-09-20 00:15)

USER 2026-09-20: *"We'll keep track of all of the especially bad cases for an optimization run
later, since this level of degeneracy shouldn't exist even at unbounded."* This section is that
record. Refresh it with:

```
python3 scripts/slow_game_census.py logs/vlq_fungus/slow_games.log --top 20
```

`slow_games.log` is the only DURABLE source -- the batch heartbeat keeps just the top 100 by
duration and rolls over, so a long run destroys its own evidence. The census tool exists so the
evidence survives the run. NOTE it only sees COMPLETED games: a still-running monster appears in the
heartbeat but not here, which is why a heartbeat reading must not be quoted as a census fact.

### Headline

```
records 958   wall 156.6 core-hours   abandoned 102 (95.8 core-hours, DISCARDED)
```

**61% of all slow-game wall produces nothing.** Those 102 games ran to the 40M-unit ceiling, were
abandoned, and their results discarded. H4+H5 account for 121 of the 156.6 core-hours.

### Signature 1 -- the same GAME is degenerate at every depth

| repro | total | cells | depths seen |
|---|---|---|---|
| `--seed 8024 --game-index 16` | 6.94 h | 10 | H1 H2 H3 H4 H5 V4 V5 V6 V7 V8 |
| `--seed 10117 --game-index 107` | 7.07 h | 5 | H1 H2 H3 H4 H5 |
| `--seed 9175 --game-index 166` | 5.19 h | 9 | H1 H2 H3 H4 H5 V5 V6 V7 V8 |
| `--seed 10079 --game-index 69` | 7.02 h | 6 | H1 H2 H3 H4 H5 V8 |
| `--seed 9062 --game-index 53` | 13.32 h | 7 | H2 H3 H4 H5 V6 V7 V8 |
| `--seed 10092 --game-index 82` | 10.94 h | 6 | H5 V4 V5 V6 V7 V8 |

A game flagged slow at BOTH d1 and d5, and on BOTH arms, is not slow because of search depth or
because the value leaf is off. The cost is in the BOARD STATE.

**Read this precisely.** At H1 these games cost only ~0.04-0.33 h; the multi-hour figures are at
H4/H5. So depth is a MULTIPLIER, not the cause -- the state is expensive at every depth and deep
search compounds it. (An earlier note claiming hours at depth 1 came from a still-RUNNING heartbeat
entry and was wrong; corrected here.)

### Signature 2 -- unit-rate collapse

The per-game ceiling is denominated in work units; wall is what we actually pay. Over slow games the
median is **36,439 units/s**, and the worst cases run at:

| repro | units/s | vs median | cell |
|---|---|---|---|
| `--seed 10110 --game-index 100` | 844 | **0.02x** | H5 |
| `--seed 11014 --game-index 3` | 1,632 | 0.04x | H5 |
| `--seed 9062 --game-index 53` | 1,739 | 0.05x | H5 |
| `--seed 10081 --game-index 71` | 1,828 | 0.05x | H5 |

A game at 0.02x the median rate reaches a units ceiling 50x slower in wall time than the ceiling
assumed. That is the whole mechanism behind a 40M-unit ceiling permitting a 7-hour game, and it is
why a wall-clock backstop is needed IN ADDITION to (never instead of) the unit ceiling.

### Cost by cell

```
H5  79.4 core-h / 313 slow games      V7  7.3 / 68      H2  2.4 / 33
H4  41.6 core-h / 237 slow games      V6  6.4 / 65      V4  0.8 / 10
H3   9.0 core-h / 115 slow games      V8  5.9 / 69      H1  0.5 /  7
                                      V5  3.2 / 41
```

### The optimization target this produces

Profiling the worst game (`perf`, ~155k samples, both leaf-depth arms) shows a FLAT profile -- top
symbol under 8% -- so there is no single hotspot to remove. Ranked by expected value:

1. **The ETB/enter/dies watcher cascade (~5-6%)** -- the known Fungus signature, an ETB cascade
   walking the whole board per enter with DB-wide `CardDatabase::Has*()` gates that are always true.
2. **`CardHasSubtype` (~3%)** -- string comparison on a hot path.
3. **`BuildSimKey` (~7%)** -- memo-key construction, the single largest symbol.
4. **`CollectActivationKeys` (2.4-3.0%)** -- added by the breakpoint delta rule (169bf491). A
   param-level pre-filter (skip a permanent with no activatable params before the per-ability tests)
   should remove most of it; `CardHasPostEntryActivation` is the existing superset predicate.

But the flat profile also says the real win is probably NOT micro-optimisation: a game at 0.02x the
normal unit rate is doing something structurally different, and characterising WHAT (board size?
Saproling count? sac-outlet combinatorics?) should come before shaving percentages. The cheapest
handle is the repro list above -- every one is a single-game, single-thread command.

### Memory: 30.4 GiB is REAL, host paging is REFUTED, and allocator churn is the new lead

Investigated 2026-09-20 03:45 after the USER observed that Windows Process Explorer reported the WSL
VM using only ~25 GB working set while the guest reported ~30 GB RSS.

**The guest number is real.** The RSS decomposition leaves no innocent explanation:

```
VmRSS    31,866,884 kB  (30.4 GiB)      RssFile      9,780 kB  (9.6 MiB)
RssAnon  31,857,104 kB  <- all of it    RssShmem         0 kB
VmHWM    32,647,056 kB  (31.1 GiB)      VmSwap           0 kB
```

Not page cache, not shared memory, not file mappings -- ~30.4 GiB of anonymous heap in one process.
The host/guest gap is Windows-side accounting: Process Explorer's **Working Set** is only what is
resident in physical RAM, so a trimmed VM understates. The comparable column is `vmmem` **Commit
Size / Private Bytes**, not Working Set.

**HOST PAGING IS REFUTED as an explanation for the unit-rate collapse.** The hypothesis was
appealing -- Windows trimming VM pages would give the guest `VmSwap: 0` while every access to a
trimmed page cost a host fault, which is the shape of a game running at 0.02x the normal rate. It is
wrong:

```
mtg:    minflt 2,255,308,300    majflt 0        (over 12 h 25 m)
guest:  pswpout 2530 pages (~10 MB, across 4 days of uptime)
```

**Zero major faults.** A process being paged by the host takes host-backed faults; this one takes
none. The degeneracy is COMPUTATIONAL. Recorded so the direction is not re-explored.

**NEW LEAD from the same data: 2.26 BILLION minor faults, ~50k/s sustained for twelve hours.**
Minor faults are individually cheap, but that volume means the process continually touches
freshly-mapped pages -- the allocator is returning memory to the kernel and re-faulting it instead
of recycling it. Consistent with `operator new` at 2.11% in the profile, and with the standing note
that cross-plan caching is dead but ALLOCATION is not. There is an existing `MTG_POOL_ALLOC` switch
to A/B against it, and glibc's `M_TRIM_THRESHOLD` / `MALLOC_ARENA_MAX` are the other handles. This
is cheap to test and independent of the search work above.

**Also: RSS is no longer flat.** 27.7 GB at 15:00, 27.8 GB at 00:10, 30.4 GiB at 03:45 -- roughly
+0.7 GB/h over the last stretch, with box headroom down from 12 GB to ~8.9 GB available. An earlier
note in this session called it "flat, definitively not a leak" on the strength of the first two
readings; that was premature. Steady anonymous growth on a pooled batch whose games all complete is
itself a defect signature worth chasing alongside the allocator churn.

## THE DESIGN, REFINED (USER 2026-09-20): TWO STAGES, NOT ONE CAP

> *"my thought is to make it 2-stage. A check at hour 1 or 2 to see how our progress is. If we have
> a lot of work still to go, we cancel it. Then at hour 3 or 4 we have a hard cap."*

* **STAGE 1 -- PREDICTIVE CUT, at hour 1-2.** Extrapolate from the work done so far: at this game's
  observed unit rate, can it still reach its unit ceiling before the hard cap? If not, cut it now
  instead of spending two more hours proving it.
* **STAGE 2 -- HARD CAP, at hour 3-4.** Unconditional. Catches anything whose rate changed after the
  stage-1 check, so no game can run unbounded regardless of what the extrapolation believed.

### Why stage 1 is REPRODUCIBILITY-NEUTRAL, which resolves this doc's main objection

The hazard recorded above is that a wall-clock trigger is load-dependent, so the skip list -- which
every other cell filters on -- stops being a deterministic function of the data. **That objection
does not apply to stage 1 once stage 2 exists.**

Stage 1 only cuts a game that cannot reach its unit ceiling before the hard cap. Any such game is
one that stage 2 would have killed anyway. **Both stages produce the same ABANDONED verdict, so the
skip list is identical either way** -- stage 1 changes only how many hours were burned getting
there. The reproducibility question is therefore entirely about stage 2's threshold, and stage 1 is
free.

The one way stage 1 can be wrong is cutting a game that would have COMPLETED (not merely reached the
ceiling) -- a game whose rate was about to recover. So the extrapolation must be conservative: cut
only when the shortfall is not close, and prefer to let stage 2 do it when in doubt. Sizing stage 1
at hour 1-2 against a stage-2 cap at hour 3-4 builds that margin in -- the gap is the safety factor.

### Why the checkpoint shape is right for THIS failure

The observed pathology is not "a game that is slightly slow" but "a game running at 830-2,600
units/s against a system assumption of 900,000" -- 25x to 1000x off. A game that far below rate is
distinguishable from a healthy one within the first hour with enormous margin, which is exactly the
regime a predictive check works in. It does not need to be a subtle estimator.

Worked against the measured cases: a 40M ceiling at ~830 units/s implies ~13.4 h to reach it. At the
hour-2 checkpoint such a game has done ~6M of 40M units -- 15% -- and needs 11 more hours against a
3-4 h cap. Not a close call. The 13.42 h and 12.63 h games in this run would each have been cut at
hour 2, for the same final verdict.

### Implementation note

Both stages fit the `gamework::Add` strided-clock sketch above; stage 1 needs no new state beyond the
deadline, because `t_used` and `t_limit` are already the numerator and denominator of the
extrapolation. Report the two distinctly (`ABANDONED-PREDICT` vs `ABANDONED-WALL`) so a run's log
says which rule fired, and so a stage-1 cut that later proves wrong is findable.

### Status / sequencing (2026-09-20)

Deferred to Monday at USER direction: development budget is nearly exhausted and the box is held by
the in-flight Fungus value-leaf run until then. There is NO quick path -- `perf` over the worst game
is FLAT (top symbol under 8%), so this is real work rather than a one-line fix, and it is a `src/`
change that cannot be built while the generation holds the binary and the frozen `src` tree.

### The degeneracy is BIMODAL, not a tail -- and board growth is REFUTED as the driver

Cost of slow games grouped by win turn (958 records, this run):

```
win_turn   games   mean_sec
5            275      207.5
8             35      209.3      <- the LONGEST games are among the CHEAPEST
7            353      396.2
6            436      437.7
ABANDONED    136     3457.3      <- 17x every completed bucket
```

**A hypothesis worth recording because it is WRONG:** that cost comes from boards growing over the
course of a game (Saproling tokens x Doubling Season x Mycoloth devour), which would make the
per-simulated-turn-step cost scale with board size and explain the FLAT profile -- every symbol in it
(`BuildSimKey`, `GameState` copy, `CardHasSubtype`, `LookupCached`, the watcher cascades,
`CollectActivationKeys`) is per-permanent work. It predicts turn-8 games are the expensive ones.
**They are not** -- 209 s, level with turn-5 games. Game length does not predict cost.

**What the shape DOES say.** There is a 17x gap with nothing in between. A smooth cost driver gives
a smooth distribution; bimodality means something SWITCHES ON. So the target is a combinatorial
TRIGGER -- a board configuration where sac outlets, legal victims and devour choices are
simultaneously live and the plan enumerator's product blows up -- not a size threshold, and not a
hot function.

**Why this matters for how the next attempt is framed.** A flat profile is normally where an
optimisation effort concludes "no win available", and that is the likely reason repeated passes over
this deck have not converged. The flat profile is real but it is a CONSEQUENCE: if the expensive
mode is a state explosion, no symbol dominates because the work is spread across per-permanent
machinery. Do not start from the profile. Start from the classification question:

> What do the 136 ABANDONED games have on board that the ~1,100 merely-slow ones do not?

That is a supervised question with 136 positives, ~1,100 negatives, and a self-contained
single-game repro for every record, all in `slow_games.log` via `scripts/slow_game_census.py`.
It needs a board-state instrument at the point the unit rate collapses -- which is a `src/` change,
hence Monday.

Note also that `units` is ONE SIMULATED TURN-STEP, so 830 units/s against a 36,439 units/s median is
not "more steps taken" -- it is each step costing ~40x more. The collapse is in per-step cost, which
is the same statement as the state explosion above and is measurable without any new machinery:
units and wall are both already recorded per game.
