# The size-7 keep gate never tests a cell at the floor -- and flip_eps 0.10 was priced on bottoming only

**Status: DEFERRED (USER 2026-10-20: "let's keep it like this for now and we'll run some tests after
the fact").** No code change. Behaviour stays as below until the measurement in §3 is run.

## 1. What the generator actually does

The refine stop rule (`freeze_hit`, `src/analyzer/ExhaustiveKeep.cpp`) freezes a size-7 cell-side once
`P(flip) = 0.5*erfc(d/(se*sqrt2)) <= flip_eps`, evaluated "at each new level c >= r0". In practice a
size-7 cell is never tested at `c == r0` (2):

* refs fix -- and with them the first freeze test -- only after EVERY size-7 cell has folded its floor
  (`!any_below_floor` gates `compute_refs`), so no cell crosses `r0` during refine, where the fold
  would test it;
* `compute_refs`' reconcile replays the test over `(r0, cnt]` only, starting at `r0 + 1`.

So the effective minimum before a size-7 freeze is **3** rollouts in every keep table generated to
date. (The bottoming sub-tables differ: an adaptive wave marks sub-cells from the floor state, so a
sub-cell confident at 2 is never refined.)

## 2. Why it matters (Bruna, 2026-10-20)

Applying `freeze_hit` exactly to Bruna's journal at refs fix (flip_eps 0.10, se_prior 8, refs vg and
`Dopt_ref[pd][1]`): of 1,180,780 live cell-sides sitting at n=2, **713,342 (60.4%) pass the gate at
c=2** and each spends one more rollout before it can freeze -- ~713k rollouts, ~14-23 h on that box.
(Consistent with the cells that HAD been speculated: 103,361 of ~162k froze at their first test, c=3.)

Testing at the floor is NOT a free fix. Every flip_eps measurement so far was made with this de-facto
minimum of 3, so freezing at 2 is an unmeasured regime. Worse, the documented 0.02 -> 0.10 evidence
(`adaptive-bottoming-cross-deck-test.md` §3b: "very nearly free") prices the **bottoming** gate. No
pricing of flip_eps on the **size-7 keep gate** was found -- 0.10 may be under-validated there on
every deck, independently of the floor question.

## 3. The measurement to run (offline, no generation)

Against shipped high-R full raws (slivers_vial R=60, burn R=60, CritterLifegain R=40): Monte-Carlo
each size-7 cell-side's rollout stream from its cell mean/variance, apply the keep stop rule with
minimum test level {2, 3} x flip_eps {0.02, 0.10}, rebuild the keep decision, and report per deck:
absolute policy quality (expected win turn of the resulting keep policy, judged against the full-R
table) and rollouts used. Quote absolute quality at each setting, not deltas (the cap-R trap in
`adaptive-bottoming-cross-deck-test.md` §1).

Decision for the USER from it: (a) whether the keep gate should test at the floor, (b) whether 0.10
holds for the keep gate at all.
