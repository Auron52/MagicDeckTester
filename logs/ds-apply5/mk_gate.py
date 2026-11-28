#!/usr/bin/env python3
# mk_gate.py <round-4 gate.json> <out.json> <arm> [FLAG=0|1 ...]: the Dragonstorm slice of mana-clean gate
# claude-queue-20261125a (seed 30000000 + gi, gi 87000-158999, the round-4 chunking: 362 jobs of ~199 games), ONE arm,
# flags pinned as given. Run the arms as separate 2-slot lane batches (the big lane is not ours).
import json, sys
src, out, arm = sys.argv[1:4]
fl = {}
for a in sys.argv[4:]:
    k, v = a.split('='); fl[k] = v == '1'
jobs, seen = [], set()
for j in json.load(open(src))['jobs']:
    key = j['name'].split('__', 1)[1]
    if key in seen: continue
    seen.add(key); k = dict(j); k['name'] = '%s__%s' % (arm, key); k['flags'] = dict(fl); jobs.append(k)
json.dump({'jobs': jobs}, open(out, 'w'), indent=0)
print(len(jobs), 'jobs', sum(j['games'] for j in jobs), 'games')
