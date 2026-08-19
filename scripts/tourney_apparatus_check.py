#!/usr/bin/env python3
"""Does the CHEAP apparatus give the same verdicts as the expensive one?

    python3 scripts/tourney_apparatus_check.py logs/tourney/run_alias/tourney.tsv \
                                               logs/tourney/run/tourney.tsv

This is the question the whole tournament turned into (user, 2026-08-19): "perhaps generating
mulligan tables is a poor use of time in general ... I mostly would want to only use it to double
check a much cheaper approach. Because this cost is too much for normal usage."

  cheap     the SHIPPED Mirrorwing table (K=17, R=40) with the four new names ALIASED into the
            buckets they replace -- zero generation cost, 24 of 60 arms resolve fully into it
  expensive the pooled table generated for this exact 60-arm set (K=20, R=10) -- ~28 h

What matters is NOT whether the two agree on the effect size to three decimals. It is whether they
give the same ANSWER: same sign, and the same better/worse recommendation. If they do, the cheap
route is the default from now on and generation becomes a rare event rather than a per-combination
step. If they disagree, the disagreement itself localises where bucketing actually matters -- which
is worth more than either number alone.

Read the caveat in both directions. The cheap table has a COARSER partition (it cannot tell an
Oracle hand from an Anger hand) but 4x the rollouts per cell; the expensive one has the exact
partition but R=10, and the repo's own measurement puts the R=10 penalty (0.032t) an order of
magnitude above the foreign-fit bias (0.004t). Neither is "the accurate one".
"""
import argparse, csv, math, sys


def load(p):
    with open(p) as fh:
        return {(r["life"], r["test"], r["A"], r["B"], r["subset"]): r
                for r in csv.DictReader(fh, delimiter="\t")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cheap")
    ap.add_argument("expensive")
    ap.add_argument("--subset", default="all", choices=["all", "drawn", "cast"])
    a = ap.parse_args()
    C, E = load(a.cheap), load(a.expensive)
    keys = sorted(k for k in set(C) & set(E) if k[4] == a.subset)
    if not keys:
        sys.exit("no comparisons in common -- did both runs finish?")

    print(f"# Apparatus check -- cheap (aliased K=17 R=40) vs expensive (pooled K=20 R=10)\n")
    print(f"subset `{a.subset}`, {len(keys)} comparisons in common\n")
    print("| life | test | comparison | cheap Δturns | exp Δturns | sign | cheap margin | "
          "exp margin | rec |")
    print("|---|---|---|---:|---:|:--:|---:|---:|:--:|")
    agree_sign = agree_rec = 0
    diffs = []
    for k in keys:
        c, e = C[k], E[k]
        cd, ed = float(c["turns"]), float(e["turns"])
        cm, em = int(c["margin"]), int(e["margin"])
        # "Same sign" only counts when at least one side is big enough to be making a claim:
        # two independent nulls agreeing on a sign is a coin flip, not corroboration.
        big = max(abs(float(c["t"])), abs(float(e["t"]))) >= 2.0
        s = "=" if (cd < 0) == (ed < 0) else "X"
        r = "=" if (cm < 0) == (em < 0) else "X"
        if big:
            agree_sign += s == "="
            agree_rec += r == "="
            diffs.append(abs(cd - ed))
        print(f"| {k[0]} | {k[1]} | {k[2]} → {k[3]} | {cd:+.4f} | {ed:+.4f} | {s} | "
              f"{cm:+,} | {em:+,} | {r} |")
    n = len(diffs)
    print(f"\nOf the {n} comparisons where either apparatus makes a claim (|t| >= 2):")
    print(f"  same sign on the effect:        {agree_sign}/{n}")
    print(f"  same better/worse recommendation: {agree_rec}/{n}")
    if diffs:
        diffs.sort()
        print(f"  |cheap - expensive| effect size: median {diffs[n // 2]:.4f}t, "
              f"max {diffs[-1]:.4f}t")
        print("\n  For scale: the effects this tournament is resolving are 0.03-0.08t, the "
              "repo's\n  measured R=10-vs-high-R penalty is 0.032t, and its own-vs-foreign fit "
              "bias is 0.004t.")


if __name__ == "__main__":
    main()
