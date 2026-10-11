import json, sys

def names(t):
    m = {}
    for s in t['mulliganSequence']:
        for c in s['hand']:
            m[c['card']] = c['cardName']
    for tr in t['turns']:
        for a in tr.get('actions', []):
            if 'card' in a and 'cardName' in a:
                m[a['card']] = a['cardName']
        for c in tr.get('boardAfter', {}).get('battlefield', []):
            if isinstance(c, dict) and 'card' in c:
                m[c['card']] = c.get('cardName', '?')
    return m

def show(path):
    t = json.load(open(path))
    m = names(t)
    print('=====', path.split('/')[-1], t['result'])
    for tr in t['turns']:
        acts = []
        for a in tr.get('actions', []):
            d = {k: v for k, v in a.items() if k not in ('card',)}
            acts.append(' '.join(f'{k}={v}' for k, v in d.items()))
        b = tr.get('boardAfter', {})
        hand = [m.get(c, str(c)) if not isinstance(c, dict) else c.get('cardName') for c in b.get('hand', [])]
        bf = [c.get('cardName') + ('(T)' if c.get('tapped') else '') for c in b.get('battlefield', []) if isinstance(c, dict)]
        gy = [m.get(c, str(c)) if not isinstance(c, dict) else c.get('cardName') for c in b.get('graveyard', [])]
        print(f"T{tr['turn']} {tr['phase']}")
        for a in acts:
            print('   ', a)
        print('    hand:', hand)
        print('    bf  :', bf)
        print('    gy  :', gy, ' opp life', b.get('opponentLife'))

for p in sys.argv[1:]:
    show(p)
