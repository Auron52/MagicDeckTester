# Keepgen kill/resume: what is exact, and what is not

**Status:** MEASURED 2026-08-23. Data recovery is exact; the size-7 refine SCHEDULE is not.
Not fixed. The measurement apparatus is `logs/resume_proof/` (regenerable in ~5 minutes, below).

## Why this was measured

`--gen-mulligan` runs are long, restart-prone (Mirrorwing was killed and resumed repeatedly), and
the design claims resume is "zero-waste, byte-identical" (`ExhaustiveKeep.cpp`, journal-replay
comment). That claim had never been tested end to end, because testing it needs a run that
COMPLETES, and every deck we resume on takes hours.

burn at `MTG_EQUIV_DEPTH=1 MTG_EQUIV_BUDGET=1` finishes in **46 s** on 32 cores while exercising
the whole path (discovery, floor, fused sub-tables, adaptive sub-refine, size-7 refine, journal
write + replay). That makes the experiment cheap enough to run repeatedly.

The raw sidecar carries no wall-clock or timestamp field -- only fingerprints and per-cell
`sum`/`sumsq`/`count` -- so `cmp` on `<stem>.raw.json` is a valid exactness test.

## Method

```
# control (uninterrupted)
MTG_KEEP_EXHAUSTIVE=1 MTG_EQUIV_DEPTH=1 MTG_EQUIV_BUDGET=1 \
MTG_KEEP_ROLLOUTS=<R> MTG_KEEP_ADAPTIVE_BOTTOM=1 \
MTG_KEEP_OUT_RAW=logs/resume_proof/A.raw.json \
MTG_KEEP_OUT_PROFILE=logs/resume_proof/A.profile.json \
MTG_EQUIV_CACHE=logs/resume_proof/gencache.json \
build/Release/mtg-analyze decks/burn/burn.txt --cards-json src/cards/data/cards.json --seed 1000000

# twin: identical command, SIGKILLed every N seconds and relaunched until it finishes by itself
bash logs/resume_proof/run_b.sh        # R=4,  kill every 8s   -> 6 kills
bash logs/resume_proof/run_d.sh        # R=16, kill every 12s  -> 9 kills
```

SIGKILL (not SIGTERM) on purpose: the journal flushes per record, so a process kill is the case
the design says it survives.

## Results

| pair | cap R | kills | `cmp` | rollouts | cell-sides more / fewer / equal | same-count-different-value |
|---|---|---|---|---|---|---|
| A vs A2 | 4 | 0 | **identical** | 122,986 = 122,986 | 0 / 0 / 37,682 | 0 |
| A vs B | 4 | 6 | differ | 122,986 -> 135,020 (+9.8 %) | 12,034 / **0** / 25,672 | **0** |
| C vs D | 16 | 9 | differ | 258,872 -> 302,461 (+16.8 %) | 14,522 / **539** / 22,645 | **0** |

Per hand size, both resumed runs:

| H | 7 | 6 | 5 | 4 | 3 | 2 | 1 |
|---|---|---|---|---|---|---|---|
| cell-sides differing | all of them | 0 | 0 | 0 | 0 | 0 | 0 |

### What IS proven

1. **An uninterrupted run is byte-reproducible.** A == A2 exactly, across 32 threads and
   ~123 k rollouts whose completion order is nondeterministic. The fold order is fixed and it holds.
2. **No completed rollout is ever lost or corrupted by a kill.** Across 75,864 cell-side
   comparisons in the two experiments, the number of cell-sides that both runs sampled to the same
   depth but that carry a different `sum`/`sumsq` is **zero**.
3. **Sub-tables (H=6..1) resume exactly.** Byte-identical in both experiments, at both caps, with
   6 and with 9 kills. The fused sub-floor + adaptive sub-refine path is exact.
4. Progress really is persisted mid-cell: the journal grew monotonically across kills
   (14,157 -> 27,899 -> 42,149 -> 55,112 -> 67,509 -> 86,953 records at R=4).

Point 2 is stronger than it looks. A cell's rollout `r` is a pure function of
`(seed_base, cell, pd, r)`, so a cell's `sum` is fully determined by its final `count`. Zero
same-count-different-value cells means the rollout stream is *exactly* recovered; the only thing a
resume can change is HOW MANY rollouts a cell ends up with.

### What is NOT proven -- the size-7 refine schedule diverges

The journal persists per-cell accumulators, the terminal freeze flag (`f=1`), the r0 prefix and the
fixed refs. It does **not** persist the refine producer's own scheduling state -- which cells it has
already handed a wave, how many waves it has run. On resume the refine loop re-plans from the
reloaded accumulators, and takes a different trajectory:

