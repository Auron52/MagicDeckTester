#!/usr/bin/env python3
"""How much win-turn can a bias-corrected bottoming argmin actually recover? Measured, not assumed.

The winner's-curse premise is confirmed (see keepraw_argmin_noise.py): at m=1 half the spread
between refined bottoming candidates is sampling noise. But confirming the DISEASE does not imply
the CURE works. The decisive theoretical fact:

    if every candidate has the SAME standard error, shrinking all of them toward a common mean is a
    MONOTONE transform of the estimates -- the argmin is literally unchanged and shrinkage buys
    exactly zero.

All the gain comes from PRECISION HETEROGENEITY: our candidates sit at R=18 (se~.174) or R=30
(se~.135), plus R=2 (se~.52) in the ~6% of decisions that fall back to the unfiltered argmin.
A shrunk estimate discounts the noisier cell's apparent advantage, so a lucky R=2 or R=18 cell
stops beating a solid R=30 one.

METHOD. For each REAL sampled candidate set we keep its REAL per-candidate se values, then draw
    theta_i ~ N(0, tau_m^2)        (true values; tau_m measured from the raw)
    X_i     = theta_i + N(0, se_i^2)   (what the generation observed)
and score selectors by REGRET = theta[chosen] - min(theta), in win-turns. Averaging over many
draws gives the expected per-decision cost of each rule. This is a pure simulation on the
measured (se_i, tau_m, N) structure -- it never treats the raw means as ground truth, which would
be circular since those means are themselves R~14 noisy.

Selectors compared:
  plain     argmin X                        -- what BuildPolicyFromTables::best_sub does today
  EB        argmin [mu + lam_i (X_i - mu)]  -- empirical Bayes, lam_i = tau^2/(tau^2+se_i^2)
  pess-k    argmin [X_i + k*se_i]           -- pessimistic; penalise uncertainty directly
  oracle    argmin theta                    -- regret 0 by construction (the bound)

The number that matters is (plain - best), the per-decision win-turn recoverable offline. It must
then be diluted by how often bottoming actually fires to compare against the A/B's +0.0184 t.
"""
import json
import re
import random
import sys
from collections import defaultdict

RAW = sys.argv[1] if len(sys.argv) > 1 else \
    'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json'
SAMPLE = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
TRIALS = int(sys.argv[3]) if len(sys.argv) > 3 else 200
BOTTOM_FLOOR = 2

ENTRY = re.compile(
    r'\{"comp":\[([\d,]+)\],"count":\[(\d+),(\d+)\],'
    r'"sum":\[([-\d.eE+]+),([-\d.eE+]+)\],"sumsq":\[([-\d.eE+]+),([-\d.eE+]+)\]\}')
HDR = re.compile(r'\{"H":(\d+),"entries":\[')

print(f'reading {RAW} ...', flush=True)
text = open(RAW).read()
bounds = [(int(m.group(1)), m.end()) for m in HDR.finditer(text)]
bounds.sort(key=lambda t: t[1])
segs = [(H, s, (bounds[i + 1][1] if i + 1 < len(bounds) else len(text)))
        for i, (H, s) in enumerate(bounds)]

tables = {}
hands7 = []
rng = random.Random(20260912)
for H, start, end in segs:
    cells = {}
    n = 0
    for m in ENTRY.finditer(text, start, end):
        comp = tuple(int(x) for x in m.group(1).split(','))
        cnt = [int(m.group(2)), int(m.group(3))]
        sm = [float(m.group(4)), float(m.group(5))]
        sq = [float(m.group(6)), float(m.group(7))]
        se = [0.0, 0.0]
        for pd in (0, 1):
            c = cnt[pd]
            if c > 1:
                mu = sm[pd] / c
                se[pd] = (max(0.0, sq[pd] / c - mu * mu) / c) ** 0.5
        if H == 7:
            n += 1
            if len(hands7) < SAMPLE:
                hands7.append(comp)
            else:
                j = rng.randrange(n)
                if j < SAMPLE:
                    hands7[j] = comp
            continue
        cells[comp] = (se, cnt)
        n += 1
    if H != 7:
        tables[H] = cells
del text
print(f'  sampled {len(hands7)} size-7 hands', flush=True)

