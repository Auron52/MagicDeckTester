#!/usr/bin/env python3
"""Read the v2-vs-v3 comparison. Run:  python3 logs/kittyv3_integrate/analyze_v2_vs_v3.py

PRE-REGISTERED TEST (set before the run; do not renegotiate it after seeing the numbers).
The decision cell is **d5** -- USER 2026-10-14: *"the higher depth is the number that matters"*,
*"I'm not so worried about d3"*. d7 b1000 is a DIRECTION CHECK at 2,000 games/arm (se ~0.041) and
must not be quoted as a powered test.

  d5 t <= -2  -> "V3 beats V2". The user's gate for dropping kitty v1 + kittyv2 rows is met.
  d5 |t| < 2  -> NOT a win. Report as indistinguishable at this sample, quote the 95% CI bound,
                 and let the user decide whether not-worse + ~40% cheaper satisfies the gate.
  d5 t >= +2  -> V2 is better on the depth that matters; rows stay.

TWO TRAPS THIS SCRIPT EXISTS TO AVOID, both of which bit on 2026-10-14:
 1. A batch job's per-game seed is `seed_base + game_index`, so seed bases spaced LESS than the
    game count replay the same games. The first run used bases 20001..20016 x 1200 games and was
    16 near-identical repeats (s20001[gi+1] == s20002[gi] for 1199 of 1199); its t was inflated
    ~4x. `v2_vs_v3.py` now asserts stride > games and no range overlap. The giveaway was cell
    deltas with sd 0.0017 where sampling alone gives ~0.057 -- ALWAYS look at the spread of the
    per-cell deltas before trusting a pooled t.
 2. These arms are NOT paired: a different decklist changes the card numbering, so the same seed
    opens a different game. se is therefore the full between-game variance, computed from the
    per-game .wins files below -- never a paired/pairwise se.
"""
import math
import os
import sys

LOSS = 9.0                      # unwon = max_turns + 1, derived from committed GT (max_turns = 8)
WINS = sys.argv[1] if len(sys.argv) > 1 else "logs/kittyv3_integrate/v2v3_wins3"
CELLS = [(5, 40, 100000, 2000, 32), (7, 1000, 200000, 2000, 8)]   # depth, bud, origin, stride, n


def games(path):
    out = []
    for line in open(path):
        p = line.split()
        if len(p) >= 2:
            t = int(p[1])
            out.append(LOSS if t == -1 else float(t))
    return out


def agg(v):
    n = len(v)
    m = sum(v) / n
    return n, m, sum((x - m) ** 2 for x in v) / (n - 1)


print(f"reading {WINS}\n")
for depth, bud, origin, stride, nb in CELLS:
    a, b, deltas, missing = [], [], [], 0
    for i in range(nb):
        s = origin + i * stride
        pa = f"{WINS}/v2_d{depth}_s{s}.wins"
        pb = f"{WINS}/v3_d{depth}_s{s}.wins"
        if not (os.path.exists(pa) and os.path.exists(pb)):
            missing += 1
            continue
        ga, gb = games(pa), games(pb)
        a += ga
        b += gb
        deltas.append(sum(gb) / len(gb) - sum(ga) / len(ga))
    if not a:
        print(f"d{depth} b{bud}: no data yet ({missing} cells missing)")
        continue
    n1, m1, v1 = agg(a)
    n2, m2, v2 = agg(b)
    se = math.sqrt(v1 / n1 + v2 / n2)
    dl = m2 - m1
    lo, hi = dl - 1.96 * se, dl + 1.96 * se
    sd_cells = (math.sqrt(sum((x - sum(deltas) / len(deltas)) ** 2 for x in deltas)
                          / (len(deltas) - 1)) if len(deltas) > 1 else float("nan"))
    print(f"d{depth} b{bud}   v2 {m1:.4f}   v3 {m2:.4f}   delta {dl:+.4f}   se {se:.4f}   "
          f"t {dl/se:+.2f}")
    print(f"           {n1:,} games/arm, {len(deltas)} cells"
          + (f", {missing} MISSING" if missing else ""))
    print(f"           95% CI [{lo:+.4f}, {hi:+.4f}]")
    # the overlap canary: per-cell delta spread must look like sampling noise, not a constant
    expect = se * math.sqrt(len(deltas))
    print(f"           per-cell delta sd {sd_cells:.4f}  (independent sampling predicts ~{expect:.4f};"
          f" far smaller means the cells share games)")
    unwon = [100.0 * sum(1 for x in v if x == LOSS) / len(v) for v in (a, b)]
    print(f"           unwon: v2 {unwon[0]:.2f}%  v3 {unwon[1]:.2f}%")
    print(f"           per-cell deltas: {' '.join(f'{x:+.3f}' for x in sorted(deltas))}\n")
