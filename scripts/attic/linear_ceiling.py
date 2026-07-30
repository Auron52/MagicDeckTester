#!/usr/bin/env python3
"""Global-linear within-decision ceiling.

feature_collision.py showed a LOOKUP-TABLE upper bound (per-decision feature->label).
The real d0 ranker is ONE GLOBAL LINEAR function. This measures the best a global
linear ranker could do on the within-decision ranking:

  - center each decision's features & label by that decision's mean (removes the
    per-decision intercept + all pre-plan constant columns -> exactly what the ranker
    sees: only plan-varying, within-decision signal)
  - fit global OLS on the stacked centered rows
  - report R^2 = fraction of within-decision label variance a global linear captures

Compare three numbers:
  collision-ceiling (feature_collision.py, lookup table) : ~0.95 aggro
  THIS (global linear)                                   : ?
  gap between them = what NONLINEARITY / feature-crosses could recover.

If global-linear ~ collision-ceiling  -> linear is enough; more capacity won't help.
If global-linear << collision-ceiling -> LINEAR FORM is the ceiling (explains fast
   data saturation); crosses / nonlinearity is the untapped lever, not more data.

Run: python3 scripts/linear_ceiling.py logs/eval/knights_rollout.rows
"""
import sys
from collections import defaultdict


def solve(A, b):
    """Gaussian elimination with partial pivoting. A: n x n, b: n."""
    n = len(b)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        piv = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[piv] = M[piv], M[c]
        if abs(M[c][c]) < 1e-15:
            continue
        for r in range(n):
            if r == c:
                continue
            f = M[r][c] / M[c][c]
            for k in range(c, n + 1):
                M[r][k] -= f * M[c][k]
    return [M[i][n] / M[i][i] if abs(M[i][i]) > 1e-15 else 0.0 for i in range(n)]


def build(rows_by_group, cross):
    X_rows, y_rows = [], []   # centered rows
    ncol = None
    for rows in rows_by_group:
        if len(rows) < 2:
            continue
        labs = [r[0] for r in rows]
        ym = sum(labs) / len(labs)
        if all(abs(l - ym) < 1e-12 for l in labs):
            continue
        ncol = len(rows[0][1])
        fmean = [sum(r[1][j] for r in rows) / len(rows) for j in range(ncol)]
        for (lab, feat) in rows:
            y_rows.append(lab - ym)
            X_rows.append([feat[j] - fmean[j] for j in range(ncol)])
    if not X_rows:
        return [], [], 0
    if cross:
        base = ncol
        aug = []
        for r in X_rows:
            ext = r[:]
            for a in range(base):
                for c in range(a, base):
                    ext.append(r[a] * r[c])
            aug.append(ext)
        X_rows = aug
        ncol = len(aug[0])
    return X_rows, y_rows, ncol


def fit_eval(Xtr, ytr, Xte, yte, ncol, ridge):
    col_var = [sum(r[j] * r[j] for r in Xtr) for j in range(ncol)]
    keep = [j for j in range(ncol) if col_var[j] > 1e-9]
    m = len(keep)
    A = [[0.0] * m for _ in range(m)]
    b = [0.0] * m
    for r, yv in zip(Xtr, ytr):
        xk = [r[keep[a]] for a in range(m)]
        for a in range(m):
            b[a] += xk[a] * yv
            ra = A[a]; xa = xk[a]
            for c in range(m):
                ra[c] += xa * xk[c]
    for a in range(m):
        A[a][a] += ridge
    w = solve(A, b)
    ss_res = ss_tot = 0.0
    for r, yv in zip(Xte, yte):
        pred = sum(r[keep[a]] * w[a] for a in range(m))
        ss_res += (yv - pred) ** 2
        ss_tot += yv * yv
    return (1.0 - ss_res / ss_tot) if ss_tot > 0 else 0.0, m


def analyze(path):
    groups = defaultdict(list)
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            if len(p) < 4:
                continue
            label = float(p[0])
            feat = [float(x) for x in p[1:-2]]
            seed, turn = p[-2], p[-1]
            groups[(seed, turn)].append((label, feat))

    cross = "--cross" in sys.argv
    heldout = "--heldout" in sys.argv
    ridge = 1e-3 if (cross or heldout) else 1e-6

    if not heldout:
        X, y, ncol = build(list(groups.values()), cross)
        if not X:
            print(f"{path}: no live decisions"); return
        r2, m = fit_eval(X, y, X, y, ncol, 1e-6)
        print(f"{path}")
        print(f"  centered rows / cols(plan-varying): {len(X)} / {m}")
        print(f"  GLOBAL-LINEAR within-decision R^2 : {r2:.3f}")
        print()
        return

    # held-out: split decisions deterministically by group hash
    items = sorted(groups.items())
    tr = [v for i, (k, v) in enumerate(items) if i % 2 == 0]
    te = [v for i, (k, v) in enumerate(items) if i % 2 == 1]
    print(f"{path}")
    for label, cr in (("linear", False), ("+cross", True)):
        Xtr, ytr, nc = build(tr, cr)
        Xte, yte, _ = build(te, cr)
        r2, m = fit_eval(Xtr, ytr, Xte, yte, nc, ridge)
        print(f"  {label:7s} held-out R^2: {r2:.3f}  (cols {m})")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        if p.startswith("--"):
            continue
        analyze(p)
