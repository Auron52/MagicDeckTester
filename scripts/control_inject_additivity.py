#!/usr/bin/env python3
"""PROTOTYPE: additivity / stacking analysis for the control-injection probe (control-scoring-mode.md section 8).

usage: control_inject_additivity.py <probe_logs dir> <probe.err>
All comparisons are PAIRED per game (every arm of a deck plays the same seeds); unwon = 13.
"""
import math
import os
import re
import sys
from collections import defaultdict

CAP = 13
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from control_inject_probe import ARMS  # noqa: E402
DECKS = ["burn", "whiteknights", "stompy", "dragons", "breaching"]


def read_wins(path):
    out = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                t = int(p[1])
                out[int(p[0])] = CAP if t < 0 else t
    return out


def ms(xs):
    n = len(xs)
    m = sum(xs) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / max(1, n - 1))
    return m, sd / math.sqrt(n)


def main():
    logdir, err = sys.argv[1], sys.argv[2]
    applied = defaultdict(lambda: [0, 0])
    pat = re.compile(r"\[ctrl-inject\] job=(\S+) gi=(\d+) .* applied=(\d+)")
    with open(err, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                a = applied[m.group(1)]
                a[1] += 1
                a[0] += 1 if int(m.group(3)) > 0 else 0

    global DECKS
    DECKS = [d for d in DECKS
             if sum(1 for fn in os.listdir(logdir) if fn.startswith(d + "__") and fn.endswith(".wins")) == 26]
    W = {}
    for d in DECKS:
        for fn in os.listdir(logdir):
            if fn.startswith(d + "__") and fn.endswith(".wins"):
                W[(d, fn[len(d) + 2:-5])] = read_wins(os.path.join(logdir, fn))

    def delta(d, arm):
        b, w = W[(d, "base")], W[(d, arm)]
        return {g: w[g] - b[g] for g in w if g in b}

    def comb(d, terms):
        """per-game sum of coef * delay(arm); terms = [(coef, arm)]"""
        ds = [(c, delta(d, a)) for c, a in terms]
        games = set.intersection(*[set(x.keys()) for _, x in ds])
        return [sum(c * x[g] for c, x in ds) for g in sorted(games)]

    arms_order = ["skip3", "land2", "land3", "land4", "land3x2", "land3x3", "crt2", "crt3", "crt4", "crt3x2",
                  "crt3x3", "sweep3", "sweep4", "ctr2", "ctr3", "ctr4", "ctr3x2", "ctr3x3", "ctr3same2",
                  "ctr3mv3", "disc2", "disc3", "disc4", "disc2x2", "disc2x3"]

    print("== Per arm: mean paired delay vs base (se), applied/tried, share of games unwon (=13) ==")
    print(f"{'arm':10s} " + " ".join(f"{d:>22s}" for d in DECKS) + f" {'mean of decks':>16s}")
    line = f"{'base avg':10s} "
    for d in DECKS:
        b = W[(d, "base")]
        cap = sum(1 for v in b.values() if v == CAP) / len(b)
        line += f"{sum(b.values()) / len(b):>13.3f} cap{cap:4.0%}  "
    print(line)
    for arm in arms_order:
        line = f"{arm:10s} "
        means, ses = [], []
        for d in DECKS:
            if (d, arm) not in W:
                line += f"{'-':>22s} "
                continue
            m, se = ms(list(delta(d, arm).values()))
            a = list(applied.get(f"{d}__{arm}", [0, 0]))
            if arm.startswith("ctr"):   # counter lines are logged only when applied: tried = games reaching T
                a[1] = sum(sum(1 for v in W[(d, arm)].values() if v >= int(e.split(":")[1]))
                           for e in ARMS[arm].split(";"))
            hit = f"{a[0] / a[1]:.0%}" if a[1] else "-"
            line += f"{m:+6.2f}({se:4.2f}) h{hit:>4s} c{sum(1 for v in W[(d, arm)].values() if v == CAP) / len(W[(d, arm)]):3.0%} "
            means.append(m)
            ses.append(se)
        if len(means) == len(DECKS):
            line += f" {sum(means) / len(means):+7.3f}({math.sqrt(sum(s * s for s in ses)) / len(ses):.3f})"
        print(line)

    tests = [
        ("land: (3+4) - 3 - 4", [(1, "land3x2"), (-1, "land3"), (-1, "land4")]),
        ("crt:  (3+4) - 3 - 4", [(1, "crt3x2"), (-1, "crt3"), (-1, "crt4")]),
        ("ctr:  (3+4) - 3 - 4", [(1, "ctr3x2"), (-1, "ctr3"), (-1, "ctr4")]),
        ("disc: (2+3) - 2 - 3", [(1, "disc2x2"), (-1, "disc2"), (-1, "disc3")]),
        ("ctr: 2 at r3 - 2 x r3", [(1, "ctr3same2"), (-2, "ctr3")]),
        ("land: x3 incr - x2 incr", [(1, "land3x3"), (-2, "land3x2"), (1, "land3")]),
        ("crt:  x3 incr - x2 incr", [(1, "crt3x3"), (-2, "crt3x2"), (1, "crt3")]),
        ("ctr:  x3 incr - x2 incr", [(1, "ctr3x3"), (-2, "ctr3x2"), (1, "ctr3")]),
        ("disc: x3 incr - x2 incr", [(1, "disc2x3"), (-2, "disc2x2"), (1, "disc2")]),
        ("sweep3 - crt3", [(1, "sweep3"), (-1, "crt3")]),
        ("skip3 - P(base in 3..12)", None),
    ]
    print()
    print("== Interaction terms (paired per game; > 0 = increasing returns, < 0 = diminishing) ==")
    print(f"{'test':26s} " + " ".join(f"{d:>15s}" for d in DECKS) + f" {'mean of decks':>16s}")
    for name, terms in tests:
        line = f"{name:26s} "
        means, ses = [], []
        for d in DECKS:
            if terms is None:
                b = W[(d, "base")]
                s = delta(d, "skip3")
                xs = [s[g] - (1 if 3 <= b[g] <= CAP - 1 else 0) for g in s]
            else:
                if any((d, a) not in W for _, a in terms):
                    line += f"{'-':>15s} "
                    continue
                xs = comb(d, terms)
            m, se = ms(xs)
            line += f"{m:+8.3f}({se:4.2f}) "
            means.append(m)
            ses.append(se)
        if len(means) == len(DECKS):
            line += f" {sum(means) / len(means):+7.3f}({math.sqrt(sum(s * s for s in ses)) / len(ses):.3f})"
        print(line)

    print()
    print("== Increment per extra copy (mean over decks): x1, then +2nd, then +3rd ==")
    for fam, a1, a2, a3 in [("land", "land3", "land3x2", "land3x3"), ("crt", "crt3", "crt3x2", "crt3x3"),
                            ("ctr", "ctr3", "ctr3x2", "ctr3x3"), ("disc", "disc2", "disc2x2", "disc2x3")]:
        vals = []
        for a in (a1, a2, a3):
            vals.append(sum(ms(list(delta(d, a).values()))[0] for d in DECKS) / len(DECKS))
        print(f"{fam:5s} 1st {vals[0]:+.3f}  2nd {vals[1] - vals[0]:+.3f}  3rd {vals[2] - vals[1]:+.3f}"
              f"   (cumulative {vals[0]:+.3f} / {vals[1]:+.3f} / {vals[2]:+.3f})")


if __name__ == "__main__":
    main()
