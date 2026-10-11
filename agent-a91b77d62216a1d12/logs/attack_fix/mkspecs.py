#!/usr/bin/env python3
"""Write the census / identity specs for ab.py, and NEW (the lever list) for the other tools."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
NEW = ['MTG_ATTACK_DOMAIN_DORK_YIELD', 'MTG_ATTACK_TAP_ASTROLABE_CREDIT', 'MTG_ATTACK_CANNONS_TRIGGER',
       'MTG_ATTACK_TAP_REPROJECT', 'MTG_ATTACK_TEAM_PUMP_ETB', 'MTG_ATTACK_PLAN_LIFEGAIN',
       'MTG_BRUNA_LETHAL_SHORTCIRCUIT', 'MTG_ATTACK_AURA_HOST_TAP', 'MTG_LAND_BOUNCE_GUARD',
       'MTG_ATTACK_TAP_CREATURE_ONLY', 'MTG_SOLVE_ATTACK_ONLY_WIN', 'MTG_ATTACK_TAP_ABILITY_KINDS',
       'MTG_ATTACK_TAP_PAYER_ORDER']
SHORT = {'MTG_ATTACK_DOMAIN_DORK_YIELD': 'noDOMAIN', 'MTG_ATTACK_TAP_ASTROLABE_CREDIT': 'noASTRO',
         'MTG_ATTACK_CANNONS_TRIGGER': 'noCANNONS', 'MTG_ATTACK_TAP_REPROJECT': 'noREPROJ',
         'MTG_ATTACK_TEAM_PUMP_ETB': 'noHOOF', 'MTG_ATTACK_PLAN_LIFEGAIN': 'noLIFE',
         'MTG_BRUNA_LETHAL_SHORTCIRCUIT': 'noBRUNASC', 'MTG_ATTACK_AURA_HOST_TAP': 'noAURAHOST',
         'MTG_LAND_BOUNCE_GUARD': 'noBOUNCE', 'MTG_ATTACK_TAP_CREATURE_ONLY': 'noCRONLY',
         'MTG_SOLVE_ATTACK_ONLY_WIN': 'noAOW', 'MTG_ATTACK_TAP_ABILITY_KINDS': 'noKINDS',
         'MTG_ATTACK_TAP_PAYER_ORDER': 'noPAYER'}
ALL = ['fivecolour', 'stompy', 'selesnya', 'bruna', 'snow', 'burn', 'dragonstorm', 'hinata', 'critter',
       'angels', 'giants', 'goblins', 'pirates', 'kitty', 'knights', 'whiteknights', 'antilife', 'auras',
       'creature_giving', 'mirrorwing', 'minotaur', 'dragons', 'breaching', 'melira', 'fluctuator',
       'soldiers', 'pd', 'fungus', 'slivers', 'th']

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'levers':
        print(','.join(NEW))
        sys.exit(0)
    arms = {'on': [], 'off': NEW}
    for l in NEW:
        arms[SHORT[l]] = [l]
    census = {'decks': {d: {'seed_d0': 5900001 + 10000 * i} for i, d in enumerate(ALL)},
              'cells': {'d0': {'games': 200, 'chunk': 100}},
              'arms': arms}
    json.dump(census, open(os.path.join(HERE, 'spec_census.json'), 'w'), indent=1)
    # Identity (a): every new lever OFF on this build vs the head binary with no flags. Same job
    # names on both sides; the head manifest carries no flags block (it does not know the names).
    idd = ['fivecolour', 'stompy', 'selesnya', 'bruna', 'snow', 'burn', 'dragonstorm', 'hinata',
           'critter', 'goblins']
    ida = {'decks': {d: {'seed_d0': 6100001 + 10000 * i, 'seed_d3': 6200001 + 10000 * i}
                     for i, d in enumerate(idd)},
           'cells': {'d0': {'games': 400, 'chunk': 100}, 'd3': {'games': 40, 'chunk': 20}},
           'arms': {'x': NEW}}
    json.dump(ida, open(os.path.join(HERE, 'spec_ida_mine.json'), 'w'), indent=1)
    ida['arms'] = {'x': []}
    json.dump(ida, open(os.path.join(HERE, 'spec_ida_head.json'), 'w'), indent=1)
    print('census: %d decks x %d arms; identity: %d decks' % (len(ALL), len(arms), len(idd)))
