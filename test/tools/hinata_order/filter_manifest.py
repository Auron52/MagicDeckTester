#!/usr/bin/env python3
"""filter_manifest.py IN.json OUT.json --drop-regex RE [--drop-regex RE ...] -- drop jobs whose name matches."""
import json, re, sys
src, dst = sys.argv[1], sys.argv[2]
pats = [re.compile(sys.argv[i + 1]) for i in range(3, len(sys.argv)) if sys.argv[i] == '--drop-regex']
m = json.load(open(src))
keep = [j for j in m['jobs'] if not any(p.search(j['name']) for p in pats)]
json.dump({'jobs': keep}, open(dst, 'w'), indent=1)
print('kept %d of %d jobs, %d games -> %s' % (len(keep), len(m['jobs']), sum(j['games'] for j in keep), dst))
