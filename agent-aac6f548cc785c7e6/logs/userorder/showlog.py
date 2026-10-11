import json, sys
d = json.load(open(sys.argv[1]))
print([k for k in d.keys()])
for t in d.get('turns', []):
    acts = []
    for a in t['actions']:
        if a['type'] in ('CAST_SPELL', 'PLAY_LAND', 'REVEAL', 'ABILITY', 'DISCARD'):
            s = a['type'][:4] + ':' + str(a.get('cardName', a.get('source', '')))
            if a.get('manaPaid'):
                s += '(' + a['manaPaid'] + ')'
            if a['type'] == 'REVEAL':
                s += '[' + ','.join(str(c.get('cardName')) + '>' + str(c.get('to', '')) for c in a.get('lookedAt', [])) + ']'
            acts.append(s)
        elif a['type'] not in ('DRAW',):
            acts.append(a['type'])
    print(t['turn'], t['phase'], ' | '.join(acts))
