#!/usr/bin/env python3
"""d8.py <slower.txt> <outdir> [--par N] [--bin B]: replay each SLOWER game at --depth 8 --budget-ms 0 (trial ON = default;
inert at b0), one thread each. Recovered = d8 turn <= the faster (off) arm's turn. Writes <outdir>/d8.txt."""
import re, sys, os, subprocess, concurrent.futures as cf
src, out = sys.argv[1], sys.argv[2]
par = int(sys.argv[sys.argv.index('--par')+1]) if '--par' in sys.argv else 6
B = sys.argv[sys.argv.index('--bin')+1] if '--bin' in sys.argv else '/workspaces/MagicDeckTester/build/scratch/idgate/sr1/mtg.bin'
os.makedirs(out, exist_ok=True)
G = []
for l in open(src):
    if not l.startswith('SLOWER'): continue
    m = re.match(r"SLOWER (\S+) gi=(\d+) (\d+)->(\d+) seed=(\d+) depth=(\S+) b=(\S+) deck=(['\"])(.*?)\8 profile=(['\"])(.*?)\10 heads=(\S+) life=(\S+)", l)
    G.append(dict(job=m[1], gi=int(m[2]), off=int(m[3]), on=int(m[4]), seed=int(m[5]), depth=m[6], b=m[7], deck=m[9], prof=m[11], heads=m[12], life=m[13]))
def opt(k, d): return sys.argv[sys.argv.index(k)+1] if k in sys.argv else d
DEP, BUD, EXTRA = opt('--depth', '8'), opt('--budget', '0'), [e.split('=',1) for e in opt('--env', '').split(',') if e]
ONLY = opt('--only', '')
if ONLY: G = [g for g in G if f"{g['job']}_gi{g['gi']}" in ONLY.split(',')]
def run(g):
    a = [B, g['deck'], '--profile', g['prof'], '--seed', str(g['seed']), '--game-index', str(g['gi']), '--games', '1', '--threads', '1',
         '--depth', DEP if DEP!='own' else g['depth'], '--budget-ms', BUD if BUD!='own' else str(int(g['b'])*int(opt('--bmul','1'))), '--ignore-play-profile']
    if '--noipp' in sys.argv: a = [x for x in a if x != '--ignore-play-profile']
    if DEP == 'own' and g['depth'] == 'None':   # the job plays the profile's own depth: pass only the budget, as the batch does
        a = [x for x in a if x != '--ignore-play-profile']; i = a.index('--depth'); del a[i:i+2]
    env = dict(os.environ, MTG_RSS_CAP_GB='20')
    for k, v in EXTRA: env[k] = v
    if g['heads'] != 'None': env['MTG_OPPONENT_HEADS'] = g['heads']
    if g['life'] != 'None': env['MTG_START_LIFE'] = g['life']
    tag = f"{g['job']}_gi{g['gi']}"; ftag = tag + opt('--tag', '')
    p = subprocess.run(a, capture_output=True, text=True, cwd='/workspaces/MagicDeckTester/.claude/worktrees/idgate', env=env)
    open(f"{out}/{ftag}.txt", 'w').write(p.stdout + p.stderr)
    m = re.search(r'avg \(turns\)\s+:\s+([\d.]+)', p.stdout + p.stderr)
    t = float(m[1]) if m else None
    return f"{tag} off={g['off']} on={g['on']} d{DEP}b{BUD}={t} {'RECOVERED' if t is not None and t <= g['off'] else 'NOT-RECOVERED'} rc={p.returncode}"
with cf.ThreadPoolExecutor(par) as ex, open(f"{out}/d8{opt('--tag', '')}.txt", 'w') as f:
    for r in ex.map(run, G): print(r, flush=True); f.write(r + '\n'); f.flush()
