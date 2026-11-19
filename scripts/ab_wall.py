#!/usr/bin/env python3
"""Order-balanced WALL comparison of a pooled `mtg --batch` A/B (arms innermost, one job per game or chunk).

    scripts/ab_wall.py <manifest.json> <batch.stdout> [--base base] [--arms fix,nr]

WHY (2026-11-19, Maze's End F5 / AB-ORDER). In a pooled A/B the per-game `ms=` and even `cpu=` of a game
are not a property of the game: they depend on how loaded the box is while it runs (here 16 cores / 32 SMT
threads, a laptop part whose all-core clock is far below its one-core boost). Maze's End d3 s192014 gi13
(identical digest and units every time) costs 12.1 s alone on a quiet core and ~24 s inside a saturated
32-thread pool. There is NO cross-game cache behind it: two identical copies run back to back on ONE thread
(--threads 1) read 0.997, and play is untouched (digests and units identical in every order).

THE BIAS. `same_seed_inflight: 1` serialises a seed's copies, so the second copy of a LONG game starts only
when the first ends -- in the run's draining tail, on an idle box, at up to 2x speed. Identical arms, 150
Maze's End d3 games, mtg.f5g:
    same_seed_inflight 1, A always first      B/A ms 0.844 (replicate 0.840)   <- pure order effect
    same_seed_inflight 1, order alternated    naive 0.936, BALANCED 0.947      <- mostly, not fully, removed
    same_seed_inflight 0, A always first      B/A ms 1.004                     <- the fix
    one job per arm, 150 games each (arm-major, deck_compare's shape)  B/A 0.966
The second copy's speed-up is not a uniform factor (only the games that reach the tail get it), so the
balanced ratio below cancels it only in expectation; the real fix is to let a seed's arms RUN SIDE BY SIDE:
same_seed_inflight 0, or >= the number of arms. Keep 1 only where the seeds are memory-heavy (deep, hard
seeds holding GBs), and then read BALANCED with that caveat, or time the arms in separate full batches.

WHAT THIS PRINTS, per arm against the base arm, on the games both played:
  * naive ratio: sum(ms arm) / sum(ms base) -- what a hand-rolled analysis reports;
  * the same ratio split by which arm of the pair the manifest DISPATCHES first (`same_seed_inflight` and the
    pool's ready-first rule make the manifest order the start order within a cost tier);
  * the ORDER-BALANCED ratio sqrt(R_base_first * R_arm_first): if running second multiplies a game's cost by
    f, the two strata read r*f and r/f, and their geometric mean is r;
  * the order factor f = sqrt(R_base_first / R_arm_first) (1.000 = no order effect);
  * the same three for `cpu=` (thread CPU time: removes descheduling, NOT SMT or clock-speed contention), and
    the units ratio (deterministic work, load-free, but not wall-faithful across search shapes).
The balanced ratio needs both strata populated: alternate the arm order per seed (scripts/deck_avg_arms.py
and scripts/ref_bench_arms.py rotate it). It warns when a pair is serialised by same_seed_inflight.

Pairing: a job name is "<arm>__<rest>" (deck_avg_arms / ref_bench_arms) or "<rest>_<arm>" (the mkab
shape, e.g. d3_g13_fix). <rest> is the pair key. Jobs with played=0 are dropped from their pair.
"""
import argparse
import collections
import json
import math
import re
import sys

JOB = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=(\d+) units=(\d+)(?: cpu=(\d+))?")


def split(name):
    if "__" in name:
        arm, rest = name.split("__", 1)
    else:
        rest, _, arm = name.rpartition("_")
    return arm, rest