# tau per m, measured on the refined-only candidate sets (matches keepraw_argmin_noise.py)
TAU = {1: 0.2960, 2: 0.3765, 3: 0.4127, 4: 0.4098, 5: 0.3094, 6: 0.1598}


def subcomps(hand, target):
    nz = [(i, hand[i]) for i in range(len(hand)) if hand[i] > 0]
    out = []
    cur = [0] * len(hand)

    def rec(k, rem):
        if rem == 0:
            out.append(tuple(cur)); return
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
    return out


# Build the real candidate sets ONCE: per (hand, m, pd) the list of candidate se values, applying
# the same bottom_floor filter + unfiltered fallback that best_sub applies.
print('building real candidate sets ...', flush=True)
sets_by_m = defaultdict(list)
for comp in hands7:
    for m in range(1, 7):
        H = 7 - m
        t = tables.get(H)
        if not t:
            continue
        subs = subcomps(comp, H)
        for pd in (0, 1):
            ref, anyc = [], []
            for s in subs:
                c = t.get(s)
                if c is None:
                    continue
                se, cnt = c
                if cnt[pd] > 1:
                    anyc.append(se[pd])
                    if cnt[pd] > BOTTOM_FLOOR:
                        ref.append(se[pd])
            cand = ref if ref else anyc          # best_sub's refined-only + fallback
            if len(cand) >= 2:
                sets_by_m[m].append(cand)

KS = [0.25, 0.5, 0.75, 1.0]
print()
print(f'simulating {TRIALS} trials per candidate set ...', flush=True)
print()
print(f'{"m":>2} {"sets":>7} {"tau":>6} {"plain":>8} {"EB":>8} '
      + ' '.join(f'{"pess"+str(k):>8}' for k in KS) + f'{"  BEST GAIN":>11}')

grand = defaultdict(float)
grand_n = 0
for m in range(1, 7):
    sets = sets_by_m.get(m)
    if not sets:
        continue
    tau = TAU[m]
    tau2 = tau * tau
    tot = defaultdict(float)
    n = 0
    r = random.Random(1234567 + m)
    gauss = r.gauss
    for cand in sets:
        N = len(cand)
        lam = [tau2 / (tau2 + s * s) for s in cand]
        for _ in range(TRIALS):
            theta = [gauss(0.0, tau) for _ in range(N)]
            X = [theta[i] + gauss(0.0, cand[i]) for i in range(N)]
            best_theta = min(theta)
            mu = sum(X) / N
            # plain argmin
            bi = min(range(N), key=lambda i: X[i])
            tot['plain'] += theta[bi] - best_theta
            # empirical Bayes shrinkage toward the candidate-set mean
            bi = min(range(N), key=lambda i: mu + lam[i] * (X[i] - mu))
            tot['EB'] += theta[bi] - best_theta
            # pessimistic
            for k in KS:
                bi = min(range(N), key=lambda i: X[i] + k * cand[i])
                tot['pess%s' % k] += theta[bi] - best_theta
            n += 1
    row = {k: v / n for k, v in tot.items()}
    best_alt = min(v for k, v in row.items() if k != 'plain')
    gain = row['plain'] - best_alt
    print(f'{m:>2} {len(sets):>7} {tau:>6.3f} {row["plain"]:>8.4f} {row["EB"]:>8.4f} '
          + ' '.join(f'{row["pess"+str(k)]:>8.4f}' for k in KS)
          + f'{gain:>11.4f}')
    for k, v in row.items():
        grand[k] += v * len(sets)
    grand_n += len(sets)

print()
if grand_n:
    row = {k: v / grand_n for k, v in grand.items()}
    best_alt_k = min((k for k in row if k != 'plain'), key=lambda k: row[k])
    gain = row['plain'] - row[best_alt_k]
    print(f'set-weighted mean regret:  plain {row["plain"]:.4f} t   '
          f'best={best_alt_k} {row[best_alt_k]:.4f} t   GAIN {gain:.4f} t per bottoming decision')
    print()
    print(f'The A/B deficit to close is +0.0184 t, measured over ALL games. A per-decision gain of')
    print(f'{gain:.4f} t only pays out on games that actually mulligan and bottom, so the effective')
    print(f'closure is {gain:.4f} * P(bottoming fires). Compare that against 0.0184 before claiming')
    print(f'this fix is sufficient.')
