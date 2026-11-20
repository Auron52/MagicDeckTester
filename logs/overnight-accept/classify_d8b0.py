"""Classify each PERSISTS game from the two-arm d8 b0 re-run.

recovers  : new arm at d8 b0 reaches the old win turn (<= old GT turn) -- the case-budget slowdown is
            search truncation the budget fix-up cannot see; nothing is unfindable.
same-d8   : both arms reach the SAME turn at d8 b0 but that turn is later than the old GT (old was luck).
variance  : the arms differ at d8 b0 AND their opening draws diverge (draw digest differs) -- a different
            physical game; not a like-for-like quality change.
real      : the arms differ at d8 b0 (new slower) with identical draws.
Draw identity is approximated by the explain step for the 'real' candidates; here we only split on turns.
"""
import os, sys, collections
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'


def wt(arm, name):
    p = f'{A}/{arm}/wins/{name}.wins'
    if not os.path.exists(p):
        return None
    for l in open(p):
        f = l.split()
        if len(f) >= 2:
            return int(f[1])
    return None


def sc(x):
    return 99 if x is None or x < 0 else x


def num(s):
    return 99 if s == 'loss' else int(s)


rows = [l.rstrip('\n').split('\t') for l in open(A + '/d8b0.idx') if l.strip()]
cnt = collections.Counter()
per = collections.defaultdict(collections.Counter)
out = []
for stem, key, gi, o, n in rows:
    deck = key.split('_')[0]
    od8, nd8 = wt('old', stem + '_d8b0'), wt('new', stem + '_d8b0')
    oc, nc = wt('old', stem + '_case'), wt('new', stem + '_case')
    if nd8 is None or (od8 is None and deck != 'redelem'):
        cls = 'pending'
    elif deck == 'redelem':
        cls = 'recovers' if sc(nd8) <= num(o) else 'new-deck(no old arm)'
    elif sc(nd8) <= num(o):
        cls = 'recovers'
    elif sc(nd8) == sc(od8):
        cls = 'same-d8'
    elif sc(nd8) < sc(od8):
        cls = 'new-better-d8'
    else:
        cls = 'differs-new-slower'
    repro = 'ok' if oc is not None and sc(oc) == num(o) else ('pending' if oc is None else f'OLDCASE={oc}')
    cnt[cls] += 1
    per[deck][cls] += 1
    out.append(f'{stem:44s} gt {o}->{n} case_old={oc} case_new={nc} d8b0 old={od8} new={nd8} {cls} repro={repro}')
open(A + '/d8b0.classified', 'w').write('\n'.join(out) + '\n')
print(dict(cnt))
for d in sorted(per):
    print(f'  {d:16s} {dict(per[d])}')
if '-v' in sys.argv:
    for l in out:
        if 'recovers' not in l:
            print(l)
