import json, sys, glob
O = '/workspaces/MagicDeckTester/logs/discard_impl/rc/dragonstorm'
for a in ['on', 'off']:
    f = glob.glob(O + '/cli_' + a + '/*.json')[0]
    d = json.load(open(f))
    names = {}
    for c in d['openingHand']:
        names[c['card']] = c['cardName']
    num = d.get('cardNumbering')
    if isinstance(num, dict):
        for k, v in num.items():
            try:
                names[int(k)] = v if isinstance(v, str) else v.get('cardName', v)
            except Exception:
                pass
    elif isinstance(num, list):
        for e in num:
            if isinstance(e, dict) and 'card' in e:
                names[e['card']] = e.get('cardName')
    print('=====', a, d['result'])
    for t in d['turns']:
        if t['turn'] > int(sys.argv[1]) if len(sys.argv) > 1 else 6:
            continue
        for ac in t['actions']:
            if 'card' in ac and 'cardName' in ac:
                names[ac['card']] = ac['cardName']
        b = t['boardAfter']
        for p in b['battlefield']:
            names[p['card']] = p['cardName']
        acts = ['%s:%s' % (ac.get('type'), ac.get('cardName', '')) for ac in t['actions']]
        bf = ['%s%s%s' % (p['cardName'], '(T)' if p.get('tapped') else '',
                          ''.join('[%s=%d]' % (c['kind'], c['count']) for c in p.get('counters', [])))
              for p in b['battlefield']]
        hand = [names.get(h, str(h)) for h in b['hand']]
        gy = [names.get(h, str(h)) if isinstance(h, int) else h for h in b['graveyard']]
        print('T%d %-8s acts=%s' % (t['turn'], t['phase'], acts))
        print('      hand=%s' % hand)
        print('      bf=%s gy=%s staged=%s opp=%s' % (bf, gy, b.get('staged'), b['opponentLife']))
