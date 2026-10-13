# Escalation-skip allowance: a per-deck calibration stage in the value-leaf pipeline

**Status (2026-10-13): proposed.** The user asked whether a new deck should get a stage that decides
whether it opts in to the escalation skip's rollout allowance. The answer proposed here is yes: a phase
**E.6** in `scripts/valueleaf.sh`, between E.5 (adopt the leaf) and F (the mulligan-generation contract).
Nothing below is built yet except the fitting tool (`scripts/esc_skip_calibrate.py`).

## What is being calibrated

The value-leaf hybrid commits a line with the cheap value-leaf probe and, when that line is unverified
and shallower than the deck's trust depth, escalates to a heuristic-rollout search on the remaining
budget. The take rule then keeps the heuristic line only if it reached the deck's take depth `T`
(`value_fallback_take_at`). An escalation that cannot reach `T` is computed and thrown away.

The **up-front skip** (`MTG_ESC_SKIP_PREDICT`, default ON, `TurnSolver.cpp`) predicts, before any
heuristic pass runs, how deep the ladder could get, and skips the escalation when that is short of `T`.
Each pass is predicted from the probe's own record at that depth as

    h[d] = pc[d] + R[d] * pl[d]        (probe units + allowance x probe leaves)

and the ladder's start gate is walked on those predictions. The fleet runs `R = 0`: the bare tree walk,
optimistic, measured lossless (16 decks x 480 held-out games, zero play changes). A deck may opt in to
`R > 0` with the sidecar key `escalation_skip_r`, which skips more of the escalations its take rule would
discard. It is opt-in because a larger allowance can skip an escalation that would have been taken.

**`R` must be per deck and per depth.**
* **Per deck:** rollout cost per leaf differs by about 30x across the fleet. Melira's 10th percentile is 2-5
  units per leaf against 60-200 elsewhere, and a fleet-wide `R = 50` skipped takeable escalations there.
* **Per depth:** the cost per leaf falls with depth, because a deeper leaf has fewer turns left to roll
  out. Selesnya's median is ~200 at d1, 119, 77, 48 and ~23 at d5. One constant overprices the deep passes.
  The start gate extrapolates the next pass from the ratio of the last two, so the skip then refuses
  escalations the ladder would have completed and taken. Selesnya at a constant 74 had three real play
  changes in ~22k held-out games, and one of them turned a T8 win into no win.

## The fit

`scripts/esc_skip_calibrate.py <batch.err>` works from a batch run at `R = 0` with `MTG_ESC_SKIP_LOG=1`,
where each `[esk]` line is one escalation decision.
* It uses every ladder escalation that ran (`path=2 skip=0`).
* `R[d]` is the **10th percentile** of `(hc[d] - pc[d]) / pl[d]`, the real heuristic pass's cost beyond
  the probe's tree, per probe leaf.
* Depths with fewer than 20 samples are priced at 0, and the vector is padded with 0 to 5 depths, because
  past the end of the list its last entry applies.
* Single-path decks (`escalation_cap > 0`) record no per-depth heuristic cost, so they get no fit and
  stay at `R = 0`.

The low quantile is deliberate. The prediction stays below most real passes, so the skip leans towards
running the escalation.

## Evidence that this shape works (2026-10-13)

The vectors were fitted on older seeds, then tested in a held-out paired A/B against `R = 0`
(`logs/esk/manifest_rv.json`, seeds 35xxxxx). Every changed game was replayed alone, as a one-game batch
chunk, to separate real play changes from batch thread-history flicker
(`batch-run-to-run-nondeterminism.md`).

| deck | games | units | play changes |
|---|---|---|---|
| Selesnya | 6000 | -7.8% | 0 |
| Mirrorwing | 6000 | -1.0% | 0 |
| KittyV3 | 2400 | -3.2% | flicker only |
| PD | 2400 | -1.3% | gi1608: no win -> T8 win |
| FungusB | 2400 | -1.9% | 0 |
| Giants | 4000 | -0.8% | 0 |

0 games slower in 23,200. Adopted: Selesnya (vector replaces 74), PD, FungusB and Giants. Mirrorwing
(70.5) and KittyV3 (43.3) keep their earlier constants, which also passed and save a little more.

## The proposed stage (phase E.6)

It runs after E.5 because the allowance is a measurement of the probe, which is the value leaf. A
regenerated leaf changes `pc` and `pl`, and today a regeneration carries the old key over unchanged
(`MulliganProfile::escalation_skip_r` warns about this). It runs before F, and before any mulligan
generation, because the allowance changes play slightly and the keep table is fitted to play. That keeps
the per-deck pipeline strictly serial: profile, then value leaf, then skip allowance, then mulligan.

The phase has five steps:

1. **Skip if not applicable.** Skip single-path decks, and decks whose calibration log shows fewer than
   ~200 ladder escalations: there is too little to save and too little to fit.
2. **Calibrate.** Run one pooled batch at play settings with `esc_skip_r = 0` and `MTG_ESC_SKIP_LOG=1`,
   then fit the vector. Size the batch to the escalation count, not to a game count.
3. **A/B.** Run one pooled batch on fresh seeds, disjoint from calibration and from the suite, with a
   paired `R = 0` arm against an `esc_skip_rv` arm. Size it to the deck's cost: ~6000 games for cheap
   decks, ~2400 for Kitty-class decks (about 20 minutes of the box).
4. **Isolation check.** Replay every game whose digest changed as one-game batch chunks
   (`seed + gi`, `game_index = gi`) at both arms, and compare game digests. Identical alone means
   flicker. Different alone means a real play change; score it by win turn, with no win counted as worse
   than any win.
5. **Verdict.** Adopt only if no real play change is slower and the net is <= 0 (*"+1 is not neutral.
   0 is neutral."*). Adopt means writing `escalation_skip_r` into the live sidecar in this tree,
   uncommitted, like E.5. A real slower game means **not adopted**, with the game named. Every outcome
   is appended to `<queue>/DECISIONS.txt` and the run continues.

Cost: two pooled batches per deck, minutes for most decks and well under an hour for the most expensive.
That is small against the value-leaf generation it follows.

**Backfill.** Run the same stage once for every deck that already has a value leaf. A standalone entry
point that runs E.6 alone would cover it.

## Open questions for the user

* **Should E.6 adopt automatically (as E.5 does for the leaf), or only stage the vector for sign-off?**
  The proposal assumes automatic adoption with the strict bar above, recorded in `DECISIONS.txt`.
* **q = 0.10 is one point measured.** q05 was also measured: 0 play changes anywhere, with about 60% of
  q10's savings. The stage could try q10 and fall back to q05 when q10 shows a real slower game, rather
  than adopting nothing.
