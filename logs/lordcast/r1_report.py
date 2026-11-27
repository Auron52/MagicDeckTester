#!/usr/bin/env python3
"""r1_report.py <rs.err> <c1.err> <feat.txt>: per-class recovery of the round-2 d0 slower games (onA slower than off)
under the amended arm onB vs off (both on the amended binary).
Classes (from the round-2 classification, logs/elves/lordcast-handoff.md Q1):
  A = frozen intent (onA with MTG_ATTACK_REPROJECT_INTENT=0 reaches off's turn), C = legendary lord kept twice
  (feat legdup), B = Goblins with more own bodies lost in the diverging main than off (lostB > lostA), other.
Order of precedence: C, A, B, other (C's games also recover under INTENT=0 only by accident of the re-read).
Prints per deck x class: n, fixed (onB <= off), still slower, faster-than-off; and the searched rows (non-d0)."""
import collections, re, sys
rs, c1, feat = sys.argv[1:4]
pat = re.compile(r"\[win\] job=([^|]+)\|([^|]+)\|([^|]+)\|(\d+) gi=(\d+) wt=(-?\d+)")
sc = lambda t: t if 0 < t <= 8 else 9
def load(f):
    W = {}
    for ln in open(f, errors="replace"):
        for m in pat.finditer(ln):
            k, s, a, _, gi, wt = m.groups(); W[(k, s, a, int(gi))] = int(wt)
    return W
R, C1 = load(rs), load(c1)
legdup, lost = set(), set()
for ln in open(feat):
    f = ln.split()
    if len(f) < 2 or not f[1].isdigit(): continue
    k, gi = f[0].lower(), int(f[1])
    if "legdup=[]" not in ln and "legdup=" in ln: legdup.add((k, gi))
    m = re.search(r"lostA=(\d+) lostB=(\d+)", ln)
    if m and int(m.group(2)) > int(m.group(1)): lost.add((k, gi))
def cls(k, gi):
    if (k, gi) in legdup: return "C"
    off, onai = C1.get((k, "d0", "off", gi)), C1.get((k, "d0", "onAI", gi))
    if off is not None and onai is not None and sc(onai) <= sc(off): return "A"
    if k == "goblins" and (k, gi) in lost: return "B"
    if k == "elves": return "elves"
    return "other"
tab = collections.defaultdict(lambda: collections.Counter())
still = collections.defaultdict(list)
for (k, s, a, gi), wt in sorted(R.items()):
    if a != "onB" or (k, s, "off", gi) not in R: continue
    off = R[(k, s, "off", gi)]
    c = cls(k, gi) if s == "d0" else s
    d = sc(wt) - sc(off)
    t = tab[(k, c)]; t["n"] += 1
    t["fixed" if d <= 0 else "slower"] += 1
    if d < 0: t["faster"] += 1
    if d > 0: still[(k, c)].append(f"{gi}:{off}->{wt}")
print(f"{'deck':10} {'class':6} {'n':>4} {'fixed':>6} {'slower':>6} {'faster':>6}")
for (k, c), t in sorted(tab.items()):
    print(f"{k:10} {c:6} {t['n']:4} {t['fixed']:6} {t['slower']:6} {t['faster']:6}")
for (k, c), v in sorted(still.items()):
    print(f"still slower {k} {c}: {' '.join(v[:40])}{' ...' if len(v) > 40 else ''}")
