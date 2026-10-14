#!/usr/bin/env python3
"""Identity manifests: every suite deck at d0 / d3 b10 / shipped, base binary vs the new binary.

gen_identity.py --out-base B.json --out-new N.json
  every deck except hinata: d0 50, d3 b10 20, shipped 20 games (dragonstorm + dragonstorm2hg: 200 / 50 / 50 --
  the other Irencrag deck); flags on the NEW side: MTG_HINATA_ORDER_FULL on (the shipping default; read only
  by HinataProvider).
  hinata + hinata2hg with MTG_HINATA_ORDER_FULL OFF on BOTH sides: "=0 restores today's behaviour exactly"
  -- d0 400, d3 40, shipped 20.
Seeds 11300001 + 1000 * job (each job's games < 1000, so disjoint).
"""
import argparse, json, re

WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155'
ap = argparse.ArgumentParser()
ap.add_argument('--out-base', required=True)
ap.add_argument('--out-new', required=True)
a = ap.parse_args()

src = open(WT + '/test/regression_cases.sh').read()
def arr(name):
    m = re.search(r'declare -A %s=\((.*?)\n\)' % name, src, re.S)
    out = {}
    for line in m.group(1).split('\n'):
        mm = re.match(r'\s*\[(\w+)\]=(.+?)\s*$', line)
        if mm:
            out[mm.group(1)] = mm.group(2).strip('"')
    return out
files, profs = arr('DECK_FILE'), arr('DECK_PROF')
assert set(files) == set(profs), 'DECK_FILE / DECK_PROF keys differ'

specs = []   # (key, base_deck, hg, cells{name:(depth,budget,games)}, hinata_off)
for k in sorted(files):
    if k == 'hinata':
        continue
    n0, n3, ns = (200, 50, 50) if k == 'dragonstorm' else (50, 20, 20)
    specs.append((k, k, False, n0, n3, ns, False))
specs.append(('dragonstorm2hg', 'dragonstorm', True, 200, 50, 50, False))
specs.append(('hinata', 'hinata', False, 400, 40, 20, True))
specs.append(('hinata2hg', 'hinata', True, 400, 40, 20, True))

jobs_b, jobs_n = [], []
seed = 11300001
for (key, base, hg, n0, n3, ns, hin_off) in specs:
    for cell, depth, bud, games in (('d0', 0, 0, n0), ('d3', 3, 10, n3), ('ship', None, None, ns)):
        j = {'name': '%s_%s' % (key, cell), 'deck': WT + '/' + files[base], 'profile': WT + '/' + profs[base],
             'games': games, 'seed': seed}
        seed += 1000
        if hg:
            j['starting_life'] = 30
            j['opponent_heads'] = 2
        if depth is not None:
            j['depth'] = depth
            j['budget_ms'] = bud
            j['ignore_play_profile'] = True
        w = 0
        if depth is None:
            w = 5000
        elif depth > 0:
            w = 2000
        if base == 'hinata' and w:
            w += 3000
        j['weight'] = w
        jb = dict(j); jn = dict(j)
        if hin_off:
            jb['flags'] = {'MTG_HINATA_ORDER_FULL': False}
            jn['flags'] = {'MTG_HINATA_ORDER_FULL': False}
        else:
            jn['flags'] = {'MTG_HINATA_ORDER_FULL': True}
        jobs_b.append(jb); jobs_n.append(jn)
json.dump({'jobs': jobs_b}, open(a.out_base, 'w'), indent=1)
json.dump({'jobs': jobs_n}, open(a.out_new, 'w'), indent=1)
print('jobs=%d games=%d' % (len(jobs_b), sum(j['games'] for j in jobs_b)))
