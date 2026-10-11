#!/usr/bin/env python3
"""scen.py <spec.json> <outdir>: run a scenario board under arms x opponent lives.

spec: {"base": {...scenario json without env/opponent_life/log_out...},
       "arms": {"old": {...env...}, "new": {...env...}},
       "lives": [..]}
Prints, per arm and life, the win turn and the T<turn> main-1 actions from log_out.
"""
import json, subprocess, sys, os, copy
R = '/workspaces/MagicDeckTester'
BIN = os.environ.get('BIN', R + '/logs/discard_impl/ab3/mtg_ab3_d6a760e5')
LEVERS = ["MTG_DRAGONSTORM_ORDER_OPAQUE", "MTG_PAYABLE_ORDER_RITUALS", "MTG_COLOR_EXACT_SCALED",
          "MTG_ATTACK_TAP_NEED_EXACT", "MTG_ATTACK_TAP_ABILITY_DISCOUNT", "MTG_ROLLOUT_LAND_ACT_COLOR",
          "MTG_SOLVE_VERIFIED_WIN", "MTG_EDF_GORGE_RED_SIZING", "MTG_SOLVE_PENDING_AURA_EXACT"]
spec = json.load(open(sys.argv[1]))
out = os.path.abspath(sys.argv[2])
os.makedirs(out, exist_ok=True)
arms = spec.get('arms') or {'old': {l: '0' for l in LEVERS}, 'new': {}}
for arm, env in arms.items():
    for life in spec['lives']:
        sc = copy.deepcopy(spec['base'])
        sc['opponent_life'] = life
        e = dict(sc.get('env', {}))
        e.update(env)
        sc['env'] = e
        lo = os.path.join(out, f'{arm}_L{life}.log.json')
        sc['log_out'] = lo
        sp = os.path.join(out, f'{arm}_L{life}.scen.json')
        json.dump(sc, open(sp, 'w'), indent=1)
        p = subprocess.run(['nice', '-n', '10', BIN, '--scenario', sp], cwd=R, capture_output=True, text=True)
        txt = p.stdout + p.stderr
        wl = [l for l in txt.splitlines() if 'win' in l.lower()][:3]
        acts = []
        try:
            g = json.load(open(lo))
            for t in g.get('turns', []):
                if t.get('turn') == sc['turn'] and t.get('phase') in ('MAIN_1', 'MAIN_2', 'COMBAT'):
                    for a in t.get('actions', []):
                        s = a.get('type', '') + (':' + a['cardName'] if 'cardName' in a else '')
                        if a.get('type') == 'ATTACK':
                            s += f"({a.get('damage')})"
                        acts.append(t['phase'][0:2] + ' ' + s)
        except Exception as ex:
            acts.append('nolog ' + str(ex))
        print(f'{arm:8s} L={life:3d} | ' + ' ; '.join(wl)[:120] + ' | ' + ' , '.join(acts))
