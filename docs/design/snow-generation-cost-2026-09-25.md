# Snow: how far the two generations are from an overnight window (2026-09-25)

Answers one question the user asked at 13:02 UTC -- *"how far are we off generating either the
value-leaf or mulligan profile in a reasonable period of time?"* -- and records the state of the
mulligan run started in reply to *"end your session with running whichever is cheaper."*

The short answer: **the mulligan profile is the cheaper of the two by a wide margin and is the one
now running; it projects ~24 h (range 20-40 h) against the ~8 h overnight window, so it is 3-5x
over.** The value leaf is 40-80 h *and* its model has already been measured as a non-adoptable
trade, so it is the more expensive run for the weaker prize. Neither is close to a single night,
but the mulligan is resumable and has repo precedent at 45.7 h.

## 1. Why the mulligan is the cheaper run, in numbers

| | value leaf | mulligan profile |
|---|---|---|
| cost as configured | **40-80 h** (phase C; `snow-valueleaf-run-2026-09-22.md` s4) | **~24 h, range 20-40 h** (below) |
| work already banked | 3 chunks of 832 (51 of 20,800 games) | **82.3% of the R=2 floor**, journal preserved |
| blocking defect | phase C pool died twice on the RSS watchdog; driver fix written, **never run** | none; resumed cleanly today |
| would the artifact be adopted? | **No on present evidence** -- phase E measured the staged model at **-0.0051 turns for 1.25x core-s**, a TRADE, and the 2026-09-21 shape probe found Snow plays fine leafless (`fit_nl` beat the heuristic control at 1.16x units) | unknown, and it is the deck's one untested mulligan |

That last row is the decisive one and it is worth stating plainly: the value leaf is not merely the
more expensive run, it is the more expensive run for an artifact this repo's standing bar would
refuse. `leaf:none` with a measured `mull_gen_depth` -- the Stompy / Goblins precedent -- is already
the live plan, and it is exactly what makes the mulligan the remaining piece of work.

## 2. The mulligan projection, and how it was derived

The 03:37-06:36 run (stopped at 2.92 h to free the box) is the measurement.

* **Scale.** `continuous size-7: 175972 cells` x 2 sides = **351,944 cell-sides**, floor R=2, cap
  R=30, plus **162,004 fused sub-table batches** (= the 81,002 size-1..6 cells x 2 sides exactly).
* **Rate.** 564,124 size-7 rollouts in 10,501 s = **53.7/s**; the resumed run reads 58-63/s. Snow is
  the slowest deck in the repo per rollout -- Angels ran at 1,482/s, CritterLifegain 910/s.
* **Floor.** 351,944 x 2 = 703,888 rollouts, of which 579,316 were banked (289,658 cell-sides
  reloaded) -> the floor completes ~33 min into the resumed run. The sub-table floor is another
  162,004 x 2 = 324,008. Both floors together: ~4.8 h from scratch, ~2 h from here.
* **Refine is the unknown, and Melira Pod bounds it.** Melira's gen is the one COMPLETED 100%-frozen
  run of comparable scale in `logs/`: 788,081 cells / 1,576,162 cell-sides, final
  `roll7=16,076,090 rollsub=7,556,638` in 164,419 s (**45.67 h**). That is **10.2 size-7 rollouts per
  cell-side** against a floor of 2 -- i.e. adaptive refine costs ~5x the floor, nowhere near the
  cap-30 worst case.
* **Applied to Snow:** 351,944 x 10.2 = 3.59M size-7 + ~1.7M sub = **~5.3M rollouts**. Less the
  ~0.6M banked, at 60/s, that is **~22 h**.
* **Why the top of the range is real.** Melira's *average* rate was 143.7/s but its late-refine rate
  was 78/s -- 0.54x -- because the cheap cells freeze first and the expensive ones are what is left.
  If Snow decays the same way its tail runs nearer 32/s, which puts the total at ~36-40 h.

