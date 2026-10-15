#!/usr/bin/env python3
"""not_to_json.py STAGE_TXT OUT.json -- the NOT lines of a stage_serial.py log as slower rows for the next stage."""
import json, re, sys
rows = []
for line in open(sys.argv[1]):
    m = re.match(r'^(\S+) seed=(\d+) gi=(\d+) old=(-?\d+) stage\d=(\S+) NOT', line)
    if m:
        rows.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5)))
json.dump(rows, open(sys.argv[2], 'w'))
print('%d rows -> %s' % (len(rows), sys.argv[2]))
