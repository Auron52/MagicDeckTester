# Fungus candidate-B mulligan generation — resume / transfer handoff

**Written for whoever picks this run up:** the user on Monday, or an agent on a second machine. It
assumes no memory of the session that started the run.

**Status 2026-09-27 12:00Z:** running, healthy, ~9.3 h in, floor pass ~70% done. The user's decision
is to let it run over Sunday and on Monday either resume it here when the machine has spare CPU or
move it to another machine. Everything needed for both routes is below.

Branch `gen/fungus-candb-mulligan-2026-09-27` pins the engine state (`57c36b5c`) — see *The branch*.

## What is running

```
build/Release/mtg-analyze decks/Fungus/candidate-b-2026-09/Fungus.cod \
    --cards-json src/cards/data/cards.json --gen-mulligan fast
```

Started 2026-09-27T02:39:12Z. It is **one process named `mtg-analyze`, not `mtg`** — `pkill -x mtg`
does not match it, and it reparents to PID 1 when its launching shell dies, so `ps` ancestry is not a
reliable handle either. Kill it by PID or not at all.

| what | where |
|---|---|
| journal (the resumable state) | `decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.journal` (54 MB and growing; **gitignored**, `.gitignore:101`) |
| bucket cache | `decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.gencache.json` (22 buckets; a CACHE HIT at startup, so bucketing is not re-derived) |
| deck + inputs (tracked in git) | `Fungus.cod`, `Fungus.profile.json`, `Fungus.value.json` in the same folder |
| progress log | `logs/Fungus_candidate-b-2026-09_mullgen/gen.log` — **APPENDED across runs**, so `head` shows an OLDER run's settings. Isolate the last block by line number. |
| core-allocation samples | `logs/Fungus_candidate-b-2026-09_mullgen/ratewatch.log` (see *Reading the rate honestly*) |
| **the resume kit** | `logs/Fungus_candidate-b-2026-09_mullgen/` — `mtg-analyze.frozen`, `cards.json.frozen`, and copies of `Fungus.cod` / `.profile.json` / `.value.json`. **Resume with these.** Self-contained, so no future pull can move them. |

## Fingerprints (authoritative — from the journal's own `meta` block)

```
play_digest 36a65944fd138cd0     commit 57c36b5c       seed_base 10
K 22   R 30   depth 1   budget_ms 3   max_turns 8   max_mull 6   opp_heads 1
bucket_fp 9996886830000686848    deck_fp 7965651960928833939    equiv_seed 20260701
```

Resume gates on **play identity** (`PlayIdentityAllows`, preferring `play_digest` and falling back to
`commit`), not on the src tree hash. `36a65944fd138cd0` was verified unchanged under the nine upstream
engine commits and under the adventure fix, which is why the run stayed valid while `src/` moved.

### Why there is a frozen binary, and why it matters here

The skill says to `cp build/Release/mtg-analyze <logdir>/mtg-analyze.frozen` before a long run. That
was **not** done when this run started, and `build/Release/` was then rebuilt several times the same
day — `/proc/<pid>/exe` read `.../mtg-analyze (deleted)`, and the running image's md5
(`bb389d836cd091358fe26b2ee7640b81`) differs from the rebuilt on-disk one
(`6eb784b1138c3393a968bcc4b770b335`). The exact running image was recovered from `/proc` and saved as
`mtg-analyze.frozen`.

**Resume with the frozen copy.** Play identity is then bit-identical by construction and no gate can
refuse. The current build would *probably* also be accepted (same `play_digest`), but a refusal costs
the run's remaining resumability, so do not spend that on "probably".

### The frozen binary does NOT pin `cards.json` — check that separately

`cards.json` is **runtime data, not baked into the binary**: an old binary loads new card data. So
freezing `mtg-analyze` pins the engine and nothing else, and a `cards.json` change can still move play
identity under a frozen binary. This is not hypothetical — upstream commits landed on
2026-09-27 touching `src/ai/TurnSolver.cpp`, `src/core/SpellEffects.h` *and* `cards.json` (433 → 439
entries) while this run was live.

Two checks, in this order, and the second is the one that actually settles it:

1. **Did any of THIS deck's cards change?** For that landing, no: 0 of the deck's 23 cards had a
   changed `cards.json` entry (8 entries changed, 6 of them newly added cards).
2. **Did the digest move anyway?** Necessary because some `Has*()` gates in this engine are **DB-wide**
   rather than deck-scoped, so merely *adding* card definitions can in principle flip one. Run the real
   configuration — frozen binary against the working tree's `cards.json` — on an isolated copy of the
   deck folder and read the startup digest:

```bash
mkdir -p logs/digestprobe/candidate-b-2026-09
cp decks/Fungus/candidate-b-2026-09/{Fungus.cod,Fungus.profile.json,Fungus.value.json,Fungus.keepmodel.gencache.json} \
   logs/digestprobe/candidate-b-2026-09/
logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen \
   logs/digestprobe/candidate-b-2026-09/Fungus.cod \
   --cards-json src/cards/data/cards.json --gen-mulligan fast
#  -> "rollout-config play digest (d1/b3, 64-game battery): 36a65944fd138cd0"   then KILL IT
```

