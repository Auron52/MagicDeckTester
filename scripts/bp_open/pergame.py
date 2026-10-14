#!/usr/bin/env python3
"""Per-game core-ms by (deck, cell) from one or more batch.out files (all arms pooled). pergame.py <batch.out>..."""
import re, sys
from collections import defaultdict

acc = defaultdict(lambda: [0, 0])
for path in sys.argv[1:]:
    for line in open(path, errors="ignore"):
        m = re.match(r"^(\S+?)_(ship|d3|d0|x3|xs)_([a-z0-9]+)\.c\d+: played=(\d+) .* ms=(\d+)", line)
        if m:
            a = acc[(m.group(1), m.group(2))]
            a[0] += int(m.group(4)); a[1] += int(m.group(5))
for (k, c), (g, ms) in sorted(acc.items(), key=lambda kv: -kv[1][1] / max(1, kv[1][0])):
    print("%-16s %-4s games=%5d  %8.0f ms/game" % (k, c, g, ms / max(1, g)))
