# The value-leaf A/B never measures the value leaf's benefit

**Status:** diagnosed with measurements, fix not implemented. Applies to `phase_measure` in
`scripts/valueleaf.sh` and `scripts/vlq_ab_report.py`. Found 2026-08-16 on Mirrorwing.

## The claim

Phase E's adoption A/B cannot detect the value leaf's benefit, because **every arm it runs has the
sidecar**. It compares `live` vs `staged` (both sidecars) and a `target_depth` sweep (sidecar
throughout). It never runs a BARE arm with no sidecar at all -- which is the only comparison in which
the leaf's contribution appears.

## What the leaf actually does (user, 2026-08-16)

> It avoids escalation if the win is found and, if not, avoids the heuristic rollouts at depths other
> than the chosen one for escalation. It is not expected to be a quality lever, but there should be
> some speed benefit.

So it is a **COST lever with quality held flat**. Any report whose verdict column is quality will
therefore read "no difference" and reject it. That is not hypothetical: a Creature Giving model was
REJECTED on that basis and later found to be 0-quality-difference and 30%+ faster.

## Measured on Mirrorwing, 2026-08-16

`pdflt` (sidecar present, built-in default d5/b20) vs `bare` (same deck, same profile, same play
settings, NO `.value.json`), paired chunk-for-chunk on identical seeds and offsets:

```
6,050 paired games:  quality  sidecar 5.0919  bare 5.1170  (sidecar BETTER by 0.0251)
                     cost     0.59x  =  41% FASTER
```

Split by intrinsic difficulty (chunks ordered by BARE cost):

| quartile | bare core-s | sidecar | speedup | quality delta |
|---|---|---|---|---|
| fastest 25% | 2068 | 1359 | 34% | -0.0219 |
| 2nd | 2666 | 1668 | 37% | -0.0290 |
| 3rd | 3339 | 2133 | 36% | -0.0168 |
| **slowest 25%** | 5147 | 2618 | **49%** | -0.0326 |

**The benefit is LARGEST on the slowest games.** That follows from the mechanism: on a slow game the
heuristic rollouts at non-chosen depths are the most expensive thing in the search, so skipping them
saves most exactly where the search hurts most. The same effect at the extreme is visible in the
depth matrix -- H4 at 127,198 ms/game against V6 at 17,320 ms/game for equal quality.

## Why the existing A/B reported "flat"

Three compounding defects, each verified:

1. **No bare arm.** Nothing to compare against. `live` and `staged` had BYTE-IDENTICAL `eval_model`
   (sha `01297909fc2768b3` both sides) -- the A/B was the same model against itself with different
   metadata.
2. **Quality is the verdict column.** `vlq_ab_report.py` prints cost as a bare ratio with NO
   uncertainty, so a real speedup cannot reach significance and a flat quality reading decides.
3. **No resolution floor on quality.** Its paired `t` is over SEED MEANS; with 5 of 7 seeds
   bit-identical the between-seed sd collapses and manufactures a small `se`. It reported
   "+0.00029, t=+1.55" off exactly TWO changed games in 7,000. The depth matrix's `dead_rung` has
   applied a `3*step/n` floor since the burn incident; the A/B reporter never inherited it.

## Fix

- **Always run a BARE arm** in phase E. It is the baseline the adoption decision needs.
- **Report cost paired per seed with se and t**, and let it carry the verdict when quality is flat --
  see `scripts/ab2_report.py`, which does both.
- **Apply the `3*step/n` resolution floor** to quality, and say UNRESOLVED rather than printing a
  delta the sample cannot support.
- Chunk the jobs (see [[phase-e-job-granularity]]) -- required anyway, and it makes chunk-level
  pairing possible, which is what enabled the difficulty-quartile split above.

## Related

The deck's SHIPPED sidecar also lacks `value_leaf_table` / `value_fallback_crossover` /
`value_no_fallback` entirely, while the staged one carries them from the regenerated matrix. Adoption
of that metadata is a separate open question and is NOT what any arm above tested.
