#!/usr/bin/env python3
"""How often does bottoming actually FIRE, and is the noise story quantitatively consistent?

Bottoming runs only when mulligan_count > 0 (AIEngine.cpp:590). So the A/B's whole-population delta
relates to the per-decision regret by

    delta_overall = f * ( regret_table - regret_lookahead ),     f = P(mulligan_count > 0)

The measured delta is +0.0184 t and the simulated table regret at the dominant depth m=1 is ~0.043 t.
That makes f a REAL TEST, not bookkeeping:

  * f high (>~0.45): consistent. Lookahead-under-confound keeps a modest edge and the winner's curse
    accounts for the loss -- the noise diagnosis stands.
  * f low  (<~0.35): the identity forces regret_lookahead < 0, which is impossible. The table would
    then be losing far more than sampling noise can explain, meaning a SYSTEMATIC error in the
    bottoming target (a modeling bug), not a precision problem -- and no amount of R or shrinkage
    would fix it.

Method: replicate ComputeDopt by Monte Carlo. Deal real 7-card hands from the decklist (so
compositions arrive with their true hypergeometric weights), evaluate KeepVal at each mulligan depth
off the raw's V tables, and run the same backward induction the generator uses. Reports Dopt, the
per-depth keep probabilities, and the resulting distribution of final mulligan count.
"""
import re
import random
import os, sys
from collections import defaultdict, Counter

RAW = os.environ.get('KEEPRAW_PATH',
                     'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json')
COD = 'decks/FiveColour/FiveColour.cod'
N = int(sys.argv[1]) if len(sys.argv) > 1 else 120000
HAND = 7

text = open(RAW).read()
BK = re.search(r'^\{"buckets":(\[\[.*?\]\]),"meta"', text, re.S)
buckets = eval(BK.group(1))          # list of lists of card names, JSON-compatible literal
K = len(buckets)
bucket_of = {}
for b, names in enumerate(buckets):
    for nm in names:
        bucket_of[nm] = b

# max_mull from meta
MM = int(re.search(r'"max_mull":(\d+)', text).group(1))

ENTRY = re.compile(
    r'\{"comp":\[([\d,]+)\],"count":\[(\d+),(\d+)\],'
    r'"sum":\[([-\d.eE+]+),([-\d.eE+]+)\],"sumsq":\[([-\d.eE+]+),([-\d.eE+]+)\]\}')
HDR = re.compile(r'\{"H":(\d+),"entries":\[')
bounds = [(int(m.group(1)), m.end()) for m in HDR.finditer(text)]
bounds.sort(key=lambda t: t[1])
segs = [(H, s, (bounds[i + 1][1] if i + 1 < len(bounds) else len(text)))
        for i, (H, s) in enumerate(bounds)]
V = {}
for H, s, e in segs:
    tab = {}
    for m in ENTRY.finditer(text, s, e):
        comp = tuple(int(x) for x in m.group(1).split(','))
        c0, c1 = int(m.group(2)), int(m.group(3))
        s0, s1 = float(m.group(4)), float(m.group(5))
        tab[comp] = (s0 / c0 if c0 else None, s1 / c1 if c1 else None)
    V[H] = tab
del text
print(f'K={K} buckets, max_mull={MM}, tables {sorted(V)}', flush=True)

# ---- decklist -> per-bucket copy counts ----------------------------------------------------------
counts = [0] * K
total = 0
unmapped = Counter()
cod = open(COD).read()
# Cockatrice XML: <zone name="main"> ... <card number="N" name="Card Name"/>
main = re.search(r'<zone name="main">(.*?)</zone>', cod, re.S)
if not main:
    sys.exit('could not find the main zone in the .cod -- refusing to guess')
for mm in re.finditer(r'<card\s+number="(\d+)"\s+name="([^"]+)"\s*/>', main.group(1)):
    n, nm = int(mm.group(1)), mm.group(2)
    b = bucket_of.get(nm)
    if b is None:
        unmapped[nm] += n
        continue
    counts[b] += n
    total += n
if unmapped:
    print('  NOTE unmapped decklist entries (sideboard/basics outside buckets):',
          dict(unmapped), flush=True)
deck_size = total
print(f'  deck_size={deck_size} (sum of bucketed copies)', flush=True)
if deck_size < HAND + 1:
    sys.exit('decklist parse failed -- refusing to report a rate from a bad parse')

# a flat bag of bucket ids, one per physical card
bag = []
for b, c in enumerate(counts):
    bag.extend([b] * c)


def keepval(comp, m, pd):
    """min V over subcompositions of `comp` of size HAND-m; None if none present."""
    target = HAND - m
    nz = [(i, comp[i]) for i in range(K) if comp[i] > 0]
    tab = V.get(target)
    if tab is None:
        return None
    best = [None]
    cur = [0] * K

    def rec(k, rem):
        if rem == 0:
            v = tab.get(tuple(cur))
            if v is not None and v[pd] is not None:
                if best[0] is None or v[pd] < best[0]:
                    best[0] = v[pd]
            return
        if k >= len(nz):
            return
        if sum(nz[j][1] for j in range(k, len(nz))) < rem:
            return
        i, cap = nz[k]
        for x in range(min(cap, rem), -1, -1):
            cur[i] = x
            rec(k + 1, rem - x)
        cur[i] = 0

    rec(0, target)
    return best[0]


rng = random.Random(4242)
print(f'dealing {N} hands per pd ...', flush=True)
for pd, lbl in ((1, 'play'), (0, 'draw')):
    # KV[m][t] = KeepVal of the t-th dealt hand at depth m
    KV = [[] for _ in range(MM + 1)]
    for _ in range(N):
        h = rng.sample(bag, HAND)
        comp = [0] * K
        for b in h:
            comp[b] += 1
        comp = tuple(comp)
        for m in range(MM + 1):
            KV[m].append(keepval(comp, m, pd))

    def mean(xs):
        xs = [x for x in xs if x is not None]
        return sum(xs) / len(xs) if xs else float('nan')

    Dopt = [0.0] * (MM + 1)
    Dopt[MM] = mean(KV[MM])
    for m in range(MM - 1, -1, -1):
        vals = [min(v, Dopt[m + 1]) if v is not None else Dopt[m + 1] for v in KV[m]]
        Dopt[m] = sum(vals) / len(vals)

    # Simulate the sequential mulligan decision: at depth m keep iff KeepVal(h,m) <= Dopt[m+1].
    # Each successive mulligan is a FRESH hand (that is what mulliganing does).
    final = Counter()
    for t in range(N):
        m = 0
        while True:
            if m >= MM:
                final[m] += 1
                break
            v = KV[m][t] if t < len(KV[m]) else None
            # fresh hand at each depth: reuse independent draws by offsetting the sample index
            idx = (t + m * 7919) % N
            v = KV[m][idx]
            if v is not None and v <= Dopt[m + 1]:
                final[m] += 1
                break
            m += 1
    tot = sum(final.values())
    f = 1.0 - final[0] / tot
    print()
    print(f'--- on the {lbl} ---')
    print('  Dopt: ' + '  '.join(f'm{m}={Dopt[m]:.4f}' for m in range(MM + 1)))
    print('  final mulligan count: ' + '  '.join(
        f'{m}:{final[m]/tot:.1%}' for m in sorted(final)))
    print(f'  f = P(mulligan_count > 0) = P(bottoming fires) = {f:.1%}')
    if f > 0:
        print(f'  => implied E[delta | mulliganed] = 0.0184 / {f:.3f} = {0.0184/f:.4f} t')
        print(f'     (compare simulated table regret at m=1 ~0.043 t; lookahead regret is the'
              f' remainder)')
