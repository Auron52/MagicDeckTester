#!/usr/bin/env python3
"""Fixed-point RANKING GBDT for the mid-game eval model — the capacity fix for standalone-d0 on
complex decks (the linear ranker matches baseline on value/combo but lags on tuned-aggro / combo
decks; see docs/design/learned-d0-policy.md).

Gradient-boosts regression trees on the PAIRWISE ranking loss (LambdaMART-lite): within each decision
the oracle-best candidate (min de-clairvoyed win turn) is the anchor, and we push it to outrank every
other candidate, weighted by the win-turn gap. Trees split on INTEGER feature thresholds and carry
fixed-point INTEGER leaf values, so serving (KeepModel MidGameEvaluator::EvalTree) is byte-identical
across platforms — the determinism contract. Output is the same sidecar schema as the linear trainer,
with a "trees" array (compact nodes: leaf = [value]; internal = [feat_name, threshold, left, right]).

    scripts/train_eval_gbdt.py --rows logs/eval/antilife_v3.rows --out decks/x.eval.json \
        --trees 120 --depth 4 --lr 0.15 --min-leaf 20

Higher = better (earlier win). No numpy. Trainer float math need not be reproducible; determinism
comes from the fixed-point quantization of the SERVED model.
"""
import sys, json, argparse, math, importlib.util, os
from collections import defaultdict

SCALE = 1000  # fixed-point scale for leaf values (mirrors the linear trainer / KeepScore)

# Reuse the linear trainer's row reader (and, optionally, its linear rank_fit for a warm start).
_spec = importlib.util.spec_from_file_location(
    "train_eval_model", os.path.join(os.path.dirname(__file__), "train_eval_model.py"))
_tm = importlib.util.module_from_spec(_spec)
sys.argv_backup = sys.argv; sys.argv = ["x"]
_spec.loader.exec_module(_tm)
sys.argv = sys.argv_backup


def build_pairs(groups, y):
    """Per decision (seed,turn): anchor = oracle-best (min win turn); pairs (anchor, other, gap)."""
    idx = defaultdict(list)
    for i, g in enumerate(groups):
        idx[g].append(i)
    pairs = []
    for members in idx.values():
        anchor = min(members, key=lambda i: y[i])
        for j in members:
            gap = y[j] - y[anchor]
            if gap > 1e-9:
                pairs.append((anchor, j, gap))
    return pairs


def pairwise_residuals(pairs, F, n):
    """Negative gradient of Sum gap*log(1+exp(-(F[a]-F[j]))): push anchors up, others down."""
    r = [0.0] * n
    loss = 0.0
    for a, j, w in pairs:
        s = F[a] - F[j]
        # g = w * sigmoid(-s); anchor gets +g, other -g
        z = math.exp(min(30.0, max(-30.0, -s)))
        sig = z / (1.0 + z)
        g = w * sig
        r[a] += g
        r[j] -= g
        loss += w * math.log1p(z) if s >= 0 else w * (-s + math.log1p(math.exp(min(30.0, s))))
    return r, loss


def pairwise_acc(pairs, F):
    ok = sum(1 for a, j, _ in pairs if F[a] > F[j])
    return 100.0 * ok / max(1, len(pairs))


class Tree:
    __slots__ = ("nodes",)  # flat: each node = (feat, thresh, left, right, value); feat<0 => leaf

    def __init__(self):
        self.nodes = []


def fit_tree(X, resid, idxs, max_depth, min_leaf, d):
    """CART regression tree (variance-reduction splits on integer thresholds). Returns a flat node
    list; node = [feat, thresh, left, right, value] with feat=-1 for leaves. Children are appended
    during recursion, so a node's child indices are always > its own index."""
    nodes = []

    def build(members, dep):
        me = len(nodes)
        my = sum(resid[i] for i in members) / len(members)
        nodes.append([-1, 0, -1, -1, my])                 # provisional leaf
        if dep >= max_depth or len(members) < 2 * min_leaf:
            return me
        tot_s = sum(resid[i] for i in members)
        tot_n = len(members)
        base = tot_s * tot_s / tot_n
        best = None                                        # (gain, feat, thresh)
        for feat in range(d):
            hist = defaultdict(lambda: [0.0, 0])
            for i in members:
                h = hist[X[i][feat]]
                h[0] += resid[i]; h[1] += 1
            vals = sorted(hist)
            if len(vals) < 2:
                continue
            ls = 0.0; ln = 0
            for t in vals[:-1]:
                h = hist[t]; ls += h[0]; ln += h[1]
                rn = tot_n - ln
                if ln < min_leaf or rn < min_leaf:
                    continue
                rs = tot_s - ls
                gain = ls * ls / ln + rs * rs / rn - base
                if best is None or gain > best[0]:
                    best = (gain, feat, t)
        if best is None or best[0] <= 1e-12:
            return me
        _, feat, t = best
        left_m = [i for i in members if X[i][feat] <= t]
        right_m = [i for i in members if X[i][feat] > t]
        li = build(left_m, dep + 1)
        ri = build(right_m, dep + 1)
        nodes[me] = [feat, t, li, ri, 0.0]                # internal
        return me

    build(idxs, 0)
    return nodes


