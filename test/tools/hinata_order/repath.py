#!/usr/bin/env python3
"""repath.py IN.json OUT.json FROM_DIR TO_DIR -- rewrite every job's deck/profile path prefix FROM_DIR -> TO_DIR.
Used to run a manifest off a copy of the deck folder on the build volume (RULES item 9: keep 9p IO light)."""
import json, sys
src, dst, a, b = sys.argv[1:5]
m = json.load(open(src))
n = 0
for j in m['jobs']:
    for k in ('deck', 'profile'):
        if k in j and j[k].startswith(a):
            j[k] = b + j[k][len(a):]
            n += 1
json.dump(m, open(dst, 'w'), indent=1)
print('rewrote %d paths in %d jobs -> %s' % (n, len(m['jobs']), dst))
