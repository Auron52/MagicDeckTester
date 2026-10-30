# Pruning proof reports — prepared without running simulations

Helpers live together in primary `build/scratch/cyclone-finish/`:
`reconstruct_prunes.py` and `prunes_proofs.py`. No source changes or engine
runs were made while preparing these reports. Read-only Git calls use
`GIT_OPTIONAL_LOCKS=0`.

Phase C and iso92 metadata were refreshed to clean frozen source tree
`ce3431d90d7f7ee88b47a791ce61595740f907f0`. Their manifests were preserved;
pre-freeze metadata is backed up separately. Later docs-only commits do not
invalidate the source-content hash.

## iso92: authoritative all-salt and actually-forced-order report

After both forty-salt process modes finish with full traces:

```
python3 build/scratch/cyclone-finish/prunes_proofs.py --root build/scratch/cyclone-finish/final-prunes iso92
```

This writes `iso92/trace-eligibility-report.json`. The existing
`reconstruct_prunes.py ... report iso92` entrypoint now uses the same logic
and writes `iso92/report.json`.

Every salt must have both outcome files and full JSON traces, correct actual
seed109092/global game index92, and trace/result agreement. Missing data
fails loudly rather than being counted as an excluded or completed pair.

Report BOTH the forty-salt mean and the predeclared T1 eligibility subset.
Eligibility requires all of:

* Same physical opening hand in the same order and identical mulligan
  sequence; missing evidence does not qualify.
* Exactly one T1 main1 Brainstorm put-back in each arm, with the same
  observable prefix and Brainstorm cast/payment before the intervention.
* Only DRAW annotations between that cast and its put-back, with the same
  physical drawn-card multiset. The logging order may differ for cards drawn
  and immediately put back, so it is deliberately compared as a multiset.
* Normal places `[Ulamog, the Infinite Gyre, Emrakul]`, reversed places
  `[Emrakul, Ulamog, the Infinite Gyre]`, with the exact same two physical
  card identities reversed. Each put-back's kept vector must agree with its
  annotated top-first order.

Each excluded salt has explicit reasons. Zero eligible pairs produces
`UNPROVEN_no_eligible_same_T1_pairs`. Positive `reversed - normal` favours
the fix. Unwon games score9. This is a conditional order experiment on one
named game, never a deck-wide effect. The all-salt result is retained so the
eligible subset cannot silently replace the full experiment.

## Movers: first divergences and paired recovery inputs

After the composed primary run completes, link `final-prunes/phasec/wins`
and `phasec/batch.log` to the primary run's corresponding outputs, then:

```
python3 build/scratch/cyclone-finish/prunes_proofs.py --root build/scratch/cyclone-finish/final-prunes movers --traces build/scratch/cyclone-finish/final-pgo-inputs/runs/primary-quality/traces
```

Default arms are the PB/PBCYC, KEEP, SITU, SEED and Ponder contrasts. Each
amendment uses its PB/PBCYC control. Selection is symmetric: every raw
SLOWER and FASTER outcome mover is listed. It is explicitly conditional on
those movers, not a deck-wide effect. All expected Phase C chunks must be
complete; traces must match the actual seed/index and recorded result.

Outputs under `final-prunes/proofs/`:

* `movers-review.json`: raw scores, trace paths and first observable
  divergence, classified as mulligan/opening hand/put-back/Ponder/scry/
  cycle/land/cast/draw/other. This is triage, NOT an automatic causal verdict.
  A SITU or SEED game can diverge earlier through repriced search or London
  bottoming; the reviewer must distinguish that from a directly removed line.
* `recovery.json`: deduplicated paired control/arm jobs at explicit d8 b0
  for the slower cases, with original actual seed/global index, profile and
  flag map. No unit-abandon or timeout cap is added. These jobs may be costly;
  generation does not launch them.
* `meta.json`: exact source/apparatus, selected cases and explicit limitations.

For aggregate limited-information screening, use the existing
`reconstruct_prunes.py ... blind --salts 4` route. It builds ONE symmetric
pooled manifest with deduplicated controls. Do not repeat those same jobs
through another helper. Four salts supports aggregate screening, not a
per-game verdict.

## Explicitly reviewed forty-salt expansions

After inspecting `movers-review.json`, select actual case IDs for a larger
whole-game blind pair, using a FRESH output directory:

```
python3 build/scratch/cyclone-finish/prunes_proofs.py --root build/scratch/cyclone-finish/final-prunes movers --traces build/scratch/cyclone-finish/final-pgo-inputs/runs/primary-quality/traces --out build/scratch/cyclone-finish/final-prunes/proofs-expanded --expand-cases CASE_ID --salts 40
```

Replace `CASE_ID` with the reviewed identifier, or comma-separate several.
`blind-expanded.json` contains BOTH arms at each identical seed/index/salt,
with shared controls deduplicated. No expansion jobs are generated unless
cases are explicitly selected. The new output directory also contains the
same recovery inputs; do not launch a duplicate copy of those recovery jobs.

After that manifest completes:

```
python3 build/scratch/cyclone-finish/prunes_proofs.py --root build/scratch/cyclone-finish/final-prunes expanded-report --inputs build/scratch/cyclone-finish/final-prunes/proofs-expanded --wins EXPANSION_WINS --traces EXPANSION_TRACES
```

The report shows all paired salt deltas, same-opening/mulligan counts and
first observable differences. It deliberately retains
`REQUIRES_MANUAL_CAUSAL_REVIEW_even_when_blind_mean_favours_an_arm`:
these are whole-game policy comparisons, not forced counterfactual decisions
from a common mid-game state. The general blind mode lacks real-resolution
choosers for seed Curator scry and Cyclone take/decline pins, so SEED cannot
be called oracle-complete merely from its blind mean.

## Helper validation

Python compilation and synthetic checks passed for strict eligibility,
wrong-hand/slot/draw/prefix rejection, forty-salt all/subset means, symmetric
outcome selection, shared recovery/control deduplication, exact seed/index
binding and selected forty-salt expansion construction. No real-game result
is claimed before PGO and the authorized runs produce their data.
