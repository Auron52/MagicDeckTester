import json
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
d = json.load(open(WT + '/src/cards/data/cards.json'))
cards = d['cards'] if isinstance(d, dict) and 'cards' in d else d
for c in cards:
    if 'Land' not in c.get('types', []):
        continue
    p = c.get('parameters', {}) or {}
    pr = p.get('produces', '')
    if isinstance(pr, str) and ('B' in pr and 'G' in pr) or pr in ('any', 'ANY', '*'):
        print(c['name'], c.get('template'), pr, {k: v for k, v in p.items() if 'produc' in k or 'tapped' in k})
