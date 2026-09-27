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

| scenario | roll/s | remaining | total |
|---|---|---|---|
| stays contended at 11.7 cores | 84 | 68 h | 83 h (3.5 d) |
| reference throughput, 24 cores | 122 | 47 h | **62 h (2.6 d)** |
| linear scale-up of the current rate to 24 cores | 173 | 33 h | 49 h (2.0 d) |

**So the 3-day budget now turns entirely on CPU availability, not on the recipe.** At reference
throughput it fits with ~10 h to spare; under sustained heavy contention it does not. No cap-R change is
needed to make it fit — free cores are.

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
