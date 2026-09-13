#!/usr/bin/env python3
"""Does a STRUCTURAL prior carry enough signal to fix the bottoming argmin offline?

Shrinking toward the candidate-set mean recovered only 0.0037 t of 0.0609 t regret
(keepraw_shrinkage_sim.py) -- it re-weights the same numbers and adds no information. The only way
to beat sampling noise without new rollouts is to let each cell BORROW STRENGTH from the rest of
the table: a hand's value is largely predictable from WHICH CARDS it holds, and every other cell
containing those cards is evidence about them.

So fit, per (hand size, play/draw), a weighted additive model of the cell mean on the bucket counts

    Vhat(comp) = b0 + sum_b beta_b * comp[b]

(precision-weighted by the cell's rollout count, ridge-stabilised because comps sum to H and the
design is therefore collinear). Then the empirical-Bayes estimate shrinks each cell toward its OWN
prediction rather than toward a shared constant:

    V_shrunk_i = Vhat_i + lam_i (X_i - Vhat_i),   lam_i = tau_r^2 / (tau_r^2 + se_i^2)

where tau_r is the RESIDUAL spread of true values around the model. Because Vhat_i differs per
candidate, this is NOT a monotone transform of X -- it genuinely re-ranks, which is precisely what
shrinking toward a common mean could not do.

THE GATE. Observed residual variance decomposes as
    var(X - Vhat) = tau_r^2 + mean(se^2)
so tau_r^2 = var(X - Vhat) - mean(se^2). The model is worth using only if tau_r is MATERIALLY
SMALLER than the raw between-candidate spread tau (~0.30 at m=1): that shortfall is the signal the
model recovers. If tau_r ~= tau the additive model explains nothing and this route dies here.

Reports the fit quality first, then simulates the regret exactly as keepraw_shrinkage_sim.py does
but with theta_i = Vhat_i + N(0, tau_r^2), so the model's real per-candidate spread is preserved.
"""
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

# cells[H][comp] = (mean[2], se[2], cnt[2])
cells = {}
hands7 = []
rng = random.Random(20260912)
K = None
for H, start, end in segs:
    tab = {}
    n = 0
    for m in ENTRY.finditer(text, start, end):
        comp = tuple(int(x) for x in m.group(1).split(','))
        if K is None:
            K = len(comp)
        cnt = [int(m.group(2)), int(m.group(3))]
        sm = [float(m.group(4)), float(m.group(5))]
        sq = [float(m.group(6)), float(m.group(7))]
        mean = [0.0, 0.0]
        se = [0.0, 0.0]
        for pd in (0, 1):
            c = cnt[pd]
            if c > 1:
                mu = sm[pd] / c
                mean[pd] = mu
                se[pd] = (max(0.0, sq[pd] / c - mu * mu) / c) ** 0.5
            elif c == 1:
                mean[pd] = sm[pd]
        if H == 7:
            n += 1
            if len(hands7) < SAMPLE:
                hands7.append(comp)
            else:
                j = rng.randrange(n)
                if j < SAMPLE:
                    hands7[j] = comp
            continue
        tab[comp] = (mean, se, cnt)
        n += 1
    if H != 7:
        cells[H] = tab
del text
print(f'  K={K} buckets, sampled {len(hands7)} size-7 hands', flush=True)


def solve(A, b, ridge):
    """Gaussian elimination with partial pivoting on (A + ridge*I) x = b."""
    n = len(b)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for i in range(n):
        M[i][i] += ridge
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) < 1e-12:
            continue
        M[c], M[p] = M[p], M[c]
        pv = M[c][c]
        for r in range(n):
            if r == c:
                continue
            f = M[r][c] / pv
            if f:
                for k in range(c, n + 1):
                    M[r][k] -= f * M[c][k]
    return [M[i][n] / M[i][i] if abs(M[i][i]) > 1e-12 else 0.0 for i in range(n)]


print()
print('=== 1. STRUCTURAL FIT: is a hand\'s value predictable from which cards it holds? ===')
print(f'{"size":>4} {"pd":>3} {"cells":>8} {"sd(V)":>7} {"mean se":>8} {"tau(raw)":>9} '
      f'{"resid sd":>9} {"tau_r":>7} {"signal":>7}')
