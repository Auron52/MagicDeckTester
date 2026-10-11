#!/usr/bin/env python3
"""HeuristicArm.h: every enum Slot identifier (in order, up to COUNT) must line up with kNames, i.e.
"MTG_" + IDENT == kNames[i], except the historical abbreviations recorded in arm_known_mismatch.txt
(written by --record on a tree verified by hand: 3aa0d856). Exit 0 = counts equal and no NEW mismatch.
Usage: check_arm_table.py [--record] [HeuristicArm.h]"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KNOWN = os.path.join(HERE, "arm_known_mismatch.txt")
args = [a for a in sys.argv[1:] if not a.startswith("--")]
p = args[0] if args else "/workspaces/MagicDeckTester/src/ai/HeuristicArm.h"
s = open(p).read()
e = s[s.index("enum Slot"):]
e = re.sub(r"//[^\n]*", "", e[e.index("{") + 1:])
e = e[:re.search(r"\bCOUNT\b", e).start()]
idents = re.findall(r"([A-Z][A-Z0-9_]*)\s*(?:=\s*0\s*)?,", e)
n = s[s.index("kNames[COUNT]"):]
names = re.findall(r'"(MTG_[A-Z0-9_]+)"', n[:n.index("};")])
pairs = ["%s %s" % (i, nm) for i, nm in zip(idents, names) if "MTG_" + i != nm]
print("enum identifiers: %d, kNames: %d, name mismatches: %d" % (len(idents), len(names), len(pairs)))
if "--record" in sys.argv:
    open(KNOWN, "w").write("\n".join(pairs) + "\n")
    print("recorded %d known mismatches -> %s" % (len(pairs), KNOWN))
    sys.exit(0)
known = set(open(KNOWN).read().split("\n")) if os.path.exists(KNOWN) else set()
new = [x for x in pairs if x not in known]
for x in new:
    print("  NEW mismatch (enum kNames): " + x)
print("tail:", idents[-6:])
sys.exit(1 if new or len(idents) != len(names) else 0)
