# A per-game WALL-CLOCK backstop: the guard that was specified but never built

**Status: BUILT AND SHIPPED 2026-09-20 (`b38a0633`), default off.** Found 2026-09-19 during the
Fungus value-leaf phase C run; specified here; implemented from this spec by the Snow session, whose
phase C had not yet started and so still had a window to land it in. See
[§ THE IMPLEMENTATION](#the-implementation-2026-09-20) at the foot of this file for what was built,
the one interaction this spec did not anticipate, and the verification.

**Everything above that section is the ORIGINAL SPEC and its evidence, preserved as written.** It is
still the authority on *why*; the implementation section is the authority on *what exists*.

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

**HOST PAGING: the refutation below was UNSOUND. Corrected 2026-09-20 09:40 -- the direction is
OPEN, not closed.** The original note read:

> *"**Zero major faults.** A process being paged by the host takes host-backed faults; this one
> takes none. The degeneracy is COMPUTATIONAL. Recorded so the direction is not re-explored."*
>
> ```
> mtg:    minflt 2,255,308,300    majflt 0        (over 12 h 25 m)
> guest:  pswpout 2530 pages (~10 MB, across 4 days of uptime)
> ```

**Guest fault counters cannot observe host paging, so `majflt 0` was never evidence either way.**
When Windows trims a guest-backing page out of the `vmmem` working set and the guest later touches
it, the fault is serviced *below* the guest by the hypervisor. From the guest page table's point of
view the page was resident the whole time, so `pgmajfault` does not increment. A guest can be paged
hard by its host while reporting zero major faults indefinitely. The counters above are consistent
with host paging, not exculpatory of it.

**And the condition demonstrably exists on this box.** 2026-09-20 09:25, with the USER reporting
Process Explorer figures alongside the guest's own:

```
Windows  vmmem Working Set  ~27.5 GB     <- guest pages RESIDENT in host RAM
Windows  vmmem Private Bytes ~50   GB     <- the VM's COMMIT (== guest MemTotal); never moves
guest    MemTotal            49,327,756 kB (50.5 GB)
guest    AnonPages           39,789,568 kB (37.9 GiB)   <- 10+ GB more than is resident
```

The guest is holding ~10 GB more anonymous memory than Windows keeps resident for the whole VM.
That gap is precisely the population of pages a guest access would fault back in invisibly.

Two accounting traps this closes, both of which read as reassurance and are not:
* **Private Bytes is the ceiling, not the usage.** `vmmem` commits the entire configured guest RAM
  up front, so it reads ~50 GB whether the guest is using 2 GB or 45 GB. It equals guest `MemTotal`.
* **A Working Set *below* Private Bytes is not headroom.** Here it means Windows is keeping less of
  the guest resident than the guest is actively using -- the opposite of slack.

**What still binds is the guest's `MemTotal`.** The in-guest OOM killer decides on that number
alone and cannot borrow the host's free RAM; if it fires it takes `mtg` (by far the largest RSS),
and Process Explorer will show a comfortable Working Set at that instant.

To actually settle host paging, measure from the HOST: Windows pagefile read rate, or
`vmmem` hard-fault delta, while a collapsed-rate game is in flight. No in-guest counter can do it.

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

## THE ARM SPLIT: the value leaf is ITSELF the optimization (2026-09-20 09:30, phase C @ 81%)

Census re-run at 1,481 SLOW-GAME records / 320.2 core-hours, this time SPLIT BY ARM. This is the
single most actionable number produced by the whole investigation, and it was invisible while the
census was read in aggregate.

Every depth-cell in the matrix holds the SAME allocated job count (832 jobs / 13 depths = 64 each),
so the columns below are directly comparable -- this is not a sampling artifact:

```
cell    slow-game cost   slow games          cell    slow-game cost   slow games
H5         143.4 core-h        490           V8          10.6 core-h        121
H4         100.8 core-h        404           V7           8.4 core-h         88
H3          38.6 core-h        193           V6           7.1 core-h         76
H2           6.1 core-h         42           V5           3.4 core-h         47
H1           0.8 core-h          9           V4           0.9 core-h         11
           -------------                                -------------
H TOTAL    289.7 core-h  (90.5%)             V TOTAL     30.4 core-h  (9.5%)
```

**The value leaf cuts the pathological tail ~13x at the deep end (H5 143.4 -> V8 10.6 core-h).**
The degeneracy this document exists to bound is overwhelmingly a property of the HEURISTIC horizon
rollout -- which is exactly the machinery the value leaf replaces with an O(1) evaluator.

### Unit-rate collapse is H-EXCLUSIVE, which identifies the mechanism

```
arm      n      p05     median      p95        min      below 5k units/s
H     1138    3,885     27,914   52,149        318      74/1138  (6.5%)
V      345   29,299     44,322   59,870      1,660       2/345   (0.6%)
```

The V distribution is tight. The H distribution has a collapsing low tail -- **7.5x apart at p05**.
Not one V cell appears in the worst-12 unit-rate table. The collapse is not a property of the deck;
it is a property of the deck RUN THROUGH THE HEURISTIC ROLLOUT.

### Root cause: the UNIT IS THE WRONG DENOMINATOR

`src/ai/SearchBudget.h:29-32`, emphasis added:

> *"Calibrated work-units per virtual millisecond. **One unit == one simulated turn-step in a
> rollout.**"* -- `NODES_PER_VIRTUAL_MS = 900`, and the constant is *"calibrated so a ~200 virtual-ms
> budget is comfortably adequate **on the reference deck**"*.

There is exactly ONE producer of units in the entire engine -- `SearchBudget::Consume()` at
`SearchBudget.h:57`, reached via `ConsumeAt` (`TurnSolver.cpp:1012`) and the greedy charge
(`TurnSolver.cpp:20763`). Every other `gamework::` reference is a reader.

So a unit is a **turn-step**, and the 900 constant bakes in the assumption that a turn-step costs
roughly the same everywhere. On Fungus it does not: per-step plan enumeration is combinatorial in
board width, so one turn-step at a wide Saproling board costs orders of magnitude more than one on
the reference deck. The game therefore burns enormous wall while ticking very few units.

**This is the precise mechanism behind this document's founding complaint.** A 40M-unit ceiling
intended to bound roughly an hour permitted 15.75 h, and every budget on this deck is ~25x off, for
one reason: *the ceiling is denominated in a currency that does not track the cost it is meant to
bound.* It is not a missing instrumentation site and not a mis-set constant -- it is the wrong unit.

### Consequences for the fix, in priority order

1. **Adopt the value leaf if phase E permits it.** It is independently the largest available win on
   this deck's pathology (~13x on the deep tail), on top of whatever quality case phase E makes.
   The tail this doc was written about largely belongs to the arm the value leaf removes.
2. **Ship the USER's two-stage wall cap anyway.** It is the only fix here that is
   reproducibility-neutral (see "Why stage 1 is REPRODUCIBILITY-NEUTRAL" above). It bounds the
   damage without touching units, so it is safe to land independently of everything else.
3. **Re-denominating the unit is a POST-ADOPTION project, and a large one.** Charging units in
   proportion to enumeration work would fix the ceiling properly -- and would change unit counts,
   hence budgets, hence play, hence **every generated artifact in the repo**. It cannot be slipped
   in alongside an adoption. Treat it as its own frozen-commit effort.

### The instrument already exists -- do NOT build one

A previous note here called for a board-state instrument (a `src/` change) to find the combinatorial
trigger. **That is not needed for the unit question.** `MTG_ROLLOUT_STATS` already gates a 13-site
per-`Consume()` attribution table (`TurnSolver.cpp:885`, `namespace unitsite`) covering
`rollout_step`, the five `FullSearchLine` loops, the four `SolveWithLookahead` loops, `esc_eval`,
`fs_bp_node` and `fs_m2_wave` -- and per its own comment the buckets *"sum EXACTLY to the units
cost.py reports"*. It is counters-only, no behaviour change, no play change, and it is already in
the frozen binary. Attribution therefore needs **no rebuild**, which matters while a generation run
holds the freeze.

**Cheap repro for it.** The census yields degenerate games at low absolute cost -- the pathology
does not require an expensive game. `--seed 8299 --game-index 291` at H2 shows a 0.03x unit-rate
collapse (848 units/s) in **108 seconds**, versus 14.77 h for `--seed 10092 --game-index 82` at H3.
Probe the cheap one:

```
MTG_ROLLOUT_STATS=1 MTG_VALUE_MODEL=0 build/Release/mtg decks/Fungus/Fungus.cod \
  --profile decks/Fungus/Fungus.profile.json --ignore-play-profile \
  --depth 2 --budget-ms 0 --max-turns 8 --seed 8299 --game-index 291 --games 1 --threads 1
```

(Run it `nice -n 19` if a generation batch owns the box: the attribution is a COUNT, so CPU
starvation stretches the wall without distorting the measurement.)

## LAZY-LEAF EVALUATION (USER 2026-09-20): defer the leaf until the exact search completes

> *"use no leaf until we have fully searched up to our depth and only use the leaf at that point.
> This way we wouldn't have to pay for any rollouts (or even value-leaf) if we found our win in the
> search window."*

Recorded here because it is the best-fitting optimization yet proposed for this deck's measured
cost shape, and because **most of it is already built and measured elsewhere in the repo.**

### It is NOT speculative -- EDF already shipped the restricted form, at 2.57x-13.4x

`docs/design/analysis-EldraziDisplacerFlicker.md` (~L4084-4117), the `MTG_LABEL_GOFF` cuts:

* **Cut 2 -- FLOOR SHORT-CIRCUIT**: a combat-pre-pass win at the floor skips the ladder outright.
* **Cut 3 -- IN-PASS FIRST-WIN BREAK**: every pass-dd win sits at one shared horizon edge, so the
  first settles the minimum; stop the pass there.

The stated insight is the USER's insight: *"a win on the current turn is that number's unbeatable
floor -- yet the ladder ran the full FSLineTail sweep over every candidate even after the floor was
achieved. The horizon-edge plan explosion (95.3% of all enumerated plans) lived exactly in those
provably-pointless sweeps."* Measured 2.57x on the yardstick, 13.4x on the worst game, with play
digests and label rows verified IDENTICAL.

**Same cost shape as Fungus**: EDF 95.3% of enumerated plans at the horizon edge; Fungus 99.7% of
work at horizon-edge nodes (`fungus-value-leaf-status`). A cut that removes horizon-edge work is
aimed at ~all of this deck's cost.

### What is NEW in the USER's version, and why it is the SAFER generalization

The EDF cuts are **gated to the `earliest_only` label path so budgeted play is structurally
untouched** -- they do not run in play, and phase C is *unbudgeted play*. So they do not currently
touch the 289.7 core-h of H-arm cost measured in the arm-split section above.

The USER's formulation differs in a way that makes generalising it tractable:

* EDF's cuts **stop early** on the first win. Sound only where the objective is `report.earliest`
  ALONE, which is true on the label path and not obviously true in play (play commits a LINE and
  may tie-break among equal-turn wins).
* The USER's version **defers** rather than stops: complete the exact depth-D search, THEN evaluate
  only the leaves that can still matter. Exactness inside the window is fully preserved, so
  tie-breaking among exact lines is untouched.

**The skip predicate needs no estimate and no new soundness argument**: a leaf at turn `t` cannot
win before turn `t`, so an exact win at turn `T` retires every leaf at turn `>= T` outright. (The
existing winless certificate could retire more, but is not required for the basic cut.)

### Cost/benefit shape is unusually favourable

Worst case is one extra walk of the INTERIOR tree, which on this deck is ~0.3% of the work (99.7%
being horizon-edge). Best case removes nearly all horizon work. And the benefit GROWS WITH DEPTH --
deeper search finds more in-window wins -- while the cost is concentrated at depth: H5 143.4 core-h
+ H4 100.8 = 76% of all H-arm slow-game cost.

### THE TRAP: a lossless cost cut is NOT play-neutral under a BUDGET

Budgets are denominated in units (`SearchBudget::Consume` is the sole producer). Consuming fewer
units per node therefore buys MORE search for the same budget, and play changes -- even though the
cut is lossless. This is almost certainly why EDF confined its cuts to the unbudgeted label path.

Consequence for sequencing:
* **UNBOUNDED regimes (phase C matrix, label generation) are play-neutral** under this cut: same
  tree, same answer, less work. That is the natural first target, and it is the regime this deck's
  degeneracy lives in.
* **Budgeted play needs the change to be paired with a budget recalibration**, or gated off, or
  accepted as an artifact-invalidating change. Do not assume "lossless" implies "safe to adopt in
  play" -- it does not.

### Relationship to the unit-denominator finding above

These are the same defect seen from two ends. The unit undercounts horizon-edge work (so the
ceiling cannot bound it); the lazy leaf removes horizon-edge work (so there is less to bound).
Doing the lazy leaf FIRST is strictly better sequencing: it shrinks the very population that makes
the denominator wrong, and unlike re-denominating, it does not invalidate artifacts in the
unbounded regimes.

### Status

Proposed by USER 2026-09-20 while phase C was at 81%. Another agent picked it up the same day; this
section exists so that agent inherits the EDF prior art, the label-path restriction, and the
budget-neutrality trap without re-deriving them. NOT implemented here.

### VERIFIED 2026-09-20: which phases actually get the EDF cuts (frozen binary ac0f1191c357)

USER asked whether the cuts are live for the value leaf. Traced rather than assumed; the answer
splits by phase, and the split is the whole point.

**Phase A (label generation) -- ON.** The gate is
`AIEngine.cpp:475: earliest_only = s_value_label_bnb && s_value_rows_path && !s_eval_rows_path`:

* `MTG_VALUE_LABEL_BNB` defaults **true** (`AIEngine.cpp:262`).
* `scripts/valueleaf.sh:676` sets `MTG_DUMP_VALUE_ROWS` and does **not** set `MTG_DUMP_EVAL_ROWS`.
* `MTG_LABEL_GOFF` defaults **true** (`TurnSolver.cpp:37167`).

=> `earliest_only == true`, so cuts 1-4 fire during phase A. Note this is the exact condition the
comment at `AIEngine.cpp:472-474` warns about: *"With BOTH dumps on, the eval rows win and we pay
the unpruned price."* Phase A gets it right by setting only the value-rows dump.

**Phase C (the depth matrix) -- STRUCTURALLY ABSENT, not merely off.** `EnumerateEarliestWins` has
exactly two callers in the tree:

* `AIEngine.cpp:477` -- the label path (above).
* `AIEngine.cpp:2188` -- guarded by `s_dump_ewins` (`MTG_DUMP_EWINS`), a diagnostic dump.

Neither is play. Play searches via `SolveWithLookahead` / `FullSearchLine`, which contain none of
this machinery, so **there is no flag that would enable the cuts in phase C.**

**Why this matters.** Phase A of this run took ~2 h (13:24 -> 15:19). Phase C has run 18 h+ and owns
the 289.7 core-h of H-arm cost in the arm-split section. So the cuts are enabled on the CHEAP phase
and unavailable on the EXPENSIVE one. That gap is precisely what the USER's lazy-leaf proposal
closes, and it is an argument for porting the idea to the play search rather than extending the
label path further.
---

## THE IMPLEMENTATION (2026-09-20)

Built from the spec above by the **Snow** value-leaf session, at the Fungus session's request. The
division of labour is worth recording because it is why this landed at all: the Fungus run was far
enough into phase C that a `src/` change could not be fitted around it, while Snow's phase C had not
started. The spec travelled; the code was written against it.

Commit `b38a0633`. **Default off everywhere** -- no clock is ever read unless a manifest or env var
arms it, so every existing manifest is byte-identical.

### What exists

| piece | where |
|---|---|
| the meter, both stages | `src/ai/GameWorkMeter.h` -- `ArmDeadline`, `CheckDeadline`, `Cause`, `Elapsed` |
| arming + reporting | `src/runner/BatchRunner.cpp` -- `CondemnRule::max_game_{predict,wall}_sec` |
| manifest keys | `condemn.max_game_predict_sec`, `condemn.max_game_wall_sec` (seconds, 0 = off) |
| env override | `MTG_MAX_GAME_PREDICT_SEC`, `MTG_MAX_GAME_WALL_SEC` (0 forces a stage OFF) |
| driver flags | `--max-game-predict-sec`, `--max-game-wall-sec` (valueleaf_depth_matrix.py) |
| unit cover | `test/unit/test_game_work_meter.cpp`, 12 cases |

**The values, USER 2026-09-20:** *"I'm thinking 1.5-2 hours check (and stop if we have a lot of work
left) and 3-4 hours hard cap."* `valueleaf.sh` phase C ships **2 h predictive / 3.5 h hard**, the
middle of each stated range. The 1.75x gap between them is stage 1's safety margin.

