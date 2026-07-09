#!/usr/bin/env python3
"""Value-of-clairvoyance / strategy-fusion diagnostic.

Given a ROLLOUT dump (non-clairvoyant greedy label = E_f[greedy]) and a SEARCHED dump
(clairvoyant label = E_f[max_{pi|f}]) over the SAME decisions, quantify how much the
clairvoyance in the searched labels would MISLEAD a non-clairvoyant policy.

Rows: "<label> <feat...> <seed> <turn>", one per candidate. Both dumps enumerate the
same candidate set in the same deterministic order per (seed,turn), so candidates pair
by position within a decision. Lower label = wins sooner = better.

Per decision:
  clair_gap      = mean(rollout - searched) >= 0   (turns the clairvoyant teacher "wins earlier")
  disagree       = argmin(searched) != argmin(rollout)   (clairvoyance picks a DIFFERENT action)
  fusion_regret  = rollout[argmin searched] - rollout[argmin rollout] >= 0
                   (honest, non-clairvoyant turns you LOSE by trusting the clairvoyant pick;
                    this is the strategy-fusion cost of training on searched labels)

If fusion_regret ~ 0  => clairvoyance is decision-irrelevant here; searched labels are safe
                         (EVPI doesn't change the ranking; averaging clairvoyant labels is fine).
If fusion_regret large => searched labels are decision-HARMFUL; must remove clairvoyance at the
                          source (reshuffle-averaged search), can't average it out.

Run: python3 scripts/evpi.py <rollout.rows> <searched.rows>
"""
import sys
from collections import defaultdict


def load(path):
    g = defaultdict(list)
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            if len(p) < 3:
                continue
            g[(p[-2], p[-1])].append(float(p[0]))
    return g


def argmin(xs):
    bi, bv = 0, xs[0]
    for i, v in enumerate(xs):
        if v < bv:
            bi, bv = i, v
    return bi


def main():
    roll = load(sys.argv[1])
    srch = load(sys.argv[2])
    keys = [k for k in roll if k in srch and len(roll[k]) == len(srch[k]) and len(roll[k]) >= 2]
    mism = sum(1 for k in roll if k in srch and len(roll[k]) != len(srch[k]))

    n = len(keys)
    if n == 0:
        print("no aligned multi-candidate decisions"); return
    gap_sum = 0.0
    disagree = 0
    live = 0           # decisions where labels actually vary (a real choice)
    fr_sum = 0.0
    fr_pos = 0         # decisions with fusion_regret > 0
    fr_max = 0.0
    for k in keys:
        r, s = roll[k], srch[k]
        gap_sum += sum(r[i] - s[i] for i in range(len(r))) / len(r)
        if max(r) - min(r) < 1e-9 and max(s) - min(s) < 1e-9:
            continue
        live += 1
        ir, isr = argmin(r), argmin(s)
        if ir != isr:
            disagree += 1
        fr = r[isr] - r[ir]     # honest turns lost by following the clairvoyant pick
        fr_sum += fr
        fr_max = max(fr_max, fr)
        if fr > 1e-9:
            fr_pos += 1

    print("%s  vs  %s" % (sys.argv[1].split("/")[-1], sys.argv[2].split("/")[-1]))
    print("  aligned decisions        : %d   (candidate-count mismatches skipped: %d)" % (n, mism))
    print("  mean clairvoyance gap     : %.3f turns  (E[rollout]-E[searched], greedy->clairvoyant headroom)" % (gap_sum / n))
    print("  live decisions (a choice) : %d" % live)
    print("  argmin DISAGREE rate      : %d/%d = %.1f%%  (clairvoyance prefers a different action)" % (disagree, live, 100*disagree/live))
    print("  FUSION REGRET mean        : %.3f turns  (honest turns lost trusting the clairvoyant pick)" % (fr_sum / live))
    print("  fusion regret >0 decisions: %d/%d = %.1f%%   max=%.2f turns" % (fr_pos, live, 100*fr_pos/live, fr_max))
    print()


if __name__ == "__main__":
    main()
