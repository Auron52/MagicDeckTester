#!/usr/bin/env python3
"""After the closing batch: HEAD-vs-new2 digest identity on the idc.* chunks.
  python3 final_check.py NEW2_DIR FINAL_DIR"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

dnew, dfin = sys.argv[1:3]
def res(d):
    r = {}
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = make_ab.RES.match(line)
        if m:
            r[m.group(1)] = (m.group(3), m.group(4))
    return r
rn, rf = res(dnew), res(dfin)
n, bad = 0, []
for name, v in sorted(rf.items()):
    if not name.startswith("idc."):
        continue
    n += 1
    o = rn.get(name[4:])
    if o != v:
        bad.append("%s: new2 %s head %s" % (name[4:], o, v))
text = "HEAD vs mtg.new2: %d chunks, %d differ\n" % (n, len(bad)) + "".join("  %s\n" % b for b in bad)
print(text, end="")
open(os.path.join(dfin, "identity_head.txt"), "w").write(text)
