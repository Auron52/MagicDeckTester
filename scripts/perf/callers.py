#!/usr/bin/env python3
"""Callers of a function from callgrind: python3 scripts/perf/callers.py <callgrind.out> <substr> [top]"""
import re, subprocess, sys
path, needle = sys.argv[1], sys.argv[2]; top = int(sys.argv[3]) if len(sys.argv) > 3 else 12
out = subprocess.run(["callgrind_annotate", "--tree=caller", "--threshold=100", path], capture_output=True, text=True).stdout
blocks = out.split("\n\n")
callers = {}
for b in blocks:
    lines = b.split("\n")
    star = [l for l in lines if re.match(r"^\s*[\d,]+ \([\d. ]+%\)\s+\*\s", l)]
    if not star or needle not in star[0]: continue
    for l in lines:
        m = re.match(r"^\s*([\d,]+) \([\d. ]+%\)\s+<\s+(.*?)(?: \((\d+)x\))?$", l)
        if m:
            ir = int(m.group(1).replace(",", "")); name = m.group(2)
            name = re.sub(r"\s*\[/workspaces.*$", "", name)
            name = name.split(":", 1)[-1] if ":" in name else name
            calls = int(m.group(3)) if m.group(3) else 0
            c = callers.setdefault(name, [0, 0]); c[0] += ir; c[1] += calls
tot = sum(v[0] for v in callers.values())
print("callers of '%s': %d distinct, %s Ir inclusive-at-callee" % (needle, len(callers), format(tot, ",")))
for name, (ir, calls) in sorted(callers.items(), key=lambda kv: -kv[1][0])[:top]:
    print("%14s Ir %10s calls  %s" % (format(ir, ","), format(calls, ","), name[:110]))
