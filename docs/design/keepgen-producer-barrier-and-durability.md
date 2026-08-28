# Keepgen: the producer-side barrier and the size-7 durability gap

Status: **DIAGNOSED, NOT FIXED** (2026-08-26). Found live on the FiveColour generation.
Both defects are in `src/analyzer/ExhaustiveKeep.cpp`, on the continuous path — which is the
only path (see `continuous-only-keepgen.md`).

This document is self-contained: the symptom, the evidence that identifies it (cheaply), the two
defects, why the existing validation could not have caught either, the fixes, and the tests that
would catch them next time.

---

## 1. Symptom

A generation appears perfectly healthy and makes no progress.

* `phase=floor`, `frozen=0/3955796 (0.0%)`, unchanged for **27.6 hours**
* `fed` climbing steadily at 25–65/s the whole time
* CPU 1170% of 1200% — all 12 workers pegged, balanced to within 0.01%
* `perf record -p` shows 100% ordinary rollout work (`SolveUncached`, `CollectActions`, mana payment)
* the journal file's mtime **frozen for 27.6 h**

Every throughput signal reads green. Nothing is wedged. The box is fully busy doing work that
does not advance the run.

---

## 2. How to identify it in one cheap query

**Do not** run whole-file sorts or greps over the journal (it is hundreds of MB, and generation
boxes run close to their memory ceiling). The decisive evidence is a histogram of the slow-rollout
dumps, sliced at the point the journal went quiet:

```sh
LN=$(grep -n 'monitor: <elapsed-when-journal-stopped>' logs/<Deck>_gen/gen.log | head -1 | cut -d: -f1)
tail -n +$LN logs/<Deck>_gen/gen.log | grep 'SLOW-ROLLOUT' | grep -oE 'size[0-9]' | sort | uniq -c
```

On FiveColour:

```
BEFORE (floor):   size7 1422 | size6 104 | size5 7 | size4 1
AFTER  (27.6 h):  size7  411 | size6   0 | size5 0 | size4 0      r= 2,3,4,5 ONLY
```

Sub-table (`size<7`) rollouts are the ONLY thing a kind-2 sub-refine wave can produce. Zero of them
over 27.6 h ⇒ **no wave is in flight** ⇒ `sub_wave_pending == 0` ⇒ nothing is stuck in a worker.
`r=2..5` with `r0=2` means every rollout is speculation.

Corroborate for free from `/proc` (no debugger, no stopping the process):

```sh
for t in /proc/<pid>/task/*; do echo "$(basename $t) $(cat $t/wchan) $(awk '{print $14}' $t/stat)"; done
```

The producer (main tid) shows **298 s of CPU over 110 h**, parked in `futex_do_wait` — it is blocked
on `QCAP` inside `feed_upto`, not doing work. The 12 workers show ~106.8 h each.

---

## 3. Defect 1 — the producer-side barrier

### Mechanism

The producer's outer loop (`ExhaustiveKeep.cpp:3143-3190`) runs, per iteration:

1. floor scan over `NC*2` cells
2. `feed_sub()` — drain the fused sub-table batches
3. change-detect classification (one-shot)
4. **`sub_refine_step()`** (`:3170`) — issue at most ONE adaptive sub-refine wave
5. **speculation filler** (`:3176`) — feed every already-floored cell up to `r0 + spec_budget`

Step 5 is unbounded. `feed_upto` blocks on `q_nf.wait(... q.size() < QCAP ...)`, so a full
speculation pass is `NC * 2 * spec_budget` queue-throttled feeds. With `spec_budget =
cont_lookahead = 4` (`:2776`, and `:2188` bakes `cont_lookahead = 4LL`), FiveColour's pass is

```
3,955,796 cell-sides x 4 = 15,823,184 feeds  ≈ 140-230 h at the observed 25-30 feeds/s
```

The phase flip (`:3212`) requires `sub_converged`, and `sub_converged` can only advance via
`sub_refine_step()` in step 4. **Step 4 is therefore reachable at most once per full speculation
pass.** The wave clock stops for the duration.

### Why it is invisible

`adaptive-batched-keepgen.md:10-12` states the design goal as a "**barrier-free** pool … no
per-wave refine barrier, no floor→refine barrier", and every barrier it removed was a **worker-side
join**. Its success metric was *cores stay busy* (`in_flight` was 213 in validation). This defect
satisfies that metric perfectly: cores are 100% busy, running the filler.

> **"Barrier-free" was defined as "no core idles". The real requirement is "no core idles AND the
> state machine keeps advancing." A filler that can outrun the progress step is a barrier in
> disguise — and it is worse than the barrier it replaced, because the old one was visible as idle
> cores.**

### It is strictly worse than what it replaced

`adaptive-batched-keepgen.md:137-138` describes the prior shape: the floor was split into
`MTG_KEEP_FLOOR_GROUPS` (**default 32**) barriered waves, and the refine loop ran discrete
barriered waves — **7 waves** measured on both completed runs (Goblins, Creature Giving).

