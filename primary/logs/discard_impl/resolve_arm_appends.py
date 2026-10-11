#!/usr/bin/env python3
"""Resolve append-only conflicts in a file: every conflict block becomes OURS then THEIRS (both kept, in order).
Refuses (exit 1) if a block's sides share a line -- that is not a pure append conflict.
Usage: resolve_arm_appends.py <file>"""
import re, sys

ALLOW_TRIVIAL = '--allow-trivial' in sys.argv  # brace-only lines may repeat (checked by eye)
p = [a for a in sys.argv[1:] if not a.startswith('--')][0]
s = open(p).read()
pat = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
n = 0
def fix(m):
    global n
    ours, theirs = m.group(1), m.group(2)
    trivial = lambda l: not l.strip() or (ALLOW_TRIVIAL and l.strip() in ('{', '}', '};'))
    common = set(l for l in ours.splitlines() if not trivial(l)) & set(l for l in theirs.splitlines() if not trivial(l))
    if common:
        sys.exit("shared lines in a conflict block, not a pure append: %r" % sorted(common)[:3])
    n += 1
    return ours + theirs
s2 = pat.sub(fix, s)
if "<<<<<<<" in s2 or ">>>>>>>" in s2:
    sys.exit("unresolved markers remain")
open(p, "w").write(s2)
print("%s: %d block(s) resolved ours-then-theirs" % (p, n))