So: **20-40 h, best estimate ~24 h.** Against the ~8 h `MTG_KEEP_OVERNIGHT_H` window that is 3-5x
over -- but Melira Pod's shipped profile took 45.67 h, so this is a two-or-three-night job, not an
intractable one. It journals continuously and resumes, which is what makes that viable.

The earlier **~138 h** figure in `Snow.value.json`'s note is not in conflict: it was the projection
at the *mis-configured* d5/b20 labeller settings, before the 2026-09-25 sweep derived d2/b1 (2.87x
cheaper). 138 / 2.87 = 48 h, and the Melira-calibrated escalation ratio brings that down further
because a scout's projection assumes more escalation than adaptive refine actually performs.

## 3. State of the run now in flight

Launched 13:09:57 UTC via `logs/snowopt/mullgen_overnight.sh` -> `scripts/mullgen.sh run decks/Snow
fast`, PID **3827293**, log `logs/Snow_mullgen/gen.log` (the stopped run's log is preserved at
`logs/snowopt/gen_0337_preserved.log`; the new run appends).

* **The journal resumed:** `RESUME(journal): reloaded 289658 cell-sides ... -> continuing`. This is
  also a *proof* worth keeping: the resume gate is `PlayIdentityAllows(play_digest, ...)`
  (`ExhaustiveKeep.cpp:2736-2742`), so a clean resume means today's three `src` commits -- `3d28578d`
  (frontier dead ends, all default-OFF), `afc34763` (untracking a `.orig`), `e7b6c6e0` (the viewer
  sacrifice fix, which touches `SpellEffects.h`) -- are **play-neutral on this deck**. Rebuilt on
  HEAD (`src` tree `df5b7c7e`) at 13:05 before launching.
* **Recipe must stay `fast`.** R=30 is part of the journal fingerprint, so `complete` (R=40) would
  silently discard the banked floor.
* **Utilisation checked at 3m54s: 2,951% of 3,200% = 92.2% of the box.** Well clear of the
  starved-pool failure mode.
* **Resume safety verified before launching:** the "fused sub-table batches were never fed on a
  journal resume" defect (`sub=0/142464`, shipped on Dragons and Mirrorwing) was fixed 2026-09-01 --
  `ExhaustiveKeep.cpp:3900`, "RESUME FIX ... must be drained in the REFINE phase" -- and
  `mullgen.sh`'s artifact check hard-fails an under-sampled sub-table regardless, before any games
  are played. **CONFIRMED IN FLIGHT at 13:41 UTC:** the 1800 s monitor reads
  `rollsub=15298 (50/s) sub=7361/162004 (4.5%, 24.5/s)` -- the fused batches are draining on a
  resumed journal, so the Dragons/Mirrorwing failure mode is not in play here.
* **The size-7 floor closed exactly where the arithmetic said it would.** 703,888 floor rollouts
  minus the 579,316 banked leaves 124,572, and the 1800 s monitor reads `roll7=124572` with the phase
  moved on to the sub-table. That is a clean check on the projection's denominator. The sub floor
  (162,004 cell-sides at 24.5/s) lands ~1.8 h in, i.e. ~15:30 UTC, and refine holds the rest.
* **Rate is running ABOVE the figure section 2 projects from:** 75-84/s against the 03:37 run's
  53.7/s, which pushes the estimate toward the low end of the 20-40 h range. Do not bank that -- the
  refine tail is where the rate decays, and the floor pass is the cheap part by construction.
* **It will not finish in one night.** Expect the floor complete, the sub-table floor down and
  refine well under way by morning. `bash scripts/mullgen.sh status decks/Snow` reports progress;
  re-running `run` resumes. `mullgen.sh run` validates and gates automatically when gen completes,
  so nothing ships un-A/B'd.

## 4. Why no engine optimization was landed first

The user's message invited it (*"feel free to optimize more and then end your session with running
whichever is cheaper"*), and the two are **mutually exclusive on one box tonight**, in two ways:

1. Generation stages run **alone** on the box (CLAUDE.md), so optimizing *while* generating is not
   available; and
