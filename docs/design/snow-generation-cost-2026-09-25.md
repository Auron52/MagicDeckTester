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
