#!/usr/bin/env python3
"""Summarise an MTG_TIER1_STATS per-game dump (core/Tier1Ledger.h).

Line format (BatchRunner): <job> <gi> <ms> <units> <peak_total> <cat>=<own_peak>/<at_total_peak> ...

Prints, for the whole dump and per deck (job name up to the first '_'):
  * per-game peak total: p50 / p90 / p95 / p99 / max (MB)
  * coverage: share of games whose peak total fits each candidate tier-1 size
  * per cache: p50 / p95 / p99 / max of its own per-game peak, and its mean share of the total at peak
Usage: tier1_stats.py <dump> [--sizes 25,50,75,100,150,200,300] [--by-deck]
"""
import argparse
import collections
import sys

MB = 1 << 20


def pct(xs, q):
    if not xs:
        return 0
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def load(path):
    rows = []
    with open(path) as f:
        for ln in f:
            p = ln.split()
            if len(p) < 5:
                continue
            r = {"job": p[0], "gi": int(p[1]), "ms": int(p[2]), "units": int(p[3]), "peak": int(p[4]),
                 "cats": {}, "work": {}}
            for t in p[5:]:
                k, v = t.split("=")
                if "/" in v:
                    own, at = v.split("/")
                    r["cats"][k] = (int(own), int(at))
                else:
                    r["work"][k] = int(v)   # core/WorkCounters.h per-game counts
            rows.append(r)
    return rows


def summary(rows, sizes, label):
    peaks = [r["peak"] for r in rows]
    print(f"== {label}: {len(rows)} games")
    print("   peak total MB  p50 %.1f  p90 %.1f  p95 %.1f  p99 %.1f  max %.1f" % tuple(
        pct(peaks, q) / MB for q in (0.5, 0.9, 0.95, 0.99, 1.0)) if peaks else "   (none)")
    cov = []
    for s in sizes:
        n = sum(1 for x in peaks if x <= s * MB)
        cov.append(f"{s}MB {100.0 * n / max(1, len(peaks)):.2f}%")
    print("   coverage      " + "  ".join(cov))
    cats = sorted({k for r in rows for k in r["cats"]})
    for c in cats:
        own = [r["cats"].get(c, (0, 0))[0] for r in rows]
        share = [r["cats"][c][1] / r["peak"] for r in rows if r["peak"] > 0 and c in r["cats"]]
        if max(own, default=0) == 0:
            continue
        print("   %-10s own-peak MB p50 %7.2f p95 %7.2f p99 %7.2f max %8.2f | mean share at peak %.1f%%" % (
            c, pct(own, .5) / MB, pct(own, .95) / MB, pct(own, .99) / MB, max(own) / MB,
            100.0 * sum(share) / max(1, len(share))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--sizes", default="25,50,75,100,150,200,300,500")
    ap.add_argument("--by-deck", action="store_true")
    a = ap.parse_args()
    sizes = [int(x) for x in a.sizes.split(",")]
    rows = load(a.dump)
    summary(rows, sizes, "ALL")
    if a.by_deck:
        by = collections.defaultdict(list)
        for r in rows:
            by[r["job"].split("_")[0]].append(r)
        for d in sorted(by, key=lambda d: -pct([r["peak"] for r in by[d]], 0.99)):
            summary(by[d], sizes, d)


if __name__ == "__main__":
    sys.exit(main())