2. any change that moves the play digest **voids the journal** -- the 82.3% banked floor -- because
   the resume gate keys on it. An engine change landed at 13:05 would have cost 2.9 h of banked work
   plus the ~3.6 h floor re-run, against a lever whose best measured value on *completing* games
   (which is all a labeller rollout ever is) is `MTG_FOLD_SEARCH_ODO`'s 0.885x.

The run is the deliverable, so the run won. `MTG_FOLD_SEARCH_ODO` is the lever that would actually
help this workload -- every labeller rollout is a completing game, which is the population it was
measured 0.885x on -- but it is a **reserved user decision** and arming it unilaterally is the
mistake that got an adoption reverted on 2026-09-04. It is also unverified at d2/b1 specifically: it
is lossless at the settings it was tested at (32/32 digests identical, 0 unrecoverable over 1.05B
rejections), and a units-budgeted labeller is precisely where "lossless" needs re-proving before
being trusted, since the budget is denominated in the units the flag removes.

## 4b. `frozen 0.0%` at six hours is the EXPECTED shape here -- do not kill on it

At 19:13 UTC (6.0 h in) the monitor reads:

```
monitor: 21602s  phase=floor  roll7=125596 (0/s)  rollsub=1418968 (92/s)
         frozen=0/351944 (0.0%)  sub=162004/162004 (100.0%, 0.0/s)  subwave=0x126499
```

Every number that looks bad there is correct:

* **`frozen 0.0%` because size-7 refine has not started.** Cells freeze in the refine phase, and
  refine is LAST -- after the size-7 floor, the sub-table floor, and the adaptive sub-refine. There is
  no partial-credit readout before it.
* **`roll7` flat at 125,596 because the size-7 floor is DONE** (124,572 was the predicted remainder)
  and the speculation filler has saturated. All 32 cores are on sub-refine rollouts.
* **`sub 100%` is the sub FLOOR, reached at 1.58 h.** The work since is the adaptive sub-refine, which
  has no done/total by construction -- its work is the shrinking set of still-ambiguous bottoming
  argmins.
* **`subwave=0x126499` means wave 1 is still being FED,** not that nothing has happened. `subwave`
  increments only after a wave's whole enqueue loop commits, and that loop is QCAP-throttled, so it
  advances at the rate the pool drains it. 126,499 of 162,004 sub cell-sides were still ambiguous
  after the floor; ~1.09M rollouts of that wave have drained in 4.4 h at 69/s.

**The shape is also, precisely, that of a known pathology -- which is why it is worth writing down
that this is not it.** `keepgen-producer-barrier-and-durability.md` records a FiveColour run where
cores stayed 100% busy and `frozen` sat at 0 for **140-230 h** because an unbounded speculation filler
ran a full NC*2 sweep between `sub_refine_step()` calls -- "a filler that can outrun the progress step
it fills for is a barrier in disguise, and worse than the worker-side barriers this design removed,
because those were visible as idle cores." That fix is in this binary (the bounded `spec_chunk`
persistent cursor, `ExhaustiveKeep.cpp:3955-3990`), and the live proof is that `rollsub` advances at
all: it is incremented **at enqueue, inside the wave's own feed loop**, so a stalled wave clock would
show `rollsub` frozen, not climbing at 92/s.

**What is genuinely unknown, and it is the projection's main risk.** `subwave` is still 0, and the
wave-size decay IS the remaining-work signal. Until wave 1 commits and wave 2 is sized, there is no
way to tell "one big wave nearly done" from "several more to come". And the sub side is already
running hotter than section 2's Melira calibration: at ~16 rollouts per task, wave 1 alone projects
~2.05M rollouts, against the 1.7M that calibration predicted for **all** sub work. Section 2's total
therefore rests on the size-7 refine estimate (2.89M rollouts, ~10 h at the observed rate) plus a sub
tail that is now the wider of the two error bars.

## 4c. CANCELLED at 19:38 UTC by the user, and where the cost actually is

