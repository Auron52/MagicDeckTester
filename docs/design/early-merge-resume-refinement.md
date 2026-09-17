# Early merge + resume-refinement-from-a-pool (feature gap)

**Status:** deferred idea, user-flagged. Not being worked on. Captured here so it isn't lost.

## The gap

Today the exhaustive-keep generator has two ways to combine rollout work, and neither lets you
**merge two partial chunks and then keep refining the merged result as one stream**:

- **Merge (`MTG_KEEP_MERGE`)** pools *finished* raw sidecars element-wise (sums per-cell
  `sum`/`sumsq`/`count`), rebuilds the policy at the pooled R, and re-emits a merged raw listing
  `pooled_seed_bases`. It is **terminal** — the merged raw is an output, not a resumable gen state.
- **Refinement** (the in-run journal, and `PROBE-CARRY`) advances a **single seed-stream**: samples are
  a pure function of `(seed_base, r, w, pd)`, and the next rollout for a cell is `r = count`. `PRIOR_RAW`
  looks like it could seed a higher-R run, but on **unchanged `play_digest` it reuses the prior exactly**
  (ships it, no re-roll) — it is a change-detection carry, not a refine-upward path.

So to reach a higher effective R you must refine **each seed independently** to that R and pool the
finished chunks. You cannot take a machine's partial floor, hand it to another machine, and have that
machine *continue refining the combined base*.

## Why it currently works this way

Per-seed independence is what makes distributed pooling **lock-free**: two machines pick disjoint
`seed_base` prefixes, each refines its own stream to whatever R it reaches, and the merge sums them with
an overlap guard on `seed_base`. Reaching R = R_a + R_b needs no coordination. Early-merge-then-refine
breaks that model: if you merge seeds {A,B} into a base and then refine with a fresh seed C, the output
raw's `seed_base` is C but it *contains* A and B samples. Unless `pooled_seed_bases` is faithfully
carried through, a later pool against a separate seed-A or seed-B run **double-counts**. The r-index
sequence also gets holes (a merged cell with `count=2` skips `r=0,1` for the refining seed) — harmless
statistically (samples stay distinct and unbiased), but it muddies provenance.

## The low-R window is clean (why this is doable, esp. early) — user insight

The per-seed-independence argument is really about **adaptive freezing**, not about the samples
themselves. Adaptive keep and adaptive bottoming **stop rolling a cell once its decision is
statistically clear**, to spend rollouts only where they change the policy. So two chunks refined
*independently* to R freeze *different* cells at *different* R, and their pooled per-cell R distribution
diverges from what a single coherent run would have produced ("profiles done with a lot of chunking late
skip fewer/other cells through adaptive bottoming and keep").

But **freezing only engages once a cell has enough samples to decide — it does not happen in the first
few R.** Below the freeze threshold every cell gets every rollout, so pooling low-R chunks (two floors,
or any pre-freeze chunks) is **exactly** a single run at the summed R — there is no adaptive divergence
to worry about. Two R=1 floors pool to a clean R=2; continuing refinement from that pooled R=2 base is
identical to a coherent run reaching R=2 then refining on. **The divergence that justifies per-seed
independence is a HIGH-R (post-freeze) effect and simply does not apply to the early stages.**

Consequence: an early merge-and-continue is *correct by construction* in the pre-freeze window, and that
window is exactly where pooling partial progress is most useful (getting off the floor fast across
machines). The only remaining requirement is the provenance bookkeeping below — not a correctness
barrier, just plumbing.

## What a clean version would need

- A merged raw that is a **valid resumable state**: carries the union `pooled_seed_bases`, and refines a
  cell by drawing from a *designated fresh* `seed_base`'s stream (so added samples never collide with any
  already-pooled seed), while counting the pre-pooled samples toward the cell's R target and variance.
- The final raw must record **every** contributing `seed_base` so the overlap guard still protects future
  pools. That is the crux: make "continue refining a pool" preserve the same provenance invariant the
  terminal merge already enforces.

## Practical impact (why it was deferred, not urgent)

For the case that surfaced it (Creature Giving: a complete weekend R=1 floor at seed 1000000 + a 45%
R=1 floor at seed 2000000), the missing feature buys ~1 floor sample of refinement head-start — negligible
against an R30 refinement. The chosen path — refine one floor to R30, then `MTG_KEEP_MERGE` the other
floor in as a proper second layer — reaches the same final sample set, provenance-safe. The feature would
matter more for genuinely distributed *partial* refinement (several machines each contributing partial
progress on the same cells), which is where end-stage-only pooling is a real convenience cost.
