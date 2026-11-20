"""Row-level compare of a variant run (<dir>/wins/<key>[__tag].wins) against OLD GT and the NEW run.
Usage: rowcmp.py <dir>   (unwon scored 21 for the sum; per-game equality counts are exact)"""
import os, sys
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58'
NEW = '/workspaces/MagicDeckTester/test/logs/overnight/wins'


def load(p):
    d = {}
    for l in open(p):
        f = l.split()
        if len(f) >= 2:
            d[int(f[0])] = 21 if int(f[1]) < 0 else int(f[1])
    return d


d = sys.argv[1]
for f in sorted(os.listdir(f'{A}/{d}/wins')):
    name = f[:-5]
    key = name.split('__')[0]
    v = load(f'{A}/{d}/wins/{f}')
    o = load(f'{WT}/test/gt_logs/{key}.wins') if os.path.exists(f'{WT}/test/gt_logs/{key}.wins') else {}
    n = load(f'{NEW}/{key}.wins')
    g = [i for i in v if i in o and i in n]
    if not g:
        continue
    so, sn, sv = (sum(x[i] for i in g) for x in (o, n, v))
    eqo = sum(v[i] == o[i] for i in g)
    eqn = sum(v[i] == n[i] for i in g)
    print(f'{name:52s} n={len(g):5d} old={so/len(g):.4f} new={sn/len(g):.4f} variant={sv/len(g):.4f} '
          f'var-old={(sv-so)/len(g):+.4f} var-new={(sv-sn)/len(g):+.4f} same-as-old={eqo} same-as-new={eqn}')
