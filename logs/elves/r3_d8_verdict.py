#!/usr/bin/env python3
"""r3_d8_verdict.py <ab.out> <d8.out> [arm] [ref]: per format, the games slower in `arm` vs `ref` at d5 b20, and whether
each RECOVERS (arm at d8 b0 wins no later than ref did at d5 b20). Prints ref's d8 b0 turn too."""
import re, sys, collections
arm = sys.argv[3] if len(sys.argv) > 3 else "bandtapfix"
ref = sys.argv[4] if len(sys.argv) > 4 else "band"
def load(p):
    R = collections.defaultdict(dict)
    for line in open(p):
        m = re.match(r'(\w+?)_(\w+)@(\d+): played=1 avg=([\d.]+)', line)
        if m: R[(m[1], int(m[3]))][m[2]] = float(m[4])
    return R
A, D = load(sys.argv[1]), load(sys.argv[2])
for fmt in ("std", "2hg"):
    slow = sorted(gi for (f, gi), a in A.items() if f == fmt and arm in a and ref in a and a[arm] > a[ref])
    rec, notr = [], []
    for gi in slow:
        a5, r5 = A[(fmt, gi)][arm], A[(fmt, gi)][ref]
        a8, r8 = D[(fmt, gi)].get(arm), D[(fmt, gi)].get(ref)
        tag = f"{gi}(d5 {a5:g}/{r5:g} d8 {a8}/{r8})"
        (rec if a8 is not None and a8 <= r5 else notr).append(tag)
    print(f"{arm} vs {ref} {fmt}: slower {len(slow)} recover {len(rec)} {rec}\n    NOT {notr}")
