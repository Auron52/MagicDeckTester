# The keepgen precompute filler: never idle behind a straggler

Status: **implemented 2026-09-22**, lever `MTG_KEEP_PRECOMPUTE` (default 1; `=0` restores the
previous behaviour for an A/B).

Background: `docs/design/adaptive-batched-keepgen.md` (the generator),
`docs/design/keepgen-producer-barrier-and-durability.md` (the earlier barrier this design removed,
and the FiveColour incident the fix here must not recreate).

---

## The defect

`AppendBreakpointVariants`-style deferral is not the issue here; this is the **floor -> refine phase
transition**. Refine cannot begin until two global aggregates are fixed:

* **`Dopt`** reads the sub-tables and requires them final.
* **`vg`** is pooled from the r0 snapshot of **every** size-7 cell.

An aggregate over all items cannot be computed from a partial set, so the transition is a genuine
data dependency, expressed as `floor_incomplete = any_below_floor || sub_remaining > 0 ||
!sub_converged`. That is a universal quantifier, so its latency is the **maximum** over N items — and
with a heavy-tailed rollout cost distribution, the maximum is set entirely by the tail.

The design already knew this and mitigated it with **floor-tail speculation**, whose comment claims
it *"kills the floor->refine barrier"*. It does not. It **defers** it, by a constant:

```
const long long cont_lookahead = 4LL;        // "internal constant, not a knob"
const long long spec_budget = cont_lookahead;
```

With floor `r0 = 2`, every live cell runs to `2 + 4 = 6` and is then forbidden from doing anything
more; `spec_saturated` turns the filler off and the cores idle.

**Measured, Fungus 2026-09-22, recipe `complete`, 24 cores:** `roll7 = 663,144 = 110,524 x 6` — to
the unit. The deferral bought **55 minutes**; the following **85 minutes** ran at **2.88 of 24
cores** with 2 of 62,444 sub-batches outstanding, `frozen` at 0, and the journal silent for 89
minutes. The run never entered refine.

### Why the cap was there, and why it was half-right

The constant is not arbitrary. Pre-refs the freeze test **cannot** run, so speculated rollouts are
folded untested and `compute_refs` must retro-test them under the **fixed floor `vg`** rather than
the rolling one. Speculation depth therefore does not merely schedule work — it **moves the
statistical procedure**, deciding how much of each cell is judged against the coarse reference. On
those terms, bounding it is correct.

What made the bound look *necessary* is an implementation coupling, not a data dependency. In the
fold loop, advancing a cell's sample count and applying its freeze test are the **same operation**:

| | needs the global threshold? | safe to run ahead? |
|---|---|---|
| computing a rollout | **no** — `run_one` is pure in `(seed_base, r, w, pd)` | yes, to `r_max` |
| folding it into the accumulator | **yes** | no — stays capped |

The real dependency is *threshold -> fold*. The one enforced was *threshold -> compute*, which was
never true. Capping compute because fold had to be capped is the whole defect.

---

## The fix: a memo, not a scheduling change

Deliberately **not** implemented by relaxing the fold, which would have made output-identity an
argument about ordering. It is a memoization, which makes identity structural:

* **Task kind `-1`** computes one rollout into a staging cache (`pre_val` / `pre_have`) and folds
  nothing: no `fold_mtx`, no `in_flight`, no `roll7`, no journal. It cannot advance the run, and must
  not be able to.
* **Kind-0 workers check the cache before rolling.** `run_one` is pure in `(seed_base, r, w, pd)` —
  a property the existing design *already* relies on, since any worker may pick up any kind-0 task —
  so a staged value is bit-identical to the one that worker would have computed.
* `fed[]`, the fold, `compute_refs`' reconcile window and `vg` are **untouched**. The generated raw
  is byte-identical to a run that never speculated.
* A kind-0 racing a kind -1 on the same `(i, pd, r)` is harmless: both compute the same number.

### Backfill only — the interlock that matters

