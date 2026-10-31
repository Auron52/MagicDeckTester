# Cyclone completion review

Status: measurements in progress, 2026-10-31. This is the concise review alongside
`cyclone-completion-checkpoint.md`; it does not replace the historical analysis ledger.
No mulligan or value-leaf generation has started during completion.

## Implementation and source

All original implementation branches are integrated in `cyclone-current`, based on
main `547e6fe8b`. Code commit `4a9ed39e5` has source tree
`8b0bc5ea046ce748c456f7e4aceef4ea11807aa5`. Original and intermediate pushed
branches and worktrees remain intact.

The fixed 512 MB/thread plan-cache quota and its companion FSL quota are removed.
Existing shared RAM-sized budgets retain exact keys and equal cache-hit/miss
pricing. The separate cache-budget redesign is not an adopted prerequisite.
Additional completion fixes cover extra-turn fold/expiry handling, put-back cache
inputs and overlapping SETUP/JUNK roles, complete generic payment splits, searched
information orders, distinct cycler-order experiments, and viewer menu/payment
handling. Three rollout/executor payment-replay defects and a generic local-FSL
latent-discovery provenance defect were found and repaired during verification.

Fresh PGO+LTO binary SHA256:
`2371bc0e0adc3daf2fe65f2fc42139146aaae11e0d9627cf2f1e75d276d630b6`.
Earlier stopped runs are preserved but supply no final-source policy verdicts.

## Completed verification

- 2,396 unit cases / 3,567,476 assertions and all 388 scenarios pass.
- Linux and Windows CI, including cross-platform determinism parity, pass.
- Three former ordinary Cyclone divergence cases reproduce identically under
  Release and PGO with zero ordinary FD/non-convergence.
- Smoke: 150 PASS, seven familiar mismatches audited and accepted, zero NEW.
  No searched slower games; all nine searched same-score play changes reviewed.
  The fresh Goblins2HG GI41 comparison is a legal T3 win on both implementations.
  Ground-truth aggregate/per-game consistency passes all 894 entries.
- Viewer: 709 strict reference checks pass. Fresh native audit covers 290
  references with 1,176 accepts, 93 choices, zero unsupported decisions and
  1,167 findable mana lines with zero lost/unaudited lines. Another 417 native
  checks carry only under explicit input/path/source proofs; four Bruna pins
  are carried explicitly. All 729 reference files remain byte-exact.
- Fresh normal memory processes at one and six threads pass. All 168 shared
  games match outcomes, digests and units. Six-thread process completes all
  468 games with shared-pool refusals and no FSL refusal. Individual game usage
  exceeds 512 MiB, confirming that the rejected per-thread cap is absent.
- Interim primary 24-thread core matches those same 168 games exactly. This is
  partial memory evidence until process completion and remaining variants pass.

The known upstream Niv1082 ordinary FD remains present in smoke, unchanged from
baseline. It is separate from the three repaired Cyclone cases. No claim of
fleet-wide zero divergence follows from the Cyclone checks.

## Measurements and decisions still open

The primary frozen-source pool has 1,490 jobs / 13,142 games with 24 workers.
Partial families are not adoption evidence. Remaining process-only checks cover
normal 12 threads, pressure at a 20 MiB shared pool, unbounded capacity, complete
shape equality and the small cache VERIFY run. Full regression, diagnostic
controls, put-back isolation/recovery classification and quiet timing are owed.

The quiet timing plan is one 695-game pool: the original 535 shipping/Hinata
controls plus 160 matched PB-ON Cyclone games, with Hinata controls shared once.
It measures both settings without adopting PB. Mixed-pool primary wall times
are not cost-gate evidence.

PB adoption is conditionally authorized only if d3 and d5 each have paired net
<= 0 against live CYCLER, the combined aggregate improves, there are zero genuine
lost lines after limited-information classification, and performance improves.
The live Phase C contrast changes only PB, with NOM2 false. Other policy choices
remain user-owned, including KEEP amendments, curve checking, leaf fidelity,
NOM2, nested exhaust, cast/cycle order and payment-split settings.

QC3 is complete but conditional on its measured package (including NOM2 true):
leaf0 versus leaf1 averages improve by 0.100 turns at d3 and 0.0833 at d5, with
unit ratios 0.544 and 0.650, but both faster and slower games occur. This is no
dominance or adoption claim. QC1's 97 selected hands out of 600 compare normal
policy with curve_check:none; this is a conditional proxy, not a tested exact
cycler-counting implementation or deck-wide benefit. The 94 one-land-only
hands are a separate unmeasured stratum.

Re-evaluation and linear landing follow completed checks. Add Cyclone to the
regression matrix only if the actual shipping configuration meets the <=3x
same-run Hinata cost gate. Report any remaining gap; do not invent an override
or begin artifact generation ahead of the requested review.
