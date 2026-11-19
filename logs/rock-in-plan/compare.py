"""usage: compare.py <logs dir> <batch out>   (paired on/off A/B table; unwon = 9 turns)"""
from pathlib import Path
import re, sys
r = Path(sys.argv[1])
out = Path(sys.argv[2]).read_text()
units, cpu = {}, {}
for m in re.finditer(r'^(on|off)__(\w+?)__o\d+: played=\d+ .* ms=(\d+) units=(\d+) cpu=(\d+)', out, re.M):
    k = (m.group(1), m.group(2))
    units[k] = units.get(k, 0) + int(m.group(4)); cpu[k] = cpu.get(k, 0) + int(m.group(5))
def wins(arm, deck):
    rows = {}
    for p in r.glob(f'{arm}__{deck}__*.wins'):
        for line in p.read_text().splitlines():
            gi, turn, digest = line.split(); rows[int(gi)] = (int(turn), digest)
    return rows
PEN = 9
sc = lambda t: PEN if t <= 0 else t
decks = sorted({p.name.split('__')[1] for p in r.glob('on__*.wins')})
T = [0] * 6
print('| deck | pairs | faster | slower | net turns (on-off) | digest-only | units on/off | cpu on/off |')
print('|---|---|---|---|---|---|---|---|')
slow, fast = [], []
for deck in decks:
    A = wins('on', deck); B = wins('off', deck); keys = sorted(set(A) & set(B))
    if not keys: continue
    e = [i for i in keys if sc(A[i][0]) < sc(B[i][0])]; l = [i for i in keys if sc(A[i][0]) > sc(B[i][0])]
    dig = [i for i in keys if A[i][0] == B[i][0] and A[i][1] != B[i][1]]
    net = sum(sc(A[i][0]) - sc(B[i][0]) for i in keys)
    uo, uf = units.get(('on', deck), 0), units.get(('off', deck), 0)
    co, cf = cpu.get(('on', deck), 0), cpu.get(('off', deck), 0)
    print(f'| {deck} | {len(keys)} | {len(e)} | {len(l)} | {net:+d} | {len(dig)} | '
          f'{uo/uf if uf else float("nan"):.4f} | {co/cf if cf else float("nan"):.4f} |')
    for i in l: slow.append((deck, i, B[i][0], A[i][0]))
    for i in e: fast.append((deck, i, B[i][0], A[i][0]))
    T[0] += len(keys); T[1] += len(e); T[2] += len(l); T[3] += net; T[4] += uo; T[5] += uf
co = sum(v for (a, _), v in cpu.items() if a == 'on'); cf = sum(v for (a, _), v in cpu.items() if a == 'off')
print(f'| ALL | {T[0]} | {T[1]} | {T[2]} | {T[3]:+d} | | {T[4]/max(1,T[5]):.4f} | {co/max(1,cf):.4f} |')
for s in slow: print('slower', *s)
for s in fast: print('faster', *s)