**Verified 2026-09-27T12:03Z: `36a65944fd138cd0`, unchanged.** Route A is therefore safe against that
landing. Re-run this check after any future upstream pull before resuming.

Three traps in that probe, all of which bit during this session:

- **Copy the deck folder.** Pointed at the live folder it fights the running generation for its own
  artifacts. Copy the `gencache.json` too or equivalence discovery re-derives from scratch.
- **Kill it the moment the digest prints.** It does not stop there — it proceeds into
  `continuous size-7: 761048 cells ... on 24 threads` and competes with the real run. `ratewatch.log`
  recorded the real run dropping from 23.8 to 17.1 cores during the ~40 s the probe overlapped.
- **Do not find the pid with `pgrep -f <path>`** — the pattern matches the invoking shell too, and
  killing that leaves the probe orphaned and running. (`pgrep -x` is no help either here: process names
  are truncated at 15 chars, so `mtg-analyze.frozen` never matches.) Use
  `ps -eo pid,args | grep '[d]igestprobe'` and kill that pid.

## Route A — resume on this machine

**Run the identical command again. That is the whole procedure.** Every completed cell is journalled
as it commits; startup replays the journal and skips those cells. A kill loses only the handful of
in-flight cells.

```bash
G=logs/Fungus_candidate-b-2026-09_mullgen
$G/mtg-analyze.frozen decks/Fungus/candidate-b-2026-09/Fungus.cod \
    --cards-json $G/cards.json.frozen --gen-mulligan fast
```

**Use the pinned `cards.json.frozen`, not `src/cards/data/cards.json`.** That is the whole point of the
kit: the two are already different (496,604 vs 510,688 bytes, different md5, as of 12:07Z) because
upstream added six cards while this run was live. The digest happens to be unchanged, so either would
work *today* — but the pinned pair is verified and cannot drift, whereas the working tree moves every
time anyone rebases. Do not make Monday's resume depend on a re-derivation nobody ran.

Note the journal path stays pointed at the **live** deck folder — that is the state you are resuming.
Only the *inputs* come from the kit.

> ### THE ONE TRAP THAT DESTROYS THE RUN: the recipe is a POSITIONAL argument
>
> `fast` is not a default. Omit it — or use `mullgen.sh run <deck>` without it — and you get
> `complete`: **cap R 40** instead of 30. That mismatches the journal fingerprint, and the generator
> does not stop and ask. **It OVERWRITES the journal.** This already destroyed a banked 72 MB journal
> on this very deck; the log still shows the `recipe: complete / cap R: 40` block from that run
> immediately before the two `fast` ones. Always pass `fast`, and confirm the startup block prints
> `recipe : fast` and `cap R (rollouts): 30` before walking away.
>
> Second trap, same incident: **never launch a second generator against this directory.** Because the
> process is named `mtg-analyze`, a `pkill -x mtg` "cleanup" silently misses it and you get two
> generators writing one journal. Check with `pgrep -x mtg-analyze` and kill by PID.

## Version discipline: exactly what this run needs, and what is allowed to drift

The run's identity is **three** things, not one, and freezing the binary only covers the first:

| input | pinned as | drifts if you… |
|---|---|---|
| engine code | `mtg-analyze.frozen` + branch `gen/…-2026-09-27` (`57c36b5c`) | rebuild `build/Release/` |
| card data (**runtime**, not baked in) | `cards.json.frozen` | pull anything touching `src/cards/data/cards.json` |
| deck + models | `Fungus.cod`, `.profile.json`, `.value.json` in the kit (also tracked in git) | revise the decklist or regenerate a sidecar |

**The rule: resume from the kit, and treat `play_digest 36a65944fd138cd0` as the acceptance test.** With
the kit, pulls on `phase-1-2-deck-analyzer` are harmless to this run — which matters, because pushing
any commit requires a rebase, so the working tree *will* keep moving. If you ever resume from the
working tree instead of the kit, re-run the digest probe first and confirm the hash.

What already happened, as the concrete warning: between this run's start and 12:07Z the same day,
upstream landed changes to `src/ai/TurnSolver.cpp`, `src/ai/AIEngine.cpp`, `src/core/SpellEffects.h`
and `cards.json` (433 → 439 entries). The digest survived all of it — but that was *checked*, not
lucky, and the next landing might not. A silent `play_digest` change does not corrupt anything: resume
is **refused** (`PlayIdentityAllows`). The cost is the run's remaining resumability, which is exactly
what the kit buys back.

## Route B — transfer to another machine

Two genuinely different things are called "handoff", and only one of them *finishes this table*:

**B1 — move the run (what "finish it elsewhere" means).** Copy the journal, the gencache and the
frozen binary; check out this branch for the source; resume with the same command. The journal is
gitignored and ~54 MB and growing, so move it out of band — do not force-add it to the branch:

