# Iterative-deepening start gate: the TRIAL (`MTG_ID_GATE_TRIAL`, default ON)

Status: built, units + smoke/regression A/B + held-out A/B measured (wip/idgate). GT NOT accepted; that is a deliberate
parent step (the change moves many decks).

## The problem
`FullSearchLine`'s ladder START GATE refuses pass k+1 when `estimate = c_k x ratio` exceeds `alpha x remaining`
(ratio = c_k / c_{k-1}, or the bootstrap `kDefaultGrowth = 6.0` after one pass). Maze's End s3 T4 (the USER's reference
position) is the motivating case: pass 1 used 5,103 of 18,000 units, the estimate was 30,618 > 1.1 x 12,897, so pass 2 was
refused. The refused pass really costs 735 (warm memos) and finds T5. The search ended T6 at the shipped d5 b20.

## Census (smoke tier, MTG_IDG_CENSUS=2 shadow trials, trial-OFF arm)
86,204 gate consultations. On rollout-leaf ladders the estimate does not predict whether a pass fits. For measured-ratio
passes, a shadow trial within `alpha x remaining` fits 69/46/47/50/54/50/54% across the estimate/allowance overshoot bins
floor(log2) = 0..6. For the 6.0 bootstrap it falls 44% -> 5%. No cutoff on any predictor separates fit from not-fit. The
bootstrap median actual/estimate is 0.167 (the real pass-2 growth median is 1.0, p90 30x); the measured ratio has median 1.0,
p10 0.33, p90 103. At refusal the median budget left is 50-59%. Raw data: `logs/idgate/census_smoke_perdeck.txt`.

## Design: start-and-abort, not a better point estimate
The gate keeps its estimate. A pass the ESTIMATE refuses (not one refused by another gate: single-pass reserve, priced
gate) is STARTED anyway as a TRIAL. Its overrun ceiling is the gate's own allowance: `alpha x remaining`, halved under
`g_reserve_value_pass`. A trial that reaches the ceiling rolls back through the existing abort path. The prior pass
commits, and a strictly better win the trial already proved is kept (`s_id_anytime`).
- Inert at an unlimited budget (the gate never refuses there), monotone in budget, units-only (deterministic,
  thread-invariant). It re-prices nothing that is findable at b0; it only lets a finite budget reach passes it used to
  decline. This is the shape the USER allows: "A budget may re-price work, never make a line unfindable".
- Scope: rollout-leaf ladders only (`!vl_active`): the heuristic ladder and the hybrid's escalation. Value-leaf and
  leafless probes are excluded. Their relaxed gate refuses only with ~0% left, a leafless pass already stops at
  exhaustion, and their leftover funds the FIT pass / escalation that follows.
- Lever: `MTG_ID_GATE_TRIAL` (EnvOn default true), heurarm slot `ID_GATE_TRIAL`. Manifest: `"flags": {"MTG_ID_GATE_TRIAL": false}`.
- Instrument: `MTG_IDG_CENSUS=1` prints one `[idg]` line per top-level gate consultation (print-only).
  `=2` adds shadow trials, which PERTURB play and units; census only, never an A/B arm.
  `MTG_ROLLOUT_STATS` counts `gate_trials` / `trial_aborts` / `trial_abort_units`.

## Results
SEE THE SECTIONS BELOW (filled in from the measured runs).
