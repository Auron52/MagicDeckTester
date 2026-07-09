#!/usr/bin/env python3
"""Label-discrimination diagnostic.

A row file is "<label> <feat...> <seed> <turn>", one row per candidate plan of a
decision. Rows sharing (seed,turn) are the competing candidates of ONE decision.

If the target is a WEAK TEACHER, the candidates of a decision collapse to few (or
one) distinct label values -> no gradient to rank them -> no model of any capacity
can learn the ranking. This measures exactly that, per decision:

  - #decisions, mean #candidates/decision
  - fraction of decisions where ALL candidates share ONE label (dead: no signal)
  - mean distinct-label-fraction (distinct labels / candidates)
  - mean label spread (max-min) per decision, and how often spread==0

Run: python3 scripts/label_discrim.py logs/eval/knights_rollout.rows [more.rows ...]
"""
import sys
from collections import defaultdict


def analyze(path):
    groups = defaultdict(list)  # (seed,turn) -> [labels]
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            if len(p) < 3:
                continue
            label = float(p[0])
            seed, turn = p[-2], p[-1]
            groups[(seed, turn)].append(label)

    multi = {k: v for k, v in groups.items() if len(v) >= 2}
    n = len(multi)
    if n == 0:
        print(f"{path}: no multi-candidate decisions")
        return

    dead = 0            # all candidates identical label
    distinct_frac = 0.0
    spread_sum = 0.0
    spread_zero = 0
    cand_sum = 0
    for k, labels in multi.items():
        cand_sum += len(labels)
        d = len(set(labels))
        if d == 1:
            dead += 1
        distinct_frac += d / len(labels)
        sp = max(labels) - min(labels)
        spread_sum += sp
        if sp == 0.0:
            spread_zero += 1

    print(f"{path}")
    print(f"  multi-candidate decisions : {n}")
    print(f"  mean candidates/decision  : {cand_sum/n:.2f}")
    print(f"  DEAD (all same label)     : {dead}/{n} = {100*dead/n:.1f}%   <- no gradient")
    print(f"  mean distinct-label frac  : {distinct_frac/n:.3f}   (1.0 = every candidate unique)")
    print(f"  mean label spread (turns) : {spread_sum/n:.3f}")
    print(f"  spread==0 decisions       : {spread_zero}/{n} = {100*spread_zero/n:.1f}%")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        analyze(p)
