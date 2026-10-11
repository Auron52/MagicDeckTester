import json, sys, glob
for f in sorted(glob.glob(sys.argv[1] + '/*.json')):
    d = json.load(open(f))
    for t in d.get('turns', []):
        for a in t['actions']:
            if a['type'] in ('ATTACK', 'TRIGGER', 'DAMAGE') or 'damage' in json.dumps(a).lower():
                print(t['turn'], t['phase'], json.dumps(a)[:300])
        keys = {k: t[k] for k in t if k not in ('actions',)}
        print('   turn-state', json.dumps(keys)[:300])