`kPredictSafety = 1.5`: stage 1 cuts only when the projection misses the hard cap by half again,
not merely by any amount. Deliberately generous -- the games this exists for project at 3-4x the cap
(a 40M ceiling at 830 units/s implies 13.4 h against 3.5 h), so the margin is free on the real
pathology while leaving a merely-slow game for stage 2 to judge on its own evidence.

`kClockStride = 4096` units between clock reads, matching `kPublishStride`. That is ~5 s of
granularity even at the 830 units/s pathological rate and ~0.1 s at a healthy one -- nothing against
a bound denominated in hours, and it answers the spec's own concern that a per-call clock read would
cost the worst game 614 million of them.

### The interaction this spec did not anticipate, and it would have silently defeated the cap

`max_game_sec`'s in-flight rule has a **`kHardOverrunFactor = 3.0` backstop of its own** that this
document's analysis missed: past 3x `max_game_sec` it condemns the cell *even when the work ceiling
is armed*, on the reasoning that "losing one cell is a bad outcome; a run that cannot terminate is a
worse one". At the shipped `max_game_sec` of 3600 s that fires at **3 hours** -- so it would have
thrown away the rest of the cell at 3 h, half an hour before a 3.5 h wall cap abandoned the single
game that deserved it. The cap would have looked armed and never had the chance to act.

