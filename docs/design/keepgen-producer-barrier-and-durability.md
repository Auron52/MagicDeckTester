# Keepgen: the producer-side barrier and the size-7 durability gap

Status (2026-08-28):
* **Defect 1 (producer-side barrier) — DIAGNOSED, OPEN at HEAD.** Verified still present:
  `sub_refine_step()` is called once per outer iteration above an unbounded speculation pass
  (`ExhaustiveKeep.cpp:3413`, `:3420-3427` at `9fbd47a2`).
* **Defect 2 (size-7 durability gap) — FIXED UPSTREAM 2026-08-23 (`a47f75dd`)**, independently, by
  the work written up in `keepgen-resume-exactness.md`. HEAD has a third journal case `jprog`
  (`:3127`, `:3141`) carrying `sv` — the per-rollout speculative values for `[r0, n)`. §4 below is
  retained because it is the analysis of *why* the gap existed and what it costs, and because it is
  the state of any run frozen before 2026-08-23.

Found live on the FiveColour generation (frozen at `2f7822a2`, 2026-08-21 — **two days before the
Defect 2 fix**, which is exactly why that run's journal is silent). Both defects are in
`src/analyzer/ExhaustiveKeep.cpp`, on the continuous path — which is the only path (see
`continuous-only-keepgen.md`).

This document is self-contained: the symptom, the evidence that identifies it (cheaply), the two
defects, why the existing validation could not have caught either, the fixes, and the tests that
would catch them next time.

## Related documents — read this first if you are diagnosing a stalled gen

`keepgen-cost-concentration.md` (Mirrorwing) describes a run with **the same surface symptom** —
`phase=floor` and `frozen=0` forever, cores 100% busy, throughput collapsing ~22x — attributed to
*cost concentration* (a deck whose search cost sits in a tiny tail of go-off hands). This document
describes the same symptom caused by the *producer barrier*. **They are different defects and can
coexist**, so do not assume either diagnosis without the discriminator:

| observation | cost concentration | producer barrier |
|---|---|---|
| journal mtime | still advancing | **frozen** |
| SLOW-ROLLOUT `size<7` entries | still appearing | **zero** |
| per-rollout core-seconds | greatly elevated | ~normal |

`keepgen-resume-exactness.md` is the authoritative reference for what kill/resume does and does not
guarantee, and it supersedes §4's "not fixed" framing as of `a47f75dd`.

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

## 4. Defect 2 — the size-7 durability gap (FIXED UPSTREAM 2026-08-23, `a47f75dd`)

> **Fixed at HEAD.** The fold now has a third case, `jprog` (`:3127`, `:3141`), which appends a
> mid-cell progress record carrying `sv` — the per-rollout speculative values over `[r0, n)` — so a
> resumed `compute_refs` can replay the reconcile. See `keepgen-resume-exactness.md`, which found
> and fixed this (plus two replay defects) independently. The analysis below is retained because it
> explains *why* the gap existed, what it costs, and because it is the live state of **any run
> frozen before 2026-08-23** — including the FiveColour run this document was written from.

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
3. ~~**Journal size-7 on progress, not only on events.**~~ **DONE upstream** (`a47f75dd`, the `jprog`
   case). Left here for the record: the fix is to make the `run_one` path do what `run_batch`
   (`:1918`) already did, which is what makes "resumable at any point" literally true.
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

Fixes 1–2 address Defect 1 and are **still outstanding**; fix 3 addressed Defect 2 and is done; fix
4 makes both self-reporting and is the highest-value remaining item — it alone would have surfaced
this within minutes instead of a day later via a stale mtime. Note that after fix 3 the journal now
advances during speculation, so **"journal mtime frozen" is no longer the tell for Defect 1 on a
post-`a47f75dd` binary** — use the SLOW-ROLLOUT `size<7` histogram (§2) and the `sub_*` counters that
fix 4 would expose.

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

**Build on the existing apparatus, do not start fresh.** `keepgen-resume-exactness.md` already
established a control-vs-SIGKILLed-twin harness (`logs/resume_proof/`, regenerable in ~5 min) which
proved data recovery exact and caught three replay defects. It asserts `cmp` on the final raw. The
two assertions above — **rollouts re-executed after resume** and **journal advances while work is
fed** — are additions to that harness, not a replacement for it. Its residual known gap (a
non-deterministic `recompute_vg` trigger, deliberately left) is unrelated to either defect here.

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
* Generation boxes run near their memory ceiling (this one: **8.42 GB peak RSS in a 10 GB WSL2 VM**,
  `memory.events: oom_kill 1`). Diagnose with `tail`, `ls`, and `/proc` reads — never a whole-file
  sort over the journal.

---

## 9. Live-run handoff — FiveColour, state as of 2026-08-28 08:42 UTC

The run this document was written from is **still going**. Everything below is what a new operator
needs; nothing here lives anywhere else.

### Identity

```
pid        1566929   (build/Release/mtg-analyze decks/FiveColour/FiveColour.cod
                      --cards-json src/cards/data/cards.json --gen-mulligan fast)
started    2026-08-21 13:32 UTC
frozen at  commit 2f7822a2   HEAD:src e196e432de177b116125a05904deff0da8232090
           (recorded in logs/FiveColour_gen/freeze.commit / freeze.src)
log        logs/FiveColour_gen/gen.log
journal    decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json.journal
resume     bash logs/FiveColour_gen/run.sh      # same command; journal replayed; no resume flag
```

### State

| | |
|---|---|
| elapsed | 163.1 h |
| phase | `floor`, `frozen=0/3955796` |
| `fed` | 19,518,133 |
| floor | **100% complete** — all 7 tables, 5,273,162 / 5,273,162 cell-sides, took ~83 h |
| speculation | 10,308,905 / 15,823,184 = **65.2%** |
| rate | 26.1/s (24 h), 18.6/s (6 h) |
| expected flip | **40–77 h** from the timestamp above |

**It is not wedged — this is Defect 1 (§3).** Do not diagnose it again from scratch; §2 has the
one-query confirmation if you want to re-verify.

The journal has been silent since 2026-08-25 00:29 and that is **expected on this binary**, which
predates the `jprog` fix (§4). ~330+ core-hours of speculation are unbanked. The 83 h floor is
safe on disk.

### Hazards — all of these have already cost something

1. **Do NOT `kill`/`TaskStop`/Ctrl-C this run.** User-requested, far past the CLAUDE.md 10-minute
   line. Only the user stops it.
2. **Do NOT rebase, pull, or checkout in this working tree.** It is deliberately parked at
   `2f7822a2` while `origin` is 227 commits ahead with heavy `src/` changes. To push docs anyway,
   use the object-database recipe below — it never writes the working tree.
3. **Do NOT rebuild `build/Release`.** Crash-resume must use the binary the run started with
   (`continuous-only-keepgen.md` says so explicitly).
4. **Keep the box quiet.** 8.42 GB peak RSS against a 10 GB VM, and the cgroup has already recorded
   `oom_kill 1`. No builds, no regression runs, no whole-file scans of the 271 MB journal — an
   earlier `sort -u` over 3.96M journal lines during diagnosis was a real OOM risk and is exactly
   what not to do.
5. **A reboot costs the whole pass, not a day.** `fed[]` reseeds from `cnt = r0` (`:2791`), so
   speculation restarts from zero and the producer re-enters the same barrier. The run only
   completes given one uninterrupted window longer than the remaining pass. Windows Update is
   paused and sleep is disabled on the host.
6. **Implementation of fixes 1, 2 and 4 is deferred by the user** until this run lands. Do not start
   them on this box.

### Pushing without touching the working tree

```sh
BLOB=$(git hash-object -w <file>)
export GIT_INDEX_FILE=/tmp/x.idx; rm -f "$GIT_INDEX_FILE"
git read-tree origin/<branch>
git update-index --add --cacheinfo 100644,$BLOB,<path-in-repo>
TREE=$(git write-tree)
COMMIT=$(git commit-tree "$TREE" -p origin/<branch> -m "...")
git push origin "$COMMIT":<branch>
unset GIT_INDEX_FILE
```

Afterwards verify `git rev-parse HEAD:src` still equals `logs/FiveColour_gen/freeze.src`. The local
branch ends up 0 ahead / N behind, so catching the checkout up later is a plain fast-forward.

### Light monitoring only

```sh
grep 'monitor:' logs/FiveColour_gen/gen.log | tail -2
ls -la decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json.journal
grep -E 'VmRSS|VmHWM' /proc/1566929/status
```

Watch, in priority order: (1) `frozen` moving off 0 — the flip, at which point the generator prints
its own floor projection, which beats any extrapolation; (2) journal mtime moving; (3) `fed`
stalling near 25,052,142 — would mean something really is wedged rather than barriered;
(4) `VmHWM` past ~9 GB.

### What still has to happen after the flip

Refinement, R=2 → cap 30, which is the larger half. Size it from §7: **57.5M–67.2M total rollouts**
(36–42% of `2,636,581 x 2 x 30`), mean final R ≈ 10. `fed` is a throughput counter, **not** progress
— see fix 4.
