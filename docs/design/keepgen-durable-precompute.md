# Make precomputed rollouts durable across a restart

**Status: PROPOSED (user-directed 2026-10-03). Not implemented.**

> *"We should probably redesign so that these items can be folded in even on a restart."*
> — USER, on learning that ~2.7 M precomputed rollouts existed only in RAM

## The gap

When a straggler holds the floor-completion barrier, the precompute filler keeps every worker busy
computing rollouts that refine will later ask for. Measured on the Fungus candidate-b K=17 run:
**9.6 h of stall, ~2.7 M rollouts computed on 22.8 cores — roughly 210 core-hours — none of it
durable.**

`ExhaustiveKeep.cpp:4041` is explicit about why: a kind -1 task is *"PRECOMPUTE: fill the memo, fold
NOTHING."* It writes `pre_val[ps]` / `pre_have[ps]`, which are plain in-memory vectors. Because
nothing is folded, nothing is journaled — which is exactly why the journal sat unwritten for 9.6 h
while the box was at 95% utilisation. Kill the process and all 210 core-h vanish; only the 369,081
already-journaled records survive.

This is the condition `keepgen-producer-barrier-and-durability.md` §5 Fix 4 asked to be *warned*
about — *"work being fed while journal age exceeds ~N minutes ... a direct violation of 'resumable at
any point'"*. The warning was never built, and neither was the durability.

## Why "just fold them" is the WRONG fix

The obvious alternative — have the filler commit its values into each cell's accumulator — is
**blocked, and for a good reason.** Nothing may be merged past `r0 + spec_budget` before refs fix:
`compute_refs`' reconcile replays the freeze test over `(r0, cnt]` and truncates at the first hit, so
if speculation had merged to varying depths the reconcile's answer would depend on how far the sweep
happened to get. `:4502-4507` says so directly — re-testing mid-sweep *"would let compute_refs fire
on a half-speculated state ... a different answer per run."*

**So the fold boundary must stay where it is.** The right change is to make the COMPUTATION durable
while leaving it uncommitted — i.e. persist the memo, not the accumulator. The user's phrasing
("folded in on a restart") is satisfied by reloading into `pre_val`/`pre_have`, from where the normal
consumer at `:4081` folds each value at the moment it was always going to.

## The precedent to extend — this is not a new subsystem

The journal **already carries per-rollout values** for exactly this reason:

* `sv` (optional field on `journal_append`): *"the per-rollout values for r in [r0, n), so a resumed
  `compute_refs` can replay the freeze test over the speculated range instead of summing zeros."*
* reloaded into `std::vector<std::vector<double>> resumed_spec`, per size-7 cell-side.

Two limits are all that stand between that and this proposal:

| | `sv` today | needed |
|---|---|---|
| range | `r0..n`, bounded by `cont_lookahead` (**<= 4** doubles/cell-side) | up to `r_max - (r0+spec_budget)` = **24** |
| when written | floor phase only | also while the filler runs |
| destination on resume | `resumed_spec` -> the reconcile | `pre_val`/`pre_have` -> the memo |

## Sketch

1. **Record shape.** Reuse the `sv` idiom: one record per cell-side carrying a contiguous `r` range.
   The filler is **depth-first per cell** (`pre_r` runs `fed[k]..r_max-1` before `pre_cursor`
   advances), so a cell naturally yields one complete contiguous range — **one record per cell-side,
   not one per rollout.** ~88,000 records for 2.1 M values, matching the existing
   "one record per cell-side completion" idiom rather than fighting it.
2. **Distinguish it from `sv`.** A precompute record is UNCOMMITTED and must never seed
   `floor_sum/sumsq/cnt` or the reconcile. Give it its own kind (e.g. `f=2`, or a `pre:[...]` field)
   so a replay cannot mistake it for folded work. **Do not overload `sv`.**
3. **Reload into the memo only**, setting `pre_have` after `pre_val` (the existing release-order
   discipline at `:4053-4054`).
4. **No new resume gate.** A precomputed value is valid under exactly the conditions every other
   record is: `bucket_fp && deck_fp && seed_base && K && max_mull && equiv_seed && R &&
   PlayIdentityAllows`. A rollout is a pure function of `(seed_base, r, w, pd)`, so if the gate passes
   the value is bit-identical. Out-of-range indices are already ignored on replay.
5. **Cadence.** Append on cell-side completion, under the existing `journal_mtx`, and let the rolling
   backup (`MTG_JOURNAL_BACKUP_S`, default 900 s) cover it. Do not add a second persistence path.

## THE HAZARD — this exact feature has shipped a bug before, read it first

`resumed_spec` exists **because carrying per-rollout values across a resume went wrong once**: on a
resume the per-rollout `slot` array was empty for rollouts a previous invocation had run, so the
reconcile *"summed zeros and could freeze a cell on a fabricated prefix"* — **905 cell-sides with a
count-inconsistent sum**, and it was invisible until terminal records became authoritative.

So the failure mode here is not "we lose the optimisation", it is **a wrong value entering a cell's
verdict and silently corrupting the keep table.** Two consequences:

* **`MTG_KEEP_PRECOMPUTE_VERIFY` gains a new and more important case.** Today it re-rolls on every
  memo hit and asserts the memo matches. Extend it to the RELOADED memo specifically — that converts
  "bit-identical because `run_one` is pure" from an argument into a checked property on the one path
  where it has previously failed. Its own comment notes the filler *"only engages when a straggler
  starves the queue -- a condition a small deck never reaches, so an end-to-end A/B on one is inert"*;
  a resume test must therefore FORCE the filler, not hope for it.
* **Test with the existing harness, not a new one.** `keepgen-resume-exactness.md` already has a
  control-vs-SIGKILLed-twin rig (`logs/resume_proof/`, ~5 min) asserting `cmp` on the final raw. The
  assertion to add is: kill inside the filler window, resume, and require (a) the final raw is
  byte-identical to the uninterrupted control, and (b) the rollouts re-executed after resume are
  ~the in-flight set, **not** the whole precompute bank. (b) is the one that proves durability;
  (a) alone passes today by recomputing everything.

## Cost

~2.7 M doubles plus keys. Batched per cell-side, order **55-80 MB** on top of a 38 MB journal. The
user has already ruled on this trade, in the context of losing a journal:

> *"Disk space is cheap. Losing work like this is extremely expensive."*

## Priority — be honest about the interaction

**Fixing the wave barrier (`keepgen-producer-barrier-and-durability.md` Defect 3) reduces the value
of this change**, because the filler only engages when the state machine has nothing admissible to
do. Fix the barrier and long stalls become rare, so there is less in-RAM work to lose.

It does not fall to zero, and the two are independent:

* the **finish** barrier recurs — a run cannot complete until its last live cell lands;
* any kill, crash, OOM or machine reboot mid-run forfeits whatever is in the memo;
* a deliberate stop-and-resume (a shared box, a rebase, a handoff) is a normal operation here.

**Recommended order: barrier first, durability second.** But note that durability is the cheaper and
lower-risk of the two, and it is the one that would have saved the 210 core-hours actually lost on
this run.

Related: `keepgen-producer-barrier-and-durability.md` (§5 Fix 4 asked for the warning; Defect 3 is
the barrier), `keepgen-resume-exactness.md` (the harness), `adaptive-batched-keepgen.md` (the journal
design), `mulligan-rollout-performance-floor.md` (why stalls are long in the first place).