The filler **refuses to enqueue unless the queue is under a quarter full**, so kind 0/1/2 always have
room and `feed_upto` never waits behind a precompute. This is the guard against recreating the
FiveColour failure, where an unbounded filler outran the `sub_refine_step()` it was filling for
(3,955,796 cell-sides x 4 = 15.8M throttled feeds, 140-230 h, cores 100% busy and `frozen` at 0). A
filler that can outrun its progress step is a barrier in disguise; this one yields the instant real
work appears.

The cache is sized like `slot[]` and **declined entirely** above 2 GB — it is pure optimisation, so
refusing it is always safe.

### Why reproducibility was kept

The alternative was to let the extra rollouts into the estimates: more samples is strictly more
information, and the artifact format already tolerates unequal per-cell counts (the multi-machine
merge pools chunks with `V = sum/count`). Rejected for now, on the user's call 2026-09-22: a profile
is the **apparatus a decklist comparison is measured against**, and scheduling-dependent sample
counts put apparatus noise straight back into that comparison — the same reason `deck_compare.py`
shares one apparatus across arms rather than regenerating per arm.

Note the honest limit of that argument, also the user's: two different lists get different bucket
counts and different cell sets, so their profiles are not commensurable objects anyway. What
reproducibility really buys is the **same-list** axis — this list today vs next month, or under
engine v1 vs v2.

Using every computed sample remains a live option, and it is a **policy** question, not an
architectural one: compute-ahead and merge-what-you-computed are now separate parameters. It would
need its own measurement, because extra samples feed the rolling `vg`, which sets other cells'
freeze thresholds — so "use everything" propagates and is not a local change.

---

## Instrumentation

The monitor gains `pre=<enqueued> hit=<consumed>`, printed only once the filler has engaged.
Both are needed: `pre > 0` is what makes an identity A/B non-vacuous (otherwise it compares two runs
that never speculated), and `hit/pre` is how much of the speculation refine actually used rather than
discarding at a freeze.

## Measurement

**burn, recipe `complete`, `MTG_KEEP_PRECOMPUTE=1` vs `=0`, same binary, same seed**
(`logs/precompute_ab/`, generated on scratch copies under `logs/` — never in `decks/burn/`, because
a keep profile is presence-gated and generating in place would have put a new table LIVE on burn):

| artifact | result |
|---|---|
| `burn.keepmodel.exhaustive.raw.json` | **byte-identical** |
| `burn.keepmodel.exhaustive.profile.json` | **byte-identical** |

**Read that result narrowly, because it is narrower than it looks.** `pre=` never appeared in arm A's
monitor: on burn the producer stays busy feeding sub-table batches, speculation never saturates, and
the precompute filler — gated on saturation — **never engaged at all** (`roll7` sat at exactly
`21,890 x 2`, the floor, while the sub-tables ran). So this A/B establishes that the change is
**inert on a generation that never idles** — a real regression gate over the new task kind, the cache
allocation and the worker branch — and establishes **nothing** about the path when it is active.

That is not a fixable property of the test: the filler engages only when a straggler starves the
queue, and the only deck known to reach that state is Fungus, which cannot currently complete a
`complete` run (see the uncharged-greedy-walk doc). **The active path is therefore unexercised
end-to-end as of 2026-09-22.**

What stands in for it, deliberately, is `MTG_KEEP_PRECOMPUTE_VERIFY=1`: on every memo hit it re-rolls
the rollout and asserts the memo matches, printing `MISMATCH=` and a loud diagnostic otherwise. That
converts the identity claim from an argument about `run_one`'s purity into something a run checks.
**Anyone resuming this work should run the first Fungus-scale generation with VERIFY=1** and confirm
`pre>0`, `hit>0`, `MISMATCH` absent, before trusting the path.

## What is NOT claimed

* **It bounds the idle; it does not abolish it.** Once every live cell is precomputed to `r_max`
  there is genuinely nothing left but the stragglers. That is real exhaustion rather than an
  artificial cap — and it is why the engine-side tail
  (`docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md`) still has to be fixed. **This
  change cannot make an unbounded rollout terminate.**
* **No wall-clock claim for the Fungus run**, which was killed rather than completed.
* Speculative work past a cell's freeze point is still discarded. That CPU is spent — but only where
  the producer had nothing else to feed.
