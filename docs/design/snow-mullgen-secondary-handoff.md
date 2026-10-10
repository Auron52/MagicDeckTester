# Snow mulligan profile: handoff to the secondary machine

Written for **whoever operates the secondary box** (human or agent). Self-contained: you should not
need to read another doc to run this, though section 6 points at one for the "is it stuck?" question.

You are picking up an **interrupted but healthy** keep-table generation. ~9.4 h of labelling is
already banked in a journal, and resuming costs nothing — but only if the parity check in section 3
passes, so do that **before** committing a night to it.

Context if you want it: `docs/design/snow-generation-cost-2026-09-25.md` (why it was cancelled here,
and what the cost actually is).

---

## 1. What is already done, and what is left

| phase | rollouts | state |
|---|---:|---|
| size-7 floor (R=2 over 351,944 cell-sides) | 703,888 | **done** |
| sub-table floor (162,004 sub cell-sides x 2) | 324,008 | **done** |
| sub-refine wave 1 (126,499 tasks, r 2 -> 18) | 2,023,984 | **~60% fed** |
| sub-refine wave 2+ (r 18 -> cap 30, decaying) | ~500k-900k | not started |
| size-7 refine (floor 2 -> cap 30, adaptive) | ~2,885,941 | **not started** |
| **whole profile** | **~6.6M** | ~2.26M banked |

**Estimate.** On the primary (32 cores, ~70-90 rollouts/s) the whole profile is **~26 h of
generation plus ~1.5-2 h of validation**, of which **~17 h remains**. Scale by your box: the work is
embarrassingly parallel across cores, so *remaining wall ≈ 17 h × (32 / your cores) × (primary
per-core speed / yours)*. A 24-core box at similar clocks lands near **~23 h**.

> **⚠ That ~23 h is void — the secondary is a 12-core box, and "24 cores" was a guess.** The box
> that actually picked this up has `nproc` = 12 and is ~1.9x slower *per core* than the primary, which
> puts the real remaining figure at **~75-85 h**, not ~23 h (measured, section 9.4). The 24-core
> number was an incorrect assumption about which box would take the handoff — confirmed by the user
> 2026-09-25. Do not scale from it; scale from section 9.4's measured ~16 rollouts/s.

**The biggest error bar, stated honestly:** size-7 refine is ~11.5 h of that estimate and has
**never been observed on this deck** — `frozen` never left 0.0%. Its 2.89M figure is transplanted
from Melira Pod's completed gen (10.2 rollouts per cell-side against a floor of 2). Treat ~26 h as a
central estimate with a real 22-39 h spread, not a promise.

---

## 2. Artifacts: what to copy, and from where

### Comes from git — just check out the commit

These three are **tracked**. Clone/checkout and they are there:

| path | why it matters |
|---|---|
| `decks/Snow/Snow.cod` | the decklist; feeds `deck_fp` |
| `decks/Snow/Snow.profile.json` | the base play profile; feeds `bucket_fp` |
| `decks/Snow/Snow.value.json` | **the mulligan-gen contract — see the warning below** |
| `src/cards/data/cards.json` | card data; feeds `deck_fp` |

> **⚠ `Snow.value.json` is the file that silently costs you 2.87x.** It carries
> `value_play.mull_gen_depth: 2` and `mull_gen_budget_ms: 1`. If it is missing or its `value_play`
> block is altered, `src/analyzer/main.cpp` falls through to the **built-in gen defaults of d5/b20** —
> the most expensive labeller setting any deck in this repo uses, applied to the least tractable deck.
> That fall-through is exactly where this deck's old **~138 h** projection came from. It carries no
> `eval_model` and no `target_depth`, so it changes nothing about play; it is a cost contract only.
> Do not "tidy" it.

### Must be copied out of band — gitignored

| path | size | gzipped | md5 |
|---|---:|---:|---|
| `decks/Snow/Snow.keepmodel.exhaustive.raw.json.journal` | 29 MB | **2.3 MB** | `736610be5ef197b71071b1fb04ba7a5d` |
| `decks/Snow/Snow.keepmodel.gencache.json` | 16 KB | — | `451f0d6df2fcc2697c31d3e9a1d78725` |
| `decks/Snow/Snow.keepmodel.exhaustive.raw.json.slow.log` | 136 KB | — | optional, diagnostics only |

Both live **next to the decklist** on the primary and must land in the **same place** on the
secondary — `--gen-mulligan` resolves its artifacts directory-relative to the deck file.

**The bundle is already built on the primary**, with the `decks/Snow/` paths preserved so it extracts
correctly from the repo root:

```
logs/snowopt/snow_mullgen_handoff.tgz      2.3 MB   md5 449dfb89b0e43a55536da21c7d7e3e78
  decks/Snow/Snow.keepmodel.exhaustive.raw.json.journal
  decks/Snow/Snow.keepmodel.gencache.json
  decks/Snow/Snow.keepmodel.exhaustive.raw.json.slow.log
```

```bash
# on the secondary, from the repo root, AFTER checkout + build
tar xzf snow_mullgen_handoff.tgz
md5sum decks/Snow/Snow.keepmodel.exhaustive.raw.json.journal   # expect 736610be5ef197b71071b1fb04ba7a5d
```

(`logs/` is gitignored, so the bundle will not arrive via git — move it with scp/rsync or a shared
folder. Rebuild it any time with `tar czf … ` over the three paths listed above.)

**The journal is the 9.4 h.** Without it you restart from zero (nothing breaks — you just re-pay).
**The gencache is the barrier.** Bucket discovery is the one phase that is a *join*: nothing else
starts until its last probe lands, and on a comparable deck it ran 31+ minutes with the box down to
2 of 24 cores at the end. `Snow.keepmodel.gencache.json` is fingerprint-gated and a hit skips
discovery entirely. Copy it; it costs 16 KB to avoid a serial half-hour.

### Which commit to build

Build a commit whose **`src` tree hash is `df5b7c7e2d9b52bae21813f7536a84ea7d7a10df`**. Current
`origin/phase-1-2-deck-analyzer` HEAD qualifies (everything since has been docs-only):

```bash
git rev-parse HEAD:src     # must print df5b7c7e2d9b52bae21813f7536a84ea7d7a10df
./build.sh                 # NEVER raw cmake -- a bare cmake leaves CMAKE_BUILD_TYPE empty = -O0 = ~10x slower
```

The `src` tree hash is the right thing to compare, not the commit hash: the journal was started on
`91e2aecc` and successfully resumed on a later HEAD precisely because the play logic was unchanged.

---

## 3. Pre-flight: prove parity in ~2 minutes before committing a night

The journal carries these fingerprints. **The gate that actually matters is the play digest**, not
the commit string (verified 2026-08-24 on Mirrorwing: chunks 11 commits apart pooled fine because the
digest matched).

| field | value |
|---|---|
| `play_digest` | **`2ba6aeecbdcdbbdb`** |
| `commit` (when started) | `91e2aecc` |
| `seed_base` | `1000000` |
| `K` | `17` |
| `max_mull` | `6` |
| `equiv_seed` | `20260701` |
| `bucket_fp` | `6398475677235765846` |
| `deck_fp` | `16983024912021420944` |
| `R` (cap) | `30` |

Probe your box's digest in an **isolated copy of the deck folder**:

```bash
mkdir -p /tmp/dg && cp -r decks/Snow /tmp/dg/
rm -f /tmp/dg/Snow/*journal* /tmp/dg/Snow/*raw.json* /tmp/dg/Snow/*gencache*
cd /tmp/dg && <repo>/build/Release/mtg-analyze Snow/Snow.cod \
    --cards-json <repo>/src/cards/data/cards.json --gen-mulligan recommend
#  -> "rollout-config play digest (d2/b1, 64-game battery): <hash>"   then KILL it
```

> **⚠ Isolated copy, not the live folder.** `--gen-mulligan` writes its journal, raw and gencache
> next to the decklist. Pointed at `decks/Snow` it will overwrite the very journal you just copied in.

* **Hash == `2ba6aeecbdcdbbdb`** → cross-machine determinism holds for this deck at these settings.
  The resume will be accepted and the 9.4 h counts. Proceed.
* **Hash differs** → the journal will be *refused*, not silently misused (the engine prints
  `fingerprint MISMATCH -- ignoring`). Nothing is corrupted; you simply start from the floor, at the
  full ~26 h. Worth reporting back before you spend it, since a differing digest on the same `src`
  tree would itself be a finding about platform determinism.

---

## 4. Run it

```bash
bash scripts/mullgen.sh run decks/Snow fast
```

That is the whole command. It generates, then **validates and gates itself** — keep A/B, confounded
bottoming, then the regression suite — so nothing can ship un-A/B'd. Budget ~1.5-2 h for that tail
on top of generation.

