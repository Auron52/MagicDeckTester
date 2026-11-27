#!/usr/bin/env python3
"""turnview.py <log.json> <turn>: hand + battlefield at the turn's draw, each phase's actions, combat lines."""
import json, sys
g = json.load(open(sys.argv[1])); T = int(sys.argv[2])
for t in g['turns']:
    if t['turn'] != T: continue
    b = t.get('boardAfter', {})
    if t['phase'] in ('DRAW', 'UPKEEP') or t['phase'].startswith('MAIN_1'):
        bf = [(p['cardName'] + ('(T)' if p.get('tapped') else '') + (f"[{p.get('power')}/{p.get('toughness')}]" if not p.get('isLand') else ''))
              for p in b.get('battlefield', [])]
        print(f"-- {t['phase']} after: bf={bf}")
        print(f"   hand={[c.get('cardName', c) if isinstance(c, dict) else c for c in b.get('hand', [])]}")
    for a in t['actions']:
        print(f"   {t['phase']}: {json.dumps(a)[:260]}")