The fix is not a re-sizing, it is a premise change. That branch exists *only* because a unit ceiling
does not bound wall clock; where a wall cap is armed, it does. So `bounded` now includes
`max_game_wall_sec > 0`, and the hard-overrun branch stands down under it. **The backstop REPLACES
the cell-condemning compromise rather than racing it** -- which is strictly better than what the
compromise bought: the cell keeps its work and only the pathological game is lost.

### What did NOT need changing, and why that is load-bearing

**The skip list is cause-agnostic.** It is built from `BatchJobResult::abandoned` (written from
`abandoned_at`), not by parsing the stderr line, so a wall-cut game enters it exactly as a unit-cut
game does and the matrix driver needed no change at all. Every cell filters on the same list however
the verdict was reached -- which is the property the spec's "hazard that constrains the design"
section is about.

**Reported apart even so**, as the spec asks: `ABANDONED-WALL` / `ABANDONED-PREDICT` against plain
`ABANDONED`, each carrying `cause=`, `elapsed=` and a derived units/s, plus a second line naming
`abandon_k`. Three things ride on the distinction -- a wall-cut run is not reproducible from its data
so an A/B against it is not like-for-like; the backstop firing *at all* means the unit ceiling failed
to bound that cell, which is the bug rather than the cap; and a stage-1 cut is the one that can be
wrong about a game, so it has to be findable afterwards.