```bash
tar czf fungus-candb-gen.tgz \
    decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.journal \
    decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.gencache.json \
    logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen \
    logs/Fungus_candidate-b-2026-09_mullgen/cards.json.frozen \
    logs/Fungus_candidate-b-2026-09_mullgen/Fungus.cod \
    logs/Fungus_candidate-b-2026-09_mullgen/Fungus.profile.json \
    logs/Fungus_candidate-b-2026-09_mullgen/Fungus.value.json
```

Take the journal **after stopping the run** (or accept losing the in-flight cells), since it is being
appended to continuously.

On the target: `git checkout gen/fungus-candb-mulligan-2026-09-27`, untar into the same relative paths,
resume per Route A. Because the tarball carries the binary *and* the card data, the target needs no
build at all as long as it is Linux/x86-64 — and the branch checkout then matters only for having the
source present. If the target must build instead (different arch), build **at this branch's commit** so
`commit` matches as well as `play_digest`, and verify the digest with the probe before resuming.

**B2 — pool a second machine (adds R, does NOT finish the cells).** This is the protocol the skill's
"Multi-machine handoff" section describes: both machines run the same deck/buckets/commit with
**distinct `seed_base`** and you merge the raw sidecars. It buys effective-R on cells already done; it
does not advance the remaining cell sweep. Requires the determinism handshake first (identical tiny
config, confirm identical `bucket_fp`/`deck_fp` and byte-identical V, with
`MTG_KEEP_REFS_OFFSET=0` pinned). Note the skill's own judgement: a slow second box is usually worth
more pointed at a *different* deck than piling R on one already resolved.

**For "I want this table finished elsewhere", B1 is the route.** B2 is for adding confidence later.

## Route C — salvage without finishing

The journal is itself a valid merge input, so **the rollouts are never stranded** even if the run
never completes:

```bash
MTG_KEEP_MERGE=1 \
MTG_MERGE_INPUTS="decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.journal" \
  logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen \
  decks/Fungus/candidate-b-2026-09/Fungus.cod --cards-json src/cards/data/cards.json
```

Caveat from the skill, and it is a hard one: a profile built from **R < 10** is structurally
unshippable. This run's floor is R=2 and escalation had not started as of this writing, so a salvage
today yields a diagnostic, not a shippable profile. Getting a shippable one needs the escalation work
or merged chunks reaching R ≥ 10.

## Progress at 18:16Z (15.6 h in) — floor sweep DONE, and a corrected cost model

```
roll7    3,044,192  = 2 x 761,048  -> size-7 R=2 floor sweep COMPLETE
sub        139,120 / 560,212 (24.8%)   rollsub 278,672     ~2.8 h left at current rate
frozen           0 / 1,522,096         -> freezing has not begun
journal  1,661,216  = 1,522,096 + 139,120  (it counts cell-side completions across both phases)
```

**Two corrections to the projections given earlier in this session, in opposite directions.**

1. **I under-counted the work by 42%.** Earlier estimates scaled only `roll7` (13.79 M rollouts) and
   omitted the sub-table phase's rollout cost entirely. The reference run spent `rollsub` 1,122,548
   over 62,444 sub cell-sides — **17.98 rollouts per sub cell-side**, *more* per cell-side than size-7's
   9.06. Scaled to this run's 560,212 sub cell-sides that is **10.07 M** more rollouts. Real total
   ≈ **23.86 M**, not 13.79 M.
2. **I under-credited the rate by the same kind of mistake in reverse.** I quoted the reference run at
   58.6 rollouts/s, computed as `roll7 / total wall time` — a partial numerator over a full denominator,
   while `rollsub` work was consuming that same wall time. The reference's *total* throughput was
   `(roll7 + rollsub) / t` = **122.1 rollouts/s**.

Net effect: the two errors largely cancel. Progress is **3.32 M of 23.86 M = 13.9%** in 15.6 h.

### REVISED 21:55Z, and it is worse — measure core-SECONDS, not wall clock

The table that stood here projected 62 h (2.6 d) by assuming this run could hit the reference's
122 rollouts/s. **It cannot, and the reason only shows up once you divide by core share.**

```
ours       60.2 roll/s at a MEASURED mean 18.4 of 24 cores  =  3.27 rollouts / core-second
reference 122.1 roll/s at 24 cores, uncontended             =  5.09 rollouts / core-second
                                    -> our rollouts are 1.56x MORE EXPENSIVE
```

That factor is a property of the *list*, not the box: candidate B is singleton-heavy, so it holds far
more degenerate Doubling-Season hands, and the log's slow-rollout table shows a single size-7 rollout
with `Doubling Season x3` taking **151.8 seconds**. Scaling the reference's *wall clock* can never
reveal this — it needs this run's own core-seconds, which is why `ratewatch.log` mattered.

Work accounting to the reference's final depth (both phases have completed their R=2 floor only):

| | ours now | reference FINAL | remaining |
|---|---|---|---|
| size-7 rollouts/cell-side | 2.00 | 9.06 | 10.74 M |
| sub rollouts/cell-side | 2.00 | 17.98 | 8.95 M |
| | | | **19.69 M (17.5% done in 19.2 h)** |

| scenario | roll/s | remaining | total |
|---|---|---|---|
| current starved level (11.6 cores) | 38 | 144 h | 163 h (6.8 d) |
| historical mean core share (18.4) | 60 | 91 h | **110 h (4.6 d)** |
| completely free box (24 cores) | 79 | 70 h | 89 h (3.7 d) |

