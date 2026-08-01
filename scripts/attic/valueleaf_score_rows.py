#!/usr/bin/env python3
"""Score a SERVED value/eval sidecar against a rows file -- the missing piece for comparing models
that were trained on DIFFERENT label distributions.

Why this exists (2026-08-01): held-out RMSE printed by the trainer is computed against the model's
OWN labels, so it is NOT comparable across dumps with different MTG_EVAL_ROWS_K. A K=8 model scores
better than a K=3 model partly because its target is less noisy, not because it predicts better.
To compare them you must score BOTH on ONE common test set. This does that.

    scripts/attic/valueleaf_score_rows.py --model m.json --rows test.rows

Prediction mirrors the trainer exactly: F = intercept/SCALE + sum_trees leaf/SCALE (leaf values
already carry the learning rate), plus the optional NEGATED linear coefs. Traversal is
`left if x[feat] <= threshold else right`, matching tree_predict.
"""
import argparse, json, math, sys

SCALE = 1000


def read_rows(path):
    feat_names, X, y = None, [], []
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            toks = line[1:].split()          # "label <feats...> seed turn"
            feat_names = toks[1:-2]
            continue
        t = line.split()
        if len(t) < 4:
            continue
        y.append(float(t[0]))
        X.append([float(v) for v in t[1:-2]])
    return feat_names, X, y


def predict(model, feat_names, x):
    idx = {n: i for i, n in enumerate(feat_names)}
    f = model["intercept"] / SCALE
    for name, q in (model.get("coefs") or {}).items():
        if name in idx:
            f += -(q / SCALE) * x[idx[name]]     # coefs are stored NEGATED (serving Score = -winturn)
    for nodes in model["trees"]:
        i = 0
        while len(nodes[i]) > 1:                 # internal node = [feat_name, thresh, left, right]
            name, thr, l, r = nodes[i]
            v = x[idx[name]] if name in idx else 0.0
            i = l if v <= thr else r
        f += nodes[i][0] / SCALE
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--rows", required=True)
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    m = json.load(open(a.model))
    m = m.get("eval_model", m)
    feat_names, X, y = read_rows(a.rows)
    if not X:
        print("no rows", file=sys.stderr); sys.exit(1)
    se = sum((predict(m, feat_names, X[i]) - y[i]) ** 2 for i in range(len(X)))
    rmse = math.sqrt(se / len(X))
    mean = sum(y) / len(y)
    base = math.sqrt(sum((v - mean) ** 2 for v in y) / len(y))
    print("%-22s n=%-5d RMSE=%.4f  baseline=%.4f  var_explained=%5.1f%%"
          % (a.label or a.model, len(X), rmse, base, 100 * (1 - (rmse / base) ** 2) if base else 0))


if __name__ == "__main__":
    main()