### Calibration-sample handling

A wall-cut game inside the calibration window enters `cc.sample` at its own `g_units` (the existing
`min(g_units, ceiling)` leaves it alone, since it is by construction below the ceiling it never
reached). That is a censored observation recorded at its censoring point: it orders correctly against
every uncut game, so the median is unmoved unless half the sample was cut. The residual is that
*which* games get cut is load-dependent, so a ceiling frozen through a wall cut is not
bit-reproducible -- the known price of stage 2, and the reason the cap is sized as a non-event.

The backstop applies to calibration games deliberately. The spec's own worst case (FiveColour V5
game 6: 82.3% of its cell's total cost, *inside* the window where no ceiling could reach it) is
exactly the place exempting them would leave open.

### Verification

* **12 doctest cases** (`test/unit/test_game_work_meter.cpp`) over both firing directions, the
  no-ceiling case (stage 1 must decline rather than invent a prediction), the false-cut guard, the
  once-only evaluation, and deadline leakage across games on a pooled worker thread.
* **End-to-end on a real Snow H5 game.** Stage 1 cut at 20.1 s: 684,131 units against a 1e11 ceiling
  projects to ~813 hours. Stage 2 cut at 45.1 s with `limit=0` -- the ceiling disarmed entirely,
  i.e. the `skip_capped` case nothing in the engine could previously reach.
