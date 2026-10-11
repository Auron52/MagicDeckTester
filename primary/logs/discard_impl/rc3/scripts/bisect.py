#!/usr/bin/env python3
"""Bisect slower games to lever(s). usage: bisect.py <slower_list.txt> <outroot> [parallel] [--levers L1,L2]
For each game: new, old, old+one-lever-on (each), new+one-lever-off (each). Prints a table."""
import subprocess, sys, os, re, concurrent.futures as cf

ROOT = '/workspaces/MagicDeckTester'
BIN = os.environ.get('BIN', ROOT + '/logs/discard_impl/ab3/mtg_ab3_d6a760e5')
ALL = ['MTG_DRAGONSTORM_ORDER_OPAQUE', 'MTG_PAYABLE_ORDER_RITUALS', 'MTG_COLOR_EXACT_SCALED',
       'MTG_ATTACK_TAP_NEED_EXACT', 'MTG_ATTACK_TAP_ABILITY_DISCOUNT', 'MTG_ROLLOUT_LAND_ACT_COLOR',
       'MTG_SOLVE_VERIFIED_WIN', 'MTG_EDF_GORGE_RED_SIZING', 'MTG_SOLVE_PENDING_AURA_EXACT']
DECKS = {'edf': ('decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod',
                 'decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json'),
         'snow': ('decks/Snow/Snow.cod', 'decks/Snow/Snow.profile.json')}
CELLS = {'d0': ['--depth', '0', '--ignore-play-profile'],
         'd3': ['--depth', '3', '--budget-ms', '10', '--ignore-play-profile'],
         'ship': ['--budget-ms', '20']}

def run(deck, cell, gi, seed, env_over, outdir):
    os.makedirs(outdir, exist_ok=True)
    d, p = DECKS[deck]
    env = dict(os.environ)
    env.update(env_over)
    cmd = ['nice', '-n', '19', BIN, ROOT + '/' + d, '--profile', ROOT + '/' + p, '--seed', str(seed),
           '--game-index', str(gi), '--games', '1', '--threads', '1', '--log-dir', outdir] + CELLS[cell]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    open(outdir + '/stdout.txt', 'w').write(r.stdout)
    open(outdir + '/stderr.txt', 'w').write(r.stderr)
    m = re.search(r'avg \(turns\)\s*:\s*([0-9.]+)', r.stdout)
    return float(m.group(1)) if m else None

def main():
    lst, outroot = sys.argv[1], sys.argv[2]
    par = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else 3
    levers = ALL
    if '--levers' in sys.argv:
        levers = sys.argv[sys.argv.index('--levers') + 1].split(',')
    games = []
    for line in open(lst):
        f = line.split()
        if not f:
            continue
        deck, cell = f[0], f[1]
        gi = int(f[2].split('=')[1]); old = int(f[3].split('=')[1]); new = int(f[4].split('=')[1])
        seed = int(f[5].split('=')[1])
        games.append((deck, cell, gi, seed, old, new))
    jobs = []
    for (deck, cell, gi, seed, old, new) in games:
        base = '%s/g%d_%s' % (outroot, gi, cell)
        oldenv = {l: '0' for l in ALL}
        jobs.append(((gi, cell), 'new', {}, base + '_new'))
        jobs.append(((gi, cell), 'old', oldenv, base + '_old'))
        for l in levers:
            e = dict(oldenv); e[l] = '1'
            jobs.append(((gi, cell), 'old+' + l[4:], e, base + '_oldp_' + l[4:]))
            jobs.append(((gi, cell), 'new-' + l[4:], {l: '0'}, base + '_newm_' + l[4:]))
    res = {}
    gmap = {(g[2], g[1]): g for g in games}
    with cf.ThreadPoolExecutor(par) as ex:
        futs = {}
        for key, tag, env, od in jobs:
            g = gmap[key]
            futs[ex.submit(run, g[0], g[1], g[2], g[3], env, od)] = (key, tag)
        for f in cf.as_completed(futs):
            key, tag = futs[f]
            res[(key, tag)] = f.result()
    for (deck, cell, gi, seed, old, new) in games:
        key = (gi, cell)
        print('== %s %s gi=%d seed=%d listed old=%d new=%d : rep new=%s old=%s' % (
            deck, cell, gi, seed, old, new, res[(key, 'new')], res[(key, 'old')]))
        on = ['%s=%s' % (l[4:], res[(key, 'old+' + l[4:])]) for l in levers]
        off = ['%s=%s' % (l[4:], res[(key, 'new-' + l[4:])]) for l in levers]
        print('   old+1:', ' '.join(on))
        print('   new-1:', ' '.join(off))
    sys.stdout.flush()

main()
