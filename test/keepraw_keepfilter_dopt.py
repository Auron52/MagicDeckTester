#!/usr/bin/env python3
"""Does giving KeepVal best_sub's refined-only filter change D_opt and the mulligan rate, and which way?

TESTS A FALSIFIABLE PREDICTION from docs/design/keep-argmin-winners-curse.md §3:

  KeepVal (ExhaustiveKeep.cpp:307) takes a plain std::min over EVERY present subcomposition, while
  best_sub (:624) restricts the SAME argmin to refined cells (cnt > bottom_floor). The unfiltered
  argmin can be won by a floor-R cell that is merely lucky, and floor cells are measured to be
  1.1-1.6 turns WORSE than refined ones -- so KeepVal is biased optimistically LOW for m >= 1.

  At m = 0 there is no argmin at all (KeepVal(h,0) = V[7][h], a plain mean). So the m=0 decision
  compares an UNBIASED hand value against an optimistically-low mulligan alternative Dopt[1].

  PREDICTION: filtering raises KeepVal(.,m>=1), which raises Dopt[1], which makes the keep test
  `KeepVal(h,0) <= Dopt[1]` easier -- so the filtered policy should KEEP MORE / MULLIGAN LESS.
  If the filtered policy instead mulligans MORE, the §3 story is wrong.

Offline: reads only the raw sidecar and the decklist. No engine, no rollouts.

usage: KEEPRAW_PATH=<raw.json> KEEPCOD_PATH=<deck.cod> keepraw_keepfilter_dopt.py [samples] [floor]
"""
import os, random, re, sys
from collections import Counter

RAW = os.environ.get('KEEPRAW_PATH', 'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json')
COD = os.environ.get('KEEPCOD_PATH', 'decks/FiveColour/FiveColour.cod')
N = int(sys.argv[1]) if len(sys.argv) > 1 else 60000
FLOOR = int(sys.argv[2]) if len(sys.argv) > 2 else 2
HAND = 7

text = open(RAW).read()
BK = re.search(r'^\{"buckets":(\[\[.*?\]\]),"meta"', text, re.S)
buckets = eval(BK.group(1))
K = len(buckets)
bucket_of = {nm: b for b, names in enumerate(buckets) for nm in names}
MM = int(re.search(r'"max_mull":(\d+)', text).group(1))

ENTRY = re.compile(
    r'\{"comp":\[([\d,]+)\],"count":\[(\d+),(\d+)\],'
    r'"sum":\[([-\d.eE+]+),([-\d.eE+]+)\],"sumsq":\[([-\d.eE+]+),([-\d.eE+]+)\]\}')
HDR = re.compile(r'\{"H":(\d+),"entries":\[')
bounds = [(int(m.group(1)), m.end()) for m in HDR.finditer(text)]
bounds.sort(key=lambda t: t[1])
segs = [(H, s, (bounds[i + 1][1] if i + 1 < len(bounds) else len(text)))
        for i, (H, s) in enumerate(bounds)]

# V[H][comp] = (mean, cnt) per pd -- counts KEPT, which is the whole point
V = {}
for H, s, e in segs:
    tab = {}
    for m in ENTRY.finditer(text, s, e):
        comp = tuple(int(x) for x in m.group(1).split(','))
        c = (int(m.group(2)), int(m.group(3)))
        sm = (float(m.group(4)), float(m.group(5)))
        tab[comp] = ((sm[0] / c[0] if c[0] else None, sm[1] / c[1] if c[1] else None), c)
    V[H] = tab
del text
print(f'{os.path.basename(RAW)}: K={K} max_mull={MM} tables {sorted(V)}  floor={FLOOR}', flush=True)

counts = [0] * K
total = 0
unmapped = Counter()
main = re.search(r'<zone name="main">(.*?)</zone>', open(COD).read(), re.S)
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
    print('  NOTE unmapped decklist entries:', dict(unmapped), flush=True)
print(f'  deck_size={total}', flush=True)
if total < HAND + 1:
    sys.exit('decklist parse failed -- refusing to report a rate from a bad parse')
bag = [b for b, c in enumerate(counts) for _ in range(c)]


def keepval(comp, m, pd, filtered):
    """min V over size-(HAND-m) subcompositions. filtered => only cells with cnt > FLOOR,
    falling back to the unfiltered set when NOTHING is refined (best_sub's own fallback)."""
    target = HAND - m
    tab = V.get(target)
    if tab is None:
        return None
    best_ref = [None]
    best_any = [None]
    cur = [0] * K
    nz = [(i, comp[i]) for i in range(K) if comp[i] > 0]

    def rec(k, rem):
        if rem == 0:
            hit = tab.get(tuple(cur))
            if hit is not None:
                v, c = hit[0][pd], hit[1][pd]
                if v is not None:
                    if best_any[0] is None or v < best_any[0]:
                        best_any[0] = v
                    if c > FLOOR and (best_ref[0] is None or v < best_ref[0]):
                        best_ref[0] = v
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
    if not filtered:
        return best_any[0]
    return best_ref[0] if best_ref[0] is not None else best_any[0]


rng = random.Random(20260915)
hands = []
for _ in range(N):
    h = rng.sample(bag, HAND)
    c = [0] * K
    for b in h:
        c[b] += 1
    hands.append(tuple(c))

print()
print(f'{"pd":>4} {"variant":>9} ' + ' '.join(f'{"Dopt[" + str(m) + "]":>9}' for m in range(MM + 1))
      + f' {"mull7%":>8}')
summary = {}
for pd in (1, 0):                                    # 1 = on the play, 0 = on the draw
    for filtered in (False, True):
        kv = {}
        for m in range(MM + 1):
            kv[m] = [keepval(h, m, pd, filtered) for h in hands]
        Dopt = [0.0] * (MM + 1)
        vals = [v for v in kv[MM] if v is not None]
        Dopt[MM] = sum(vals) / len(vals) if vals else 0.0
        for m in range(MM - 1, -1, -1):
            acc = n_ = 0.0, 0
            s = 0.0
            n_ = 0
            for v in kv[m]:
                if v is None:
                    continue
                s += min(v, Dopt[m + 1])
                n_ += 1
            Dopt[m] = s / n_ if n_ else 0.0
        # mull-from-7: the m=0 decision, keep iff KeepVal(h,0) <= Dopt[1]
        tot = mull = 0
        for v in kv[0]:
            if v is None:
                continue
            tot += 1
            if v > Dopt[1]:
                mull += 1
        rate = 100.0 * mull / tot if tot else 0.0
        summary[(pd, filtered)] = (Dopt[:], rate)
        print(f'{("play" if pd else "draw"):>4} {("FILTERED" if filtered else "plain"):>9} '
              + ' '.join(f'{d:>9.4f}' for d in Dopt) + f' {rate:>7.2f}%')

print()
print('PREDICTION (§3): FILTERED should raise Dopt[1] and therefore MULLIGAN LESS from 7.')
for pd in (1, 0):
    dp, rp = summary[(pd, False)]
    df, rf = summary[(pd, True)]
    lbl = 'play' if pd else 'draw'
    d1 = df[1] - dp[1]
    dr = rf - rp
    ok = 'CONFIRMED' if (d1 > 0 and dr < 0) else ('REFUTED' if (d1 != 0 or dr != 0) else 'no change')
    print(f'  {lbl}: Dopt[1] {dp[1]:.4f} -> {df[1]:.4f} ({d1:+.4f}),  '
          f'mull7 {rp:.2f}% -> {rf:.2f}% ({dr:+.2f}pp)   {ok}')