def tree_predict(nodes, x):
    i = 0
    while nodes[i][0] >= 0:
        feat, t, l, r, _ = nodes[i]
        i = l if x[feat] <= t else r
    return nodes[i][4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--trees", type=int, default=120)
    ap.add_argument("--depth", type=int, default=4)
    ap.add_argument("--lr", type=float, default=0.15)
    ap.add_argument("--min-leaf", type=int, default=20)
    ap.add_argument("--init-model", default=None,
                    help="warm-start from an EXISTING linear sidecar (e.g. the faithful-anchor "
                         "linear d0) instead of a free rank_fit; boosts trees on THAT base's residual "
                         "and re-emits its exact linear coefs. Use for anchor-dominated (Vial) decks "
                         "where a free linear warm-start reintroduces the noise the anchor removed.")
    ap.add_argument("--init-linear", action="store_true",
                    help="warm-start F from the linear rank model (hybrid: linear + boosted trees)")
    ap.add_argument("--regression", action="store_true",
                    help="VALUE model: boost squared-error on win turn (predict absolute win turn) "
                         "instead of pairwise ranking; writes a value sidecar (Score = win turn)")
    args = ap.parse_args()

    feat_names, X, y, groups = _tm.read_rows(args.rows)
    n, d = len(X), len(X[0])
    print("rows=%d features=%d decisions=%d" % (n, d, len(set(groups))), file=sys.stderr)

    # Optional warm start: fit the linear ranker and initialise F to its (goodness) score, so trees
    # only correct where linear is wrong. coef_wt is in win-turn units (+ = later win); serving
    # goodness = -coef_wt . feats, which is exactly what we accumulate into F.
    lin_coefs_wt = None
    if args.init_model:
        with open(args.init_model) as f:
            base = json.load(f)["eval_model"]
        if int(base.get("intercept", 0)) != 0:
            print("WARN: --init-model intercept != 0 is dropped (ranking intercept is 0)", file=sys.stderr)
        # stored coefs are GOODNESS fixed-point (higher=better) = -coef_wt*SCALE; invert to win-turn units.
        gcoef = base.get("coefs", {})
        lin_coefs_wt = [-float(gcoef.get(name, 0)) / SCALE for name in feat_names]
        print("warm-started from anchored linear sidecar %s (%d nonzero coefs)"
              % (args.init_model, sum(1 for v in gcoef.values() if v)), file=sys.stderr)
    elif args.init_linear:
        lin_coefs_wt, _ = _tm.rank_fit(X, y, groups, 0.001, 600, 0.3)
        print("warm-started from linear rank model", file=sys.stderr)

    def boost(tr_idx, te_idx, held_groups, report):
        """Boost on tr_idx, reporting held-out metrics on te_idx. Returns (trees, reg_intercept)."""
        reg_intercept = 0.0
        F = [0.0] * n
        pairs = te_pairs = None
        if args.regression:
            # VALUE model: init F at the mean win turn, boost squared-error residuals toward y.
            reg_intercept = sum(y[i] for i in tr_idx) / max(len(tr_idx), 1)
            F = [reg_intercept] * n
        else:
            pairs_all = build_pairs(groups, y)
            if held_groups:
                te_pairs = [(a, j, w) for (a, j, w) in pairs_all if groups[a] in held_groups]
                pairs    = [(a, j, w) for (a, j, w) in pairs_all if groups[a] not in held_groups]
            else:
                pairs = pairs_all
            if report:
                print("pairs=%d" % len(pairs), file=sys.stderr)
            if lin_coefs_wt is not None:
                for i in range(n):
                    F[i] = sum(-lin_coefs_wt[j] * X[i][j] for j in range(d))    # linear goodness init

        def rmse(idxs):
            if not idxs: return float("nan")
            return math.sqrt(sum((F[i] - y[i]) ** 2 for i in idxs) / len(idxs))

        trees = []
        for rnd in range(args.trees):
            if args.regression:
                resid = [y[i] - F[i] for i in range(n)]                    # squared-error negative gradient
            else:
                resid, _ = pairwise_residuals(pairs, F, n)
            nodes = fit_tree(X, resid, tr_idx, args.depth, args.min_leaf, d)
            for i in range(n):
                F[i] += args.lr * tree_predict(nodes, X[i])
            # scale leaves by lr for serving (so serving sums the same increments)
            for nd in nodes:
                if nd[0] < 0:
                    nd[4] *= args.lr
            trees.append(nodes)
            if report and (rnd % 20 == 0 or rnd == args.trees - 1):
                if args.regression:
                    msg = "round %3d  train_RMSE=%.4f turns" % (rnd, rmse(tr_idx))
                    if te_idx:
                        msg += "  heldout_RMSE=%.4f  (n_tr=%d n_te=%d)" % (rmse(te_idx), len(tr_idx), len(te_idx))
                    print(msg, file=sys.stderr)
                else:
                    msg = "round %3d  train_pair_acc=%.1f%%" % (rnd, pairwise_acc(pairs, F))
                    if te_pairs is not None:
                        msg += "  heldout_pair_acc=%.1f%%" % pairwise_acc(te_pairs, F)
                    print(msg, file=sys.stderr)
        return trees, reg_intercept

    # TWO passes, always, with no flag to get either one wrong.
    #
    # A held-out number is the only thing that separates learning from memorising -- train RMSE falls
    # monotonically as trees are added whatever the model is doing -- so it is not optional and there
    # is no reason a caller would want it off. But the split trains on 75% of the rows, so measuring
    # and SHIPPING in one pass would mean shipping a model deliberately fitted on less data just to
    # print a diagnostic. Hence: measure on the split, then refit on everything and ship that. Same
    # recipe both times, so the reported number describes the model that ships.
    #
    # This used to be a --holdout flag, off by default. Nothing passed it, so every value model in
    # this repo was accepted on an in-sample RMSE alone.
    gids = sorted(set(groups))
    held_groups = set(gids[::4])                                  # every 4th DECISION, not every 4th row
    tr_idx = [i for i in range(n) if groups[i] not in held_groups]
    te_idx = [i for i in range(n) if groups[i] in held_groups]
    boost(tr_idx, te_idx, held_groups, report=True)                # measured, then discarded
    trees, reg_intercept = boost(list(range(n)), [], set(), report=False)   # shipped

    # Quantize to fixed-point integer trees and emit the sidecar.
    jtrees = []
    for nodes in trees:
        jn = []
        for feat, t, l, r, v in nodes:
            if feat < 0:
                jn.append([int(round(v * SCALE))])
            else:
                jn.append([feat_names[feat], int(t), int(l), int(r)])
        jtrees.append(jn)
    coefs_obj = {}
    if lin_coefs_wt is not None:
        # store NEGATED fixed-point coefs so serving Score = -winturn (higher=better), matching the
        # linear trainer's write_sidecar convention; F was accumulated with the same orientation.
        for name, c in zip(feat_names, lin_coefs_wt):
            q = int(round(-c * SCALE))
            if q != 0:
                coefs_obj[name] = q
    intercept = int(round(reg_intercept * SCALE))   # regression: mean-win-turn init; ranking: 0
    if args.out:
        obj = {"eval_model": {"intercept": intercept, "coefs": coefs_obj, "trees": jtrees}}
        with open(args.out, "w") as f:
            json.dump(obj, f)
            f.write("\n")
        leaves = sum(1 for nodes in trees for nd in nodes if nd[0] < 0)
        kind = "VALUE=win-turn" if args.regression else "ranker"
        print("wrote %s (%d trees, %d leaves, fixed-point x%d, %s)"
              % (args.out, len(trees), leaves, SCALE, kind), file=sys.stderr)


if __name__ == "__main__":
    main()
