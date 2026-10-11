#!/usr/bin/env python3
"""Real casts the executor ATTEMPTED (exec-tap 'before X') minus the casts the game log RECORDS, per turn.
usage: failcasts2.py <run_dir_with_stderr_and_json> [...]"""
import sys, re, glob, os, json, collections

for d in sys.argv[1:]:
    att = collections.Counter()
    for line in open(os.path.join(d, 'stderr.txt'), errors='replace'):
        m = re.match(r'\[exec-tap\] T(\d+) before (.+?): float', line)
        if m:
            att[(int(m.group(1)), m.group(2))] += 1
    fs = sorted(glob.glob(os.path.join(d, '*.json')))
    done = collections.Counter()
    if fs:
        g = json.load(open(fs[0]))
        for t in g['turns']:
            for a in t['actions']:
                if a.get('type') == 'CAST_SPELL':
                    done[(t['turn'], a['cardName'])] += 1
    fails = []
    for k, n in sorted(att.items()):
        if n > done[k]:
            fails.append('T%d %s x%d' % (k[0], k[1], n - done[k]))
    print('%s: %s' % (os.path.basename(d), fails if fails else '-'))
