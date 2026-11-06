#!/usr/bin/env python3
"""Per-decision PRICED work against the decision's budget, from a turn census (MTG_TURN_CENSUS).

Sizes the priced decision ceiling (decisionwork::PricedScope, MTG_DECISION_PRICED_X): a decision trips when
its priced work exceeds X * L * REF_NS, L = the decision's budget in units (census `budget` column).
For each candidate X prints how many decisions would trip, the share of all priced time that sits ABOVE the
ceiling (the work a ceiling at X could remove at most), and the worst decisions.

Priced work = sum over work kinds of price_k * w_k (census columns w_step_c .. w_plan_emit; prices default to
core/WorkCounters.h kPriceNs, override with --prices name=ns,...). The w_* columns are thread_local, so they
are exact in a multi-threaded run even where the census marks a row contended.
Run the census with MTG_TURN_CENSUS_JOB=1 so --by-deck can attribute rows.
Usage: census_priced.py <census.tsv> [--ref-ns 25000] [--x 2,5,10,20,50] [--by-deck] [--top 20]
"""
import argparse
import collections
import sys

KINDS = ["step_c", "step_f", "greedy", "enum", "plans", "odo", "apply", "pay", "simkey", "plan_emit"]
DEFAULT_PRICE_NS = {"step_c": 21426, "step_f": 21426, "greedy": 569, "enum": 18455, "plans": 2866,
                    "odo": 511, "apply": 8097, "pay": 2561, "simkey": 3749, "plan_emit": 10000}


def pct(v, q):
    if not v:
        return 0.0
    return v[min(len(v) - 1, int(q * len(v)))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("census")
    ap.add_argument("--ref-ns", type=float, default=25000.0)
    ap.add_argument("--x", default="2,5,10,20,50,100")
    ap.add_argument("--prices", default="")
    ap.add_argument("--by-deck", action="store_true")
    ap.add_argument("--top", type=int, default=15)
    a = ap.parse_args()
    price = dict(DEFAULT_PRICE_NS)
    for kv in filter(None, a.prices.split(",")):
        k, v = kv.split("=")
        price[k] = float(v)
    xs = [float(x) for x in a.x.split(",")]
    rows = []
    hdr = None
    with open(a.census) as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if hdr is None:
                hdr = {n: i for i, n in enumerate(parts)}
                missing = [k for k in KINDS if k != "plan_emit" and "w_" + k not in hdr]
                if missing:
                    sys.exit(f"census lacks work columns {missing} (built before the w_* columns?)")
                if "w_plan_emit" not in hdr:
                    print("warning: old census has no w_plan_emit; its priced total excludes emitted-plan work", file=sys.stderr)
                continue
            L = int(parts[hdr["budget"]])
            if L <= 0:
                continue   # unlimited root: no ceiling applies
            pr = sum(price[k] * int(parts[hdr["w_" + k]]) for k in KINDS if "w_" + k in hdr)
            board = parts[-1]
            job = board[1:board.index("]")] if board.startswith("[") else "-"
            rows.append((pr / (L * a.ref_ns), pr, L, int(parts[hdr["units"]]), int(parts[hdr["wall_us"]]),
                         job, int(parts[hdr["seed"]]), int(parts[hdr["turn"]])))
    if not rows:
        sys.exit("no budgeted rows")
    tot = sum(r[1] for r in rows)
    rat = sorted(r[0] for r in rows)
    print(f"{len(rows)} budgeted decisions, priced total {tot / 1e9:.1f} s")
    print("ratio = priced / (L x %.0f ns):  p50 %.3f  p90 %.3f  p99 %.2f  p99.9 %.2f  max %.1f" % (
        a.ref_ns, pct(rat, .5), pct(rat, .9), pct(rat, .99), pct(rat, .999), rat[-1]))
    print("   X   trips   trip%    priced share above ceiling   (work a ceiling at X removes at most)")
    for x in xs:
        trips = [r for r in rows if r[0] > x]
        above = sum(r[1] - x * r[2] * a.ref_ns for r in trips)
        print("%5g %7d %7.3f%%   %6.2f%%" % (x, len(trips), 100.0 * len(trips) / len(rows), 100.0 * above / tot))
    print(f"top {a.top} decisions by ratio: ratio priced_s L units wall_s job seed turn")
    for r in sorted(rows, key=lambda r: -r[0])[: a.top]:
        print("   %8.1f %8.2f %7d %9d %8.2f  %s s%d t%d" % (r[0], r[1] / 1e9, r[2], r[3], r[4] / 1e6, r[5], r[6], r[7]))
    if a.by_deck:
        by = collections.defaultdict(list)
        for r in rows:
            by[r[5].split("_")[0]].append(r)
        print("per deck: n  ratio p50 p99 max | priced_s | trips at each X")
        for d, rr in sorted(by.items(), key=lambda kv: -sum(r[1] for r in kv[1])):
            v = sorted(r[0] for r in rr)
            trips = " ".join("%d" % sum(1 for r in rr if r[0] > x) for x in xs)
            print("   %-16s %7d %6.3f %6.2f %7.1f | %8.1f | %s" % (d, len(rr), pct(v, .5), pct(v, .99), v[-1],
                                                                 sum(r[1] for r in rr) / 1e9, trips))


if __name__ == "__main__":
    sys.exit(main())
