#!/usr/bin/env python3
"""Measure the winner's-curse exposure of the exhaustive bottoming argmin, from a raw sidecar.

WHY: the FiveColour confounded bottoming A/B rejected the profile (+0.0184 t). The hypothesis was
that the sub-table argmin suffers SELECTION BIAS -- at sub-table R~14 the argmin over a hand's
subcompositions picks the luckiest estimate rather than the best hand. That hypothesis rests on
three numbers that must be MEASURED, not assumed:

  1. se   -- the standard error of a sub-cell's mean win-turn (sd / sqrt(cnt))
  2. N    -- how many candidate subcompositions a bottoming decision chooses among
  3. tau  -- the spread of TRUE values across those candidates

Only the ratio decides it. Within one candidate set, the observed variance of the estimates is
    var_obs = tau^2 + mean(se^2)
so the NOISE SHARE = mean(se^2) / var_obs is the fraction of the apparent spread that is pure
sampling noise. Near 1 => the argmin is ranking noise and the pick is close to random among
candidates. Near 0 => the candidates are genuinely far apart, the argmin is safe, and the
winner's-curse diagnosis is WRONG and must be abandoned.

Reads the raw's per-cell (sum, sumsq, count) only. No rollouts, no engine, no model fitting.
"""
import json
import re
import random
import sys
from collections import defaultdict

RAW = sys.argv[1] if len(sys.argv) > 1 else \
    'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json'
SAMPLE = int(sys.argv[2]) if len(sys.argv) > 2 else 4000

ENTRY = re.compile(
    r'\{"comp":\[([\d,]+)\],"count":\[(\d+),(\d+)\],'
    r'"sum":\[([-\d.eE+]+),([-\d.eE+]+)\],"sumsq":\[([-\d.eE+]+),([-\d.eE+]+)\]\}')
HDR = re.compile(r'\{"H":(\d+),"entries":\[')

print(f'reading {RAW} ...', flush=True)
with open(RAW, 'r') as f:
    text = f.read()
print(f'  {len(text)/1e6:.1f} MB', flush=True)

# Segment the text by hand size so each entry is attributed to the right table.
bounds = [(int(m.group(1)), m.end()) for m in HDR.finditer(text)]
bounds.sort(key=lambda t: t[1])
segs = []
for i, (H, start) in enumerate(bounds):
    end = bounds[i + 1][1] if i + 1 < len(bounds) else len(text)
    segs.append((H, start, end))

# tables[H][comp_tuple] = (mean[2], se[2], cnt[2], var_per_rollout[2])
tables = {}
hands7 = []          # sampled size-7 comps (the decisions we will audit)
rng = random.Random(20260912)

for H, start, end in segs:
    cells = {}
    n = 0
    for m in ENTRY.finditer(text, start, end):
        comp = tuple(int(x) for x in m.group(1).split(','))
        c0, c1 = int(m.group(2)), int(m.group(3))
        s0, s1 = float(m.group(4)), float(m.group(5))
        q0, q1 = float(m.group(6)), float(m.group(7))
        mean = [0.0, 0.0]
        se = [0.0, 0.0]
        var = [0.0, 0.0]
        cnt = [c0, c1]
        for pd, (c, s, q) in enumerate(((c0, s0, q0), (c1, s1, q1))):
            if c > 0:
                mu = s / c
                mean[pd] = mu
                v = max(0.0, q / c - mu * mu)
                var[pd] = v
                se[pd] = (v / c) ** 0.5 if c > 1 else float('nan')
        if H == 7:
            # size-7 is 1.98M cells; keep a reservoir sample of the decisions, not the table
            n += 1
            if len(hands7) < SAMPLE:
                hands7.append((comp, cnt, mean))
            else:
                j = rng.randrange(n)
                if j < SAMPLE:
                    hands7[j] = (comp, cnt, mean)
            continue
        cells[comp] = (mean, se, cnt, var)
        n += 1
    if H != 7:
        tables[H] = cells
    print(f'  size {H}: {n} cells' + ('  (sampled)' if H == 7 else ''), flush=True)

