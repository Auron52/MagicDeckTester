#!/usr/bin/env python3
"""Train a served escalation confidence-gate from MTG_ESCALATION_DUMP rows and emit a JSON the
C++ gate (MTG_ESCALATION_GATE) loads.  Predicts P(NO-OP) = P(escalation would not move win_turn)
so the engine can SKIP predicted no-op escalations.

Row (same as the dump): taken wt_changed turn committed gap est_wt <46 midgame feats>
Feature vector (ORDER MUST MATCH the C++ gate): [committed, gap, turn, est_wt] + 46 feats.
Label y = 1 - wt_changed  (1 = no-op / safe to skip).

Output JSON: { "mean":[...], "std":[...], "w":[bias, w1, ...], "nfeat":N,
               "order":"committed,gap,turn,est_wt,+46midgame" }
The gate scores z = w0 + sum_k w[k+1]*(raw[k]-mean[k])/std[k]; skip iff sigmoid(z) > threshold.

Pure python (no numpy/sklearn) — mirrors scripts/esc_analyze.py's logistic exactly.
"""
import sys, json, math

def load(paths):
    X, y = [], []
    for path in paths:
        for ln in open(path):
            p = ln.split()
            if len(p) < 7:
                continue
            v = list(map(float, p))
            wt_changed = int(v[1])
            turn, committed, gap, est = v[2], v[3], v[4], v[5]
            feats = v[6:]
            X.append([committed, gap, turn, est] + feats)
            y.append(1 - wt_changed)          # 1 = no-op (skippable)
    return X, y

def standardize(cols):
    d = len(cols[0]); n = len(cols)
    m = [0.0]*d; s = [1.0]*d
    for j in range(d):
        vals = [r[j] for r in cols]
        mu = sum(vals)/n
        var = sum((x-mu)**2 for x in vals)/max(1, n-1)
        m[j] = mu; s[j] = math.sqrt(var) if var > 1e-12 else 1.0
    return m, s

def design(rows, m, s):
    return [[1.0] + [(r[j]-m[j])/s[j] for j in range(len(m))] for r in rows]

def train_logistic(X, y, iters=600, lr=0.3, l2=1.0):
    d = len(X[0]); w = [0.0]*d; n = len(X)
    for _ in range(iters):
        g = [0.0]*d
        for xi, yi in zip(X, y):
            z = sum(w[k]*xi[k] for k in range(d))
            p = 1.0/(1.0+math.exp(-max(-30, min(30, z))))
            e = p - yi
            for k in range(d):
                g[k] += e*xi[k]
        for k in range(d):
            reg = 0.0 if k == 0 else l2*w[k]
            w[k] -= lr*(g[k]/n + reg/n)
    return w

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: esc_train_gate.py <out.json> <dump1> [dump2 ...]")
    out_path = sys.argv[1]
    X, y = load(sys.argv[2:])
    n = len(X); npos = sum(y)
    if n < 40 or npos in (0, n):
        sys.exit(f"degenerate training set: rows={n} no-op={npos}")
    m, s = standardize(X)
    Xd = design(X, m, s)
    w = train_logistic(Xd, y)
    # in-sample fit sanity
    correct = sum(1 for xi, yi in zip(Xd, y)
                  if (1.0/(1.0+math.exp(-max(-30, min(30, sum(w[k]*xi[k] for k in range(len(w))))))) >= 0.5) == (yi == 1))
    obj = {"mean": m, "std": s, "w": w, "nfeat": len(m),
           "order": "committed,gap,turn,est_wt,+46midgame"}
    json.dump(obj, open(out_path, "w"))
    print(f"trained on rows={n} no-op={npos} ({npos/n:.0%})  in-sample acc={correct/n:.3f}  "
          f"nfeat={len(m)}  -> {out_path}")

if __name__ == "__main__":
    main()
