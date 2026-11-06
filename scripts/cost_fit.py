#!/usr/bin/env python3
"""Fit per-game wall clock on per-game work counts (MTG_TIER1_STATS dump with core/WorkCounters.h).

Model: ms ~= sum_k price_k * count_k, non-negative prices, no intercept. Regressors:
  interior = units - step_c   (charged interior plan-loop units and priced sites)
  step_c, step_f, greedy, enum, plans, odo, apply, pay, simkey, plan_emit (core/WorkCounters.h)
Prints the prices (us per event), each regressor's share of the fitted time, and the fit quality:
the ratio actual/predicted per game (median, p5, p95) overall, per deck, and on the slowest games --
a cost model that bounds the tail must keep that ratio near 1 on the tail, not just on average.
Also prints how the CURRENT unit (units only) predicts the same games, for comparison.
Usage: cost_fit.py <dump> [--min-ms 50] [--exclude-d0]
"""
import argparse
import collections
import sys



def solve(M, v):
    """Gaussian elimination with partial pivoting (small dense system)."""
    n = len(v)
    A = [row[:] + [v[i]] for i, row in enumerate(M)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(A[r][c]))
        A[c], A[p] = A[p], A[c]
        if abs(A[c][c]) < 1e-300:
            continue
        for r in range(n):
            if r != c:
                f = A[r][c] / A[c][c]
                for k in range(c, n + 1):
                    A[r][k] -= f * A[c][k]
    return [A[i][n] / A[i][i] if abs(A[i][i]) > 1e-300 else 0.0 for i in range(n)]


def nnls_normal(G, h, iters=200):
    """Lawson-Hanson NNLS on the normal equations G x = h (G = A'WA, h = A'Wy). Pure python."""
    n = len(h)
    x = [0.0] * n
    P = [False] * n
    def grad(x):
        return [h[i] - sum(G[i][j] * x[j] for j in range(n)) for i in range(n)]
    w = grad(x)
    for _ in range(iters):
        cand = [i for i in range(n) if not P[i] and w[i] > 1e-12 * max(1.0, max(abs(t) for t in w))]
        if not cand:
            break
        P[max(cand, key=lambda i: w[i])] = True
        while True:
            idx = [i for i in range(n) if P[i]]
            zs = solve([[G[i][j] for j in idx] for i in idx], [h[i] for i in idx])
            z = [0.0] * n
            for i, v in zip(idx, zs):
                z[i] = v
            if all(z[i] > 0 for i in idx):
                x = z
                break
            neg = [i for i in idx if z[i] <= 0]
            alpha = min(x[i] / (x[i] - z[i]) if x[i] - z[i] != 0 else 0.0 for i in neg)
            x = [x[i] + alpha * (z[i] - x[i]) for i in range(n)]
            for i in range(n):
                if P[i] and x[i] <= 1e-15:
                    P[i] = False
        w = grad(x)
    return x


def fit(X, y, wts):
    n = len(X[0])
    G = [[sum(wt * wt * r[i] * r[j] for r, wt in zip(X, wts)) for j in range(n)] for i in range(n)]
    h = [sum(wt * wt * r[i] * yy for r, yy, wt in zip(X, y, wts)) for i in range(n)]
    # scale columns for conditioning
    sc = [max(1e-30, G[i][i]) ** 0.5 for i in range(n)]
    Gs = [[G[i][j] / (sc[i] * sc[j]) for j in range(n)] for i in range(n)]
    hs = [h[i] / sc[i] for i in range(n)]
    xs = nnls_normal(Gs, hs)
    return [xs[i] / sc[i] for i in range(n)]


sys.path.insert(0, __file__.rsplit("/", 1)[0])
from tier1_stats import load  # noqa: E402

KINDS = ["interior", "step_c", "step_f", "greedy", "enum", "plans", "odo", "apply", "pay", "simkey", "plan_emit"]


def features(r):
    w = r["work"]
    interior = max(0, r["units"] - w.get("step_c", 0))
    return [interior] + [w.get(k, 0) for k in KINDS[1:]]


def ratio_report(label, rows, pred):
    rat = sorted(r["ms"] / max(1e-9, p) for r, p in zip(rows, pred))
    n = len(rat)
    if n == 0:
        return
    print("   %-22s n=%6d  actual/pred p5 %.2f  p50 %.2f  p95 %.2f  max %.1f" % (
        label, n, rat[int(.05 * n)], rat[n // 2], rat[min(n - 1, int(.95 * n))], rat[-1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--min-ms", type=int, default=50)
    a = ap.parse_args()
    rows = [r for r in load(a.dump) if r["units"] > 0 and r["ms"] >= a.min_ms and r["work"]]
    X = [features(r) for r in rows]
    y = [float(r["ms"]) for r in rows]
    # Weight by 1/ms so every game's RELATIVE error counts (the tail must be predicted, not just the bulk).
    wts = [1.0 / max(v, 1.0) for v in y]
    coef = fit(X, y, wts)
    pred = [sum(c * v for c, v in zip(coef, r)) for r in X]
    print(f"{len(rows)} searched games >= {a.min_ms} ms")
    print("prices (us per event) and share of fitted time:")
    tot = sum(pred)
    for i, (k, c) in enumerate(zip(KINDS, coef)):
        col = [r[i] for r in X]
        print("   %-9s %10.3f us   share %5.1f%%   total events %.3g" % (k, 1000 * c, 100 * c * sum(col) / tot, sum(col)))
    print("fit quality (actual / predicted):")
    ratio_report("ALL (priced)", rows, pred)
    u = [float(r["units"]) for r in rows]
    cu = fit([[v] for v in u], y, wts)[0]
    upred = [v * cu for v in u]
    ratio_report("ALL (units only)", rows, upred)
    idx = sorted(range(len(rows)), key=lambda i: -y[i])[: max(1, len(rows) // 100)]
    ratio_report("slowest 1% (priced)", [rows[i] for i in idx], [pred[i] for i in idx])
    ratio_report("slowest 1% (units)", [rows[i] for i in idx], [upred[i] for i in idx])
    by = collections.defaultdict(list)
    for i, r in enumerate(rows):
        by[r["job"].split("_")[0]].append(i)
    print("per deck: median actual/pred  priced | units-only   (spread across decks is the mispricing)")
    for d, ii in sorted(by.items(), key=lambda kv: -sum(y[i] for i in kv[1]))[:30]:
        rp = sorted(y[i] / max(1e-9, pred[i]) for i in ii)
        ru = sorted(y[i] / max(1e-9, upred[i]) for i in ii)
        print("   %-16s n=%5d  %5.2f | %5.2f" % (d, len(ii), rp[len(rp) // 2], ru[len(ru) // 2]))


if __name__ == "__main__":
    sys.exit(main())