| | prior | current |
|---|---|---|
| barriers | ~39 (32 floor + 7 refine) | effectively **1** |
| cost of each | tail of the slowest cell in the group | the entire speculation pass |
| bounded by | one cell's rollout (~21 min worst observed) | **NC** — deck size, not cell cost |
| worst case | ~13.6 h of partially idle cores | **140–230 h** of fully-busy misdirected cores |
| visible? | yes — cores idle | no — utilisation reads 100% |

~39 small honest barriers were replaced with one enormous disguised one.

### Why validation missed it: scale

`adaptive-batched-keepgen.md:117` validated floor speculation on **Slivers (R=8)** — byte-identical
raw with speculation on, on-rerun, and off. That proves **correctness**, and says nothing about
**scheduling**. One filler pass on Slivers is minutes; the producer returns promptly and waves fire
normally. The defect is scale-dependent and the crossover was never computed.

> **Scale rule for any filler: `cost(one filler pass) / cost(the tail it covers)` must be ≪ 1.**
> Filler cost scales with `NC` (deck complexity); filler benefit scales with the tail length
> (roughly constant, a handful of slow cells). The curves cross. On FiveColour it is ~15.8M feeds
> against a sub-refine tail of maybe thousands — computable from the startup line before launch.

### The escape hatch was retired

`MTG_KEEP_NO_FLOOR_SPEC=1` restored the old barrier and was kept as an explicit opt-out
("temporary A/B knob; the target design is speculation-always"). The no-off-switches cleanup removed
it — `floor_spec = !cfg.recommend_only` (`:2775`) reads no env; see the comment at `:2773`. The one
lever that could have rescued a live run without a code change no longer exists.

---

## 4. Defect 2 — the size-7 durability gap

### Mechanism

A **size-7** cell-side is journaled at exactly two moments in its life (`:2925-2929`):

```cpp
if (jfloor)      { journal_append(HAND, i, pd, js, jq, jn, 0); }   // c first reaches r0
else if (jterm)  { journal_append(HAND, i, pd, js, jq, jn, 1); }   // terminal freeze / cap
```

Everything between those two events is **unbanked**. That is not just the speculation window — it
is the whole refine phase as well. A cell walking `R=2 → 30` four rollouts at a time writes nothing
until it freezes or caps.

On resume, replay takes the highest-`cnt` record per cell-side (`:2300-2321`), which for any live
cell is its floor record at `n = r0`. `fed[]` is an in-memory vector (`:2781`) reseeded from the
reloaded counts (`:2791`), so **every live cell restarts from `r0`** — including redoing the entire
speculation pass, which walks straight back into Defect 1.

Measured exposure on FiveColour: **3.28M cell-sides ≈ 330 core-hours** unbanked and growing.
Expected steady-state exposure during refine, with mean final `R ≈ 10`: on the order of
`(live cells) x (R - r0) / 2 ≈ 15M rollouts`.

### The sub-tables already do it right

`run_batch` journals on **every** commit, unconditionally (`:1918`) — which is why the H1–H6 tables
recorded 1,317,366 clean records. Size-7 cells go through the `run_one` + fold path instead and get
the two-event treatment. **The incremental behaviour was implemented on the cheap tables and not on
the expensive one** (size-7 is 3.96M of 5.27M cell-sides and effectively all the cost).

### The stated guarantee is false

`adaptive-batched-keepgen.md` asserts this property as delivered:

* `:13` — "**Incremental + restartable** — resumable at any point"
* `:50` — "**Completion IS persistence**: there is no 'checkpoint' event."
* `:53` — "a crash loses only **the few in-flight cells**"

`:53` is currently wrong by ~6 orders of magnitude.

"Completion IS persistence" is a sound premise **only while completions keep happening**. Two
changes shipped the same day (2026-07-24): the per-cell journal, and floor→refine speculation. The
second created a regime where cells accumulate work and **never complete** — past their floor
crossing, unable to freeze because freezing is gated on `refs_ready`, still false. The persistence
model has no event for that regime, so it writes nothing. The second change invalidated the first
change's premise and nothing reconciled them.

### Why validation missed it: the assertion was blind to it

The resume test (`:65-67`) killed the run **mid-refine** ("refs fixed, 158 terminal cells") and
asserted a **byte-identical final raw**.

`run_one` is a pure function of `(seed_base, r, w, pd)` and resume restores the *fixed* refs from the
REFS record rather than recomputing them. So a cell re-refining from `r0` replays exactly the same
rollouts and produces exactly the same numbers. **Discarding 330 core-hours is invisible to a
byte-identity assertion** — you redo the work, you get the same answer, the test passes. It could
never have failed.

The doc's own sentence states the wasteful behaviour next to the claim there is none (`:55`):

> **byte-identical + zero-waste**: terminal (`f=1`) size-7 cells set `frozen7` and are **skipped**;
> floored cells **re-refine from `r0`**

"Zero-waste" was defined as *don't redo cells that already finished*. The requirement is *don't redo
work*. Those agree only when the live-cell population is small.

---

## 5. Fixes

