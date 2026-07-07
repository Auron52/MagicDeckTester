#!/usr/bin/env python3
# Synthesize an R=k raw sidecar from the R60 pooled raw (means+counts) + run1's per-cell sumsq (variance).
# Model: the keep A/B isolates the KEEP decision, which depends only on cell MEANS. A k-sample mean has
# variance sigma^2/k around the true mean; the pooled D_60 is our best estimate (variance sigma^2/N, N=pooled
# count). To draw a plausible R=k estimate centered on D_60:  D_k = D_60 + Normal(0, sigma^2*(1/k - 1/N)),
# clamped to 0 extra noise when k>=N (can't simulate more samples than we have). Counts are left at the
# pooled values (the keep A/B holds bottoming identical, so the bottom_floor/count path is irrelevant here);
# only the stored `sum` is rewritten so sum/count = D_k. R=60 with any seed => zero noise => byte-identical
# to pooled (pipeline check).
#   Usage: keepmodel_synth_raw.py <R> <seed> <pooled.raw.json> <out.raw.json> [sumsq_source.raw.json]
# sumsq_source (per-cell variance) is OPTIONAL now that sumsq survives the merge -- a pooled raw carries it,
# so it defaults to <pooled.raw.json>. Pass a separate single-run sidecar only for a pooled raw generated
# before the sumsq-through-merge change (e.g. antilife's original pooled.raw.json -> pass run1.raw.json).
import json, sys, random

R = int(sys.argv[1]); SEED = int(sys.argv[2])
POOLED, OUT = sys.argv[3], sys.argv[4]
RUN1 = sys.argv[5] if len(sys.argv) > 5 else POOLED

pooled = json.load(open(POOLED))
run1   = json.load(open(RUN1))

if 'sumsq' not in run1['sizes'][0]['entries'][0]:
    sys.exit(f"ERROR: {RUN1} has no per-cell 'sumsq' (needed for the noise variance). Pass a sumsq "
             f"source as the 5th arg -- a direct generation sidecar, or a pool merged AFTER the "
             f"sumsq-through-merge change.")

# Per-cell per-sample variance sigma^2 from run1: sumsq/count - (sum/count)^2, indexed by (size_idx, comp).
sig2 = {}
for si, sz in enumerate(run1['sizes']):
    for e in sz['entries']:
        key = (si, tuple(e['comp']))
        cnt = e['count']; sm = e['sum']; sq = e['sumsq']
        v = []
        for pd in range(2):
            n = cnt[pd]
            if n > 0:
                var = sq[pd]/n - (sm[pd]/n)**2
                v.append(max(0.0, var))
            else:
                v.append(0.0)
        sig2[key] = v

rng = random.Random(SEED)
perturbed = 0; total = 0
for si, sz in enumerate(pooled['sizes']):
    for e in sz['entries']:
        key = (si, tuple(e['comp']))
        s2 = sig2.get(key, [0.0, 0.0])
        cnt = e['count']; sm = e['sum']
        new_sum = []
        for pd in range(2):
            N = cnt[pd]; total += 1
            if N <= 0:
                new_sum.append(sm[pd]); continue
            D60 = sm[pd]/N
            extra = s2[pd] * (1.0/R - 1.0/N) if R < N else 0.0
            if extra > 0:
                Dk = D60 + rng.gauss(0.0, extra**0.5); perturbed += 1
            else:
                Dk = D60
            new_sum.append(Dk * N)     # keep count=N, rewrite sum so sum/count = Dk
        e['sum'] = new_sum

# Keep pooled meta so MTG_KEEP_MERGE accepts it (fingerprints intact); tag R for provenance.
pooled['meta']['R'] = R
pooled['meta']['synthetic_R'] = R
pooled['meta']['synthetic_seed'] = SEED
json.dump(pooled, open(OUT, 'w'))
print(f"R={R} seed={SEED}: perturbed {perturbed}/{total} cell-sides -> {OUT}")