* at cap R=4 the divergence is one-sided (+9.8 %, 12,034 cell-sides get one extra rollout, none get
  fewer) -- the cap masks it;
* at cap R=16 with 9 kills it is **two-sided**: +16.8 % rollouts overall, but **539 cell-sides
  finish with FEWER samples than the uninterrupted run**, up to 13 fewer.

So a resumed gen both wastes work and, on some cells, ships a *less* refined estimate than an
uninterrupted gen would have. The values are not wrong -- same estimator, different sample sizes,
and pooling is designed to handle unequal counts -- but:

* a gen's output is **not reproducible** once it has been interrupted, which is a property the raw
  sidecar's whole fingerprint-and-pool apparatus is otherwise built on; and
* the drift grows with the number of resumes, and Mirrorwing is precisely the deck we resume most.

## Root causes, and what was fixed (2026-08-23)

Three separate defects, found by narrowing with the harness above. All three are in the size-7 path;
the sub-tables were exact throughout.

### 1. The replay's "highest n wins" rule cannot represent a TRUNCATION (fixed)

Floor speculation runs a cell past `r0` with **no freeze test**; `compute_refs`' reconcile then
replays the test over `(r0, cnt]` and **truncates** `cnt` to the first hit. That truncation is
journaled as a terminal (`f=1`) record whose `n` is *below* the progress record written during
speculation -- so a pure highest-`n` replay discarded it, and the cell resumed above its own freeze
level, unfrozen, and refined on. Always in the "more" direction, and it saturates after ONE resume
(3 kills produced the same +17.1% as 9).

Latent until progress records existed (`69d5ba77`): before them the only pre-refs record sat at
exactly `n == r0`, which can never outrank a truncation.

**Fix:** a terminal record applies unconditionally and LOCKS its cell-side. Order-independence -- the
property the highest-`n` rule exists for -- is preserved.

### 2. The reconcile replay reads an EMPTY slot array after a resume (fixed)

The reconcile reads per-rollout values out of the in-memory `slot[]` array. On a resume that array
holds nothing for rollouts a *previous* invocation ran, so the replay summed **zeros** and could
freeze a cell on a fabricated prefix. This was invisible while defect 1 was discarding the
reconcile's output; fixing defect 1 alone exposed it immediately -- 905 cell-sides with a
count-inconsistent sum, the first time this harness ever saw a wrong VALUE.

**Fix:** floor-phase progress records carry `sv`, the speculative values for `[r0, cnt)` (bounded by
`cont_lookahead`, so <= 4 doubles per cell-side); replay refills `slot`/`have` from them.

*This is the lesson of the pair: fix 1 without fix 2 is actively harmful, and only an
end-to-end exactness gate catches that.*

### 3. The rolling vg is not journaled -- and its schedule is not deterministic (partly fixed)

`vg_roll` (the freeze shrink target) is a GLOBAL quantity re-derived from every cell's accumulators
as the completed frontier advances, and it was reset to the floor vg on resume. Pinning it
(`MTG_KEEP_REFS_OFFSET=0`) removed 98% of the cells that came back UNDER-sampled (539 -> 9), which
is what identified it.

**Fixed:** `(vg_base, vg_roll)` is journaled write-ahead -- *before* it is published, because the
instant it moves a concurrent fold may freeze a cell against it, and that verdict has to be
recoverable. Restored on resume, so refine re-enters at the same vg epoch.

**NOT fixed -- and this is what still blocks the gate.** `recompute_vg` is triggered off
`min_live_c`, computed from **unlocked racy reads** of `S7.cnt`. So which epoch a given fold sees is
a function of producer timing, not of the data. Evidence: adding the (tiny) write-ahead I/O under
`fold_mtx` moved the UNINTERRUPTED control by one rollout, 258,872 -> 258,873. The earlier
`A == A2` result was the "lagged safe margin" holding by luck, not by construction. **A resume cannot
be byte-exact while the freeze target is scheduled off a racy frontier.**

### The unit that matters: the SHIPPED DECISION TABLE

Sample counts are an internal detail. What ships is `exhaustive_keep.entries` -- a keep/bottom
decision per hand-type. Weighting each hand-type by its hypergeometric draw probability gives the
only figure that describes the deck as played:

| | kills | hand-types decided differently (of 10,945) | share of REAL opening hands |
|---|---|---|---|
| pre-fix | 9 | 62 | **0.72 %** |
| post-fix | 9 | 6 | **0.0042 %** |
| post-fix | 3 | **0** | **0 %** |

`bottom_keep` was IDENTICAL in every run ever compared here -- only `keep` moved.

