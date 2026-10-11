import re, sys
from collections import defaultdict
tot = defaultdict(int)
units = defaultdict(int)
for line in open(sys.argv[1]):
    m = re.match(r"(d0|d3)_(on|off)\.c\d+: played=\d+ avg=[0-9.]+ digest=\w+ ms=(\d+) units=(\d+)", line)
    if m:
        tot[(m.group(1), m.group(2))] += int(m.group(3))
        units[(m.group(1), m.group(2))] += int(m.group(4))
for cell in ("d0", "d3"):
    on, off = tot[(cell, "on")], tot[(cell, "off")]
    print("%s: ms on %d off %d (x%.3f); units on %d off %d (x%.3f)" % (
        cell, on, off, on / max(1, off), units[(cell, "on")], units[(cell, "off")],
        units[(cell, "on")] / max(1, units[(cell, "off")])))
