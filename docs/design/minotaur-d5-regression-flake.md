# The minotaur d5 regression cells are NONDETERMINISTIC in the full tier

**Status:** open, pre-existing, reproducible on demand. Found 2026-08-30. This is a determinism
defect in the regression harness's core contract, and it means the ground truth committed for these
two cells is a snapshot of one coin flip.

## The observation

Two cells flip between exactly two play digests across identical runs of an identical binary. The
**score never moves** — 4.9600 and 4.9040 to four decimal places every time. Only the chosen line
changes.

```
minotaur_regression_d5_s2002   52937a4626b089b6 (GT)  <->  cf3f51e7546fa082
minotaur_regression_d5_s3003   8ec3f5ad5d923ca2 (GT)  <->  bddf6e1b6a550719
```

Measured, 5 full-tier runs per arm, same machine, sequential (never concurrent):

| build | full-tier runs failing ≥1 minotaur d5 cell |
|---|---|
| HEAD + the Mirrorwing candidate-card work | **4 / 5** |
| control: `9b54274f` (upstream's minotaur value-leaf adoption, unmodified) | **2 / 5** |

`regression.sh --deck=minotaur` is **stable**: 3/3 pass. The flake needs the full 80-cell pool.

**It is pre-existing.** The control has no local engine changes at all and still flakes. 4/5 vs 2/5
is not a real difference at n=5 (Fisher p ≈ 0.5), so there is no evidence the Mirrorwing work
changes the rate.

## Why it matters more than the failing cells

`9b54274f` **rebaselined three tiers** and committed GT for these cells. Whatever the run that
produced it happened to roll is now the recorded truth, and roughly a third to four fifths of
subsequent runs will disagree with it. Any A/B that touches minotaur d5 will read this as signal.

The score being stable is the small mercy: the *metric* GT compares (`avg`) is unaffected, so
measurements that only read `avg` are safe. It is the **play digest** that is unreliable.

## It needs a large, HETEROGENEOUS pool — isolation ladder

The cell is perfectly deterministic on its own. Contamination appears only as the pool grows:

| pool | runs | result |
|---|---|---|
| 1 job — this cell alone, 32 threads | 5 | **stable, = GT** |
| 2 jobs — + `slivers_regression_d0_s2002` | 5 | stable, = GT |
| 32 jobs — every d5 cell | 1 | stable, = GT *(under-sampled)* |
| **80 jobs — the full tier** | 5 + 5 + 5 | **flaky, 40–80%** |

So this is cross-**job** contamination inside one `mtg --batch` process, not intra-job threading:
32 threads on a single job is stable, which rules out a plain race within the job's own games.

### REFUTED: `ProfileCache` eviction

`ProfileCacheCap()` defaults to 3 and the batch runner shares one cache across every job, so
eviction was the first suspect. It is **not** the cause — with eviction switched off entirely the
flake persists at the same rate:

| arm (5 full-tier runs each) | failing |
|---|---|
| default cap 3 | 4 / 5 |
| **cap 64 — no eviction** | **3 / 5** |
| control `9b54274f`, default cap | 2 / 5 |

Note that raising the cap stops eviction but leaves the cache **shared**, and if anything makes
sharing *more* persistent. Shared-mutable-state-in-the-cache is therefore not excluded; only the
eviction mechanism is.

## Leading hypothesis, not confirmed

**`thread_local` state surviving a job boundary.** The batch runner reuses a worker thread across
consecutive jobs, and those jobs are *different decks*. `TurnSolver.cpp` carries a large amount of
`thread_local` state (`g_decision_epoch`, `g_cantrip_order_site`, `g_bp_hand_before`,
`g_bp_plan_casts`, memo tables, scratch vectors, …). Anything semantic that is not reset when a
thread moves from deck A to deck B becomes a channel between them, and *which* thread picks up which
game varies run to run — giving exactly this signature: stable alone, stable in a small pool, flaky
in a big heterogeneous one, at a rate that depends on how much foreign work shares the process.

The `enum-memo` / `solve-memo` tables are the specific things to check first (`MTG_ENUM_MEMO`,
`MTG_SOLVE_MEMO`, plus their `_VERIFY` variants, which look purpose-built for this). If a memo key
does not fully capture deck identity, an entry created while running deck A can be hit while running
deck B. That is the same class of defect as the canonical-memo-key order-invariance work.

One note for whoever picks this up: `test/regression.sh`'s manifest emitter is deterministic — job
order is fixed by the case list, so manifest ordering is *not* the source. Its comment "Ordering is
lossless — results are unchanged" is the claim this defect contradicts, but the ordering it refers
to is not what varies.

## Method warning — how this was nearly misdiagnosed twice

Both wrong turns came from **concluding on a single run of a flaky thing**:

1. An `OnGoblinEnters` projection line was blamed, on a probe showing filtered-pass / full-fail. The
   filtered tier passes with *or* without that line; the probe never isolated the code.
2. `ProfileCache` eviction was then declared the root cause on the strength of **one** clean run at a
   raised cap — which, at a ~20–60% pass rate, is barely evidence at all.

Both collapsed when the same binary produced 80/80 and then 79/80 back to back. **On this suite,
treat any single run as one sample of a distribution until proven otherwise**, and repeat every arm
of a comparison before believing the contrast. The suite's whole premise is bit-reproducibility,
which makes it very easy to read one run as ground truth.

## What would close it

1. **Get a minimal reproducer.** The ladder above narrows it to "needs the big heterogeneous pool";
   bisecting which *other* decks must be present to poison minotaur d5 would name the channel. The
   d5-only pool (32 jobs) was sampled once and passed — re-sample it properly before believing it.
2. **Test the memo hypothesis:** run the full tier ~5x with `MTG_ENUM_MEMO=0 MTG_SOLVE_MEMO=0`. If
   the flake vanishes, the memo key is the channel. (Expect it to be slow — the memos are a large
   speedup — so budget for it.)
3. **Audit the job boundary.** Whatever `thread_local` state a worker carries should be reset when it
   picks up a job belonging to a different deck, the same way `ClearForcedMulligan()` had to be added
   when the pooled runner started reusing engines across games.
4. Re-derive GT for `minotaur_regression_d5_s2002` / `_s3003` once runs are reproducible. Until then
   those two keys are **unreliable** and must not be used as an A/B baseline.

**Do not size any of these from a single run.** Every arm above is 5 runs because 1 run is worth
almost nothing here — see the method warning.

## Related

* `docs/design/etb-cascade-projection-gap.md` — a separate, latent projection gap found alongside
  this. Its fix is blocked on this defect, because a genuine GT movement cannot currently be
  distinguished from the flake.
* The earlier, still-unexplained batch-pool contamination episode (closed as irreproducible). Same
  smell — a pooled-batch result that would not reproduce. This one reproduces in ~2 minutes and is
  the better handle.
