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
    """Return (feat_names, X, y, groups). Features are the columns between label and seed/turn.
    groups[i] = (seed, turn) identifies the decision row i belongs to (a candidate of that decision)."""
    feat_names, X, y, groups = None, [], [], []
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
            groups.append((int(toks[-2]), int(toks[-1])))
    if feat_names is None and X:
        feat_names = ['f%d' % i for i in range(len(X[0]))]
    return feat_names, X, y, groups


def within_center(X, y, groups):
    """Subtract each decision's (seed,turn) mean from features and label. This is the fixed-effects
    'within' transform: it removes cross-decision variance so the fit explains only WITHIN-decision
    (candidate-to-candidate) differences -- exactly what d0 ranking needs. Features constant across a
    decision's candidates (the pre-plan board) center to 0 and drop out; only plan-varying features
    carry signal. Returns (Xc, yc)."""
    from collections import defaultdict
    idx = defaultdict(list)
    for i, g in enumerate(groups):
        idx[g].append(i)
    d = len(X[0])
    Xc = [row[:] for row in X]
    yc = list(y)
    for g, members in idx.items():
        n = len(members)
        mean = [0.0] * d
        my = 0.0
        for i in members:
            my += y[i]
            for j in range(d):
                mean[j] += X[i][j]
        my /= n
        for j in range(d):
            mean[j] /= n
        for i in members:
            yc[i] = y[i] - my
            for j in range(d):
                Xc[i][j] = X[i][j] - mean[j]
    return Xc, yc


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


def rank_fit(X, y, groups, lam, epochs, lr):
    """Pairwise-ranking fit (learning-to-rank) targeting d0 PICK-ACCURACY directly, instead of
    regressing absolute win-turn (which confounds 'developing' with 'not yet won' and, over collinear
    plan features, can rank the do-nothing plan above casting -- the observed d0 collapse). Within each
    decision we anchor on the ORACLE-BEST candidate (min label) and push it to outrank every other
    candidate, weighted by the win-turn gap. Returns coefs in WIN-TURN units (positive => later win, so
    write_sidecar negates to Score=higher-better) and intercept 0 (it cancels in every pairwise diff and
    never affects an argmax). Feature standardization is folded back into the returned coefs."""
    from collections import defaultdict
    import math
    n, d = len(X), len(X[0])
    mean = [sum(X[i][j] for i in range(n)) / n for j in range(d)]
    var = [sum((X[i][j] - mean[j]) ** 2 for i in range(n)) / n for j in range(d)]
    std = [math.sqrt(v) if v > 1e-12 else 0.0 for v in var]

    def z(i):
        return [((X[i][j] - mean[j]) / std[j]) if std[j] > 0 else 0.0 for j in range(d)]

    idx = defaultdict(list)
    for i, g in enumerate(groups):
        idx[g].append(i)
    pairs = []                                   # (d_vec, weight): want w . d_vec > 0
    for members in idx.values():
        best = min(members, key=lambda i: y[i])
        zb = z(best)
        for j in members:
            gap = y[j] - y[best]
            if gap <= 1e-9:
                continue
            zj = z(j)
            pairs.append(([zb[k] - zj[k] for k in range(d)], gap))
    if not pairs:
        return [0.0] * d, 0.0
    w = [0.0] * d
    for ep in range(epochs):
        grad = [0.0] * d
        for dv, wt in pairs:
            s = sum(w[k] * dv[k] for k in range(d))
            g = -wt / (1.0 + math.exp(min(30.0, max(-30.0, s))))   # -wt * sigmoid(-s)
            for k in range(d):
                grad[k] += g * dv[k]
        for k in range(d):
            grad[k] = grad[k] / len(pairs) + lam * w[k]
            w[k] -= lr * grad[k]
    # Score = w . z(x) = Sum (w[j]/std[j]) * (x[j]-mean[j]); the per-feature serving coef is w/std, and
    # its win-turn-unit form (for write_sidecar's negation) is the negative of that.
    coef_winturn = [(-(w[j] / std[j]) if std[j] > 0 else 0.0) for j in range(d)]
    return coef_winturn, 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rows', required=True)
    ap.add_argument('--out', default=None, help='sidecar path, e.g. decks/<deck>.eval.json')
    ap.add_argument('--lam', type=float, default=1.0, help='ridge lambda (small; n>>d)')
    ap.add_argument('--learning-curve', action='store_true',
                    help='data-sufficiency study: held-out RMSE at increasing train sizes')
    ap.add_argument('--center', action='store_true',
                    help='within-decision centering: fit only WITHIN-decision variation (ranking-targeted)')
    ap.add_argument('--rank', action='store_true',
                    help='pairwise learning-to-rank (targets d0 pick-accuracy, not win-turn RMSE)')
    ap.add_argument('--epochs', type=int, default=300)
    ap.add_argument('--lr', type=float, default=1.0)
    args = ap.parse_args()

    feat_names, X, y, groups = read_rows(args.rows)
    n, d = len(X), (len(X[0]) if X else 0)
    print("rows=%d features=%d decisions=%d label[min/mean/max]=%.2f/%.2f/%.2f"
          % (n, d, len(set(groups)), min(y), sum(y) / n, max(y)), file=sys.stderr)

    if args.center:
        # Fit on the within-decision transform; the intercept is meaningless for ranking (it adds
        # to every candidate's Score equally, so it never changes the argmax). Serving uses w.feats.
        X, y = within_center(X, y, groups)
        print("within-centered: fitting candidate-to-candidate differences only", file=sys.stderr)

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

    if args.rank:
        coefs, intercept = rank_fit(X, y, groups, args.lam, args.epochs, args.lr)
        # Report pairwise accuracy: over anchor-vs-other pairs, how often does the model score the
        # oracle-best higher (the trainable proxy for d0 pick-accuracy).
        from collections import defaultdict as _dd
        idx = _dd(list)
        for i, g in enumerate(groups):
            idx[g].append(i)
        ok = tot = 0
        for members in idx.values():
            best = min(members, key=lambda i: y[i])
            sb = sum(-coefs[j] * X[best][j] for j in range(d))     # Score = -winturn_coef . feats
            for j in members:
                if y[j] - y[best] <= 1e-9:
                    continue
                sj = sum(-coefs[k] * X[j][k] for k in range(d))
                tot += 1
                ok += (sb > sj)
        print("pairwise ranking: anchor-outranks-other %.1f%% (%d pairs)" % (100.0 * ok / max(1, tot), tot),
              file=sys.stderr)
        terms = sorted(zip(feat_names, coefs), key=lambda t: -abs(t[1]))
        print("intercept=%.3f  top terms (win-turn / unit; +=later win):" % intercept, file=sys.stderr)
        for name, c in terms[:14]:
            if abs(c) > 1e-4:
                print("    %-24s %+.4f" % (name, c), file=sys.stderr)
        if args.out:
            obj = write_sidecar(args.out, feat_names, coefs, intercept)
            print("wrote %s (%d nonzero coefs, fixed-point x%d)"
                  % (args.out, len(obj["coefs"]), SCALE), file=sys.stderr)
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
