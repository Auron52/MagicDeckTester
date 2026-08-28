#!/usr/bin/env python3
"""Pool several rounds of the SAME A/B into one verdict.

Used by scripts/mullgen.sh for the profile-comparison (regen) A/B, which escalates: if a round
lands too close to call, another round of fresh seeds is run and the ROUNDS ARE POOLED rather than
compared. The accept bar is "at all worse on average" with no significance margin, so a near-tie
must not be settled by noise -- one more round is much cheaper than adopting or rejecting a table
on a coin flip (user, 2026-08-28: "We want to be fairly confident in the result either way").

Pooling is over SEEDS, not over rounds' means: every round runs the same games-per-seed, but taking
a mean-of-means would weight a short round equally with a long one. Seeds are disjoint by
construction (mullgen allocates a fresh block per round), and a duplicate seed is dropped loudly
rather than double-counted.

  keepmodel_pool_ab.py --a-tag old --b-tag new <dir1> [dir2 ...]

Prints the pooled table and writes <dir-last>/pooled_delta.txt (mean B-A, turns).
"""
import argparse, os, re, sys


def arm(d, tag):
    """seed -> avg win turn, parsed from the batch runner's per-job summary lines."""
    out, fn = {}, os.path.join(d, f"batch_{tag}.log")
    if os.path.exists(fn):
        for ln in open(fn):
            m = re.match(r"s(\d+): played=\d+ avg=([\d.]+)", ln)
            if m:
                out[m.group(1)] = float(m.group(2))
    return out


ap = argparse.ArgumentParser()
ap.add_argument("--a-tag", required=True)
ap.add_argument("--b-tag", required=True)
ap.add_argument("dirs", nargs="+")
args = ap.parse_args()

per, seen, rounds = [], {}, []
for i, d in enumerate(args.dirs, 1):
    a, b = arm(d, args.a_tag), arm(d, args.b_tag)
    got = 0
    for s in sorted(set(a) & set(b), key=int):
        if s in seen:
            print(f"WARNING: seed {s} already counted (round {seen[s]}); dropping the copy in {d}")
            continue
        seen[s] = i
        per.append((s, a[s], b[s], i))
        got += 1
    rounds.append((i, d, got))

if not per:
    print("no paired seeds found -- nothing to pool")
    sys.exit(1)

mA = sum(x for _, x, _, _ in per) / len(per)
mB = sum(y for _, _, y, _ in per) / len(per)
delta = mB - mA
ds = sorted(y - x for _, x, y, _ in per)
n = len(ds)
med = ds[n // 2] if n % 2 else 0.5 * (ds[n // 2 - 1] + ds[n // 2])
sd = (sum((d - delta) ** 2 for d in ds) / (n - 1)) ** 0.5 if n > 1 else 0.0
se = sd / (n ** 0.5) if n > 1 else 0.0

print(f"\n=== POOLED A/B ({args.b_tag} vs {args.a_tag}) over {len(rounds)} round(s), {n} seeds ===")
for i, d, got in rounds:
    print(f"  round {i}: {got:>3} seeds  {d}")
print(f"\n{args.a_tag:>14}{args.b_tag:>14}{'delta B-A':>14}{'seeds B<A':>14}")
nlt = sum(1 for d in ds if d < -1e-9)
print(f"{mA:>14.4f}{mB:>14.4f}{delta:>+14.4f}{str(nlt)+'/'+str(n):>14}")
print(f"\nspread: min {ds[0]:+.4f}  median {med:+.4f}  max {ds[-1]:+.4f}"
      f"   sd {sd:.4f}  se {se:.4f}  mean/se {(delta/se if se else 0):+.2f}")
print(f"\npooled delta {delta:+.6f}t")

with open(os.path.join(args.dirs[-1], "pooled_delta.txt"), "w") as f:
    f.write(f"{delta:.6f}\n")
with open(os.path.join(args.dirs[-1], "pooled_se.txt"), "w") as f:
    f.write(f"{se:.6f}\n")
