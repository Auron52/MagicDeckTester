#!/usr/bin/env python3
"""merge_manifests.py OUT.json IN1.json [IN2.json ...] -- concatenate manifests' jobs into ONE pooled batch.
Job names must be unique across the inputs."""
import json, sys
out, ins = sys.argv[1], sys.argv[2:]
jobs = []
for p in ins:
    jobs.extend(json.load(open(p))['jobs'])
names = [j['name'] for j in jobs]
assert len(names) == len(set(names)), 'duplicate job names'
json.dump({'jobs': jobs}, open(out, 'w'), indent=1)
print('jobs=%d games=%d -> %s' % (len(jobs), sum(j['games'] for j in jobs), out))
