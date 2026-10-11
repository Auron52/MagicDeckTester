#!/usr/bin/env python3
"""Reorder this round's HeuristicArm slots (enum + kNames) into commit order. Behavior-neutral."""
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = W + '/src/ai/HeuristicArm.h'
ORDER = ['ATTACK_DOMAIN_DORK_YIELD', 'ATTACK_TAP_ASTROLABE_CREDIT', 'ATTACK_CANNONS_TRIGGER',
         'ATTACK_TAP_REPROJECT', 'ATTACK_TEAM_PUMP_ETB', 'ATTACK_PLAN_LIFEGAIN',
         'BRUNA_LETHAL_SHORTCIRCUIT', 'LAND_BOUNCE_GUARD', 'ATTACK_AURA_HOST_TAP',
         'ATTACK_TAP_CREATURE_ONLY', 'ATTACK_TAP_ABILITY_KINDS', 'ATTACK_TAP_PAYER_ORDER',
         'SOLVE_ATTACK_ONLY_WIN']
lines = open(p).read().split('\n')
# enum block: the run of lines starting at ATTACK_DOMAIN_DORK_YIELD, up to COUNT
i0 = next(i for i, l in enumerate(lines) if l.startswith('    ATTACK_DOMAIN_DORK_YIELD,'))
i1 = next(i for i in range(i0, len(lines)) if lines[i].strip() == 'COUNT')
enum = {l.split(',')[0].strip(): l for l in lines[i0:i1]}
assert sorted(enum) == sorted(ORDER), sorted(enum)
lines[i0:i1] = [enum[k] for k in ORDER]
# names block
n0 = next(i for i, l in enumerate(lines) if l.strip() == '"MTG_ATTACK_DOMAIN_DORK_YIELD",')
n1 = next(i for i in range(n0, len(lines)) if lines[i].strip() == '};')
names = {l.strip()[5:-2]: l for l in lines[n0:n1]}
assert sorted(names) == sorted(ORDER), sorted(names)
lines[n0:n1] = [names[k] for k in ORDER]
open(p, 'w').write('\n'.join(lines))
print('ok')