print()
print('=== 1. SUB-CELL PRECISION (the tables the bottoming argmin reads) ===')
print(f'{"size":>4} {"cells":>8} {"meanR":>7} {"medR":>5} {"sd/roll":>8} {"mean se":>8} {"med se":>7}')
for H in sorted(tables, reverse=True):
    cs = tables[H]
    rs, sds, ses = [], [], []
    for mean, se, cnt, var in cs.values():
        for pd in (0, 1):
            if cnt[pd] > 1:
                rs.append(cnt[pd])
                sds.append(var[pd] ** 0.5)
                ses.append(se[pd])
    if not rs:
        continue
    rs.sort(); ses.sort()
    print(f'{H:>4} {len(cs):>8} {sum(rs)/len(rs):>7.2f} {rs[len(rs)//2]:>5} '
          f'{sum(sds)/len(sds):>8.3f} {sum(ses)/len(ses):>8.4f} {ses[len(ses)//2]:>7.4f}')


def subcomps(hand, target):
    """All subcompositions of `hand` summing to `target` (componentwise bounded)."""
    nz = [(i, hand[i]) for i in range(len(hand)) if hand[i] > 0]
    out = []
    cur = [0] * len(hand)

    def rec(k, rem):
        if rem == 0:
            out.append(tuple(cur))
            return
        if k >= len(nz):
            return
        i, cap = nz[k]
        # prune: can the remaining buckets still supply `rem`?
        left = sum(nz[j][1] for j in range(k, len(nz)))
        if left < rem:
            return
        for x in range(min(cap, rem), -1, -1):
            cur[i] = x
            rec(k + 1, rem - x)
        cur[i] = 0

    rec(0, target)
    return out


print()
print('=== 2. THE BOTTOMING DECISION: candidates, spread, and how much of it is noise ===')
print(f'sampled {len(hands7)} size-7 hands; m = mulligan depth, keep 7-m cards')
print()
print('CRITICAL: this generation ran --gen-mulligan fast => adaptive_bottom=TRUE => the bottoming')
print('argmin (best_sub) is passed bottom_floor=r0=2 and RESTRICTED to cells with cnt>2, falling')
print('back to unfiltered only if no candidate is refined. The keep argmin (KeepVal/ArgminSub) has')
print('NO such filter. So the two decisions face different candidate sets and must be measured')
print('separately -- measuring bottoming on the unfiltered set overstates its noise exposure.')


def audit(min_cnt, label):
    print()
    print(f'--- {label} (candidates require cnt > {min_cnt}) ---')
    print(f'{"m":>2} {"size":>4} {"decisions":>9} {"meanN":>7} {"maxN":>5} '
          f'{"sd(est)":>8} {"mean se":>8} {"tau":>7} {"NOISE SHARE":>11} {"fallback":>9}')
    for m in range(1, 7):
        H = 7 - m
        if H not in tables:
            continue
        t = tables[H]
        nlist, varobs, msq, shares = [], [], [], []
        fallback = 0
        total = 0
        for comp, cnt7, mean7 in hands7:
            for pd in (0, 1):
                vals, ses = [], []
                any_present = False
                for s in subcomps(comp, H):
                    c = t.get(s)
                    if c is None:
                        continue
                    mean, se, cn, var = c
                    if cn[pd] > 1:
                        any_present = True
                    if cn[pd] > min_cnt and cn[pd] > 1:
                        vals.append(mean[pd])
                        ses.append(se[pd])
                if any_present:
                    total += 1
                if any_present and not vals:
                    fallback += 1
                if len(vals) < 2:
                    continue
                n = len(vals)
                mu = sum(vals) / n
                vo = sum((v - mu) ** 2 for v in vals) / (n - 1)
                ms = sum(e * e for e in ses) / n
                nlist.append(n)
                varobs.append(vo)
                msq.append(ms)
                shares.append(min(1.0, ms / vo) if vo > 0 else 1.0)
        if not nlist:
            continue
        mvo = sum(varobs) / len(varobs)
        mms = sum(msq) / len(msq)
        print(f'{m:>2} {H:>4} {len(nlist):>9} {sum(nlist)/len(nlist):>7.1f} {max(nlist):>5} '
              f'{mvo**0.5:>8.4f} {mms**0.5:>8.4f} {max(0.0,mvo-mms)**0.5:>7.4f} '
              f'{sum(shares)/len(shares):>10.1%} '
              f'{(fallback/total if total else 0):>8.1%}')


audit(1, 'KEEP argmin -- unfiltered, what KeepVal/ArgminSub actually see')
audit(2, 'BOTTOMING argmin -- refined-only, what best_sub actually sees (bottom_floor=2)')

print()
print('READING IT: noise share near 100% => the argmin ranks noise (winner\'s curse is real and')
print('the fix must add a prior); near 0% => candidates are genuinely separated, argmin is safe,')
print('and the selection-bias diagnosis is refuted.')