1. **Bound the filler.** Give speculation a persistent cursor and a per-iteration budget (a few
   multiples of `QCAP`), so the producer returns to the state machine at a bounded interval.
   Scheduling-only: `run_one` is a pure function of `(seed_base, r, w, pd)` and the fold is
   deterministic, so reordering feeds cannot change any value.
2. **Make the filler preemptible by what it fills for.** Call `sub_refine_step()` from *inside* the
   speculation loop, not only above it.
3. **Journal size-7 on progress, not only on events.** Append a record whenever a cell's `cnt`
   advances past a checkpoint (at minimum, at the speculation cap). This is making the `run_one`
   path do what `run_batch` (`:1918`) already does, and it is what makes "resumable at any point"
   literally true.
4. **Instrument progress, not busyness.** The monitor (`:1550-1552`) prints `fed`, `frozen`, `phase`
   — all of which read healthy here. Add:
   * `sub_waves=N pending=N remaining=N converged=0|1`
   * journal record count and **age since last write**
   * a loud warning when work is being fed while journal age exceeds ~N minutes — that is a direct
     violation of "resumable at any point" and should say so
   * a completion fraction with a **real denominator**. `fed` is a throughput counter with none; the
     `/3955796` printed beside it belongs to `frozen` (`:3055` stores `cells = NC*2`, `:3240` prints
     `fed_total` as "rollouts fed"). Reading `fed/cells` as progress understates a run ~2x.
5. **Optional guard:** refuse to start (or at least print the ratio of) a filler pass whose cost
   exceeds the tail it covers — the scale rule in §3, checked at startup where all the inputs are
   already known.

Fixes 1–2 address Defect 1; fix 3 addresses Defect 2; fix 4 makes both self-reporting. Fix 4 alone
would have surfaced this within minutes instead of a day later via a stale mtime.

---

## 6. Tests that would have caught these

The scheduler has **three regimes** — floor, speculation (pre-refs), refine (post-refs). The existing
resume test covers only the third.

1. **Waste-counting resume test, all three regimes.** Kill and resume; assert the count of rollouts
   **executed after resume** is ~the in-flight set, not every live cell's progress past `r0`.
   Byte-identity is necessary but not sufficient — assert *efficiency* separately, because
   determinism guarantees correctness regardless of how much work is thrown away.
2. **Progress-liveness assertion.** In any generation test, assert the journal advances while work
   is being fed. This is the test that distinguishes "busy" from "progressing".
3. **Run both on Slivers.** Slivers can reproduce both defects — the whole run is 452 s. It never
   failed because the assertions measured the wrong properties, not because the deck is too small.
   A deliberate kill inside the speculation window covers regime 2.

---

## 7. Reference measurements

FiveColour (frozen commit `37d4105d`, `d2/b1`, `max_mull=6`, cap R=30, floor R=2, 27 buckets):

```
distinct hands  size7 1,977,898  size6 517,283  size5 116,063  size4 21,703
                size3 3,244  size2 363  size1 27      (total 2,636,581)
size-7 cell-sides           3,955,796
fused sub-table batches     1,317,366
floor feeds  = 3,955,796*2 + 1,317,366 = 9,228,958      (took ~83 h on 12 cores)
speculation ceiling         15,823,184 feeds
```

Realized-R from two COMPLETED runs (end-of-run `adaptive sampling:` block, which prints per-size
realized R and `total rollouts X vs uniform Y`):

| deck | size-7 cells | realized / uniform | size-7 mean R | refine waves |
|---|---|---|---|---|
| Goblins | 416,851 | 12,998,432 / 35,792,340 = **36.3%** | ~10 (max 30) | 7 |
| Creature Giving | 569,931 | 20,106,047 / 47,345,520 = **42.5%** | ~10 (max 30) | 7 |

Use ~36–42% of `total_cells * 2 * r_max` to size a new deck's total rollout count. Mean final R is
**~10**, not 4–6.

---

## 8. Operational notes for a live run hitting this

* The floor is safe. `journal_f.flush()` runs per record (`:1843`) — a process kill or a clean
  reboot keeps it; only a hard power cut can truncate the tail (it is `flush()`, not `fsync()`).
* A reboot does **not** cost a day — it costs the whole pass. `fed[]` reseeds from `cnt = r0`
  (`:2791`), so speculation restarts from scratch and the producer re-enters the same barrier. The
  run only completes if it gets one uninterrupted window longer than the pass.
* There are **no signal handlers** in the analyzer, so a live process cannot be asked to checkpoint.
* Journal resume does **not** compare `commit` or `play_digest` (`:2284-2290` checks `bucket_fp`,
  `deck_fp`, `seed_base`, `K`, `max_mull`, `equiv_seed`, `R`, and depth/budget/max_turns/start_life).
  So a scheduling or instrumentation fix can land without stranding a journal — but a **play-logic**
  change would be silently accepted and mixed with samples rolled under the old engine. That
  asymmetry should become an enforced check, not a convention.
* Generation boxes run near their memory ceiling (this one: 7.45 GB peak RSS in a 10 GB WSL2 VM,
  `memory.events: oom_kill 1`). Diagnose with `tail`, `ls`, and `/proc` reads — never a whole-file
  sort over the journal.
