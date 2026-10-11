#!/usr/bin/env python3
"""Held-out paired A/B spec: per deck, `on` (all defaults), `off` (every new lever off), and one arm
per relevant lever with just that lever off. Seeds 51xxxxx (d0) / 56xxxxx (d3): disjoint from the ab3
seeds (1xxxxxx), the rc3 held-out (9xxxxxx) and this round's census (59xxxxx) / identity (6xxxxxx)."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkspecs import NEW, SHORT
HERE = os.path.dirname(os.path.abspath(__file__))
INV = {v: k for k, v in SHORT.items()}
D0 = {
    'fivecolour': ['noDOMAIN', 'noCANNONS', 'noREPROJ', 'noKINDS', 'noPAYER', 'noAOW'],
    'stompy':     ['noHOOF', 'noPAYER', 'noREPROJ', 'noAOW'],
    'selesnya':   ['noLIFE', 'noHOOF', 'noREPROJ', 'noPAYER', 'noAOW', 'noBOUNCE'],
    'bruna':      ['noBRUNASC', 'noBOUNCE', 'noAURAHOST', 'noCRONLY', 'noREPROJ', 'noPAYER', 'noAOW'],
    'snow':       ['noASTRO', 'noAOW', 'noREPROJ', 'noPAYER', 'noKINDS'],
    'critter':    ['noLIFE', 'noAOW'],
    'angels':     ['noLIFE', 'noAOW'],
    'goblins':    ['noKINDS'],
}
D3 = {
    'fivecolour': ['noDOMAIN', 'noCANNONS', 'noKINDS', 'noPAYER'],
    'stompy':     ['noHOOF', 'noPAYER'],
    'selesnya':   ['noLIFE', 'noHOOF', 'noPAYER'],
    'bruna':      ['noBRUNASC', 'noBOUNCE'],
    'snow':       ['noASTRO', 'noAOW'],
    'critter':    ['noLIFE'],
    'angels':     ['noLIFE'],
}
arms = {'on': [], 'off': NEW}
for l in NEW:
    arms[SHORT[l]] = [l]
decks = {}
jobs = []
for i, d in enumerate(D0):
    decks[d] = {'seed_d0': 5100001 + 20000 * i, 'seed_d3': 5600001 + 20000 * i}
spec = {'decks': decks, 'cells': {'d0': {'games': 3000, 'chunk': 250}, 'd3': {'games': 300, 'chunk': 25}},
        'arms': arms, 'deck_arms': {}, 'deck_cells': {}}
# ab.py's mk crosses every listed cell with every listed arm; the d3 arm subset is applied below.
for d in D0:
    spec['deck_arms'][d] = ['on', 'off'] + D0[d]
    spec['deck_cells'][d] = ['d0'] + (['d3'] if d in D3 else [])
json.dump(spec, open(os.path.join(HERE, 'spec_heldout.json'), 'w'), indent=1)
spec['cell_arms'] = {d: {'d3': ['on', 'off'] + D3[d]} for d in D3}
json.dump(spec, open(os.path.join(HERE, 'spec_heldout.json'), 'w'), indent=1)
print('ok')
