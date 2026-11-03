#!/usr/bin/env python3
"""Compare two `mtg --batch` runs of the same manifest, per job and per game.

Each run is a prefix P with P.log (stdout: "<job>: played=N avg=A digest=D ms=M units=U") and P.err
(stderr; per-game "SLOW-GAME <ms>ms job=<j> gi=<g> wt=<w> units=<u>" lines, i.e. run with
MTG_SLOW_GAME_MS=1). Reports jobs whose digest/avg differ, games whose win turn or units differ, and
the wall ratio (sum of per-game ms) overall and per deck.
Usage: cmp_runs.py <prefixA> <prefixB> [--show 20]
"""
import argparse
import collections
import re
import sys

JOB = re.compile(r"^(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+)")
GAME = re.compile(r"SLOW-GAME (\d+)ms\s+job=(\S+) gi=(\d+) wt=(-?\d+) units=(\d+)")


def load(prefix):
    jobs, games = {}, {}
    with open(prefix + ".log") as f:
        for ln in f:
            m = JOB.match(ln.strip())
            if m:
                jobs[m.group(1)] = (int(m.group(2)), m.group(3), m.group(4))
    with open(prefix + ".err") as f:
        for ln in f:
            m = GAME.search(ln)
            if m:
                games[(m.group(2), int(m.group(3)))] = (int(m.group(1)), int(m.group(4)), int(m.group(5)))
    return jobs, games


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--show", type=int, default=20)
    a = ap.parse_args()
    ja, ga = load(a.a)
    jb, gb = load(a.b)
    common = sorted(set(ja) & set(jb))
    djobs = [j for j in common if ja[j] != jb[j]]
    print(f"jobs: {len(common)} common, {len(djobs)} differ (digest/avg/played)")
    for j in djobs[: a.show]:
        print(f"   {j}: A={ja[j]} B={jb[j]}")
    keys = sorted(set(ga) & set(gb))
    wt = [k for k in keys if ga[k][1] != gb[k][1]]
    un = [k for k in keys if ga[k][2] != gb[k][2]]
    print(f"games: {len(keys)} common, win-turn differs {len(wt)}, units differ {len(un)}")
    for k in un[: a.show]:
        print(f"   {k[0]} gi={k[1]}: wt {ga[k][1]}->{gb[k][1]} units {ga[k][2]}->{gb[k][2]}")
    if wt:
        better = sum(1 for k in wt if 0 < gb[k][1] < ga[k][1] or (ga[k][1] < 0 < gb[k][1]))
        print(f"   win turn: B better {better}, B worse {len(wt) - better}")
    ms_a = sum(ga[k][0] for k in keys)
    ms_b = sum(gb[k][0] for k in keys)
    print(f"wall (sum game ms): A {ms_a / 1000:.0f}s B {ms_b / 1000:.0f}s  B/A {ms_b / max(1, ms_a):.3f}")
    by = collections.defaultdict(lambda: [0, 0])
    for k in keys:
        d = k[0].split("_")[0]
        by[d][0] += ga[k][0]
        by[d][1] += gb[k][0]
    for d, (x, y) in sorted(by.items(), key=lambda kv: -kv[1][0])[:15]:
        print(f"   {d:16s} A {x / 1000:8.0f}s  B/A {y / max(1, x):.3f}")


if __name__ == "__main__":
    sys.exit(main())
