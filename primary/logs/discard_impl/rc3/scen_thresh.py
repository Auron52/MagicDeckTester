#!/usr/bin/env python3
"""scen_thresh.py <spec.json> <outdir> [arm...]: for each arm, sweep opponent life and print the
chosen main-1 casts per life (compact: runs of equal plans collapsed)."""
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
want = sys.argv[3:] or list(arms)
lives = spec.get('lives', list(range(1, 31)))
for arm in want:
    env = arms[arm]
    rows = []
    for life in lives:
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
        m1 = []; m2 = []; atk = None; win = '?'
        for l in (p.stdout + p.stderr).splitlines():
            if l.startswith('scenario: win_turn='):
                win = l.split()[1].split('=')[1]
        try:
            g = json.load(open(lo))
            for t in g.get('turns', []):
                if t.get('turn') != sc['turn']:
                    continue
                for a in t.get('actions', []):
                    if a.get('type') == 'ATTACK':
                        atk = a.get('damage')
                    elif a.get('type') in ('CAST_SPELL', 'ACTIVATE', 'EQUIP') or 'cardName' in a:
                        nm = a.get('type', '')[:4] + ':' + a.get('cardName', '?')
                        (m1 if t.get('phase') == 'MAIN_1' else m2).append(nm)
        except Exception as ex:
            m1.append('nolog')
        rows.append((life, ' '.join(m1), ' '.join(m2), atk, win))
    print(f'== {arm}')
    prev = None
    for r in rows:
        key = r[1:4]
        if key != prev:
            print(f'  L>={r[0]:3d}: m1[{r[1]}] atk={r[3]} m2[{r[2]}] win={r[4]}')
            prev = key