**The 3-day budget is now out of reach under every scenario, including a perfectly free box.** That is a
change from the previous two estimates in this document and it is the number to plan against. Measured
contention, for the record: mean **18.4 of 24 cores** over 595 samples, with **42% of samples below 18
cores** — the neighbouring container's load is bursty and heavy (loadavg 46–49 while we held 11.6 cores).

**Remaining uncertainty is now one thing only: whether the reference's final depths (9.06 and 17.98
rollouts/cell-side) transfer to a harder list.** Harder cells could plausibly escalate *further*, which
would push these numbers up again; adaptive escalation could equally settle many cells early, which
would pull them down. Do not treat 4.6 d as firm until that multiplier is observed.

> **CORRECTION (2026-09-29, see the 51.0 h section below).** This paragraph originally read
> *"escalation had only just begun at 21:49Z (`roll7` +768)"*. It had not. That +768 was a
> **sub-refine wave-boundary artefact**, and `roll7` then sat flat for another 36 h; escalation did
> not start until the waves collapsed at 51.0 h. The sub-table multiplier has since come in at
> **124%** of the reference, not the 97% reported on 2026-09-28 — that figure divided a live counter
> by the reference's finished total. Read §2 of the 51.0 h section before quoting any multiplier
> from this document.

**Contention is real, bursty, and was observed.** At 18:14Z the generation held **11.7 of 24 cores at
loadavg 48.8** (≈2x oversubscription) while a neighbouring container ran heavy work; by 18:16Z it had
recovered to 23.2. Neither number is "the" rate — which is exactly why `ratewatch.log` samples core
share every 60 s. Quote a rate only with its core share attached.

### Watch out for this when monitoring: floor completion LOOKS like a stall

`roll7` stops advancing permanently when the R=2 sweep finishes — it sits at exactly `2 x cell_sides`
with `(0/s)` while the sub-table phase advances `rollsub`/`sub`/`journal` instead. A monitor keyed on
`roll7` alone reports a stall at the moment of success; the first one here did. Two further traps in the
same area:

- **`phase=` stays `floor` across that transition.** The sub-table batches run *inside* `phase=floor`
  (the startup line says so: `continuous size-7: … (+ 560212 fused sub-table batches + adaptive
  sub-refine)`), so a phase-change trigger never fires for it.
- **`frozen=` stays `0/…` for a very long time** and is not a progress indicator early on. Freezing had
  not begun 15.6 h in.

Judge liveness on the **tuple** `(roll7, rollsub, sub, journal)` — a genuine stall is all four frozen.
`phasewatch2.sh` does this; `phasewatch.sh` (v1) is kept only as the illustration of the bug.

Three more traps found on 2026-09-29, all of which produced a wrong number before I caught them:

- **`ratewatch.log`'s timestamp has NO DATE** — it is bare `HH:MM:SS`, so on a multi-day run
  `grep '^05:4'` silently matches *every* day at once. It already has **42 duplicate timestamp keys**.
  Doing that mixed yesterday's contended samples (11.7 cores, loadavg 44) into today's free-box window
  and made the box look half-starved. Select by **line position** (`tail -N`), never by timestamp, or
  add a date column on the next run. The script is live, so it has not been edited — see
  `never-edit-a-running-script`.
- **`subwave=NxM`'s size field lags the index** by up to one monitor period; read a repeated size as
  "not yet updated", not "the set stopped shrinking".
- **A wave boundary is a real, visible idle trough.** At the final convergence the box fell to
  **3.2 of 24 cores for ~3 minutes** while the producer ran the serial `recompute_sub()` /
  `compute_sub_wave_tasks()` step with nothing else admissible. Short and unavoidable here, but it is
  the "waves are a loop" signature, and it will corrupt any rate measurement whose window straddles
  it: a 10-minute sample across this transition read **17.5 cores and 2.25 rollouts/core-second**
  against 23.8 cores on either side. Take rate samples inside a single stable phase.

## Reading the rate honestly

As of 12:00Z (9.3 h in): floor pass **70.2%** (1,068,127 of 1,522,096 cell-sides), 2.14 M rollouts.

**`roll7 / journal` is exactly 2.000, which means escalation has not started at all.** The floor is a
pure R=2 sweep over 761,048 size-7 cells (two sides each). Still ahead: the rest of the floor, 560,212
fused sub-table batches (`rollsub` is still 0), and adaptive sub-refine. So *floor 70%* is emphatically
**not** *run 70%* — rollout-weighted the run is ~15% done, because escalation is ~78% of the work.

Projection, scaling the reference run (`logs/Fungus_mullgen/gen.log`, **same cap=30**, 110,524
cell-sides at 9.06 rollouts/cell-side, finished in 4.75 h) by this list's exact cell count — candidate
B is **13.77x** that list, 761,048 distinct size-7 hands against 55,262 for the shipped list, driven
by singleton-heavy composition:

