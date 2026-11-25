#!/usr/bin/env python3
"""Maze hold-up final A/B readout: ab_analyze.py <batch.out>. Pairs maze_<fmt>_g<gi>_{off,on} by (fmt, gi).
Win turn = avg= (one game per job; a game not won counts as 99). Net = slower - faster (negative = ON better).
Prints per-format net, mean win turns, cpu/units ratios, and every slower game (fmt gi off on seed)."""
import re, sys, collections
BASE = {"play": 31100000, "2hg": 31300000}
rows = collections.defaultdict(dict)
for l in open(sys.argv[1]):
    m = re.match(r'maze_(\w+?)_g(\d+)_(on|off): played=(\d+) avg=(\S+) digest=(\S+) ms=(\d+) units=(\d+) cpu=(\d+)', l)
    if not m: continue
    fmt, gi, arm = m.group(1), int(m.group(2)), m.group(3)
    wt = float(m.group(5))
    if wt <= 0: wt = 99.0
    rows[(fmt, gi)][arm] = dict(wt=wt, dig=m.group(6), units=int(m.group(8)), cpu=int(m.group(9)))
for fmt in ("play", "2hg"):
    pairs = [(gi, r) for (f, gi), r in sorted(rows.items()) if f == fmt and "on" in r and "off" in r]
    if not pairs: continue
    slower = [(gi, r) for gi, r in pairs if r["on"]["wt"] > r["off"]["wt"]]
    faster = [(gi, r) for gi, r in pairs if r["on"]["wt"] < r["off"]["wt"]]
    same_dig = sum(1 for gi, r in pairs if r["on"]["dig"] == r["off"]["dig"])
    n = len(pairs)
    mo = sum(r["off"]["wt"] for _, r in pairs) / n; mn = sum(r["on"]["wt"] for _, r in pairs) / n
    cpu_o = sum(r["off"]["cpu"] for _, r in pairs); cpu_n = sum(r["on"]["cpu"] for _, r in pairs)
    u_o = sum(r["off"]["units"] for _, r in pairs); u_n = sum(r["on"]["units"] for _, r in pairs)
    print(f"{fmt}: pairs={n} slower={len(slower)} faster={len(faster)} net={len(slower)-len(faster):+d} "
          f"identical-digest={same_dig} mean wt off={mo:.4f} on={mn:.4f} (d={mn-mo:+.4f}) "
          f"cpu on/off={cpu_n/cpu_o:.4f} units on/off={u_n/u_o:.4f}")
    for gi, r in slower:
        print(f"  SLOWER {fmt} gi={gi} seed={BASE[fmt]+gi} off={r['off']['wt']:.0f} on={r['on']['wt']:.0f}")
