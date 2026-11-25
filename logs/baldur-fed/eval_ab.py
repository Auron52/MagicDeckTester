#!/usr/bin/env python3
"""eval_ab.py <batch.out> : per-row paired net (slower - faster; negative = the fix is better), turn sums, cpu ratio,
and the slower / faster game lists. Lines: `<row>_g<gi>_<arm>: played=1 avg=T digest=D ms=.. units=.. cpu=..`."""
import re, sys, collections
pat = re.compile(r'^(\w+?)_g(\d+)_(off|on): played=\d+ avg=([\d.]+) digest=(\w+) ms=(\d+) units=(\d+) cpu=(\d+)')
d = collections.defaultdict(dict)
for l in open(sys.argv[1]):
    m = pat.match(l.strip())
    if m:
        row, gi, arm, t, dg, ms, un, cpu = m.groups()
        d[row][(int(gi), arm)] = (float(t), dg, int(cpu))
for row in sorted(d):
    r = d[row]; gis = sorted({g for g, a in r if (g, 'off') in r and (g, 'on') in r})
    slow = [g for g in gis if r[(g, 'on')][0] > r[(g, 'off')][0]]
    fast = [g for g in gis if r[(g, 'on')][0] < r[(g, 'off')][0]]
    ident = sum(1 for g in gis if r[(g, 'on')][1] == r[(g, 'off')][1])
    toff = sum(r[(g, 'off')][0] for g in gis); ton = sum(r[(g, 'on')][0] for g in gis)
    coff = sum(r[(g, 'off')][2] for g in gis); con = sum(r[(g, 'on')][2] for g in gis)
    print(f"{row}: pairs={len(gis)} identical={ident} slower={len(slow)} faster={len(fast)} net={len(slow)-len(fast):+d} "
          f"turnsum off={toff:.0f} on={ton:.0f} (d={ton-toff:+.0f}) cpu on/off={con/max(coff,1):.4f} ({coff/1000:.0f}s / {con/1000:.0f}s)")
    if slow: print("  slower:", ' '.join(f"g{g}({r[(g,'off')][0]:.0f}->{r[(g,'on')][0]:.0f})" for g in slow))
    if fast: print("  faster:", ' '.join(f"g{g}({r[(g,'off')][0]:.0f}->{r[(g,'on')][0]:.0f})" for g in fast))