| rate estimator | roll/s | remaining | total |
|---|---|---|---|
| cumulative over the whole run | 64.2 | 50 h | **60 h (2.5 d)** |
| post-warm-up marginal (t > 2.1 h) | 48.6 | 67 h | 76 h (3.2 d) |
| best clean 10 min | 74.7 | 43 h | 53 h (2.2 d) |

**Do not project from a single short window.** Hourly marginal rate on this run has ranged 25.7–141.5
rollouts/s. An earlier estimate in this session quoted 80 h by using the single most pessimistic hour
as though it were the steady state, which is how the budget question got framed as tighter than it is.
The cumulative rate (64.2/s) is within 10% of the reference run's 58.6/s.

**That 25.7 h⁻¹ hour is UNATTRIBUTED, and do not let anyone tell you otherwise.** It is tempting to
call the whole swing cell difficulty because the slowest hour was overnight — an earlier draft of this
very document did — but **no core-share data exists for those hours**, so the claim is unsupported.
The competing container's load is *bursty*: sometimes low-CPU work like development or card analysis,
sometimes a regression suite, and the latter takes a large bite. Either explanation fits the dip and
nothing on disk separates them. `ratewatch.log` (started 11:57Z) is what resolves this going forward:
a difficulty trough shows the rate falling while our core share stays near 24, whereas a contention
trough shows the core share itself dropping. Read that file before attributing anything.

**Contention, and why it is hard to see from in here.** Other agents run in *separate containers* and
are therefore **invisible to this container's `ps`/`top` process list** — "nothing else is running" is
not a conclusion you can reach that way. Two signals do work: host `/proc/loadavg` (it is the HOST's,
not ours) against `nproc`, and the generation's own core allocation sampled from `/proc/<pid>/stat`
(`ps` `%CPU` is a **lifetime average** and useless for this). At 12:00Z: loadavg 25.08/31.10/30.84 on
24 cores with **no cgroup quota** (`cpu.max` = `max`), and the generation holding **23.7 of 24 cores**.

**Do not generalise that 23.7 figure — it was sampled during a benign window.** The other container
was running *audits* at the time, which are low-CPU; that is precisely why we had essentially the whole
box despite a second container being active. The same reading during a regression suite next door would
be materially lower. So "we measured 23.7 of 24 cores" answers *what was happening at 12:00Z*, not
*what this run generally gets*. `ratewatch.log` samples cores + load + counters every 60 s, which is
what turns this from one anecdote into an attributable series.

**The dominant uncertainty is not CPU — it is the escalation multiplier.** 9.06 rollouts/cell-side is
transferred from a list 13.8x smaller. If candidate B's singleton-heavy hands make keep calls more
marginal on average they escalate harder: 1.25x on that multiplier is ~4.2 days, 1.5x is ~5.1. The
multiplier becomes directly measurable in the first hour after the floor completes, which is the
number actually worth deciding on.

## 2026-09-29 05:40Z (51.0 h in) — sub-refine CONVERGES, and escalation has not started

Three things resolved at once, and one of them is a retraction.

### 1. The sub-refine waves hit the reference's convergence cliff

`subwave` history, with `rollsub` at each transition:

| t | subwave | rollsub | this wave cost |
|---|---|---|---|
| 300 s | `0x0` | 0 | — |
| 69,307 s (19.3 h) | `0x453873` | 1,138,488 | wave 0 = 7,272,588 |
| 146,716 s (40.8 h) | `1x341752` | 8,411,076 | wave 1 = 4,098,028 |
| 183,320 s (50.9 h) | `2x341752` ← stale size | 12,509,104 | |
| 183,620 s (51.0 h) | **`2x4052`** | 12,534,836 | wave 2 ≈ 25 k |
| 183,920 s (51.1 h) | **`4x103`** | 12,559,840 | waves 3–4 ≈ 25 k |

**Wave 2 is 4,052 cell-sides — a 98.8% collapse from 341,752 — and waves 3 and 4 followed within one
monitor period at 103.** The sub-table phase is finished at ~12.56 M rollouts; the last three waves
cost ~50 k between them. The reference converged at wave 4 too.

**The size field lags the index by up to one monitor period.** At 183,320 s it read `2x341752` —
wave 2's index with wave *1's* size. Do not read a repeated size as "the set stopped shrinking";
the reference log does the same thing (`1x40891` for 3,300 s before correcting itself to `1x27223`).
Wait for the next monitor line before concluding anything from a size.

**The decay ratio transferred almost exactly**, which is the useful transferable fact:
wave 1 / wave 0 is **56.4%** here (4,098,028 / 7,272,588) against **55.7%** in the reference
(340,296 / 611,104). Then both cliff-edge immediately after wave 1 — the reference's waves 2–4 cost
3,108 rollouts between them and converged. Two waves of decay then a collapse looks to be the shape
of this generator's sub-refine, not a property of one list.

### 2. RETRACTION: "the sub phase is running at 97% of the reference" was a partial-numerator error

On 2026-09-28 this document and my report to the user said the reference multiplier had transferred
favourably — 17.37 rollouts/cell-side here against the reference's 17.98, i.e. 97%. **That compared
our in-progress numerator to the reference's final one.** `rollsub / sub_batches` was 17.37 only
because we were mid-wave-1; the reference's 17.98 was its *completed* total. The honest comparison
now that our sub phase has converged:

