#!/usr/bin/env python3
"""recovery_verdict.py WINS_DIR PREFIX [--failed-out FILE] -- verdicts of gen_recovery.py jobs.

A job is named <prefix>_<cell>_gi<gi>_o<old_wt>; it RECOVERS when its win turn (loss = 9) is <= the old win turn
(old loss = 9). Prints per-cell recovered / not, and lists the failures; --failed-out writes them in the
analyze_ab.py --slower-out row shape (cell, seed, gi, old_wt, stage_wt) for the next stage."""
import glob, json, os, re, sys
from collections import defaultdict
d, prefix = sys.argv[1], sys.argv[2]
out = None
if '--failed-out' in sys.argv:
    out = sys.argv[sys.argv.index('--failed-out') + 1]
BASES = {'hinata_ship': 11100001, 'hinata_d3': 11110001, 'hinata_d0': 11120001,
         'hinata2hg_ship': 11150001, 'hinata2hg_d3': 11160001, 'hinata2hg_d0': 11170001}
pat = re.compile(r'^%s_(?P<cell>.+)_gi(?P<gi>\d+)_o(?P<old>-?\d+)\.wins$' % re.escape(prefix))
score = lambda w: w if w > 0 else 9
rec = defaultdict(lambda: [0, 0])
fails = []
for f in sorted(glob.glob(os.path.join(d, prefix + '_*.wins'))):
    m = pat.match(os.path.basename(f))
    if not m:
        continue
    lines = [l.split() for l in open(f) if l.strip()]
    if not lines:
        continue
    wt = int(lines[0][1]); old = int(m.group('old')); cell = m.group('cell'); gi = int(m.group('gi'))
    ok = score(wt) <= score(old)
    rec[cell][0 if ok else 1] += 1
    if not ok:
        fails.append((cell, BASES.get(cell, 0) + gi, gi, old, wt))
for cell in sorted(rec):
    print('%-16s recovered=%d  not=%d' % (cell, rec[cell][0], rec[cell][1]))
for x in fails:
    print('  NOT  %-16s seed=%d gi=%d  old %d  stage %d' % x)
if out:
    json.dump(fails, open(out, 'w'))
