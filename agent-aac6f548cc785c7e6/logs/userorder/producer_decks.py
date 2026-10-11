"""Which decks hold a PRODUCING cast MTG_PAYABLE_ORDER_RITUALS puts in the producer-only domain:
ritual float, a same-turn mana rock (castable artifact with mana_rock), a Treasure minter (creates_treasures),
or a dynamic-discount card (Hinata / Soulfire). Prints per deck the producer names and the regression tag."""
import json, glob, os, re
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
d = json.load(open(WT + '/src/cards/data/cards.json'))
cards = d['cards'] if isinstance(d, dict) and 'cards' in d else d
kind = {}
for c in cards:
    p = c.get('parameters', {}) or {}
    types = c.get('types', [])
    k = []
    if p.get('ritual_floating_mana', 0) > 0:
        k.append('ritual')
    if p.get('mana_rock') and 'Land' not in types and 'Creature' not in types:
        k.append('rock')
    if p.get('creates_treasures', 0) > 0:
        k.append('treasure')
    if p.get('etb_creates_treasures', 0) > 0:
        k.append('etb_treasure')
    if p.get('max_casts_after') is not None and p.get('max_casts_after', -1) >= 0:
        k.append('castcap')
    if p.get('hinata_cost_reducer') or p.get('soulfire_own_target_discount'):
        k.append('dynamic')
    if k:
        kind[c['name']] = k
rows = []
for f in sorted(glob.glob(WT + '/decks/*/*.cod') + glob.glob(WT + '/decks/*/*.txt')):
    if '/v' in os.path.relpath(f, WT).split('/', 2)[-1]:
        pass
    txt = open(f, errors='replace').read()
    hits = {n: kind[n] for n in kind if re.search(r'(^|[\s"\'>])' + re.escape(n) + r'($|[\s"\'<])', txt, re.M)}
    if hits:
        rows.append((os.path.relpath(f, WT), hits))
for f, h in rows:
    tags = sorted({t for v in h.values() for t in v})
    print(f, tags, sorted(h))
