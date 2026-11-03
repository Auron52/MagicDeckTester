#!/usr/bin/env python3
"""Priced decision ceiling A/B report (run_ab_x.sh): quality vs tail per X.

Jobs are named <base>_x<X> (held-out regression shapes, arms X in {0,..}) and tailx<X>_<tail job> (tail corpus,
whose X=0 arm is a separate run passed as --tail0 <prefix>, jobs named tail_<tail job>). Per-game results come
from the SLOW-GAME lines (run with MTG_SLOW_GAME_MS=1); games are paired on (base job, game index).

QUALITY, per deck and overall, for each X against X=0: games whose win turn moved, how many got better/worse,
and the summed win-turn delta (a non-win counts as turn --nowin, default 11). TAIL: per-game wall p50/p99/p99.9/
max and the total, per arm; and per tail-corpus game the wall at each X.
Usage: ab_x_report.py <abx prefix> [--tail0 <ct prefix>] [--nowin 11]
"""
import argparse
import collections
import re
import sys

GAME = re.compile(r"SLOW-GAME (\d+)ms\s+job=(\S+) gi=(\d+) wt=(-?\d+) units=(\d+)")
ARM = re.compile(r"^(.*)_x(\d+)$")
TAILARM = re.compile(r"^tailx(\d+)_(.*)$")


def pct(v, q):
    return v[min(len(v) - 1, int(q * len(v)))] if v else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("abx")
    ap.add_argument("--tail0", default="")
    ap.add_argument("--nowin", type=int, default=11)
    a = ap.parse_args()
    reg = collections.defaultdict(dict)    # x -> (base, gi) -> (ms, wt, units)
    tail = collections.defaultdict(dict)   # x -> tail job -> (ms, wt, units)
    with open(a.abx + ".err") as f:
        for ln in f:
            m = GAME.search(ln)
            if not m:
                continue
            ms, job, gi, wt, u = int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))
            t = TAILARM.match(job)
            if t:
                tail[int(t.group(1))][t.group(2)] = (ms, wt, u)
                continue
            r = ARM.match(job)
            if r:
                reg[int(r.group(2))][(r.group(1), gi)] = (ms, wt, u)
    if a.tail0:
        with open(a.tail0 + ".err") as f:
            for ln in f:
                m = GAME.search(ln)
                if m and m.group(2).startswith("tail_"):
                    tail[0][m.group(2)[5:]] = (int(m.group(1)), int(m.group(4)), int(m.group(5)))
    xs = sorted(reg)
    if 0 not in reg:
        sys.exit("no X=0 arm")
    w = lambda t: t if t > 0 else a.nowin
    print("=== QUALITY (held-out regression shapes), each X vs X=0, paired per game")
    for x in xs:
        if x == 0:
            continue
        keys = sorted(set(reg[0]) & set(reg[x]))
        by = collections.defaultdict(lambda: [0, 0, 0, 0, 0])   # n, moved, better, worse, sum delta
        for k in keys:
            d = k[0].split("_")[0]
            b = by[d]
            b[0] += 1
            d0, d1 = w(reg[0][k][1]), w(reg[x][k][1])
            if d0 != d1:
                b[1] += 1
                b[2 if d1 < d0 else 3] += 1
                b[4] += d1 - d0
        tot = [sum(v[i] for v in by.values()) for i in range(5)]
        print(f"X={x}: {tot[0]} paired games, moved {tot[1]} (better {tot[2]}, worse {tot[3]}), "
              f"summed win-turn delta {tot[4]:+d} ({tot[4] / max(1, tot[0]):+.4f}/game)")
        for d, b in sorted(by.items(), key=lambda kv: -kv[1][1]):
            if b[1]:
                print(f"   {d:16s} n={b[0]:5d} moved {b[1]:3d} better {b[2]:3d} worse {b[3]:3d} delta {b[4]:+d} ({b[4] / b[0]:+.4f}/game)")
    print("=== TAIL (held-out regression shapes): per-game wall by arm")
    for x in xs:
        v = sorted(g[0] for g in reg[x].values())
        print(f"X={x:3d}: n={len(v)} sum {sum(v) / 1000:8.0f}s  p50 {pct(v, .5)}ms  p99 {pct(v, .99) / 1000:.1f}s  "
              f"p99.9 {pct(v, .999) / 1000:.1f}s  max {v[-1] / 1000 if v else 0:.1f}s")
    if tail:
        print("=== TAIL CORPUS: wall s (win turn) by X")
        txs = sorted(tail)
        print("   %-48s" % "game" + "".join("%16s" % f"X={x}" for x in txs))
        names = sorted(set().union(*[set(tail[x]) for x in txs]), key=lambda n: -max(tail[x].get(n, (0,))[0] for x in txs))
        for n in names:
            cells = []
            for x in txs:
                g = tail[x].get(n)
                cells.append("%16s" % ("-" if g is None else f"{g[0] / 1000:.1f} ({g[1]})"))
            print("   %-48s" % n[:48] + "".join(cells))
        for x in txs:
            v = sorted(g[0] for g in tail[x].values())
            print(f"   X={x}: n={len(v)} sum {sum(v) / 1000:.0f}s max {v[-1] / 1000 if v else 0:.1f}s")


if __name__ == "__main__":
    sys.exit(main())
