"""Compare a lever-flip run against GT old/new. Usage: cmpflip.py <dir> [-v]
restored = flip reproduces the OLD GT turn (lever is the cause); other = neither."""
import os, sys, collections
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
d = sys.argv[1]
idx = {l.split()[0]: l.split() for l in open(A + '/d8b0.idx') if l.strip()}
cnt = collections.defaultdict(collections.Counter)
lines = []
for f in sorted(os.listdir(f'{A}/{d}/wins')):
    name = f[:-5]
    stem, kind = name.rsplit('_', 1)
    if stem not in idx:
        continue
    _, key, gi, o, n = idx[stem]
    v = open(f'{A}/{d}/wins/{f}').read().split()
    w = 'loss' if not v or v[1] == '-1' else v[1]
    tag = 'restored' if w == o else ('still-new' if w == n else 'other')
    deck = key.split('_')[0]
    cnt[(deck, kind)][tag] += 1
    lines.append(f'{name:50s} gt {o}->{n} flip={w} {tag}')
for k in sorted(cnt):
    print(k, dict(cnt[k]))
if '-v' in sys.argv:
    print('\n'.join(lines))