| | sub batches | final rollsub | rollouts/batch |
|---|---|---|---|
| reference (shipped Fungus) | 62,444 | 1,122,548 | 17.98 |
| candidate B | 560,212 | ~12.6 M | **~22.4 (124%)** |

So the sub phase cost **24% more per batch** than the reference, not 3% less. This is the same
mistake as the 58.6-vs-122.1 rollouts/s error earlier in this run (a partial numerator over a full
denominator) and it is what `projections-separate-exact-from-estimated` is about. **Rule for anyone
reading a progress figure off this run: never divide a live counter by a finished run's total.** Wait
for the phase to converge, or compare like-for-like fractions.

It costs ~2.5 M rollouts against the estimate (~7 h), which is real but not decisive. The projection
below absorbs it.

### 3. Why `roll7` has been flat for 36 h — a producer interlock, not a stall

`roll7` sat at 3,045,736 from 53,705 s to 183,320 s: the floor is exactly `2 x 1,522,096 =
3,044,192` and the extra 1,544 arrived in two tiny bumps, at the wave 0→1 and 1→2 boundaries. That is
not escalation. **No size-7 work beyond the bare floor has run.**

The mechanism is in `src/analyzer/ExhaustiveKeep.cpp`. The floor-phase speculation filler
(`ExhaustiveKeep.cpp:3968`) feeds already-floored cells up to `r0 + spec_budget`, and it sits
*after* `sub_refine_step()` (`:3939`) in the producer loop. `sub_refine_step` pushes an entire wave
inline, blocking on `q_nf.wait(… q.size() < QCAP)` for every task — so with a 341,752-task wave the
producer is stuck inside it for most of the wave, and reaches the speculation filler only in the
wave's drain tail. Hence bumps at the boundaries and nothing between.

**This costs nothing and needs no action.** Speculation exists purely to fill *idle* cores, and there
were none — the box ran at 23.4–23.8 of 24 cores on mandatory kind-2 sub-refine work the whole time.
It is also work that `compute_refs` may truncate, so skipping it can only save. The consequence is
scheduling, not loss: the size-7 escalation the reference did *interleaved* with its sub waves is, on
this run, **all still ahead of us**.

Note the asymmetry with the FiveColour bug recorded at `ExhaustiveKeep.cpp:3946` — there the
speculation filler outran the sub-refine step and was fixed by bounding it to `spec_chunk`. The wave
push on the other side is still unbounded, which is what produced this. Harmless here; worth knowing
before anyone "fixes" a flat `roll7`.

**What happens next, in order.** `spec_active` is a latch (`:3962`, deliberately — it is the
interlock that stops `compute_refs` firing on a half-speculated state), so now that the waves have
collapsed the speculation sweep will run to **saturation** before refine can start:

1. speculation to `r=6` over every live cell-side — **6,088,384 rollouts** (4 per cell-side; 1,544 done)
2. `compute_refs` publishes → `phase=refine`
3. refine escalation to the final mean r, plus freezing (`frozen=` finally leaves `0/1522096`)
4. write-out of the raw journal (148 MB and growing)

Step 2 is what `phasewatch3.sh` is waiting for, so the watcher will fire on it. Expect **`roll7` to
start climbing steadily now** — that is the escalation rate, and it is the only number left that
matters.

### The revised projection

Reference mean final depth is **9.06 rollouts/cell-side** (floor 2 + speculation 4 + refine 3.06).
At that multiplier candidate B's total `roll7` is `9.06 x 1,522,096 = 13.79 M`, so **remaining
escalation ≈ 10.74 M rollouts** — now the overwhelming majority of what is left, with the sub phase
converged and freezing cheap per cell.

**MEASURED at 06:05Z on the clean converged state** (the first honest escalation sample this run has
allowed): 900 s window, `roll7` +75,837 = **84.3 rollouts/s at 23.78 of 24 cores = 3.54
rollouts/core-second.** That is **20% better** than the 2.95 rollouts/core-second the floor sweep
gave, not worse — most likely warm state and precompute hits on cell-sides the floor already visited.
**Do not reuse 2.95 for escalation.**

Remaining splits into two kinds of work that must NOT be priced alike:

| | rollouts | rate | hours |
|---|---|---|---|
| speculation to `r=6` (in progress; 101,663 done) | 5.99 M | 84.3/s **measured** | 19.7 |
| refine escalation to the final mean r | 4.66 M | unknown | 15.3–30.7 |

| if refine costs | refine | total | finish |
|---|---|---|---|
| like speculation (84.3/s) | 15.3 h | **86.5 h (3.60 d)** | Wed 09-30 17:09Z |
| 1.25x dearer | 19.2 h | 90.3 h (3.76 d) | Wed 09-30 20:59Z |
| 1.5x dearer | 23.0 h | 94.2 h (3.92 d) | Thu 10-01 00:49Z |
| 2x dearer | 30.7 h | 101.8 h (4.24 d) | Thu 10-01 08:29Z |