So the pre-fix bug shipped a policy that mulliganed differently on ~1 in 140 real opening hands: a
genuinely different deck. What is left is ~1 in 24,000 at nine resumes and bit-identical at three,
which is the realistic case. That 170x gap is the whole argument for stopping: the residual is
smaller than the noise of the thing it perturbs.

### How much the residual actually costs -- measured, and the reason to STOP here

The pre-fix divergence was a ONE-SHOT: 3 kills produced the same +17.1% as 9, because defect 1 fired
once (at the single floor-speculation phase) and then saturated. With defects 1+2 fixed that one-shot
is gone, and what remains is a small drift that scales with the NUMBER OF RESUMES:

| | kills | divergent cell-sides (of 37,706) | extra work | wrong values |
|---|---|---|---|---|
| post-fix, rolling vg | 3 | **4** | **+0.00 %** | 0 |
| post-fix, rolling vg | 9 | 3,410 | +3.79 % | 0 |
| post-fix, vg PINNED | 18 | 9,513 | +11.42 % | 0 |

~380 cell-sides and ~0.4 % per resume. A gen killed a handful of times -- the realistic case -- is now
within a rounding error of exact.

Note the last row: **pinning the vg (`MTG_KEEP_REFS_OFFSET=0`) is NOT a cheap workaround.** It removes
the under-sampling direction entirely (`fewer` = 0, which is what identified defect 3) but diverges
*more* overall, so at least one per-resume source besides the vg schedule remains unidentified. It
also costs -0.5 % on the uninterrupted run.

**Recommendation: leave it.** What exactness buys is reproducibility and provenance -- it does not buy
correctness. The values are already exact, the estimates unbiased, and pooling is designed for unequal
counts. Against that:

* the remaining source is not isolated, so the next step is more measurement, not a known edit;
* the confirmed part is a hot-path change. `min_live_c` is an O(NC) scan (~218k cell-sides on
  Mirrorwing) already run every producer iteration on unlocked reads. Making its epoch schedule exact
  means either holding `fold_mtx` -- the single mutex guarding every rollout commit across 32 workers
  -- across that scan, or restructuring vg into a pure function of level, which then has to define
  what a frozen cell (which stops early) contributes at levels it never reached;
* any change to the freeze schedule moves every generated profile, so the shipped keep profiles would
  need regeneration to stay comparable -- and Mirrorwing's is the expensive one;
* this is the path that just produced three defects, two of which only became visible *because* of a
  partial fix.

Revisit only if a profile ever has to be reproduced bit-for-bit.

### Where the three fixes leave it

| | divergent cell-sides | under-sampled | extra rollouts | wrong values |
|---|---|---|---|---|
| before | 15,061 | 539 | +16.84 % | 0 |
| after | ~3,400 | 25 | +2.2 – 3.8 % | 0 |

86% of the divergence and ~80% of the wasted work are gone; no value is ever count-inconsistent. The
gate (`cmp`) still **fails**, and will keep failing until defect 3's scheduling is made deterministic
-- e.g. deriving vg at FIXED levels (`r0 + e*refs_offset`) from a snapshot taken under `fold_mtx`
when the frontier provably first covers that level, rather than whenever the producer happens to
observe it. That is a change to a hot, carefully-tuned path and was not attempted here.

### Ruled out

* **The `fed[]` cursor.** Suspected of making every resume re-run all banked rollouts; it is
  correctly seeded from the reloaded counts (`fed[k] = S7.cnt[i][pd]`, "resume: continue past
  reloaded cnt"). Resume does not redo banked work.
* **Floor-phase vs refine-phase resumes.** Killing *only* during refine (3 kills) reproduced the
  full divergence, so it is not a floor-phase-specific effect.

### Open attribution

The progress records (`kJournalStride`, commit `69d5ba77`, 2026-08-22) changed what a resumed run
reloads: cells now come back part-refined instead of at `r0`. Whether the schedule divergence
predates that change or was introduced/reshaped by it has **not** been tested -- it needs the same
A-vs-B experiment built at the parent commit. Note that the divergence is about producer state that
was never journaled under either behaviour, so a fix is needed either way; the attribution only
tells us whether it got better or worse.

## If this gets fixed

The fix is to journal the refine producer's scheduling state alongside the accumulators, so a
resume re-enters the schedule where it left off rather than re-planning. The exactness test above is
the acceptance gate: `cmp A.raw.json B.raw.json` must pass at R=16 with >= 9 kills.

## Related

* `docs/design/adaptive-batched-keepgen.md` -- the continuous/journal design this tests
* `docs/design/keepgen-cost-concentration.md` -- the Mirrorwing collapse that motivated the audit
* Commit `69d5ba77` -- journal progress records; `1d43bd85` -- sub-table progress reporting