def ratio(num, den):
    return num / den if den else float("nan")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest")
    ap.add_argument("stdout", help="the batch's stdout (the `<job>: played=.. ms=.. units=.. cpu=..` lines)")
    ap.add_argument("--base", default=None, help="base arm (default: the arm of the manifest's first job)")
    ap.add_argument("--arms", default=None, help="comma list of arms to compare (default: all others)")
    a = ap.parse_args()

    jobs = json.load(open(a.manifest))["jobs"]
    pos = {j["name"]: i for i, j in enumerate(jobs)}
    res = {}
    for line in open(a.stdout):
        m = JOB.match(line.strip())
        if m and int(m.group(2)) > 0:
            res[m.group(1)] = dict(ms=int(m.group(5)), units=int(m.group(6)),
                                   cpu=int(m.group(7)) if m.group(7) else None, digest=m.group(4))
    pairs = collections.defaultdict(dict)       # rest -> arm -> job name
    for j in jobs:
        arm, rest = split(j["name"])
        pairs[rest][arm] = j["name"]
    base = a.base or split(jobs[0]["name"])[0]
    arms = sorted({arm for p in pairs.values() for arm in p} - {base})
    if a.arms:
        arms = [x for x in a.arms.split(",") if x]
    print(f"base arm: {base}   pairs: {len(pairs)}   jobs with results: {len(res)}")
    for arm in arms:
        # strata: 0 = base dispatched first, 1 = arm dispatched first
        acc = [collections.Counter(), collections.Counter()]
        n = [0, 0]
        dig = 0
        tiered = 0
        for rest, p in pairs.items():
            jb, ja = p.get(base), p.get(arm)
            if jb not in res or ja not in res:
                continue
            # The pool's LPT sort orders by weight, depth, budget_ms and profile BEFORE manifest position, so a
            # pair whose arms differ in one of those is dispatched in sort order, not manifest order.
            if any(jobs[pos[jb]].get(k) != jobs[pos[ja]].get(k) for k in ("weight", "depth", "budget_ms", "profile")):
                tiered += 1
            s = 0 if pos[jb] < pos[ja] else 1
            n[s] += 1
            rb, ra = res[jb], res[ja]
            dig += rb["digest"] != ra["digest"]
            for k in ("ms", "units"):
                acc[s][k + "_b"] += rb[k]
                acc[s][k + "_a"] += ra[k]
            if rb["cpu"] is not None and ra["cpu"] is not None:
                acc[s]["cpu_b"] += rb["cpu"]
                acc[s]["cpu_a"] += ra["cpu"]
        tot = acc[0] + acc[1]
        N = n[0] + n[1]
        if N == 0:
            print(f"\n{arm}: no paired games")
            continue
        print(f"\n{arm} vs {base}: {N} paired games ({n[0]} {base}-first, {n[1]} {arm}-first), "
              f"digests differing {dig}")
        ser = sum(1 for p in pairs.values() if p.get(base) in pos and p.get(arm) in pos
                  and 0 < (jobs[pos[p[base]]].get("same_seed_inflight") or 0) < len(p))
        if ser:
            print(f"  WARNING: {ser} pair(s) run SERIALISED (same_seed_inflight below the arm count): the later copy of a"
                  " long game runs in the draining tail and reads fast (identical arms: 0.84). For wall, use"
                  " same_seed_inflight 0 (or >= arms); otherwise read BALANCED, which cancels it only in expectation")
        if tiered:
            print(f"  WARNING: {tiered} pair(s) differ in weight/depth/budget_ms/profile, so the pool's LPT sort, not the"
                  " manifest, decides which arm starts first; the strata (and BALANCED) are approximate for them")
        print(f"  {'metric':6s} {'naive':>7s} {base+'-1st':>10s} {arm+'-1st':>10s} {'BALANCED':>9s} {'order f':>8s}"
              f"   per-game {base} / {arm}")
        for k in ("ms", "cpu", "units"):
            if tot[k + "_b"] == 0:
                continue
            naive = ratio(tot[k + "_a"], tot[k + "_b"])
            r = [ratio(acc[s][k + "_a"], acc[s][k + "_b"]) if n[s] else float("nan") for s in (0, 1)]
            if n[0] and n[1] and k != "units":
                bal = math.sqrt(r[0] * r[1])
                f = math.sqrt(r[0] / r[1])
                tail = f"{bal:9.3f} {f:8.3f}"
            elif k == "units":
                tail = f"{naive:9.3f} {'--':>8s}"
            else:
                tail = f"{'(1 stratum)':>9s} {'--':>8s}"
            rs = " ".join(f"{x:10.3f}" if x == x else f"{'--':>10s}" for x in r)
            print(f"  {k:6s} {naive:7.3f} {rs} {tail}   "
                  f"{tot[k + '_b'] / N:,.0f} / {tot[k + '_a'] / N:,.0f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
