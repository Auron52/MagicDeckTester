#!/usr/bin/env python3
"""Paired report for the MTG_PAYSAC_RANK sweep (throwaway scaffolding).

Every arm runs the SAME manifest, so per-game win turns pair directly by (job, game_index).
Reads the per-job `.wins` files (`game_index win_turn digest`; wt <= 0 means unwon, scored
max_turns+1 exactly as ComputeAvgTurns does) and reports each arm against `base`.

An UNPAIRED read of this effect got the sign wrong once already -- see §6 of
docs/design/lump-mana-sources-as-payment-sources.md -- so the paired t is the only number here
that settles anything. `digest changed` counts games whose PLAY differed at all, which is the
sensitivity check: an arm that changes no digests cannot possibly move the metric.
"""
import math, os, sys

root = sys.argv[1] if len(sys.argv) > 1 else "logs/paysac_rank"
maxt = int(sys.argv[2]) if len(sys.argv) > 2 else 8


def load(arm_dir):
    """(job, game_index) -> (loss-penalized win turn, play digest)."""
    out = {}
    for fn in sorted(os.listdir(arm_dir)):
        if not fn.endswith(".wins"):
            continue
        job = fn[:-5]
        with open(os.path.join(arm_dir, fn)) as f:
            for line in f:
                p = line.split()
                if len(p) < 2:
                    continue
                wt = int(p[1])
                out[(job, int(p[0]))] = (wt if wt > 0 else maxt + 1, p[2] if len(p) > 2 else "")
    return out


arms = {}
for name in sorted(os.listdir(root)):
    if name.startswith("arm_"):
        arms[name[4:]] = load(os.path.join(root, name))

base = arms.pop("base")
print(f"base: n={len(base)}  avg={sum(v[0] for v in base.values())/len(base):.4f}")
print(f"{'arm':>6} {'n':>6} {'avg':>8} {'delta':>9} {'se':>8} {'t':>7} "
      f"{'better':>7} {'worse':>6} {'digest~':>8}")
for arm, cur in sorted(arms.items(), key=lambda kv: int(kv[0])):
    keys = [k for k in base if k in cur]
    d = [cur[k][0] - base[k][0] for k in keys]
    n = len(d)
    mean = sum(d) / n
    var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0.0
    se = math.sqrt(var / n) if n > 1 else 0.0
    t = mean / se if se > 0 else 0.0
    dig = sum(1 for k in keys if cur[k][1] != base[k][1])
    print(f"{arm:>6} {n:>6} {sum(cur[k][0] for k in keys)/n:>8.4f} {mean:>+9.4f} "
          f"{se:>8.4f} {t:>+7.2f} {sum(1 for x in d if x<0):>7} "
          f"{sum(1 for x in d if x>0):>6} {dig:>8}")
