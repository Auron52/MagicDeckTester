#!/usr/bin/env python3
"""Per-deck cost of a regression tier from its batch.log. ms = per-job THREAD-WALL (sum over its games),
so absolute numbers inflate under host contention; SHARES within one pooled run are comparable.
units = search work (0 at d0). Usage: tier_cost.py [smoke|regression|overnight] [batch.log]  (default log: test/logs/<mode>/batch.log)"""
import re, sys, collections
mode = sys.argv[1] if len(sys.argv) > 1 else "overnight"
log = sys.argv[2] if len(sys.argv) > 2 else f"test/logs/{mode}/batch.log"
D = collections.defaultdict(lambda: collections.Counter())
for l in open(log):
    m = re.match(r"(\S+): played=(\d+) .*?ms=(\d+) units=(\d+)", l)
    if not m: continue
    name, g, ms, u = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
    deck = name.split(f"_{mode}_")[0]; dp = re.search(r"_d(\d)_", name).group(1)
    c = D[deck]; c["ms"] += ms; c["g"] += g; c["u"] += u; c[f"ms_d{dp}"] += ms; c[f"g_d{dp}"] += g; c["jobs"] += 1
tot = sum(c["ms"] for c in D.values()); tu = sum(c["u"] for c in D.values()) or 1
print(f"{mode}: {sum(c['jobs'] for c in D.values())} jobs, {sum(c['g'] for c in D.values())} games, "
      f"{tot/3.6e6:.1f} worker-h (wall, sum of jobs)")
print(f"{'deck':20} {'share':>6} {'cum':>6} {'worker-h':>8} {'games':>6} {'ms/game':>8} | {'d0 %':>5} {'d3 %':>5} {'d5 %':>5} | {'units %':>7}")
cum = 0
for deck, c in sorted(D.items(), key=lambda kv: -kv[1]["ms"]):
    cum += c["ms"]
    print(f"{deck:20} {c['ms']/tot:6.1%} {cum/tot:6.1%} {c['ms']/3.6e6:8.2f} {c['g']:6} {c['ms']/c['g']:8.0f} | "
          + " ".join(f"{c[f'ms_d{d}']/c['ms']:5.0%}" if c['ms'] else "    -" for d in "035") + f" | {c['u']/tu:7.1%}")
