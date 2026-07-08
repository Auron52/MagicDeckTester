#!/usr/bin/env python3
"""Train the mid-game eval model (linear, fixed-point) from de-clairvoyed label rows.

Reads rows emitted by the engine's MTG_DUMP_EVAL_ROWS hook -- one row per candidate plan at each
real decision:

    # label <feat0> <feat1> ... <featN> seed turn      (header, once)
    <expected_win_turn> <f0> <f1> ... <fN> <seed> <turn>

and fits ridge regression predicting a plan's de-clairvoyed EXPECTED win turn from its
non-clairvoyant features. It stores NEGATED, fixed-point coefficients so the engine's integer
MidGameEvaluator::Score() = -predicted_win_turn (HIGHER = better plan; the seam ranks by higher
Score). Runtime determinism comes from the fixed-point quantization, so this trainer's float math
need not be cross-platform reproducible. No numpy -- a hand-rolled ridge solve over the small
(D+1)x(D+1) normal-equations system. See docs/design/learned-d0-policy.md.

Usage:
    scripts/train_eval_model.py --rows logs/eval/th_train.rows --out decks/treasure_hunt.eval.json
    scripts/train_eval_model.py --rows R --learning-curve      # data-sufficiency study (held-out RMSE)
"""
import sys, json, argparse, math

SCALE = 1000  # fixed-point scale for coefs/intercept (win-turn precision ~1/1000; mirrors SCORE_SCALE)


def read_rows(path):
    """Return (feat_names, X, y, seeds). Features are the columns between label and the seed/turn tail."""
    feat_names, X, y, seeds = None, [], [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                toks = line[1:].split()            # "label <feat names...> seed turn"
                feat_names = toks[1:-2]
                continue
            toks = line.split()
            if len(toks) < 4:
                continue
            y.append(float(toks[0]))
            X.append([float(v) for v in toks[1:-2]])
            seeds.append(int(toks[-2]))
    if feat_names is None and X:
        feat_names = ['f%d' % i for i in range(len(X[0]))]
    return feat_names, X, y, seeds


def gauss_solve(A, b):
    """Solve A x = b (A is n x n) by Gaussian elimination with partial pivoting. Pure python."""
    n = len(A)
    M = [A[i][:] + [b[i]] for i in range(n)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(M[r][col]))
        if abs(M[piv][col]) < 1e-12:
            continue
        M[col], M[piv] = M[piv], M[col]
        pv = M[col][col]
        for r in range(n):
            if r == col:
                continue
            fac = M[r][col] / pv
            if fac == 0.0:
                continue
            for c in range(col, n + 1):
                M[r][c] -= fac * M[col][c]
    x = [0.0] * n
    for i in range(n):
        x[i] = M[i][n] / M[i][i] if abs(M[i][i]) > 1e-12 else 0.0
    return x


def ridge_fit(X, y, lam):
    """Ridge regression with an (unpenalized) intercept. Returns (coefs[d], intercept)."""
    n = len(X)
    d = len(X[0])
    D = d + 1                                   # +1 for the intercept column
    ATA = [[0.0] * D for _ in range(D)]
    ATy = [0.0] * D
    for i in range(n):
        row = X[i] + [1.0]
        yi = y[i]
        for a in range(D):
            ra = row[a]
            ATy[a] += ra * yi
            ATAa = ATA[a]
            for b in range(D):
                ATAa[b] += ra * row[b]
    for a in range(d):                          # ridge on weights only, not the intercept
        ATA[a][a] += lam
    w = gauss_solve(ATA, ATy)
    return w[:d], w[d]


def rmse(X, y, coefs, intercept):
    if not X:
        return float('nan')
    s = 0.0
    for i in range(len(X)):
        pred = intercept + sum(coefs[j] * X[i][j] for j in range(len(coefs)))
        s += (pred - y[i]) ** 2
    return math.sqrt(s / len(X))


def write_sidecar(path, feat_names, coefs, intercept):
    """Store NEGATED fixed-point coefs so Score() = -predicted_win_turn (higher = better)."""
    obj = {"intercept": int(round(-intercept * SCALE)), "coefs": {}}
    for name, c in zip(feat_names, coefs):
        q = int(round(-c * SCALE))
        if q != 0:
            obj["coefs"][name] = q
    with open(path, 'w') as f:
        json.dump({"eval_model": obj}, f, indent=2)
        f.write('\n')
    return obj


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rows', required=True)
    ap.add_argument('--out', default=None, help='sidecar path, e.g. decks/<deck>.eval.json')
    ap.add_argument('--lam', type=float, default=1.0, help='ridge lambda (small; n>>d)')
    ap.add_argument('--learning-curve', action='store_true',
                    help='data-sufficiency study: held-out RMSE at increasing train sizes')
    args = ap.parse_args()

    feat_names, X, y, seeds = read_rows(args.rows)
    n, d = len(X), (len(X[0]) if X else 0)
    print("rows=%d features=%d label[min/mean/max]=%.2f/%.2f/%.2f"
          % (n, d, min(y), sum(y) / n, max(y)), file=sys.stderr)

    if args.learning_curve:
        # Deterministic interleaved split (every 5th row held out) so the curve is reproducible.
        te = [i for i in range(n) if i % 5 == 0]
        tr = [i for i in range(n) if i % 5 != 0]
        Xte, yte = [X[i] for i in te], [y[i] for i in te]
        print("# train_rows  held_out_RMSE(turns)", file=sys.stderr)
        for frac in (0.05, 0.1, 0.25, 0.5, 1.0):
            m = max(d + 2, int(len(tr) * frac))
            idx = tr[:m]
            c, b = ridge_fit([X[i] for i in idx], [y[i] for i in idx], args.lam)
            print("  %-11d %.4f" % (m, rmse(Xte, yte, c, b)), file=sys.stderr)
        return

    coefs, intercept = ridge_fit(X, y, args.lam)
    print("train RMSE = %.4f turns" % rmse(X, y, coefs, intercept), file=sys.stderr)
    # Disclose the strongest terms (in ORIGINAL win-turn units: +coef => later win per unit feature).
    terms = sorted(zip(feat_names, coefs), key=lambda t: -abs(t[1]))
    print("intercept=%.3f  top terms (win-turn / unit):" % intercept, file=sys.stderr)
    for name, c in terms[:12]:
        if abs(c) > 1e-4:
            print("    %-22s %+.4f" % (name, c), file=sys.stderr)

    if args.out:
        obj = write_sidecar(args.out, feat_names, coefs, intercept)
        print("wrote %s (%d nonzero coefs, fixed-point x%d)"
              % (args.out, len(obj["coefs"]), SCALE), file=sys.stderr)


if __name__ == '__main__':
    main()
