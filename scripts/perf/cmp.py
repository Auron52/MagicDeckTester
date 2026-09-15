#!/usr/bin/env python3
"""Ir diff between two callgrind run directories: python3 scripts/perf/cmp.py <dirA> <dirB> [tag...]"""
import re, subprocess, sys
a, b = sys.argv[1], sys.argv[2]
tags = sys.argv[3:] or ["goblins", "dragonstorm", "kitty", "fluctuator"]
def total(path):
    out = subprocess.run(["callgrind_annotate", path], capture_output=True, text=True).stdout
    m = re.search(r"^\s*([\d,]+)\s+.*PROGRAM TOTALS", out, re.M)
    return int(m.group(1).replace(",", "")) if m else None
for t in tags:
    ta, tb = total("%s/callgrind.%s.out" % (a, t)), total("%s/callgrind.%s.out" % (b, t))
    if ta and tb:
        print("%-12s %14s -> %14s  %+.2f%%" % (t, format(ta, ","), format(tb, ","), 100.0 * (tb - ta) / ta))
    else:
        print("%-12s missing (%s, %s)" % (t, ta, tb))
