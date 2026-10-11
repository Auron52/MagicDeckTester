#!/usr/bin/env python3
"""cmpwins.py <dirA> <prefixA> <dirB> <prefixB> [max_turns]: paired per-game comparison of .wins logs.
Each .wins line: <game_index> <win_turn> <digest>; win_turn<=0 -> unwon -> max_turns+1."""
import sys, glob, os
def load(d, pre, mt):
    out = {}
    for f in glob.glob(os.path.join(d, pre + '.c*.wins')):
        for l in open(f):
            p = l.split()
            if len(p) < 2: continue
            gi, wt = int(p[0]), int(p[1])
            out[gi] = (wt if wt > 0 else mt + 1, p[2] if len(p) > 2 else '')
    return out
mt = int(sys.argv[5]) if len(sys.argv) > 5 else 8
A = load(sys.argv[1], sys.argv[2], mt); B = load(sys.argv[3], sys.argv[4], mt)
common = sorted(set(A) & set(B))
d = sum(B[g][0] - A[g][0] for g in common)
faster = [g for g in common if B[g][0] < A[g][0]]
slower = [g for g in common if B[g][0] > A[g][0]]
dig = sum(1 for g in common if A[g][1] != B[g][1])
print(f'n={len(common)} sumA={sum(A[g][0] for g in common)} net(B-A)={d:+d} faster={len(faster)} slower={len(slower)} digest-diff={dig}')
if len(sys.argv) > 6 or True:
    print('  slower:', ' '.join(f'{g}:{A[g][0]}->{B[g][0]}' for g in slower[:40]))
    print('  faster:', ' '.join(f'{g}:{A[g][0]}->{B[g][0]}' for g in faster[:40]))
