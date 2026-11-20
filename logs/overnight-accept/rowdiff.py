"""Per-row old(committed gt_logs) vs new(run wins) summary for given deck prefixes."""
import sys, os, glob
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58'
NEW = '/workspaces/MagicDeckTester/test/logs/overnight/wins'
MAXT = 21  # unwon scored as max_turns+1 approx; only used for counting


def load(p):
    d = {}
    for l in open(p):
        f = l.split()
        if len(f) >= 2:
            d[int(f[0])] = (int(f[1]), f[2] if len(f) > 2 else '')
    return d


decks = sys.argv[1:]
for p in sorted(glob.glob(NEW + '/*.wins')):
    key = os.path.basename(p)[:-5]
    if key.split('_')[0] not in decks:
        continue
    op = f'{WT}/test/gt_logs/{key}.wins'
    if not os.path.exists(op):
        print(key, 'NO OLD');
        continue
    o, n = load(op), load(p)
    sl = fa = pc = 0
    dsum = 0
    for gi in n:
        if gi not in o:
            continue
        a, b = o[gi][0], n[gi][0]
        a = MAXT if a < 0 else a
        b = MAXT if b < 0 else b
        if b > a: sl += 1
        elif b < a: fa += 1
        elif o[gi][1] != n[gi][1]: pc += 1
        dsum += b - a
    print(f'{key:38s} games={len(n):5d} slower={sl:4d} faster={fa:4d} digest-only={pc:4d} sumdelta={dsum:+d} avg={dsum/len(n):+.4f}')
