#!/usr/bin/env python3
"""mkscen.py <deck> <gamedir> <turn> <out_spec.json> [--lives a:b] [--arm-extra JSON]
Build a scen_thresh spec for the start of <turn>'s main 1 from a game's JSON log: battlefield =
end of turn-1 (ours untapped, not sick), hand = end-of-(turn-1) hand + the turn's draw, lives,
graveyard, attachments. Opponent permanents are not staged (the provider re-creates them)."""
import json, glob, os, sys
DECKS = {
    'fivecolour': ('decks/FiveColour/FiveColour.cod', 'decks/FiveColour/FiveColour.profile.json'),
    'stompy': ('decks/StompySurprise/StompySurprise.cod', 'decks/StompySurprise/StompySurprise.profile.json'),
    'giants': ('decks/Giants/Giants.cod', 'decks/Giants/Giants.profile.json'),
    'bruna': ('decks/Bruna/Bruna.cod', 'decks/Bruna/Bruna.profile.json'),
}
deck, gdir, turn, outp = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
lives = None
depth = 0
budget = 0
args = sys.argv[5:]
for i, a in enumerate(args):
    if a == '--lives':
        lo, hi = args[i + 1].split(':'); lives = list(range(int(lo), int(hi) + 1))
    if a == '--depth':
        depth = int(args[i + 1])
    if a == '--budget':
        budget = int(args[i + 1])
g = json.load(open(sorted(glob.glob(os.path.join(gdir, '*.json')))[-1]))
num = {}
for n, ids in g['cardNumbering'].items():
    for i in ids: num[i] = n
prev = [t for t in g['turns'] if t['turn'] == turn - 1][-1]['boardAfter']
cur = [t for t in g['turns'] if t['turn'] == turn]
drawn = []
for t in cur:
    if t.get('phase') == 'DRAW':
        drawn += [a.get('cardName') for a in t.get('actions', []) if a.get('type') == 'DRAW']
bf = []
idmap = {}
for c in prev['battlefield']:
    idmap[c['card']] = c['cardName']
for c in prev['battlefield']:
    e = {'name': c['cardName'], 'controller': 0}
    if 'Token' in c['cardName'] or c['cardName'].endswith('Creature'):
        e = None
    if e is None:
        continue
    if c.get('attachedTo') is not None:
        e['equips'] = idmap.get(c['attachedTo'], '')
    bf.append(e)
hand = [num.get(i, str(i)) for i in prev['hand']] + drawn
gy = [num.get(i, str(i)) for i in prev['graveyard']]
D, P = DECKS[deck]
base = {'deck': D, 'profile': P, 'turn': turn, 'on_the_play': True, 'max_turns': turn,
        'resume_at': 'main1', 'active_life': prev['playerLife'], 'battlefield': bf, 'hand': hand,
        'graveyard': gy, 'library_filler': 'Island' if deck == 'fivecolour' else ('Mountain' if deck == 'giants' else 'Forest'),
        'library_size': 30, 'depth': depth, 'budget_ms': budget}
if lives is None:
    ol = prev['opponentLife']; lives = list(range(max(1, ol - 8), ol + 9))
json.dump({'base': base, 'lives': lives, 'real_opp_life': prev['opponentLife']}, open(outp, 'w'), indent=1)
print('opp', prev['opponentLife'], 'hand', hand, 'gy', gy)
print('bf', [e['name'] + ('->' + e['equips'] if 'equips' in e else '') for e in bf])
