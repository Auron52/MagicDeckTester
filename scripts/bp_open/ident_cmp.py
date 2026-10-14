#!/usr/bin/env python3
"""Identity check: per-job digests of the base binary's identity run vs the ident_* jobs of a lever batch.
  ident_cmp.py <base batch.out> <lever batch.out>"""
import re, sys

pat = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=(\d+) units=(\d+)")


def load(path, prefix=""):
    out = {}
    for line in open(path, errors="ignore"):
        m = pat.search(line)
        if m and m.group(1).startswith(prefix):
            out[m.group(1)[len(prefix):]] = (m.group(4), int(m.group(2)), m.group(3), int(m.group(6)))
    return out


base = load(sys.argv[1])
lev = load(sys.argv[2], "ident_")
both = sorted(set(base) & set(lev))
same = [j for j in both if base[j][0] == lev[j][0] and base[j][3] == lev[j][3]]
diff = [j for j in both if j not in same]
print("jobs compared %d, identical (digest + units) %d, differ %d, games %d" % (
    len(both), len(same), len(diff), sum(base[j][1] for j in both)))
decks = sorted({j.split("_")[0] for j in both})
print("decks: %s" % ",".join(decks))
for j in diff:
    print("  DIFF %s base=%s lever=%s" % (j, base[j], lev[j]))
missing = sorted(set(base) - set(lev))
if missing:
    print("not yet run in the lever batch: %d" % len(missing))