> *"I think I would like to cancel and look at optimizing. This is clearly still too slow."*

Cancelled at 6.47 h. **The watcher was stopped FIRST** -- it would otherwise have seen the gen die
with the journal present and faithfully relaunched it 30 s later. Then the gen by captured PID.

**Nothing was lost.** The journal grew 14.6 MB -> **30 MB** and is preserved
(`decks/Snow/Snow.keepmodel.exhaustive.raw.json.journal`), ending on well-formed records. Banked:
the size-7 floor, the whole sub-table floor, and roughly half of sub-refine wave 1. The journal's
`"n":18` records also settle the wave arithmetic -- the adaptive sub-refine steps a sub cell-side
from the floor of 2 straight to **18** rollouts, which is the ~16-per-task figure section 4b
inferred. Re-running `mullgen.sh run decks/Snow fast` resumes it, subject to the play digest.

### The tail is NOT the cost on this deck -- the median rollout is

This matters because it points the optimization somewhere different from the matrix work, where
34.7% of games ceiling-bound were 90.2% of cell wall.

| | |
|---|---|
| rollouts over the 30 s slow threshold | 689 |
| their total cost | 32,979 core-s = **9.16 core-h** |
| the run's total | ~192 core-h (6.0 h x 32) |
| **tail share of the work** | **4.8%**, from **0.044% of rollouts** |

So a tail cap -- the instinctive fix, and the one `slow-rollout-tail-and-the-uncharged-greedy-walk.md`
was opened for on Fungus -- buys about 5% here. **The cost is the ordinary rollout**, at 0.45 core-s
and 18,584 units apiece (~40 units/ms, ~25 us per work unit).

### Which is the defect the user already ruled on, now measured on Snow

The ordinary rollout is expensive for the reason that doc names: `SearchBudget` counts one unit per
simulated turn-step, and the greedy subset walk inside a step bills nothing. Measured at Snow's
shipped labeller settings (d2/b1) with `MTG_BF_CENSUS=1 MTG_ROLLOUT_STATS=1`:

```
[score] depth=2 budget_ms=1 rollouts=16 work_units=434182 units_per_rollout=27136
[rollout-stats]   bf_scored greedy_subsets=2793865 search_subsets=170425
```

**6.43 uncharged greedy subset visits per charged unit** (6.83 counting search subsets). At the
doc's own calibrated 1:1 exchange rate that means **the budget undercounts a Snow labeller rollout's
true work by ~7x**. For scale, Fungus's *degenerate* cell measured 27.4 per unit -- Snow's figure is
a quarter of that, but it is the **typical** rollout rather than a pathological one. A uniform 7x
undercount, not a tail.

