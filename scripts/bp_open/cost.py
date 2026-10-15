#!/usr/bin/env python3
"""Per-game core-ms by <deck>_<cell>_<arm> over every batch.out under the bp_open scratch tree.
Usage: cost.py [scratch-root]   (default /workspaces/MagicDeckTester/build/scratch/bp_open)"""
import os, re, sys, collections

root = sys.argv[1] if len(sys.argv) > 1 else '/workspaces/MagicDeckTester/build/scratch/bp_open'
pat = re.compile(r'^(\S+?)\.c\d+: played=(\d+) .* ms=(\d+) units=(\d+)')
ms = collections.defaultdict(int)
games = collections.defaultdict(int)
for d in sorted(os.listdir(root)):
    f = os.path.join(root, d, 'batch.out')
    if not os.path.isfile(f):
        continue
    for line in open(f, errors='replace'):
        m = pat.match(line.strip())
        if not m:
            continue
        name = m.group(1)
        if name.startswith('chk_'):
            continue
        ms[name] += int(m.group(3))
        games[name] += int(m.group(2))
cell = collections.defaultdict(list)
for k in ms:
    parts = k.split('_')
    # <deck...>_<cell>_<arm>
    key = '_'.join(parts[:-1])
    cell[key].append(ms[k] / max(1, games[k]))
for k in sorted(cell):
    v = cell[k]
    print('%-34s %9.0f ms/game (max over arms, %d arms)' % (k, max(v), len(v)))
