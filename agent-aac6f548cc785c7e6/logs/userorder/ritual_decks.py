import json, glob, os, re
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
d = json.load(open(WT + '/src/cards/data/cards.json'))
cards = d['cards'] if isinstance(d, dict) and 'cards' in d else d
col = {}
for c in cards:
    p = c.get('parameters', {}) or {}
    if p.get('ritual_floating_mana', 0) > 0 or p.get('untap_x_mana_sources'):
        col[c['name']] = (p.get('ritual_float_color', ''), p.get('ritual_floating_mana'), p.get('max_casts_after'))
print('ritual cards:', col)
for f in sorted(glob.glob(WT + '/decks/*/*.cod') + glob.glob(WT + '/decks/*/*.txt')):
    txt = open(f, errors='replace').read()
    hits = [n for n in col if n in txt]
    if hits:
        print(os.path.relpath(f, WT), hits)