**Finish: Wednesday 2026-09-30 afternoon through Thursday 2026-10-01 morning**, plus freezing and the
148 MB+ journal write-out (the reference's post-escalation tail was ~1.2 h on 13.8x less data). Run
start, for the record: **2026-09-27 02:39Z**.

**The measured 84.3/s does NOT narrow the bracket, and it would be a mistake to quote the 3.60 d row
alone.** Speculation is *uniform shallow* work — every live cell-side from r=3 to r=6 — while refine
escalation goes *deep on the hardest cells only*, toward `cap=30`. The measurement is therefore from
the easy half, and the dearer rows are live possibilities rather than padding. Two further cautions:
per-monitor-period rates across this same span ranged **49–145/s**, so one 15-minute window is not a
steady state (`one-run-t-stat-is-not-evidence` applies to rates too); and the whole projection still
rests on the reference's 9.06 rollouts/cell-side transferring — the assumption that has already been
wrong once on this run (§2).

### 2026-09-29 19:56Z (65.3 h in) — the 84.3/s sample was over-read by 38%

**Overall progress: 18,740,443 of 26,349,058 rollouts = 71.1%.** (`rollsub` is final at 12,559,852;
`roll7` is 6,180,591 of a projected 13,789,206.) Speculation is **51.5%** done (3,136,399 of
6,088,384). Refine escalation has not started; `frozen=` is still `0/1522096`; journal 207.6 MB.

**The 14.1 h cumulative escalation rate is 61.3 rollouts/s, not the 84.3/s measured over 15 minutes.**
The doc's own caution against projecting from a single short window was correct, and I still led with
the short window — don't repeat that. `ratewatch.log` separates the two causes cleanly:

| | 15-min sample | 14.1 h actual |
|---|---|---|
| rate | 84.3/s | **61.3/s** |
| mean cores | 23.78 | **19.67** (min 11.55; **34.4% of samples below 18**) |
| productivity | 3.54 roll/core-s | **3.12 roll/core-s** |

So the shortfall is **roughly half contention, half genuine cost**: 17% of it is core share (the
neighbouring container came back during the day, exactly the bursty pattern the user described), and
12% is escalation genuinely costing more per rollout than the first clean window suggested. Neither
alone explains it, which is why the core column has to be read before attributing a rate change to
difficulty. Hourly marginal rates across the window ran **16–194/s**.

Projection, holding productivity at the measured 3.12 roll/core-s and varying only CPU and the refine
multiplier:

| cores | refine cost | total | finish |
|---|---|---|---|
| free box (23.85) | same | 93.7 h (3.90 d) | Thu 10-01 00:21Z |
| free box (23.85) | 1.5x | 102.4 h (4.27 d) | Thu 10-01 09:03Z |
| as observed (19.67) | same | 99.7 h (4.16 d) | Thu 10-01 06:23Z |
| as observed (19.67) | 1.5x | 110.3 h (4.60 d) | Thu 10-01 16:57Z |

**Finish: Thursday 2026-10-01, somewhere between 00:20Z and 17:00Z**, plus freezing and the journal
write-out. The box is free again as of 19:56Z (23.85 cores over the last hour), so the upper rows are
the live ones if it stays that way.

Note the total work estimate has grown from 23.86 M to **26.35 M rollouts** across this run's
revisions, entirely because `rollsub` finished at 12.56 M against a reference-scaled 10.07 M (§2).
`roll7`'s 13.79 M is still the *unverified* half of that total — it assumes the reference's 9.06
rollouts/cell-side transfers, and nothing has tested it yet because refine has not begun.

### 2026-09-30 10:37Z (79.9 h in) — 80.3% done, and every projection this run has been optimistic

**Overall: 21,153,688 of 26,349,058 rollouts = 80.3%.** Speculation is **91.2%** done (5,549,644 of
6,088,384; 538,740 left). Refine escalation has still not started, `frozen=` is still `0/1522096`,
journal 258.4 MB. The run is healthy — 24 of 27 threads in state `R`, journal age 0 s.

**Productivity is falling monotonically, and that is the story of this whole run:**

| sampled | rate | mean cores | rollouts/core-second |
|---|---|---|---|
| 15 min at speculation start | 84.3/s | 23.78 | **3.54** |
| next 14.1 h | 61.3/s | 19.67 | **3.12** |
| next 14.7 h | 45.7/s | 18.87 | **2.42** |

The log says plainly why, and it is not scheduling: `SLOW-ROLLOUT` lines at **30,000–45,000 ms each**,
and an all-time worst single rollout of **559,664 ms (9.3 minutes)** on `Utopia Mycon x4; Peat Bog;
Forest x2`. The offenders cluster on `Saproling Burst` + `Wild Growth` + `Utopia Mycon` hands — the
token-explosive board states. Escalation visits cells in index order but spends its *depth* on the
marginal ones, so the hard cells arrive progressively. Hourly marginals over the last 16 h ran
**12.7–90.3/s** with no idle cores at any point.

| scenario | total | finish |
|---|---|---|
| free box (23.85 cores), refine same cost | 104.9 h (4.37 d) | Thu 10-01 11:33Z |
| free box, refine 1.5x dearer | 116.1 h (4.84 d) | Thu 10-01 22:45Z |
| contended as now (18.87 cores), refine same | 111.5 h (4.65 d) | Thu 10-01 18:09Z |
| contended as now, refine 1.5x dearer | 125.7 h (5.24 d) | Fri 10-02 08:18Z |

**Read the revision history before trusting any of these, because it has a direction:**

| stated | projection |
|---|---|
| 09-28 21:47Z | 87–109 h |
| 09-29 06:05Z | 86.5–101.8 h |
| 09-29 19:56Z | 93.7–110.3 h |
| 09-30 10:37Z | 104.9–125.7 h |

**Every revision but one has moved later, and always for the same reason: the per-rollout cost was
measured on easier work than what remained.** The floor sweep priced uniform R=2 work; the 15-minute
window priced shallow speculation; each then under-priced what came next. Anyone re-projecting this
run should assume the same bias applies to the table above — the `1.5x dearer` rows are the prudent
ones, not the central ones, and refine escalation is the deepest and therefore dearest work in the
run. Contention is now the *smaller* of the two effects (18.87 vs 23.85 cores is 21%; productivity has
fallen 32%).

The next milestone is real: at 91.2% of speculation, `compute_refs` should publish within a few hours
and `phase=` should leave `floor` for the first time in the run. That is also the first direct
measurement of whether the reference's 9.06 rollouts/cell-side transfers — the assumption the entire
`roll7` target rests on, still untested. `phasewatch3.sh` is waiting on exactly that transition.

### 2026-09-30 15:55Z (85.3 h in) — MILESTONE: `phase=refine`, and 59.7% froze in one step

```
monitor: 306934s  phase=refine  roll7=9132576 (4/s)  frozen=909147/1522096 (59.7%)
         subwave=5 conv  journal=4954371 (0s ago)  cap=30
```

Three things landed together, and the first two are exactly as predicted:

- **Speculation saturated at `roll7=9,132,576`** — precisely `2 x 1,522,096 + 4 x 1,522,096`, the
  figure this document predicted when the mechanism was worked out at 51.0 h. The model of what
  speculation is and what it costs was right.
- **`compute_refs` published and `phase` left `floor`** for the first time in the run, after 85 h.
- **`frozen` went 0 → 909,147 of 1,522,096 (59.7%) in a single monitor period**, and the journal banked
  550,588 records in the same step. Those cell-sides were settled by the r≤6 speculation data alone;
  the reconcile truncated them and no further escalation is owed on any of them. The reference did the
  same thing at its own transition (0 → 79.7%).

**But our freeze fraction is materially worse than the reference's, and that is the new headline
risk.** 59.7% against 79.7% means **40.3% of cell-sides still need refine, against the reference's
20.3% — twice the proportion.** That is consistent with this list being harder (singleton-heavy,
more marginal keep calls), and it re-opens the question of what refine costs:

| scaling | refine rollouts | vs. previous estimate |
|---|---|---|
| A: reference mean depth 9.06/cell-side (used until now) | 4,656,630 | — |
| B: reference cost per **unfrozen** cell-side (13.87) | 8,499,157 | **1.83x A** |
| absolute bound: `cap=30` on every unfrozen cell-side | 14,710,776 | 3.16x A |

**B is the mechanistically sound one** — refine only touches unfrozen cell-sides, so scaling by their
count beats scaling by a whole-population mean. A was wrong in the optimistic direction, which is the
same bias this document already flagged against itself.

**First refine measurement, ONE period only — do not lean on it.** 307234 s: `roll7` +9,787 and
`frozen` +879, i.e. **11.1 rollouts per cell-side frozen**, against the reference's 13.87. On that
figure the remaining refine is ~6.8 M, between A and B. One 300 s window is exactly what has misled
this projection twice already; it needs hours, not minutes, and the box is at **12 of 24 cores**
(loadavg 48) right now, so the neighbouring container is heavy again.

**Also note the transition itself cost a near-total stall:** core share fell to **0.12** for one
sample and took ~4 minutes to climb back, while `compute_refs`, the freeze pass and a 550 k-record
journal write ran serially. Expect the same at the end of refine.

## The branch

`gen/fungus-candb-mulligan-2026-09-27` → `57c36b5c`, the commit the journal records. Its `src` tree
(`e45467b5…`) differs from the later `phase-1-2-deck-analyzer` HEAD (the adventure fix touched
`src/core/SpellEffects.h`), which is exactly why the branch exists: building here reproduces the
play identity the journal was generated under, matching **both** `commit` and `play_digest` rather
than relying on the digest alone.

It deliberately carries **only source**. The journal is gitignored by policy (`decks/**/*.journal`;
the repo commits the gzipped `.raw.json.gz`, never an uncompressed raw), and it grows, so it travels
as a tarball per Route B1.

The branch was created with `git branch` and **not checked out** — checking it out would swap `src/`
and `cards.json` under a 9-hour job. `cards.json` in particular is read at runtime rather than baked
into the binary. Leave the working tree on `phase-1-2-deck-analyzer` while the run is live.
