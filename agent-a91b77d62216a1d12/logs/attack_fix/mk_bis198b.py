#!/usr/bin/env python3
"""ship198: ab3 old arm + all v1 levers off, then each v1 lever alone back on."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
AB3 = ['MTG_DRAGONSTORM_ORDER_OPAQUE', 'MTG_PAYABLE_ORDER_RITUALS', 'MTG_COLOR_EXACT_SCALED',
       'MTG_ATTACK_TAP_NEED_EXACT', 'MTG_ATTACK_TAP_ABILITY_DISCOUNT', 'MTG_ROLLOUT_LAND_ACT_COLOR',
       'MTG_SOLVE_VERIFIED_WIN', 'MTG_EDF_GORGE_RED_SIZING', 'MTG_SOLVE_PENDING_AURA_EXACT']
V1 = ['MTG_ATTACK_DOMAIN_DORK_YIELD', 'MTG_ATTACK_TAP_ASTROLABE_CREDIT', 'MTG_ATTACK_CANNONS_TRIGGER',
      'MTG_ATTACK_TAP_REPROJECT', 'MTG_ATTACK_TEAM_PUMP_ETB', 'MTG_ATTACK_PLAN_LIFEGAIN',
      'MTG_BRUNA_LETHAL_SHORTCIRCUIT', 'MTG_ATTACK_AURA_HOST_TAP', 'MTG_LAND_BOUNCE_GUARD',
      'MTG_ATTACK_TAP_CREATURE_ONLY']
args = ['python3', os.path.join(HERE, 'recover.py'), 'mk', sys.argv[1], 'alloff=' + ','.join(AB3 + V1)]
for l in V1:
    args.append('only_' + l.replace('MTG_', '') + '=' + ','.join(AB3 + [x for x in V1 if x != l]))
subprocess.check_call(args, env=dict(os.environ, GAMES=os.path.join(HERE, 'games_b198.txt')))
