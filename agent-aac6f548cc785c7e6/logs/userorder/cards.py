import json, sys
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
d = json.load(open(WT + '/src/cards/data/cards.json'))
cards = d['cards'] if isinstance(d, dict) and 'cards' in d else d
names = sys.argv[1:] or ["Dragonstorm", "Scourge of Valkas", "Dragonlord Kolaghan", "Karrthus, Tyrant of Jund",
         "Utvara Hellkite", "Apex of Power", "Rite of Flame", "Desperate Ritual", "Pyretic Ritual",
         "Seething Song", "Lotus Bloom", "Ruby Medallion", "Irencrag Feat", "Lathliss, Dragon Queen"]
for c in cards:
    if c.get('name') in names:
        o = {k: v for k, v in c.items() if k not in ('notes', 'review')}
        print(json.dumps(o)[:1200])
        print()
