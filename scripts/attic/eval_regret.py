#!/usr/bin/env python3
"""Within-decision rank-regret diagnostic for the learned eval model.

Each real decision emits one row per candidate plan (same seed+turn). A d0 policy driven by the
model picks, at each decision, the candidate with the highest model Score (= -predicted win turn).
This groups rows by (seed,turn), and for each decision compares the model's PICK to the oracle's
best candidate:

    regret(decision) = label(model_argmax) - min_over_candidates(label)   # turns lost vs oracle-best

Reports the regret distribution and pick-accuracy. This is the metric that governs standalone-d0
quality (unlike global RMSE, which is dominated by cross-decision variance). See learned-d0-policy.md.

    scripts/eval_regret.py --rows logs/eval/th_v2.rows --model /tmp/th_v2.eval.json
"""
import argparse, json, sys
from collections import defaultdict


def read_rows(path):
    feat_names, rows = None, []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s.startswith('#'):
                feat_names = s[1:].split()[1:-2]
                continue
            t = s.split()
            if len(t) < 4:
                continue
            label = float(t[0])
            feats = [float(v) for v in t[1:-2]]
            seed, turn = int(t[-2]), int(t[-1])
            rows.append((seed, turn, label, feats))
    return feat_names, rows


def load_model(path, feat_names):
    """Return a coefs vector aligned to feat_names (Score = intercept + coefs.feats; higher=better)."""
    obj = json.load(open(path)).get("eval_model", {})
    cmap = obj.get("coefs", {})
    intercept = obj.get("intercept", 0)
    coefs = [cmap.get(n, 0) for n in feat_names]
    return coefs, intercept


def score(coefs, intercept, feats):
    return intercept + sum(c * v for c, v in zip(coefs, feats))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--by-turn", action="store_true", help="also break regret down by turn number")
    args = ap.parse_args()

    feat_names, rows = read_rows(args.rows)
    coefs, intercept = load_model(args.model, feat_names)

    groups = defaultdict(list)
    for seed, turn, label, feats in rows:
        groups[(seed, turn)].append((label, feats))

    regrets, exact, spread_pos = [], 0, 0
    per_turn = defaultdict(list)
    for (seed, turn), cands in groups.items():
        labels = [c[0] for c in cands]
        best_label = min(labels)
        if max(labels) > best_label:
            spread_pos += 1                       # decisions where the choice actually matters
        picked = max(cands, key=lambda c: score(coefs, intercept, c[1]))
        r = picked[0] - best_label
        regrets.append(r)
        per_turn[turn].append(r)
        if abs(r) < 1e-9:
            exact += 1

    n = len(regrets)
    regrets.sort()
    mean = sum(regrets) / n
    p50 = regrets[n // 2]
    p90 = regrets[int(n * 0.9)]
    print("decisions=%d  (with >1 distinct label: %d)" % (n, spread_pos))
    print("PICK-ACCURACY (model_argmax == oracle-best): %.1f%%" % (100.0 * exact / n))
    print("REGRET turns  mean=%.3f  median=%.3f  p90=%.3f  max=%.3f" % (mean, p50, p90, regrets[-1]))
    # Regret restricted to decisions that matter (label spread > 0) -- the honest number.
    mm = []
    for (seed, turn), cands in groups.items():
        labels = [c[0] for c in cands]
        if max(labels) == min(labels):
            continue
        picked = max(cands, key=lambda c: score(coefs, intercept, c[1]))
        mm.append(picked[0] - min(labels))
    if mm:
        mm.sort()
        print("REGRET on decisions-that-matter  n=%d  mean=%.3f  median=%.3f  p90=%.3f"
              % (len(mm), sum(mm) / len(mm), mm[len(mm) // 2], mm[int(len(mm) * 0.9)]))
    if args.by_turn:
        print("# per-turn mean regret:")
        for t in sorted(per_turn):
            rs = per_turn[t]
            print("  turn %-2d n=%-5d mean_regret=%.3f" % (t, len(rs), sum(rs) / len(rs)))


if __name__ == "__main__":
    main()