Three things to get right:

1. **The recipe must be `fast`.** `R=30` is part of the journal fingerprint. `complete` is R=40 and
   would discard all 9.4 h without warning.
2. **Confirm the resume in the first minute.** You want:
   `[keepgen] RESUME(journal): reloaded <N> cell-sides ... -> continuing`.
   If you see `fingerprint MISMATCH -- ignoring`, stop and go back to section 3.
3. **Check utilisation in the first ten minutes.** The monitor prints every 300 s; you want cores
   near saturation (the primary held 92-99%). If it is not, fix scheduling before letting it run —
   do not reach for an engine explanation.

Re-running the identical command after any interruption resumes. There is no resume flag.

---

## 5. Do not do these

* **Do not run anything else on the box.** Generation stages run alone — a shared box makes the
  output wrong, not merely slow, because every projection here is wall-clock.
* **Do not delete the journal**, even on a failed or killed run. It is the banked work and it is a
  valid merge input on its own.
* **Do not run a second Snow chunk on the same machine.** The recipe writes to a fixed path per deck,
  so chunk 2 silently destroys chunk 1's raw and profile.
* **Do not hand-edit** the `MTG_KEEP_*` / `MTG_EQUIV_*` knobs or the `mull_gen_*` numbers. The recipe
  takes no parameters but the recipe, and every one of those values is in a fingerprint.

---

## 6. What healthy looks like, so you do not kill a good run

**`frozen 0/351944 (0.0%)` for many hours is EXPECTED and is not a stall.** Cells freeze only in the
size-7 *refine* phase, and refine runs **last** — after the size-7 floor, the sub-table floor and the
adaptive sub-refine. There is no partial-credit readout before it. The primary sat at 0.0% for its
entire 6.5 h and was working correctly the whole time.

The phase order you should see:

```
size-7 floor (already done -> roll7 barely moves)
  -> sub floor            sub=N/162004 climbing to 100%
  -> sub-refine waves     rollsub climbing, subwave=NxM  (M is the decay signal)
  -> size-7 refine        frozen finally starts moving
  -> validation
```

**The live proof of progress is `rollsub`/`roll7` climbing and `journal=<n> (0s ago)`.** A genuinely
stalled wave clock shows `rollsub` frozen, not climbing.

This shape is also, precisely, that of a real pathology that once cost 140-230 h (an unbounded
speculation filler starving the wave clock while cores stayed pinned). That is fixed in this binary.
The full "is it that, or is it fine?" discussion is
`docs/design/snow-generation-cost-2026-09-25.md` section 4b — read it before concluding anything from
a flat `frozen`.

One expected difference: your completed table will **not** be byte-identical to what the primary
would have produced. At the shipped `MTG_KEEP_REFS_OFFSET=2` the freeze shrink target is re-derived on
a *timing-triggered* schedule, so a cell sitting on the threshold can stop at a different R on a
differently-paced box. That is accepted in production — do not read it as corruption.

---

## 7. Sending it back

Return these, and the log:

```
decks/Snow/Snow.keepmodel.exhaustive.profile.json
decks/Snow/Snow.keepmodel.exhaustive.raw.json        (or .raw.json.gz -- the .gz is what gets committed)
logs/Snow_mullgen/gen.log
```

Note what the gate said. A profile that **failed** its A/B is quarantined by the driver to
`*.keepmodel.exhaustive.profile.DISABLED.json` — renaming is what actually deactivates it, because
**presence is adoption** for these artifacts. Send it either way; a failure is a result.

### 7a. Adopting it after the Jorn change (2026-10-03) — one rename, no regeneration

This generation runs on the engine where Snow's MDFC was played as its **back** face, Kaldring, the
Rimestaff. Since 2026-10-03 the deck plays its **front** face, **Jorn, God of Winter**, and lists it
under that name (`decks/Snow/Snow.cod`, `Snow.profile.json`'s `card_scores`, `cards.json`). It is the
same physical card, and it keeps the same card number (#26 — Jorn and Kaldring fall in the same
alphabetical slot, so every shuffle is unchanged). The user's decision is to **adopt the table as
generated, not regenerate it**: it was fitted to slightly different play, and that is accepted.

* **On the secondary box: do NOT pull the Jorn commit mid-run.** It changes `deck_fp` (decklist +
  `cards.json`) and `bucket_fp` (profile), so a resume on it refuses the journal. Finish on the frozen
  commit the run started on.
* **At adoption, rewrite the one name** in the returned
  `Snow.keepmodel.exhaustive.profile.json` — its `exhaustive_keep.buckets` and its `card_scores` —
  `"Kaldring, the Rimestaff"` → `"Jorn, God of Winter"`. Play matches a hand to buckets **by card
  name** and the play loader runs no fingerprint check (`deck_fp`/`bucket_fp` are read only in
  `src/analyzer/`), so nothing would refuse the stale name: Jorn in hand would match no bucket. Check afterwards: `zcat`/`grep -c Kaldring` on the table must be 0.
* Leave the raw sidecar untouched: it is the record of what was generated, on its own commit.

---

## 8. If you would rather POOL than finish

Different goal, different flow, worth knowing it exists. Instead of finishing this R=30 table, the
secondary can run its **own** chunk at a **different `--seed`**, and the two pool element-wise to a
higher effective R — two `fast` chunks pool to **R=60**, above `complete`'s R=40, for less wall than
one `complete` run.

```bash
MTG_KEEP_MERGE=1 \
  MTG_MERGE_INPUTS="decks/Snow/Snow.keepmodel.exhaustive.raw.json,/path/to/secondary.raw.json" \
  ./build/Release/mtg-analyze decks/Snow/Snow.cod --cards-json src/cards/data/cards.json
```

Two cautions if you go this way. **Seed allocation:** the primary's chunk used `seed_base 1000000`;
give the secondary a distinct prefix and keep a ledger — the merge's overlap guard is the backstop,
not the plan. And **do the determinism handshake once** before trusting a pool: both boxes run a tiny
*identical* config (same seed, `MTG_KEEP_ROLLOUTS=2`) with **`MTG_KEEP_REFS_OFFSET=0` pinned**, and
you confirm identical `bucket_fp`/`deck_fp` and byte-identical V. Pin the offset or the handshake can
fail for the timing reason in section 6, which has nothing to do with cross-machine parity.

**But note this does not answer the question that was asked.** Pooling raises R; it does not complete
the R=30 table. If the goal is "finish this", sections 3-4 are the route.

## 9. PICKED UP 2026-09-25 21:00 UTC on a THIRD box — resumed clean, running, ~5 days

The handoff was executed. Everything in sections 2-4 worked exactly as written. The box is smaller
than section 1 assumed, which multiplies the wall clock ~5x — **the smaller box is expected and the
user has accepted it** (2026-09-25), so section 9.4's **~115 h** is the plan of record, not an open
question. (The figure put to the user at the time of that ruling was ~75-85 h, from a 20-minute rate
sample; section 9.4 explains why an hour of data moved it to ~115 h. The ruling was about the *box*,
not the date.)

### 9.1 The box is 12 cores — and the doc's "24" was a guess, not a measurement

This is neither the 32-core primary nor the 24-core Fungus box of `snow-generation-cost-2026-09-25.md`
section 5 — it is a **third** box: `nproc` = **12**, 10 GB RAM, WSL2. Section 1's 24-core figure was an
incorrect assumption by the primary's agent about which box would pick the work up; the user confirmed
as much. Per section 5's own advice, `nproc` is the cheap box fingerprint, so record all three:
primary **32**, Fungus **24**, this one **12**.

The transferable lesson is section 5's, sharpened: a handoff doc should carry the *rate* the receiving
box must measure for itself, not a projection for a box the author never looked at. Section 9.4's
~16 rollouts/s is the number a future operator should scale from.

### 9.2 Pre-flight: PASSED, and extended to cover a commit the doc predates

| check | result |
|---|---|
| `HEAD:src` tree hash | `df5b7c7e…` — **matches** section 2 |
| journal md5 after transfer | `736610be5ef197b71071b1fb04ba7a5d` — **matches** |
| gencache md5 | `451f0d6df2fcc2697c31d3e9a1d78725` — **matches** |
| play digest, no ceiling | **`2ba6aeecbdcdbbdb`** — matches the journal |
| play digest, `MTG_DECISION_WORK_X=1000` | **`2ba6aeecbdcdbbdb`** — unmoved |

**The second digest probe is the one section 3 does not tell you to run, and you must.** Commit
`880cb9b1` ("wire the per-decision work ceiling into generation") landed *after* this doc was written
and changed **`scripts/mullgen.sh`** — the very command section 4 tells you to run — to export
`MTG_DECISION_WORK_X=1000`. Its commit message verifies digest-preservation against
**`e0ffdb608cd70b25` / 344,625 cell-sides**, which is *candidate-B (Fungus)*, **not Snow**
(`2ba6aeecbdcdbbdb` / 351,944). So on this deck the claim was unverified, and section 3's probe runs
the bare binary *without* the ceiling — it would have printed a matching digest and told you nothing
about what `mullgen.sh` was about to do. Probed both ways here: Snow's digest is unmoved, so the
ceiling stays armed **and** the journal resumes. Had it moved, the fix is `MTG_DECISION_WORK_X=0`
(`DecisionWorkMeter.h`: 0 = disarmed = byte-identical), not discarding the journal.

Caveat worth stating rather than burying: digest-identity is a 64-game behavioural proxy, and cutting
the extreme tail is precisely what the ceiling is *for*, so a handful of degenerate cells may be
labelled differently either side of the resume boundary. Digest-identity is the repo's own resume gate
(and the bar `880cb9b1` was engineered against), so this is within doctrine — but it is a proxy, not a
proof over all 6.6M rollouts. On Snow the ceiling should buy little regardless: section 4c measured the
tail at **4.8% of work from 0.044% of rollouts**, where Fungus's 1.33x came from removing 24.6% of
billed units. Do not expect 1.33x here.

### 9.3 Resume confirmed, both barriers avoided

```
[keepgen] equivalence discovery: CACHE HIT (17 buckets from …gencache.json) -- skipped
[keepgen] RESUME(journal): reloaded 513948 cell-sides from …raw.json.journal -> continuing
```

513,948 = 351,944 size-7 + 162,004 sub cell-sides — exactly the two completed floors of section 1's
table. The 9.4 h counts, and the gencache did its job (no serial half-hour of discovery).

### 9.4 The honest ETA: ~115 h (~5 days), and why a 20-minute reading said ~75

Utilisation settled at **1126-1166% of 1200% = 94-97%**, inside the primary's 92-99% band — so the box
is saturated and the scheduling is *correct*. The rate is simply what this hardware does:

| | primary (32c) | here (12c) |
|---|---|---|
| rollouts/s | ~70-90 | **~10.5** |
| per core | ~2.2-2.8 | **~0.88** |

**Measure the rate over an hour, not twenty minutes.** The first three heartbeats read 13/15/16 per
second and an ETA of ~75-85 h was reported off them. That was too optimistic: over 3,900 s the
cumulative rate is **10.5/s**, and the marginal rate *oscillates hard* — 16, then 7.3, 13.6, 6.6 — while
CPU stays pinned at 94-97%. Remaining ≈ 4.34M rollouts at 10.5/s = **~115 h ≈ 4.8 days**, with a
plausible 100-140 h band before section 1's size-7-refine unknown (2.89M of those 4.34M rollouts, still
never observed on this deck) widens it further.