MODEL = {}     # (H,pd) -> coefficient vector
TAU_R = {}     # (H,pd) -> residual spread of truth around the model
for H in sorted(cells, reverse=True):
    tab = cells[H]
    for pd in (0, 1):
        rows = [(comp, mean[pd], se[pd], cnt[pd])
                for comp, (mean, se, cnt) in tab.items() if cnt[pd] > 1]
        if len(rows) < K + 10:
            continue
        d = K + 1
        A = [[0.0] * d for _ in range(d)]
        bv = [0.0] * d
        for comp, y, se_i, c in rows:
            w = float(c)
            x = (1.0,) + comp
            for i in range(d):
                if x[i]:
                    xi = x[i] * w
                    Ai = A[i]
                    for j in range(d):
                        if x[j]:
                            Ai[j] += xi * x[j]
                    bv[i] += xi * y
        tr = sum(A[i][i] for i in range(d)) / d
        beta = solve(A, bv, ridge=max(1e-9, tr * 1e-7))
        # residuals, precision-weighted the same way
        sw = rv = my = mse = 0.0
        for comp, y, se_i, c in rows:
            x = (1.0,) + comp
            yh = sum(beta[i] * x[i] for i in range(d) if x[i])
            w = float(c)
            sw += w
            rv += w * (y - yh) ** 2
            my += w * y
            mse += w * se_i * se_i
        mu_y = my / sw
        var_y = sum(float(c) * (y - mu_y) ** 2 for _, y, _, c in rows) / sw
        resid = rv / sw
        msq = mse / sw
        tau_raw = max(0.0, var_y - msq) ** 0.5
        tau_r = max(0.0, resid - msq) ** 0.5
        MODEL[(H, pd)] = beta
        TAU_R[(H, pd)] = tau_r
        sig = (1.0 - (tau_r / tau_raw) ** 2) if tau_raw > 0 else 0.0
        print(f'{H:>4} {pd:>3} {len(rows):>8} {var_y**0.5:>7.4f} {msq**0.5:>8.4f} '
              f'{tau_raw:>9.4f} {resid**0.5:>9.4f} {tau_r:>7.4f} {sig:>6.1%}')
print()
print('"signal" = fraction of TRUE between-cell variance the additive model explains.')


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


print()
print('=== 2. REGRET with a model-based prior vs the plain argmin ===')
print(f'{"m":>2} {"size":>4} {"sets":>7} {"tau_r":>7} {"sd(Vhat)":>9} '
      f'{"plain":>8} {"EB-model":>9} {"GAIN":>8} {"closes":>7}')
grand = defaultdict(float)
gn = 0
for m in range(1, 7):
    H = 7 - m
    tab = cells.get(H)
    if not tab:
        continue
    sets = []
    for comp in hands7:
        subs = subcomps(comp, H)
        for pd in (0, 1):
            if (H, pd) not in MODEL:
                continue
            beta = MODEL[(H, pd)]
            ref, anyc = [], []
            for s in subs:
                c = tab.get(s)
                if c is None:
                    continue
                mean, se, cnt = c
                if cnt[pd] > 1:
                    x = (1.0,) + s
                    vh = sum(beta[i] * x[i] for i in range(len(x)) if x[i])
                    rec = (se[pd], vh)
                    anyc.append(rec)
                    if cnt[pd] > BOTTOM_FLOOR:
                        ref.append(rec)
            cand = ref if ref else anyc
            if len(cand) >= 2:
                sets.append((cand, TAU_R[(H, pd)]))
    if not sets:
        continue
    tot = defaultdict(float)
    n = 0
    sdvh = 0.0
    r = random.Random(987654 + m)
    gauss = r.gauss
    for cand, tau_r in sets:
        N = len(cand)
        ses = [c[0] for c in cand]
        vh = [c[1] for c in cand]
        mvh = sum(vh) / N
        sdvh += (sum((v - mvh) ** 2 for v in vh) / N) ** 0.5
        t2 = tau_r * tau_r
        lam = [t2 / (t2 + s * s) if (t2 + s * s) > 0 else 1.0 for s in ses]
        for _ in range(TRIALS):
            theta = [vh[i] + gauss(0.0, tau_r) for i in range(N)]
            X = [theta[i] + gauss(0.0, ses[i]) for i in range(N)]
            bt = min(theta)
            bi = min(range(N), key=lambda i: X[i])
            tot['plain'] += theta[bi] - bt
            bi = min(range(N), key=lambda i: vh[i] + lam[i] * (X[i] - vh[i]))
            tot['model'] += theta[bi] - bt
            n += 1
    pl = tot['plain'] / n
    md = tot['model'] / n
    gain = pl - md
    print(f'{m:>2} {H:>4} {len(sets):>7} {sets[0][1]:>7.4f} {sdvh/len(sets):>9.4f} '
          f'{pl:>8.4f} {md:>9.4f} {gain:>8.4f} {(gain/pl if pl else 0):>6.1%}')
    grand['plain'] += pl * len(sets)
    grand['model'] += md * len(sets)
    gn += len(sets)

print()
if gn:
    pl = grand['plain'] / gn
    md = grand['model'] / gn
    print(f'set-weighted: plain {pl:.4f} t   model-EB {md:.4f} t   GAIN {pl-md:.4f} t/decision '
          f'({(pl-md)/pl:.1%} of the curse)')
    print(f'compare: shrink-to-mean recovered 0.0037 t (6%). Deficit to close: +0.0184 t overall.')