That is exactly the defect behind the USER RULING of 2026-09-24 (*"this might point more toward the
need for properly bounding things by budget"*), which is still **unlanded** -- `MTG_SOLVE_CHARGE` is
set by nothing in `scripts/`, `test/` or any manifest.

### What charging would buy the LABELLER, and what it would cost

Melira rejected `MTG_SOLVE_CHARGE` on **play** quality (613->370 s but avg 4.96->5.24, -0.28 t). A
labeller is not judged on play quality -- it is judged on whether it RANKS hands like the shipped
policy, which is the only reason d2/b1 was adoptable at all. **That test had never been run for this
flag.** Run now, 200 paired openers from the real opening distribution, R=30, seed 9201 (held out),
both arms carrying the census so the ratio is a ratio:

| | base d2/b1 | + `MTG_SOLVE_CHARGE` |
|---|---|---|
| wall | 257.3 s | **169.8 s = 0.660x (1.52x faster)** |
| units/rollout | 18,584 | 25,970 (a different currency -- it now includes the walk) |
| rho vs base | -- | 0.9968 |
| mean shift | -- | +0.0180 t |
| dispersion sd | -- | 0.0288 |
| pairwise order agreement | -- | **10,685/10,685 = 100.0%** |

Against the bar the d2/b1 adoption cleared (rho >= 0.9984, dispersion <= 0.030, agreement 100.0%):
**agreement ties it, dispersion passes at 0.0288, and rho MISSES at 0.9968.** Reported as it fell --
two of three, not a pass. The honest reading is that charging the walk costs this labeller very
little ranking fidelity for 1.52x, but it is not the clean sweep d2/b1 was.

**This is evidence for the budget repair, not authority to flip the flag.** Enabling it for
generation alone runs straight into the doc's open question 4 -- a table fitted under an engine we do
not ship -- and the 2026-09-24 ruling deliberately chose the global repair over per-phase patches.

### The strategic answer: no single lever makes this an overnight job

1.52x takes ~20-24 h to ~13-16 h. Still not 8 h. Stacking what is measured and available:

| lever | factor | state |
|---|---|---|
| budget-currency repair (the walk billed) | **1.52x** | measured here; unlanded, user-ruled direction |
| `MTG_FOLD_SEARCH_ODO` | 1.13x | built, verified lossless, RESERVED |
| K 17->16 (`-1 Rimescale Dragon +1 Rimefeather Owl`) | ~1.30x (cells 0.769x) | a DECKLIST change, user's call |

Together ~2.2x -> **~9-11 h**, which finally approaches the window. Any one of them alone does not.
That is the answer to *"this is clearly still too slow"*: it is too slow by roughly 3x, the largest
single piece of it is a defect you have already ruled on, and closing the gap needs the stack rather
than a lever.

## 5. A live collision risk on this box, for whoever reads this first

Another session pushed `9d8e1718` at 13:18 UTC about **relaunching candidate B's (Fungus) mulligan
generation overnight**. At 13:25 only one engine process exists -- this Snow gen, PID 3827293 at
3,122% -- so they are not running now. But if that gen starts while this one is up, **both are
wrong**: generations must run alone (CLAUDE.md), and the wall-clock projection in section 2 is void
the moment the box is shared. Whoever notices first should check
`ps -eo pid,etime,pcpu,args | grep mtg` before drawing any conclusion from a rate, and coordinate
rather than assume the other run is stale -- neither of these is an agent's own experiment to kill,
so neither may be stopped unilaterally past the ~10 minute mark.

Note also their finding, which bears on Snow directly: a journal resume is refused by a **card fix**
but survives a **perf commit**, because `play_digest` is behavioural. Snow's resume today therefore
says today's three commits changed no Snow behaviour -- it does not promise the next one won't.

### RESOLVED 2026-09-25 19:20 UTC — they are DIFFERENT boxes, there is no collision

Checked from the Fungus side, which is the session section 5 is about. Two independent proofs:

* **Core count.** Section 3 reads utilisation as `2,951% of 3,200%` — a **32-core** box. The box
  running candidate B reports `nproc` = **24**, and its own gen sits at 2,375% of 2,400%.
* **Process table.** At 19:17 UTC, `ps -eo pid,etime,pcpu,args` on the Fungus box shows exactly one
  engine process: `mtg-analyze` PID 549263, 06:15:42 elapsed, 2,375%. PID 3827293 is not there, and
  today's PIDs on this box are in the 5-6×10^5 range, so 3.8×10^6 was never allocated here.

Both generations have therefore been running side by side since ~13:05 UTC **without sharing a
box**, and neither wall-clock projection is void. Nothing needs stopping, which is the outcome
section 5 correctly refused to assume either way.

The practical note for next time: `nproc` (or the denominator of a utilisation reading) is a cheaper
box fingerprint than a PID, because PIDs are only comparable within one namespace — and a PID from a
box that has since restarted looks exactly like a PID from somewhere else.

## 6. If `MTG_FOLD_SEARCH_ODO` is armed later

If the user arms it, the honest sequence is: prove digest-identity at d2/b1 first, and only then
accept that it voids the journal and re-run the floor -- ~11.5% off a ~24 h run is ~2.8 h, against
~3.6 h of floor to redo, so **it does not pay for itself on this run** and only makes sense if the
gen is going to be restarted for another reason anyway.
