#!/usr/bin/env python3
"""Compare identity digests: tip binary (ident_tip.out) vs my binary's def_* / off_* jobs (ident_mine.out)."""
import re
S = "/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3"
pat = re.compile(r'^(\S+): played=(\d+) avg=([\d.]+) digest=(\w+)')

def load(p):
    d = {}
    for line in open(p):
        m = pat.match(line)
        if m:
            d[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    return d

tip = load(S + "/ident_tip.out")
mine = load(S + "/ident_mine.out")
print("%-16s %-28s %-34s %-34s" % ("job", "tip (played avg digest)", "mine levers=0", "mine defaults"))
for job in sorted(tip):
    t = tip[job]
    o = mine.get("off_" + job)
    d = mine.get("def_" + job)
    fo = "SAME" if o and o == t else ("DIFF %s %s" % (o[1], o[2]) if o else "-")
    fd = "SAME" if d and d == t else ("DIFF avg %s %s" % (d[1], d[2]) if d else "-")
    print("%-16s %-28s %-34s %-34s" % (job, " ".join(t), fo, fd))
