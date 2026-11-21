#!/usr/bin/env python3
"""Compare an arm against base in the ENUM-GAP A/B. usage: eg_compare.py <logs_dir> <ab.out> <arm> [base=off]"""
import re, sys
from pathlib import Path
r = Path(sys.argv[1]); out = Path(sys.argv[2]).read_text(); arm = sys.argv[3]; base = sys.argv[4] if len(sys.argv) > 4 else 'off'
units, cpu = {}, {}
for m in re.finditer(r'^(\w+?)__(\w+?)__o\d+: played=\d+ .* ms=(\d+) units=(\d+) cpu=(\d+)', out, re.M):
    k = (m.group(1), m.group(2)); units[k] = units.get(k, 0) + int(m.group(4)); cpu[k] = cpu.get(k, 0) + int(m.group(5))
def wins(a, deck):
    rows = {}
    for p in r.glob(f'{a}__{deck}__*.wins'):
        for line in p.read_text().splitlines():
            gi, turn, digest = line.split(); rows[int(gi)] = (int(turn), digest)
    return rows
sc = lambda t: 9 if t <= 0 else t
decks = sorted({p.name.split('__')[1] for p in r.glob(f'{arm}__*.wins')})
print(f'{arm} vs {base}')
print('| deck | pairs | faster | slower | net turns | digest-only | units ratio | cpu ratio |')
print('|---|---|---|---|---|---|---|---|')
for deck in decks:
    A = wins(arm, deck); B = wins(base, deck); keys = sorted(set(A) & set(B))
    if not keys: continue
    e = [i for i in keys if sc(A[i][0]) < sc(B[i][0])]; l = [i for i in keys if sc(A[i][0]) > sc(B[i][0])]
    dig = [i for i in keys if A[i][0] == B[i][0] and A[i][1] != B[i][1]]
    net = sum(sc(A[i][0]) - sc(B[i][0]) for i in keys)
    uo, uf = units.get((arm, deck), 0), units.get((base, deck), 0); co, cf = cpu.get((arm, deck), 0), cpu.get((base, deck), 0)
    print(f'| {deck} | {len(keys)} | {len(e)} | {len(l)} | {net:+d} | {len(dig)} | {uo/uf if uf else float("nan"):.4f} | {co/cf if cf else float("nan"):.4f} |')
    for i in l: print(f'  slower {deck} gi{i} {B[i][0]}->{A[i][0]}')
