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
| **frozen binary** | `logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen` — **resume with this** |

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

## Route A — resume on this machine

**Run the identical command again. That is the whole procedure.** Every completed cell is journalled
as it commits; startup replays the journal and skips those cells. A kill loses only the handful of
in-flight cells.

```bash
logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen \
    decks/Fungus/candidate-b-2026-09/Fungus.cod \
    --cards-json src/cards/data/cards.json --gen-mulligan fast
```

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

## Route B — transfer to another machine

Two genuinely different things are called "handoff", and only one of them *finishes this table*:

**B1 — move the run (what "finish it elsewhere" means).** Copy the journal, the gencache and the
frozen binary; check out this branch for the source; resume with the same command. The journal is
gitignored and ~54 MB and growing, so move it out of band — do not force-add it to the branch:

```bash
tar czf fungus-candb-gen.tgz \
    decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.journal \
    decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.gencache.json \
    logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen
```

On the target: `git checkout gen/fungus-candb-mulligan-2026-09-27`, untar into the same relative
paths, resume per Route A. Using the frozen binary means the target does not even need to build, as
long as it is Linux/x86-64. If it must build instead, build **at this branch's commit** so `commit`
matches as well as `play_digest`.

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

## Progress, and reading the rate honestly

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
rollouts/s, and the *lowest* hour was overnight — so the swing is dominated by cell difficulty, not
CPU. An earlier estimate in this session quoted 80 h by using the single most pessimistic hour as
though it were the steady state; the cumulative rate (64.2/s) is already within 10% of the reference
run's 58.6/s, which is the cleanest evidence that CPU share is not the main term.

**Contention, and why it is hard to see from in here.** Other agents run in *separate containers* and
are therefore **invisible to this container's `ps`/`top` process list** — "nothing else is running" is
not a conclusion you can reach that way. Two signals do work: host `/proc/loadavg` (it is the HOST's,
not ours) against `nproc`, and the generation's own core allocation sampled from `/proc/<pid>/stat`
(`ps` `%CPU` is a **lifetime average** and useless for this). At 12:00Z: loadavg 25.08/31.10/30.84 on
24 cores with **no cgroup quota** (`cpu.max` = `max`), and the generation holding **23.7 of 24 cores**
— i.e. real outside demand, but we were still getting essentially the whole box.
`ratewatch.log` samples cores + load + counters every 60 s so Monday's decision can attribute rate to
difficulty vs starvation instead of guessing.

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
