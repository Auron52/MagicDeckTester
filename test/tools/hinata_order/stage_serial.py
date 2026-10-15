#!/usr/bin/env python3
"""stage_serial.py SLOWER.json STAGE OUT.txt [--cells a,b] [--flag NAME=0/1 ...] -- one-at-a-time single-game
recovery repros (RULES item 4: single thread, niced, outside the batch lock), NEW arm (order on + flags).
stage 1: --depth max(5, old wt) --budget-ms 100; stage 2: --depth 8 --budget-ms 0. Appends one line per game:
"<cell> seed=<s> gi=<g> old=<o> stage<N>=<wt> RECOVERS|NOT"."""
import json, os, subprocess, sys
slower, stage, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
cells = None
flags = {}
args = sys.argv[4:]
i = 0
while i < len(args):
    if args[i] == '--cells':
        cells = set(args[i + 1].split(',')); i += 2
    elif args[i] == '--flag':
        k, v = args[i + 1].split('='); flags[k] = v; i += 2
    else:
        i += 1
S = '/workspaces/MagicDeckTester/build/scratch/hinata-order'
T = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155/test/tools/hinata_order'
done = set()
if os.path.exists(out):
    for line in open(out):
        p = line.split()
        if len(p) >= 3:
            done.add((p[0], p[2]))
score = lambda w: w if w > 0 else 9
for (cell, seed, gi, old, new) in json.load(open(slower)):
    if cells and cell not in cells:
        continue
    if (cell, 'gi=%d' % gi) in done:
        continue
    hg = '1' if cell.startswith('hinata2hg') else '0'
    if stage == 1:
        depth, bud = max(5, old if old > 0 else 9), 100
    else:
        depth, bud = 8, 0
    logdir = '%s/games/s%d_%s_gi%d' % (S, stage, cell, gi)
    env = dict(os.environ, **flags)
    r = subprocess.run(['bash', T + '/onegame2.sh', logdir, str(seed), str(gi), hg, '1',
                        '--depth', str(depth), '--budget-ms', str(bud), '--ignore-play-profile'],
                       capture_output=True, text=True, env=env)
    wt = None
    for tok in r.stdout.split():
        if tok.startswith('wt='):
            wt = int(tok[3:])
    verdict = 'RECOVERS' if (wt is not None and score(wt) <= score(old)) else 'NOT'
    with open(out, 'a') as f:
        f.write('%s seed=%d gi=%d old=%d stage%d=%s %s\n' % (cell, seed, gi, old, stage, wt, verdict))
