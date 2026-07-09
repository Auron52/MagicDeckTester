#!/usr/bin/env python3
"""Feature-collision diagnostic — is the FEATURE REPRESENTATION the ceiling?

Rows: "<label> <feat...> <seed> <turn>", one per candidate; rows sharing (seed,turn)
are the candidates of one decision. The d0 ranker can only separate candidates that
DIFFER in features. If two candidates have identical feature vectors but different
labels, that label difference is UNLEARNABLE by any model on these features.

Per decision, decompose label variance:
  total    = variance of candidate labels
  residual = mean within-(feature-group) variance   (collisions: same x, diff y)
  explained_frac = 1 - residual/total   (upper bound on what ANY model can fit)

A low explained_frac => the features, not the data or model capacity, are the ceiling.
More data cannot help; only richer plan-varying features can.

Optionally restrict to plan-VARYING feature columns (--varycols): the d0 ranker only
sees columns that differ across a decision's candidates anyway.

Run: python3 scripts/feature_collision.py logs/eval/knights_rollout.rows
"""
import sys
from collections import defaultdict


def var(xs):
    if len(xs) < 2:
        return 0.0
    m = sum(xs) / len(xs)
    return sum((x - m) ** 2 for x in xs) / len(xs)


def analyze(path):
    groups = defaultdict(list)  # (seed,turn) -> [(label, feat_tuple)]
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            if len(p) < 4:
                continue
            label = float(p[0])
            feat = tuple(p[1:-2])
            seed, turn = p[-2], p[-1]
            groups[(seed, turn)].append((label, feat))

    dec_total = 0.0
    dec_resid = 0.0
    n = 0
    collide_labelvar = 0   # decisions whose label spread is >0 but fully collided
    live = 0               # decisions with total var > 0
    for k, rows in groups.items():
        if len(rows) < 2:
            continue
        labels = [r[0] for r in rows]
        t = var(labels)
        if t == 0.0:
            continue
        live += 1
        # group by feature vector, compute within-group (irreducible) variance
        fg = defaultdict(list)
        for lab, feat in rows:
            fg[feat].append(lab)
        resid = sum(len(v) * var(v) for v in fg.values()) / len(rows)
        dec_total += t
        dec_resid += resid
        n += 1
        if resid >= t - 1e-9:
            collide_labelvar += 1

    if n == 0:
        print(f"{path}: no live decisions")
        return
    explained = 1.0 - dec_resid / dec_total
    print(f"{path}")
    print(f"  live decisions (labelvar>0): {live}")
    print(f"  mean total label var       : {dec_total/n:.4f}")
    print(f"  mean residual (collision)  : {dec_resid/n:.4f}")
    print(f"  EXPLAINED-BY-FEATURES frac : {explained:.3f}   <- ceiling for ANY model")
    print(f"  fully-collided decisions   : {collide_labelvar}/{live} = {100*collide_labelvar/live:.1f}%")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        analyze(p)
