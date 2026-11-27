#!/usr/bin/env python3
"""report.py <batch.err> <batch.out> [armA=on] [armB=off] [--list]: paired per-game A/B over the lordcast manifest.

net = A - B in turns (unwon = 9, max_turns 8 + 1). Also prints the null arm (a second B copy) as the noise floor
and core-ms per game per arm (cpu field of the chunk lines; arms innermost in one pool, so comparable)."""
import collections, re, sys
args = [a for a in sys.argv[1:] if not a.startswith("--")]
err, out = args[0], args[1]
A = args[2] if len(args) > 2 else "on"
B = args[3] if len(args) > 3 else "off"
LIST = "--list" in sys.argv
LOSS = 9
W = collections.defaultdict(dict)       # (key, shape, arm) -> {gi: wt}
pat = re.compile(r"\[win\] job=([^|]+)\|([^|]+)\|([^|]+)\|\d+ gi=(\d+) wt=(-?\d+)")
for ln in open(err, errors="replace"):
    for m in pat.finditer(ln):
        k, s, a, gi, wt = m.groups()
        W[(k, s, a)][int(gi)] = int(wt)
CPU = collections.defaultdict(lambda: [0, 0])
cp = re.compile(r"^([^|]+)\|([^|]+)\|([^|]+)\|\d+: played=(\d+) .*?cpu=(\d+)")
for ln in open(out, errors="replace"):
    m = cp.match(ln)
    if m:
        k, s, a, n, c = m.groups()
        CPU[(k, s, a)][0] += int(n); CPU[(k, s, a)][1] += int(c)
sc = lambda t: t if 0 < t <= 8 else LOSS
cells = sorted({(k, s) for (k, s, a) in W})
print(f"{'cell':24s} {'n':>6s} {'net':>6s} {'/game':>8s} {'fast':>5s} {'slow':>5s} {'null-disc':>9s} "
      f"{'cpu/g ' + B:>10s} {'cpu/g ' + A:>10s} {'ratio':>6s}")
slow_all = []
for k, s in cells:
    a, b, nl = W.get((k, s, A), {}), W.get((k, s, B), {}), W.get((k, s, "null"), {})
    gis = sorted(set(a) & set(b))
    if not gis: continue
    d = [(g, b[g], a[g]) for g in gis]
    net = sum(sc(x) - sc(y) for g, y, x in d)
    fast = [(g, y, x) for g, y, x in d if sc(x) < sc(y)]
    slow = [(g, y, x) for g, y, x in d if sc(x) > sc(y)]
    ng = sorted(set(nl) & set(b))
    disc = sum(1 for g in ng if nl[g] != b[g])
    ca, cb = CPU.get((k, s, A), [0, 0]), CPU.get((k, s, B), [0, 0])
    pa = ca[1] / ca[0] if ca[0] else 0; pb = cb[1] / cb[0] if cb[0] else 0
    print(f"{k + ' ' + s:24s} {len(gis):6d} {net:+6d} {net / len(gis):+8.4f} {len(fast):5d} {len(slow):5d} "
          f"{(str(disc) + '/' + str(len(ng))) if ng else '-':>9s} {pb:10.1f} {pa:10.1f} {(pa / pb if pb else 0):6.3f}")
    for g, y, x in slow: slow_all.append((k, s, g, y, x))
    if LIST and slow: print("    slower:", " ".join(f"gi{g}:{y}->{x}" for g, y, x in slow[:60]))
    if LIST and fast and len(fast) <= 30: print("    faster:", " ".join(f"gi{g}:{y}->{x}" for g, y, x in fast))
missing = [(k, s, ar) for (k, s, ar) in W if ar in (A, B) and len(W[(k, s, ar)]) == 0]
if missing: print("EMPTY:", missing)
