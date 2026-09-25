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