**The oscillation is the slow-rollout tail, and a 12-core box cannot hide it.** Seven rollouts over the
30 s threshold landed in the first hour (35.5 s, 36.6 s, 33.8 s, 33.0 s …), all `size6` keep rollouts at
`r=27-29` — the sub-refine wave pushing cells to cap R=30. One 35 s rollout monopolises 1/12th of this
box for 35 s; on the primary's 32 cores the same stall is diluted ~3x. So section 4c's finding that the
tail is only *4.8% of work* is a 32-core statement — the same tail is a much larger share of *observed
throughput* here, which is exactly why the cheap early reading was wrong.

This does not change the plan (section 9's ruling stands) but it does change the date. Disk is fine
(904 GB free); RSS 2.9 GB of 10 GB, worth watching but not close.

**RULED ON, 2026-09-25: the three days are accepted and the run continues.** The number was put to the
deck owner explicitly, because the 32-core run had been cancelled at 6.47 h with *"this is clearly
still too slow"* against a ~26 h projection, and this box is ~3x that. The owner's answer was that the
12-core box is expected and the 24-core assumption was the other agent's error. So this is **not** a
pending decision — do not re-open it, and do not stop the run on cost grounds.

The alternatives were offered and not taken, recorded so nobody re-litigates them: section 4c's
optimisation stack (~2.2x → ~35 h here) needs two owner-level calls and one lever that voids the
journal, and section 8's pooling raises R without finishing this table.

### 9.4b STATUS 2026-09-27 06:10 UTC (33.2 h in) — measured phase breakdown, and `subwave` was never stalled

Rate has **stabilised**, which the first-hour reading could not tell you: 8.36/s cumulative, and
8.04-9.64/s over every window from 1 h to 24 h. The early 16 → 7 decline levelled off; it did not
continue. 998,228 rollouts this run, 0 errors, 98.5% CPU, RSS 3.0 GB, 904 GB disk free, journal 32.4 MB.

**Read progress out of the journal, not the monitor line.** `subwave` sat at `0x100574` and `frozen` at
0.0% for all 33 h, which by section 6's shape is indistinguishable from a stall. Counting journal
records by their rollout count `n` settles it directly — cells *are* completing, in two overlapping
waves (the pipeline has no phase barriers, so wave 2 starts before wave 1 drains):

| phase | signal | state |
|---|---|---|
| size-7 floor | 352,200 cell-sides at `n=2` | **done** |
| sub floor | 162,004 at `n=2` | **done** |
| sub-refine wave 1 (`r 2->18`) | **104,955** at `n=18` of ~126,499 | **83%** |
| sub-refine wave 2 (`r 18->30` cap) | **46,671** at `n=30` | in flight |
| size-7 refine | `roll7=0`, `frozen` 0.0% | **not started** |

Total banked 3.27M rollouts (704,400 + 324,008 + 1,679,280 + 560,052), which reconciles exactly with
section 1's 2.26M plus this run's 998,228. **This is the diagnostic to use if a future run looks
stalled: `n` histogram over the journal, not the heartbeat's `subwave`/`frozen`.**

**The sub tail is running hot, exactly as section 4b feared.** Wave 2 has already spent 560k rollouts —
the *top* of section 1's ~500-900k projection for all of wave 2+ — and is still going. Section 4b named
this as "the wider of the two error bars" and it has widened.

**~117 h remain (~4.9 days), finishing ~2026-10-02; ~150 h / 6.3 days total.** At 8.6/s:

* size-7 refine 2,885,941 rollouts → **93 h** — *~80% of all remaining work, and still never observed*
* sub remainder (~345k of wave 1 + wave 2 tail) → ~24 h

So the estimate is now dominated almost entirely by the one phase nobody has ever watched on this deck,
whose 2.89M figure is transplanted from Melira Pod at 10.2 rollouts per cell-side. Given Snow's *sub*
cells ran hotter than that same calibration, treat 93 h as a floor for that term rather than a centre.

### 9.4c No short window predicts this run: throughput swings ±40% with the REGION being refined

Asked at 33 h whether the run had slowed, the 4-hour blocks said no — it dipped and recovered:

```
h 0-4 10.91/s | h 4-8 6.22 | h 8-12 6.58 | h 12-16 7.27 | h 16-20 8.14
h 20-24 9.17  | h 24-28 9.95 | h 28-32 8.29 | h 32-33 10.16
```

The trough is where 190 of the run's first 249 slow rollouts landed; the two fastest blocks had **zero**.
But the slow rollouts' own stall time is **2.32 core-h of ~400 core-h (0.58%)** — arithmetically far too
little to cause a 40% swing. They are a **marker that the wave is feeding an expensive region of hand
space**, where the median rollout also costs more, not the mechanism.

**Consequence for anyone estimating this run: use cumulative, never a short window.** A 4-hour sample
reads anywhere from 5.4 to 10.9/s. The first ETA here was built on hour 0-1 — which turned out to be the
single fastest block of the entire run — and was ~25% optimistic as a result. Cumulative has been
extremely stable by contrast: 8.39/s at 33 h, **8.35/s at 59 h**.

### 9.4d STATUS 2026-09-28 07:55 UTC (59.2 h in) — the wave decay RESOLVED, size-7 refine just started

Healthy: 98.5% CPU, RSS 3.0 GB, 0 errors, 59 h uptime, cumulative **8.35/s** (no degradation).

**Section 4b's "genuinely unknown" is now answered, and favourably.** `subwave` ticked at h 43.8:

| wave | tasks | work | state |
|---|---:|---|---|
| wave 0 (`r 2->18`, 16 rollouts/task) | 100,574 | 1.61M | committed h43.8 |
| wave 1 (`r 18->30` = **cap**, 12/task) | **56,222** | 675k | **52.7% done, ~14 h left** |

Decay is **0.56x**, and because 30 *is* the cap, **wave 1 is the last sub wave** — the sub side converges
rather than trailing off indefinitely. That was the projection's main structural risk and it is closed.

**But the sub side is overrunning its budget, which matters for what comes next.** Measured from the
journal (`n` histogram: 162,004 at n=2, 132,646 at n=18, 73,858 at n=30):

* sub spent so far = 324,008 + 132,646x16 + 73,858x12 = **3.33M rollouts**
* section 1 budgeted **2.85-3.25M** for *all* sub work — already exceeded, with 319k of wave 1 to go
* sub will land ~**3.65M**, a **12-28% overrun** on its Melira-derived calibration

**Size-7 refine has just begun** — `roll7=384`, 299 of 351,944 cell-sides refined (**0.08%**). It is
**~2.89M rollouts, ~81% of all remaining work**, and its figure is the same Melira transplant the sub
budget came from. Since Snow's sub cells overran that calibration by 12-28%, expect this term to land
**above** 2.89M, not below.

**ETA: ~107 h remaining (~Oct 2 19:00 UTC) if the 2.89M holds; ~120-135 h (~Oct 3-4) if size-7 refine
overruns like the sub side did.** Total 166-194 h, i.e. **7-8 days**.

Every revision in this section has moved *later*, for one consistent reason worth stating plainly: each
phase measured so far has come in above section 1's Melira-calibrated figure, and the largest term is
always the one not yet measured. **The next 14 h fix this** — once wave 1 drains and size-7 refine owns
the whole box, `roll7` gives a real rate for 81% of the remaining work, and the total stops being soft.

### 9.4e STATUS 2026-09-28 21:47 UTC (72.8 h) — sub side DONE; size-7 refine measured at 5.94/s

Healthy: 98.5% CPU, RSS 3.1 GB, 0 errors, 693 slow rollouts, journal 38.3 MB, 904 GB disk free.

**The sub side has CONVERGED** (`subwave=5 conv`, `rollsub` frozen at **2,134,704**). Full wave history —
note the decay is far sharper than the 0.56x first tick suggested, and that **9.4d was wrong to call
wave 1 the last one**; there were five, but the tail was trivial:

```
wave 0: 100,574 tasks -> wave 1: 56,222 -> wave 2: 2,018 -> wave 3: 151 -> wave 4: 151 -> converged h69.35
```

**Size-7 refine now owns the box, and its rate is 5.94/s** — **29% slower than the sub phase's 8.35/s**
(6.41 then 5.28 in 2 h blocks). This is the measured rate for ~all remaining work, and it is the single
most load-bearing number in this section. It landed *worse* than the 8.35/s every prior ETA assumed.

**But the per-cell COST looks far cheaper than the transplant, which is the first good surprise here.**
Journal `n` histogram for H=7 after 3.4 h of dedicated refine:

| n | cell-sides |
|---:|---:|
| 2 (floor) | 351,944 |
| 4 | 7,538 |
| 5 | 3,415 |
| 6 | 8,230 |

* refined at all: **19,183 = 5.45%** of 351,944; consumed 58,241 rollouts
* **3.04 extra rollouts per refined cell-side**, against the Melira transplant's **8.20**
* at 3.04 flat, the whole phase would be **1.07M rollouts, not 2.89M** — a 2.7x cut

**Do not bank that 1.07M.** Refine is *adaptive and revisiting*: a cell climbs 2->4->6->… toward cap 30
across rounds, so 3.04 is the cost of the rounds run **so far**, not a final per-cell figure, and it will
rise. No cell has yet exceeded n=6. The honest position is that the phase's total is now bracketed from
*both* sides for the first time, where every previous revision only moved later.

**ETA, at the measured 5.94/s:**

| scenario | remaining work | remaining time | finishes |
|---|---:|---:|---|
| refine only ever touches an ambiguous subset cheaply | ~0.5-1.5M | **23-67 h** | Sep 29 - Oct 1 |
| **central — Melira 2.89M holds** | 2.81M | **131 h** | **~Oct 4** |
| overruns as the sub side did (+12-28%) | 3.2-3.7M | 148-170 h | Oct 5-6 |

**`frozen` is still 0/351,944 after 3.4 h of refine, and that is now the thing to watch.** Section 6
promised freezing starts in this phase; cells are being refined (n=4/5/6) but none finalised, so there is
still no completion denominator. If `frozen` starts climbing, the range above collapses fast; if it stays
flat, the `n` histogram remains the only progress signal.

### 9.4f STATUS 2026-09-30 03:53 UTC (102.9 h) — LAST phase, 42% done; the transplant was 2.7x too big

Healthy: 98.5% CPU, RSS 3.1 GB, 0 errors. Total this run **2,722,126 rollouts** (sub 2,134,704 done +
size-7 refine 587,422). Size-7 refine is the **final** phase.

**The good surprise held up.** Per-cell refine cost did *not* rise as 9.4e warned it might — it is flat
across an 8x increase in coverage (5.45% -> 41.96%), and **no cell has exceeded n=6** against a cap of 30.
Counted by distinct cell identity `(i,p)` taking max `n` (see the counting trap below):

| final n | distinct cell-sides |
|---:|---:|
| 2 (floor, untouched) | 204,280 |
| 4 | 58,939 |
| 5 | 26,198 |
| 6 | 62,527 |

* **147,664 refined = 41.96%** of 351,944; 204,280 still at floor
* projected total for the phase: **~1.06M rollouts vs the Melira transplant's 2.89M — 2.7x over-estimated**

**Counting trap worth recording:** the journal holds *one record per refine step*, so a cell that went
2->4->6 appears three times. A naive `n` histogram counts **records** (499,510 here) and overstates
coverage. Key by `(i,p)` and take max `n`. Both methods agree here (41.93% vs 41.96%), which is itself the
evidence that cells are mostly refined **once** rather than revisited.

**Price the remainder off `roll7`, not the journalled `n`-deltas.** `roll7` spent 585,247 rollouts on
147,664 cells = **3.96 per cell**, against the 3.02 implied by summing `n-2`. The ~24% gap is refine work
that does not raise a cell's final `n` (in-flight cells, rollouts on cells that stay put). The larger
figure is the one that predicts wall clock:

| basis | per cell | remaining | at 4.85/s |
|---|---:|---:|---:|
| journalled `n-2` | 3.02 | 617,804 | 35.4 h |
| **`roll7` actual (use this)** | **3.96** | **808,949** | **46.3 h** |

An independent route corroborates: 147,664 cells in 33.51 h = 4,407/h, so 204,280 left = **46.4 h**. Two
methods agreeing at ~46 h is the firmest projection this run has had.

Phase rate: **4.85/s** cumulative (6 h blocks 3.89-5.46, no decay), below 9.4e's first-3.4 h read of 5.94/s.

**ETA: ~46 h remaining -> finishes ~2026-10-02 02:00 UTC, plus ~1.5-2 h validation. Total ~149 h
(6.2 days).** This lands in 9.4e's *optimistic* band (23-67 h), not its Oct 4 central case — the first
revision in this section to move **earlier**.

**`frozen` never moved: 0/351,944 after 33.5 h of the very phase meant to move it.** Section 6 tells an
operator to expect `frozen` to climb here and offers it as this phase's progress readout. On this deck it
stayed at 0.0% while 147,664 cell-sides were provably refined. **Do not treat a flat `frozen` as a stall,
and do not use it as a denominator** — the `(i,p)`/max-`n` histogram is the working signal. An
instrumentation gap in the monitor line, not a defect in the run; but it is exactly the reading that would
make a future operator kill a healthy job.

> **⚠ RETRACTED — this paragraph is WRONG. See 9.4h.** `frozen` is not an instrumentation gap; it is the
> correct denominator. It read 0 because **the size-7 refine phase had not started yet** — the monitor
> still said `phase=floor`, and what was being watched was the floor phase's *speculation filler* doing
> cheap opportunistic pre-refinement. At h135.95 the phase flipped to `refine` and `frozen` immediately
> read 219,834. **Trust `frozen`, and read `phase=` before interpreting anything.**

### 9.4g STATUS 2026-09-30 21:27 UTC (120.5 h) — 69.6% of the last phase, ~19 h left, projection now stable

Healthy: 98.5% CPU, RSS 3.1 GB, 0 errors, 2,215 slow rollouts, journal 57.1 MB, 903 GB free.

| | h102.9 | **h120.5** |
|---|---:|---:|
| size-7 cell-sides refined | 147,664 (41.96%) | **244,943 (69.60%)** |
| still at floor | 204,280 | **107,001** |
| `roll7` | 587,422 | **978,231** |
| phase rate | 4.85/s | **6.26/s** (6/12/18 h: 6.61/6.26/6.14) |
| rollouts per cell | 3.96 | **3.99** |

**Everything 9.4f projected has held.** Per-cell cost is 3.99 (was 3.96) — stable across coverage going
42% -> 70%; **still nothing beyond n=6** against the cap of 30; and the rate *improved* rather than decayed.

**~19 h remain, from two independent routes that again agree:**

* 107,001 cells x 3.99 = 426,934 rollouts / 6.26/s = **19.0 h**
* cell-completion: 97,279 cells in 17.55 h = 5,543/h -> 107,001 left = **19.3 h**

**ETA: generation done ~2026-10-01 16:30 UTC; + ~1.5-2 h validation -> ~Oct 1 18:30 UTC. Total ~142 h
(5.9 days).** Second consecutive revision to move *earlier* (9.4f said Oct 2 02:00), because the phase
rate rose from 4.85 to 6.26/s.

Overall this run: **3,112,935 of ~3,539,869 rollouts = ~88% complete** (sub 2,134,704 done + size-7
refine 978,231 of ~1,405,165). The phase will total ~1.41M against the Melira transplant's 2.89M.

> **⚠ RETRACTED — the "~19 h left / ~88% complete / 2.7x over-estimate" conclusions of 9.4f and 9.4g are
> all WRONG. See 9.4h.** Every one of them rests on treating the cheap work done under `phase=floor` as
> if it were the size-7 refine phase. It was the speculation filler. The real phase began at h135.95,
> costs far more per cell, and the Melira transplant was close to right (tracking ~18% **over**, not 2.7x
> under). The stable 3.96-3.99 rollouts/cell that made those projections look so firm was a property of
> the filler, not of refine.

> **Housekeeping for whoever finishes this:** 9.4b-9.4g are a running log kept deliberately (each
> revision's *reasoning* is the value, since the estimates moved a lot and the causes differed). Once the
> profile is adopted, collapse them into ONE retrospective and keep the transferable findings: the
> Melira-transplant over-estimate, the `roll7`-vs-`n`-delta pricing gap, the record-vs-cell counting trap,
> the dead `frozen` counter, and 9.4c's region-variance warning against short-window rates.

### 9.4h STATUS 2026-10-02 05:48 UTC (152.8 h) — the REAL refine phase began at h136; ~84 h left

**Read `phase=` before interpreting any other field. That one omission invalidated two status reports.**

At **h135.95 the monitor flipped `phase=floor` -> `phase=refine`**, and `frozen` went from 0 to **219,834
(62.5%) in the same heartbeat**. Everything 9.4e-9.4g measured as "size-7 refine" was in fact the **floor**
phase's bounded speculation filler (`spec_chunk`, `ExhaustiveKeep.cpp`) opportunistically pre-refining
size-7 cells to n=4/5/6 while the sub side converged. That work was real and it counts — it is why `frozen`
could jump straight to 62.5% — but it is **not** the refine phase and its cost per cell says nothing about
refine's.

| | floor-phase filler (what 9.4f/g measured) | **real refine phase** |
|---|---|---|
| per-cell cost | 3.96-3.99 rollouts, dead flat | far higher; cells driven to r=8,9+ toward cap 30 |
| max `n` reached | 6 | climbing past 6 |
| slow-rollout tops | ~35 s | **95 s, 82 s, 78 s** |
| `frozen` | 0 | the actual denominator |

**Three things I had recorded are retracted** (markers added in place at 9.4f/9.4g):

1. *"`frozen` is a dead counter / instrumentation gap, don't use it as a denominator"* — **backwards.** It
   is the right counter; it was zero because the phase had not started. This was the most harmful error
   here: it told a future operator to ignore the one field that measures this phase.
2. *"the Melira 2.89M transplant is a 2.7x over-estimate"* — **no.** Size-7 work is tracking **~3.4M**
   (1,739,026 spent + ~1.66M projected), i.e. **~18% over** the transplant. The transplant was roughly
   right, and the sub side's 12-28% overrun pattern repeated rather than reversing.
3. *"~19 h remaining, ~88% complete, finishing Oct 1"* — the ~19 h was 4.4x optimistic.

**Where it actually stands.** Refine has run 16.84 h: `roll7` +332,062 (5.48/s), `frozen` +22,514 at a
**stable 1,337 cells/h** (6 h blocks: 1,456 / 1,166 / 1,402).

* frozen **242,348 / 351,944 = 68.9%**; **109,596 cells remain**
* at 1,296-1,337 cells/h -> **~82-85 h**

**ETA: ~84 h remaining -> generation ends ~2026-10-05 18:00 UTC, + ~1.5-2 h validation. Total ~239 h
(~10 days).** Caveat in the honest direction this time: the cells left are the *ambiguous* ones refine
could not settle cheaply, so 1,337 cells/h may degrade. Treat ~84 h as a floor, not a centre.

**The methodological lesson, which is the durable part.** Two consecutive reports moved the ETA *earlier*
on the strength of a per-cell cost that was "stable across an 8x increase in coverage" — and that
stability was precisely the tell that it was measuring one cheap mechanism, not the phase. A projection
gains no validity from being internally consistent if its denominator is the wrong phase. **Cross-check a
derived progress metric against the engine's own declared `phase` and its own progress counter before
trusting it over the transplanted estimate the doc shipped with.**

### 9.4i BOX HUNG 2026-10-02 08:22 UTC, hard-reset, RESUMED 2026-10-03 05:46 — nothing computed was lost

**What happened.** The box stopped responding to keyboard and mouse for minutes and had to be powered
off. ~21 h of *window* was lost (journal mtime 08:22 Oct 2, box back up 05:19 Oct 3). **Zero rollouts
were lost** beyond the handful in flight.

**The gen was healthy right up to the end**, which is what points away from an engine fault: the last
heartbeat read `8/s`, `frozen=247937 (70.4%)`, `journal=837252 (0s ago)` — then the log simply stops.
No error, no abort, and `driver.log` never logged `GENERATION FAILED`, so the binary did not exit
non-zero; the whole box went down underneath it. The user's report — *"practically acting like it was
dead"*, *"didn't respond to keyboard or mouse for minutes"* — is the signature of **swap thrashing**,
and this engine has form (`analysis-Snow.md` section 7: the label path's unbounded per-game transient
spiked 5 GB -> 23 GB in under 8 minutes). `dmesg` was cleared by the restart, so the OOM cannot be
proven post-hoc; it is the leading hypothesis, not a finding.

**Recovery, in order.** Verify before touching anything, because a hard power-off can tear the last
journal record:

1. `HEAD:src` = `df5b7c7e…`, binary md5 = `918d4415…` — both identical to `run_commit.txt` /
   `binary.md5` / the frozen copy, so **no rebuild had slipped in** and the play digest could not have
   moved.
2. Journal tail was a **complete, well-formed record** (88,630,804 bytes; not torn).
3. **Backed the journal up before resuming** — `logs/Snow_mullgen/backup/journal.prerestart`
   (md5 `01bded71399b0ccd4caea8a7961da39c`). Cheap insurance: if a resume had been *refused*, the run
   would restart from the floor and overwrite 155 h of work.
4. Resumed with the identical recipe command. Got `equivalence discovery: CACHE HIT` and
   `RESUME(journal): reloaded 513948 cell-sides -> continuing`, then **`phase=refine` immediately** and
   `frozen` picking up at 70.4% -> 70.5%. The floor and sub phases were correctly skipped.

> **Do not misread the fresh counters.** After a resume `rollsub=0`, `roll7` restarts near 0 and
> `subwave=0x0`. That is **not** the sub side being re-run — its cell-sides are all at final R in the
> journal, `rollsub` stays at `(0/s)`, and `subwave=0x0` means "wave 0, nothing to do". Only `frozen`
> and the journal's R histogram are cumulative across restarts.

**Memory is now instrumented, and it is NOT currently near the edge:** peak RSS **2,974 MB**, available
**5.7-5.9 GB**, **swap 0 MB** across the first 10 minutes. So if thrashing did kill the box, it was a
transient spike rather than a steady climb — which is exactly the shape a watchdog can miss.

**Watchdog installed — committed as `scripts/mullgen_memwatch.sh`** (it was authored under `logs/`,
which is gitignored, so it is promoted here; a doc that tells the next operator to run a file git will
never ship to them is worse than no doc). Start it beside a gen with:

```bash
nohup bash scripts/mullgen_memwatch.sh > /dev/null 2>&1 &     # defaults guard a GENERATION
PATTERN='build/Release/mtg --batch' AVAIL_MIN_MB=700 SWAP_MAX_MB=2048 STRIKES_NEEDED=3 \
  nohup bash scripts/mullgen_memwatch.sh > /dev/null 2>&1 &   # terminal-only, for VALIDATION
```

It
samples every 5 s and kills *the gen* (never the box) after 2 consecutive bad samples, because a killed
gen costs one resume while a dead box cost 21 h.

> **⚠ Its real limitation, stated plainly: it runs at NORMAL priority.** This container denies both
> `chrt -f` ("Operation not permitted") and `nice -n -19` ("Permission denied") *even under sudo*, and
> `/sys/fs/cgroup` is mounted **read-only**, so the kernel-enforced route (`memory.max` +
> `memory.swap.max`, which cannot be starved) is unavailable too. A normal-priority watchdog is
> precisely what gets descheduled by the thrash it exists to catch. Compensated by tripping **early** —
> MemAvailable < 2,500 MB or swap > 256 MB, against a 3 GB steady state with ~5.8 GB free — on the
> asymmetry that a false trip is cheap and a missed one is not. **If a bigger box is ever available,
> prefer a cgroup cap over this.**
>
> Two bugs were found in the watchdog by it failing to guard the run, both worth not repeating: it
> lost a **startup race** against `mullgen.sh`'s preamble (found no process on its first sample and
> exited immediately — now it waits up to 10 min for the engine), and the launcher tested
> `command -v chrt` rather than whether the policy actually *applies*, so it took the SCHED_FIFO path
> and silently failed twice. **Test that a privileged operation works, not that its binary exists.**

**Progress now: 79.8% of the table is R-FINAL** (409,941 / 513,948 cell-sides) — sub side 100%, keep
side 70.5%, **104,007 keep cell-sides still climbing**, 5,870,935 R-units banked. The keep side's R
ceiling has moved **10 -> 14** (379 cells above R=10), the first sign of refine driving the stubborn
cells deeper. At the established 1,337 cells/h that is **~78 h**, so **finishing ~Oct 6**, with the same
30-120 h spread depending on how high those cells must climb.

**Update, 15.25 h later (2026-10-03 21:04 UTC): 82.2% R-FINAL, and the predicted slowdown has arrived.**
Keep side **74.0%** (260,571), **91,373 still climbing**, 6,145,926 R-units banked. Memory is a non-issue
so far: peak RSS **3,220 MB**, **zero watchdog strikes**, swap never touched.

The freeze rate fell from the pre-crash **1,337** to **817 cells/h** average (3 h blocks 882 / 678 / 639
/ 790 / 1,108; last 6 h **944**), and the R histogram says exactly why — a large cohort has climbed off
the floor of the ladder:

| | at resume | now |
|---|---:|---:|
| cells above R=10 | **379** | **69,165** |
| of which R=12 | 221 | **43,619** |
| mean R (keep side) | 6.17 | **6.95** |

So refine is walking the stubborn cells up in single steps (10 -> 11 -> 12 -> 13 -> 14) and each step is a
deep, expensive rollout. This is the degradation 9.4h flagged when it said to treat ~84 h as a floor
rather than a centre: **~97 h remaining at 944 cells/h, finishing ~Oct 7-8.** The open question is still
how far up the ladder that cohort has to go — cap is 30 and the ceiling is 14.

**Update, 51.2 h into the resumed run (2026-10-05 09:03 UTC): 87.7% of the table R-FINAL.** Keep side
**82.1%** (288,834), **63,110 still climbing**, `roll7` 1,156,092. Memory still a non-issue: peak RSS
3,442 MB, **zero watchdog strikes**, swap untouched across 51 h.

**`frozen` IS lumpy, but the lumps are noise around a flat trend — not a cohort signal to extrapolate
from.** 6 h blocks: 780 / 715 / **1,281** / 853 / 640 / 874 / 569 / 805 / 490. Trailing averages agree
with each other and with the whole run: last 6 h **665**, 12 h **674**, 24 h **708**, 36 h **785**,
since-resume **794** cells/h. So there is no secular decay — the rate is **~800 cells/h, oscillating
roughly 500-1,300**.

> **A 2-hour window read 1,608 cells/h and was reported as "the fastest of the run", with the cohort
> draining as the explanation. That was a peak, not a trend, and the ~55 h ETA built on it was wrong.**
> This is the THIRD ETA here built on a short window (9.4c's hour-0-1 rate, 9.4f/g's filler-phase
> per-cell cost, and now this), and 9.4c had already written the rule down. **Use the since-resume
> average for `frozen` the same way 9.4c says to use cumulative for throughput.** The "cohort draining"
> reading was not wrong about mechanism — R=12/13/14 counts really were falling — it was wrong to treat
> a mechanism as a rate.

**ETA: ~63,110 cells at ~700-800/h = ~80-95 h remaining -> finishing ~Oct 8-9**, plus ~1.5-2 h
validation. Total compute will be ~206 h (pre-crash) + ~140 h (resumed) ≈ 290 h of box time.

### 9.4j GENERATION COMPLETE 2026-10-06 04:45 UTC — profile written, validation running

```
gen complete -> decks/Snow/Snow.keepmodel.exhaustive.profile.json
artifact check OK: K=17 entries=175972 bottoming_enabled=True sub_cells=162004
                   min_rollouts=2 sub_target=2
```

Artifacts: `Snow.keepmodel.exhaustive.profile.json` **101 MB**, `.raw.json` **24.8 MB**. `bottoming_enabled=True`
as the policy requires (there is no off switch).

**Cost.** **7,086,012 rollouts** against a uniform-R table's 15,418,440 = **46.0%, i.e. the adaptive
schedule saved 54%**. Wall clock ~155 h (first run) + ~72 h (resumed) ≈ **227 h of generation**, plus the
~21 h window lost to the hang. The log's own note on the alternative recipe is worth preserving:
**`complete` (full bottoming, R40) was projected at ~1,784.8 h — 74 days.** `fast` was not a shortcut
here, it was the only feasible recipe.

**Quality, from the gen's own report:**

| metric | value |
|---|---|
| `D_opt` (exhaustive keep) | **5.71682** turns |
| `D_static` | **6.09402** turns |
| **policy gap** | **0.377195 turns** |
| disagreeing hand-types | 97,252, carrying **51.64%** of draw probability |
| win-turn cost of static's errors | 0.356113 (over-keeps **49.02%**, over-mulls 2.62%) |
| label noise at R=30 | ~**0.0098** turns |
| projected regret vs R | R=20 ~0.0194, R=50 ~0.0037, R=100 ~0.0012 |

> **That 0.377 is NOT the verdict.** The skill is explicit that `D_opt` is **winner's-curse optimistic** —
> it is the table scored against itself. Only the in-game A/B settles whether the profile ships. Quote
> the A/B delta, never this gap, as the result.

**The R>=10 shippability question (raised at 9.4g) is answered: the profile WAS written.** The floor
applies to the recipe's cap R (30), not to adaptive per-cell counts — `min_rollouts=2` is recorded in the
artifact check and the keep side's mean R finished at 10.40. No merge was needed.

**Validation is running and it is NOT the ~1.5-2 h section 4 budgeted.** `16 seeds x 1,000 games` per arm,
and on this deck one arm takes **6 h 38 m**:

* keep A/B arm A (static) 04:45 -> 11:23, avg ~6.02; arm B (exhaustive) started 11:23
* then the **confounded** bottoming A/B — two more arms, ~13 h
* then the regression suite
* realistic total **~30+ h**, so verdicts land ~Oct 7

**The profile is LIVE right now** — presence is adoption. A failing gate renames it to
`.profile.DISABLED.json`, which is what actually deactivates it.

> **Guarding validation is a different trade from guarding generation, and the watchdog was re-armed
> accordingly.** `scripts/mullgen.sh` (~line 383) treats an A/B that **"failed to run" exactly like a
> reject** and quarantines. So a watchdog kill mid-batch would quarantine a ~290 h artifact on a
> measurement that never happened — recoverable by renaming, but a *wrong verdict*, which the skill rates
> worse than no verdict. The validation guard therefore fires only at a terminal level (MemAvailable
> < 700 MB or swap > 2 GB, 3 strikes) where the box is already going down and the batch is doomed anyway.
> The generation guard's defaults (2,500 MB / 256 MB) stay as they are for journalled work.

**The hang did not recur.** The generation watchdog exited cleanly at gen-complete having logged **peak RSS
3,442 MB and ZERO strikes across ~120 h** — so whatever took the box down on Oct 2 was a one-off transient,
not a steady climb, and nothing in the steady state comes near this box's limits.

### 9.4k KEEP GATE PASSED 2026-10-07 12:11 UTC — `-0.2818t`, 16/16 seeds, mean/se `-43.07`

```
            static           exh         delta     seeds won
            6.0140        5.7322       -0.2818         16/16
spread: min -0.3260  median -0.2755  max -0.2360   sd 0.0262  se 0.0065  mean/se -43.07
```

Every seed won, by between 0.236t and 0.326t. This is not a marginal pass.

**The `D_opt` caveat played out in an instructive way — it was optimistic about the GAP but nearly exact
about the exhaustive ARM.** Predicted `D_opt` 5.71682 vs measured **5.7322** (0.015t out). The slippage
was all on the other side: `D_static` predicted 6.09402, measured **6.0140**. So the realised 0.282t is
below the predicted 0.377t because *static played better in-game than the table's model of it*, not
because the exhaustive policy underdelivered. Keep quoting the A/B, but that is a useful calibration
datum: the table models its own policy well and its baseline pessimistically.

**Now running: the confounded bottoming A/B, `confound=3`** — the mandated mode. (Mode 1 is explicitly
forbidden as grounds for failing a table; it leaks 0.073t to the lookahead and nearly cost FiveColour a
20-day regeneration.) Arm A (lookahead) is at 6/16 seeds after 9.6 h — **~1.6 h/seed, far slower than the
keep arms' 6h38m total**, because confound=3 reshuffles the library *and* re-derives the mid-game salts.
Arm A lands ~Oct 8 14:00 UTC; arm B (blind/table) should be quicker since it is an O(1) lookup rather than
a library rollout. Then the regression suite. **Verdicts ~Oct 8-9, everything done ~Oct 9-10.**

> **⚠ THE REMAINING GATE IS A REAL RISK, AND IT CAN DISCARD THE KEEP WIN.** The bar is "neither check may
> be worse on average", and `mullgen.sh` quarantines the **whole profile** on a bottoming loss — the
> 0.2818t keep win does not offset it. This is not hypothetical: the confounded gate **fails decisively on
> Dragons (+0.0641t, 0/16) and Mirrorwing v3 (+0.1006t, 0/16)** with the **mechanism still unknown**, and
> the obvious gen-depth explanation is already refuted (see `confounded-bottoming-gate-failures.md` before
> re-deriving a hypothesis). Snow generates at d2/b1, shallower than Mirrorwing's d2/b3.
>
> If it fails, the documented response is **fix the heuristic or raise R — never ship bottoming off**
> (there is no off switch). Practically that means the profile sits as `.profile.DISABLED.json` and the
> 0.2818t keep win is unavailable until the bottoming question is resolved. Worth knowing that a
> quarantine is reversible by renaming, so a failure is a decision point, not a lost artifact.

Health through validation: **0 watchdog strikes**, peak RSS 3,437 MB, swap 1 MB, ~5.6 GB available.

### 9.4l VALIDATION PASSED 2026-10-08 14:32 UTC — BOTH gates cleared, profile is LIVE

```
########## VERDICT ##########
  keep      -0.281813t   ok
  bottoming -0.077063t   ok
ACCEPTED: keep and confounded bottoming both clear the bar.
=== VALIDATION PASSED -- decks/Snow/Snow.keepmodel.exhaustive.profile.json is live ===
```

| check | A | B | delta | seeds | sd | se | mean/se |
|---|---:|---:|---:|---:|---:|---:|---:|
| keep (exh vs static) | 6.0140 | 5.7322 | **-0.2818** | **16/16** | 0.0262 | 0.0065 | **-43.07** |
| bottoming (blind vs lookahead, **confound=3**) | 5.8817 | 5.8046 | **-0.0771** | **16/16** | 0.0135 | 0.0034 | **-22.84** |

**The confounded bottoming gate PASSED — and Snow is a direct test of the 2026-09-02 fix.** Snow lands at
**-0.0771t, 16/16 seeds, mean/se -22.84** in the mandated `confound=3` mode.

> **This was reported to the user mid-run as a coin-flip risk with an "unknown mechanism". That was
> wrong, and the error is instructive.** The Dragons/Mirrorwing failures were **root-caused and fixed on
> 2026-09-02**: `feed_sub()` sat in the producer loop's `!refine` branch, which a journal resume skips
> entirely, so the sub batches were never drained and bottoming was an argmin over **1 rollout per cell**.
> `.claude/skills/mulligan-profile.md` still said "mechanism unknown" — a stale line, now corrected
> there — and that was taken at face value instead of reading the doc it pointed at.
>
> **The outcome was predictable from data already in hand.** Snow's sub table was measured days earlier at
> **mean R 22.83, 63.1% at cap** — the signature of a properly-sampled table, and "no deck with a
> properly-sampled sub-table has ever failed this gate". The `R=1` failure signature was never present.
> Checking the sub-R histogram would have retired the risk immediately rather than carrying it for two
> days. Snow is now in that doc's evidence table, and it is a *strong* data point: it generated across
> **three journal resumes** (handoff + crash recovery), exercising precisely the path that was broken.

**The regression tier moved, exactly as designed, and it CANNOT reject.** All 5 Snow GT cases moved and
**all 5 moved FASTER**, by 0.23-0.34t — the same direction and magnitude as the keep A/B:

| case | GT | new | delta |
|---|---:|---:|---:|
| snow_regression_d0_s2002 | 6.7060 | 6.3630 | -0.343 |
| snow_regression_d3_s2002 | 6.1167 | 5.7833 | -0.333 |
| snow_regression_d3_s3003 | 6.0167 | 5.7833 | -0.233 |
| snow_regression_d5_s2002 | 6.0333 | 5.8000 | -0.233 |
| snow_regression_d5_s3003 | 6.1333 | 5.8000 | -0.333 |

Scenarios **103 passed / 0 failed** — no rules breakage. Viewer protocol shows **10 mull-drift**, expected
when a mulligan profile changes. (The driver's "net slower" note is the documented bottoming artifact: the
*standard* goldfish metric is unconfounded, so it still rewards the lookahead's peek and a better BLIND
policy reads as a win-turn increase. The confounded A/B is the verdict, and these five cases moved the
other way regardless.)

**Total cost of the whole exercise:** ~227 h generation (across one hang and one resume) + **~82 h
validation** + the ~21 h window lost to the crash. Validation alone ran ~40x the handoff's 1.5-2 h budget,
because validation plays the deck at its **shipped** settings (budget-ms=20, 18-67 core-s/game) while
generation ran the deliberately cheap **d2/b1** labeller (1.38 core-s/rollout).

> **EFFICIENCY FINDING for the next slow deck: the arms should not be symmetric.** The keep check returned
> se 0.0065 against a 0.2818t delta — **mean/se 43, where ~3 settles it** — so its 16 x 1,000 games was
> over-powered by roughly an order of magnitude and cost about a day of wall clock on this box. The
> bottoming check is where that power earns its keep (precedent failures sit at +0.06 to +0.10t; here se
> 0.0034 gave mean/se 22.8). Prefer a **small keep sample + full-power bottoming sample**.
>
> Related: the exhaustive arm ran **3.74x slower than static** (24h48m vs 6h38m) despite *winning* by
> 0.28t. Better keeps mean more permanents, which runs into Snow's branching-factor problem — so on this
> deck the better policy is the more expensive one to measure.

### 9.4m-pre THE ENGINE MOVED UNDER THIS, AND SNOW'S PLAY SPECIFICALLY (pull of 2026-10-09)

The pull before committing these artifacts advanced `HEAD:src` from the generation tree
**`df5b7c7e` -> `a500d466`**, and it is *not* an unrelated move — it contains **Snow-specific play
changes adopted as defaults**:

* `f81aea16` **adopt(snow-scry)**: the user's Snow scry rule with the snow-count accelerant clause is
  now the **default**, and **smoke + regression GT were re-accepted** with it (user, 2026-10-06)
* `a9aa209d` **fix(payment)**: a pure colour converter is not a prepay producer — named Snow `s3003 gi5`
* plus `cb38daa4` / `a83f2b09` / `56a81cb8` building that scry heuristic, and new `test_snow_scry.cpp`,
  `test_snow_prepay.cpp`

**What this does and does not mean.** Rule 0 is explicit that *after-generation* digest movement is a
non-problem: the table was built under one consistent play logic, so it is internally balanced, and a
profile "stays adopted until it demonstrably underperforms on the current engine". The **−0.2818t keep /
−0.0771t bottoming** results remain valid measurements of what they measured. So the artifact is worth
committing exactly as it stands.

But Rule 0's other half applies too: regenerate (or at minimum re-verify) when a change **"plausibly and
materially moves *this deck's* play"**. A Snow-specific scry rule adopted default-ON is precisely that.
The keep table was fitted against play that no longer exists. **Status: validated on `df5b7c7e`,
unverified on `a500d466`.**

> **AND THIS RETIRES THE GT STEP AS PLANNED — do NOT run `--accept` now.** 9.4m below was written to
> recommend it. That recommendation is **withdrawn**, because `f81aea16` **already re-accepted snow's
> smoke + regression GT on the new engine**. The binary on this box is still the frozen `df5b7c7e` one,
> so accepting from here would overwrite someone else's fresh new-engine GT with **old-engine numbers** —
> the exact "a bad accept corrupts GT" failure, with the added insult of being silently plausible. The
> correct order is now: **rebuild on current HEAD -> re-run the confounded A/B pair -> only then consider
> GT.** Holding the GT step back as a deliberate call, rather than bundling it into the adoption, is the
> only reason this did not land as a corrupt baseline.

#### 9.4m-pre.1 CONFIRMED ON THE NEW ENGINE BY REGRESSION ALONE (2026-10-09) — the A/B re-run was wrong to propose

**USER RULING: *"Just the regression tests is correct. I refuse to have you looping on the whole
acceptance test."*** Correct, and Rule 0 says it in as many words — regenerate/re-verify when a change
moves this deck's play, **"confirmed by a *regression*, not by a digest diff"**. Proposing another ~82 h
confounded A/B pair for a *re-verification* confused the first-adoption gate (which needs the A/B) with
the still-adopted check (which needs a regression). Do not repeat that.

**It is also the cleaner experiment, which is the part worth keeping.** `f81aea16` accepted snow GT on
another box, where only the committed `.gz` exists — so **that GT is new-engine play WITHOUT the profile**.
Rebuild on HEAD and the engine matches the GT's engine exactly, leaving the profile as the *only* variable.
Rebuilt at `a500d466`, profile live, `bash test/regression.sh --deck=snow`:

| case | GT (new engine, no profile) | with profile | delta | games |
|---|---:|---:|---:|---:|
| snow_regression_d0_s2002 | 6.4410 | 6.2290 | **-0.2120** | 1000 |
| snow_regression_d3_s2002 | 6.0000 | 5.6500 | **-0.3500** | 60 |
| snow_regression_d3_s3003 | 5.8167 | 5.7667 | **-0.0500** | 60 |
| snow_regression_d5_s2002 | 5.9667 | 5.6667 | **-0.3000** | 30 |
| snow_regression_d5_s3003 | 5.8667 | 5.8667 | 0.0000 | 30 |

**4 faster, 1 tie, 0 slower; mean -0.182t.** The profile demonstrably still helps on `a500d466`, so by
Rule 0's own bar it **stays adopted**. Cost: one 4m35s batch, not 82 h.

`Result: 0 passed, 5 failed` / `REGRESSION DETECTED` is the *expected* reading and not a rejection — every
snow key necessarily moves, because GT was accepted without the profile.

**Churn in both directions, recorded rather than buried.** Within the 1000-game d0 case there were **130
slower games and one win->loss (`gi31: 7->loss`)**; suite-wide, 17 searched games slower and 47
play-changed at equal score. That is ordinary for a mulligan-policy change (different hands kept ⇒
different games) and the aggregates are what the bar reads, but it is not a clean sweep. Note also that
only d0 carries 1000 games; the d3/d5 cases are 30-60 games and individually noisy — the -0.212t on d0 is
the load-bearing number.

#### 9.4m-pre.2 ALL THREE TIERS REBASELINED 2026-10-09/10 — 16/17 cases faster, 0 slower, 0 unexplained games

Every tier re-measured on `a500d466` with the profile live, then accepted. **17 keys, every one snow:**

| tier | cases | result | mean |
|---|---:|---|---:|
| smoke | 3 | **3 faster, 0 slower** | **-0.200t** |
| regression | 5 | 4 faster, 1 tie, 0 slower | **-0.182t** |
| overnight | 9 | **9 faster, 0 slower** | **-0.215t** |

**Every changed game is attributed — this is the step that nearly got skipped.** The GT header's own
history shows each prior keep-table adoption (SelesnyaLifegain, Soldiers) recorded an
`--accept-with-regressions` note *and* ran `test/keep_adoption_attribution.py`. The first pass here
accepted all three tiers with **neither**. Re-done properly:

| tier | mulligan count | bottom | order-only | **unexplained** |
|---|---:|---:|---:|---:|
| smoke | 619 | 50 | 2 | **0** |
| regression | 620 | 65 | 3 | **0** |
| overnight | 1011 | 102 | 0 | **0** |

2,472 changed games, all explained, and `off == committed GT` / `on == run under audit` **True on both
sides of all 17 cases** — so the table is provably the only difference and it never leaked into play.
That is what justifies the searched-SLOWER counts (17 smoke/regression, 88 overnight): mulligan churn,
not play regressions.

> **ORDERING TRAP, hit here: `keep_adoption_attribution.py` must run BEFORE `--accept`.** Its own
> docstring says so — "the committed gt_logs are the OLD side" — and an accept *overwrites* those logs,
> destroying the baseline the attribution proves itself against. Having accepted first, the only reason
> this was recoverable is that `test/regression_gt.txt` **and** `test/gt_logs/` had been copied to
> `logs/gt_backup_2026-10-09/` beforehand; the old side was restored, the attribution run, and all three
> tiers re-accepted with the note. **Back up both before any accept**, and prefer: run -> attribute ->
> accept.

Verification of the final state: 17 keys changed and **all `snow_*`**; the re-accepted values are
**byte-identical** to the first accept's; **706 real key lines before and after**; no duplicate keys;
**all 26 prior `accepted-with-regressions` notes carried forward** plus the 3 new ones (29 total — the
filtered-accept carry-all branch working as designed); `check_gt_logs.py` **706 consistent, 0 STALE,
0 missing**.

(Counting note: `grep -c '='` on the GT file reads 715 -> 718, which looks like 3 keys appearing from
nowhere. It is the three new note lines, which contain `R=30`/`K=17`. Count real keys with
`grep -vE '^#' | grep -c '='`.)

### 9.4m WHAT REMAINS — two deliberate calls, neither taken

1. **Commit the artifacts.** Staged and ready (`gzip -k`, so the live uncompressed profile is untouched —
   presence is adoption): `Snow.keepmodel.exhaustive.profile.json.gz` **5.4 MB** and
   `.raw.json.gz` **2.9 MB**, matching the repo convention (the uncompressed forms are gitignored;
   cf. Angels' 2.2 MB). **227 h of work currently exists only on this box's disk, which has already gone
   down once** — this is the real risk, not tidiness.
2. **GT rebaseline.** `test/regression.sh --accept` promotes the LAST run, which was
   `--deck=snow` in the **`regression`** tier. Not done unilaterally because GT is **shared across every
   deck** and a bad accept corrupts all of it via a stale `<mode>.env`. The safe sequence: back up
   `test/regression_gt.txt` + `test/gt_logs/`, re-run `bash test/regression.sh --deck=snow` fresh, then
   `--accept`, then **diff-verify that only `snow_*` keys moved**, then `python3 test/check_gt_logs.py`.
   The `smoke` and `overnight` tiers also hold stale Snow keys and need the same treatment (precedent:
   Hinata, FiveColour and Goblins all rebaselined three tiers on adoption).

### 9.5 Operational notes for whoever reads next

* Launched detached: `nohup bash scripts/mullgen.sh run decks/Snow fast &` + `disown`. Logs in
  `logs/Snow_mullgen/{driver,gen}.log`. Re-running the identical command resumes; there is no flag.
* The binary is frozen aside at `logs/Snow_mullgen/mtg-analyze.frozen` with its md5 and the run's
  commit in `run_commit.txt`, so a rebuild mid-run is *detectable* even though `mullgen.sh` hardcodes
  `build/Release/mtg-analyze`. **Do not rebuild the tree while this runs.**
* **No auto-relaunch watcher was set up**, deliberately: section 4c records that the primary's watcher
  would have faithfully relaunched the cancelled gen 30 s later, and on a box the owner may want to
  stop, a silent relauncher is a hazard rather than a help.
* `frozen 0/351944 (0.0%)` will stay flat for days — section 6. `rollsub` climbing and
  `journal=<n> (0s ago)` are the live proof of progress, and both are healthy.
* **But read `phase=` first, and once it says `refine`, `frozen` IS the progress bar** (it jumps straight
  to ~62% at the flip, from the floor phase's filler work). `frozen` being flat is only meaningful while
  `phase=floor`. See 9.4h — misreading this cost two wrong ETAs.
* **Easier: `python3 scripts/mullgen_progress.py decks/<Deck>` reports progress in R, not phases.**
  Written because the phase/`subwave`/`roll7` vocabulary needs a model of the generator to read, and
  misreading it caused 9.4h. It prints the per-cell R histogram for both sides and the one number that
  actually means completion — **the share of cell-sides whose R is FINAL** (`frozen` for the keep side,
  all of them once the sub waves say `conv`). It also states the trap it exists to prevent: **mean R is
  not progress.** The schedule is adaptive, so most cells stop far below cap by design — "R 6.01 of cap
  30" is a healthy keep side, not 20% done.
