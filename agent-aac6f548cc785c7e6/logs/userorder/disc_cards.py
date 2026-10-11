import json
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
d = json.load(open(WT + '/src/cards/data/cards.json'))
cards = d['cards'] if isinstance(d, dict) and 'cards' in d else d
for c in cards:
    p = c.get('parameters', {}) or {}
    if p.get('discount_max_targets', 0) > 0 or p.get('discount_targets_scale_x') or p.get('damage_equals_top_mv'):
        print(c['name'], c.get('mana_cost'), c.get('types'), {k: v for k, v in p.items() if 'discount' in k or k in ('damage_equals_top_mv',)})
