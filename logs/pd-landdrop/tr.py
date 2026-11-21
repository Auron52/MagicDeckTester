import json, sys
d = json.load(open(sys.argv[1]))
maxt = int(sys.argv[2]) if len(sys.argv) > 2 else 99
names = {}
for t in d['turns']:
    for a in t['actions']:
        if 'card' in a and 'cardName' in a:
            names[a['card']] = a['cardName']
    for x in t['boardAfter']['battlefield']:
        names[x['card']] = x['cardName']
for t in d['turns']:
    if t['turn'] > maxt:
        break
    acts = []
    for a in t['actions']:
        s = a.get('type', '') + ':' + str(a.get('cardName', a.get('card', '')))
        extra = {k: v for k, v in a.items() if k not in ('type', 'cardName', 'card')}
        if extra:
            s += str(extra)[:60]
        acts.append(s)
    b = t['boardAfter']
    print(f"T{t['turn']} {t['phase']}: L{b['playerLife']} opp{b['opponentLife']} | " + ' ; '.join(acts))
    if t['phase'] in ('MAIN_2', 'END', 'END_STEP'):
        print('   bf:', [x['cardName'] + ('*' if x.get('tapped') else '') for x in b['battlefield']],
              ' hand:', [names.get(h, h) for h in b['hand']])
