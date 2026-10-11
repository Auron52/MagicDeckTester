import json, sys
d = json.load(open(sys.argv[1]))
lo, hi = int(sys.argv[2]), int(sys.argv[3])
for t in d.get('turns', []):
    if not (lo <= t['turn'] <= hi):
        continue
    print(t['turn'], t['phase'], json.dumps(t['actions'])[:900])