* **Play byte-identical with the cap unarmed**: 104 games over 4 configs (snow d3/b10, d5/b20, d0;
  stompysurprise d3/b10), every play digest equal to the pre-change binary.

### Two corrections to this document's optimization section, from reading the Snow tree

* **`CollectActivationKeys` (target #4) does not exist in this repository.** Neither the symbol nor
  commit `169bf491` is reachable from `origin/phase-1-2-deck-analyzer` -- that work is still
  unpushed, so the "cheap fix, `CardHasPostEntryActivation` already exists" item cannot be actioned
  by anyone but its author. Worth pushing if it is wanted from elsewhere.
* **`CardHasSubtype` (target #2) is already half-fixed.** `SpellEffects.h:4366` takes a
  `std::string_view` and allocates nothing, and `CardHasSubtypeId` gives an interned-id form for hot
  sites -- both landed 2026-09-17 off the *same* Fungus profiling that found `CountControlledDragons`
  at 45.1% of a game. The remaining ~3% is the up-to-four `std::string == string_view` compares, so
  the actionable item is narrower than stated: migrate the hot call sites to `CardHasSubtypeId`.

## PER-SITE UNIT ATTRIBUTION of a degenerate game (2026-09-20 10:15, frozen binary)

The arm-split section above ends with "the instrument already exists -- do NOT build one". This
section is that instrument's output. `MTG_ROLLOUT_STATS=1` on the census's worst game
(`--seed 8299 --game-index 291`, H2, `--budget-ms 0 --max-turns 8`, single-threaded, `nice -n 19`).
Log: `logs/unitsite/h2_s8299_gi291.log`.

Identity check first, because the SLOW-GAME repro line prints the LOOP index, not the base offset:
the probe reported `gi=0`, which looks like the wrong game. It is not. The census records the same
game at H2 as `wt=7 units=86081`; the probe returned `wt=7 units=92047` (7% apart, explained by
`--max-turns 8` and `--ignore-play-profile`). A different game would differ by orders of magnitude,
since the median Fungus game never crosses the 30 s SLOW-GAME threshold at all.

### The same game across the H ladder (from the census -- exact)

| cell | wall | units | units/s |
|------|------|-------|---------|
| H2 | 101.5 s | 86,081 | 848 |
| H3 | 216.7 s | 192,752 | 889 |
| H4 | 1,874 s | 988,598 | 528 |
| H5 | 3,485 s | 1,761,045 | 505 |

**One game, 1.58 core-hours across four cells.** Note the rate degrades only ~1.7x from d2 to d5 --
so WITHIN this game units track wall tolerably. The collapse documented earlier is a BETWEEN-game
effect, which matters for how the fix is framed: a per-game ceiling in units mis-ranks *games*, not
*depths*.

### Where the units actually go (exact, sums to 100%)

| site | units | share | what it counts (`TurnSolver.cpp:889-896`) |
|------|-------|-------|-------------------------------------------|
| `la_cand` | 31,891 | 34.6% | `SolveWithLookahead`: the root candidate loop |
| `rollout_step` | 30,227 | 32.8% | `SimulateToEnd`: one simulated turn-step (the leaf) |
| `greedy_fallback` | 29,121 | 31.6% | `SolveWithLookahead`: depth<=0 greedy fallback |
| `fs_pre` | 808 | 0.9% | `FullSearchLine`: main pre-combat plan loop |

**`SearchBudget.h:29-30` says "One unit == one simulated turn-step in a rollout". That describes
`kRolloutStep` and nothing else -- 32.8% of this game's budget.** The other 67.2% is candidate
scoring and greedy-fallback probes, which are not turn-steps and do not cost what a turn-step costs.
The comment is not a small documentation slip: it is the assumption the per-game ceiling inherits.

Two exact identities pin the sites to concrete operations:

* `la_cand` (31,891) == `cand_scored` (31,891). One unit per candidate scored.
* `greedy_fallback` (29,121) == solve-memo lookups (`hits 679 + misses 28,442`). One unit per
  greedy-fallback memo probe.

### The whole game is inside the heuristic ladder's COMMIT pass

```
heuristic-ladder totals: decisions=6 warm=8862 commit=83185 warm_share_of_ladder=0.0963
```

8,862 + 83,185 = 92,047 = `units_total`. **Every unit in this game is ladder work, and 90.4% of it
is the commit pass over just SIX decisions** -- 13,864 units per decision. That figure is the
per-game reflection of the 90.5% H-arm share measured across the whole matrix; the two were derived
independently and agree.

### The memos are not amortizing, and that is a lead

| memo | hits | misses | hit rate |
|------|------|--------|----------|
| solve-memo | 679 | 28,442 | **2.3%** (`clears=1`) |
| enum-memo | 48 | 1,104 | **4.2%** |
| leaf-TT (commit pass) | -- | -- | **23.9%**, 5,052 lookups/decision |

A 2.3% hit rate means the solve memo pays 29,121 hash-and-store operations to avoid 679 solves.
`clears=1` says the table hit its 16,384-entry cap and was wiped mid-game, so a large share of those
stores were evicted before any read. This also names a source for the allocator-churn lead in the
memory section above: ~28k inserts per game that are never read back.

**Do not reach for `MTG_BIG_SOLVE_MEMO`.** `TurnSolver.cpp:18478-18479` already records the
measurement: on the "g88 monster" class, 262144 -> 13.2 s, 16384 -> 3.7 s, 4096 -> **2.6 s**, all
the same T4 win. Bigger was worse; the 16384 default is deliberately ~4x off the monster class. The
indicated direction for a degenerate board is therefore a SMALLER cap via `MTG_SOLVE_MEMO_CAP`
(a plain `EnvInt`, `TurnSolver.cpp:20356`), not a bigger one.

### Hypothesis, NOT yet measured: the wall lives in enumeration, which is charged nothing

1,104 enum-memo misses produced 31,891 scored candidates -- ~29 candidates per enumeration. Plan
enumeration is combinatorial in board width, but **no unit site charges for enumeration itself**;
the budget charges its CONSUMERS (candidates scored). On a narrow reference board, candidate count
is a fair proxy for enumeration cost, which is why the calibration holds there. On a wide Fungus
board the enumerator's internal work grows while the post-dedup candidate count stays bounded, so
the proxy decouples -- in the direction observed.

This is arithmetic consistency, not a measurement. To settle it, time the enumerator directly
(a wall-per-site instrument) and compare enumeration wall against `la_cand` units. That needs a
rebuild and therefore must wait for the freeze to lift.

### What is actionable without a rebuild

The unbudgeted regime is play-neutral (a cheaper node cannot buy extra search when there is no
budget to re-spend -- see the lazy-leaf trap section), so cache-shape levers can be A/B'd on the
frozen binary and verified play-identical by `units_total` and win turn. Sweep in flight:
`MTG_SOLVE_MEMO_CAP` in {1024, 4096, 16384, 65536} on this game, `logs/memocap/`.
