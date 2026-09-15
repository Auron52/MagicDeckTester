#!/usr/bin/env python3
"""Does folding the OTHER pd's observation into the prior shrink the bottoming winner's curse?

keepgen-bottoming-winners-curse.md §8 lists this as an unused lever: "A cell's play and draw true
values correlate at 0.885 (sizes 5-6), so each is strong evidence about the other -- unused
information that would tighten tau further. Not pursued because the gate, not the estimator,
dominates the result."

The gate is no longer available (docs/design/no-lookahead-bottoming.md: a shipped profile may not
defer decisions to the lookahead bottomer), so the estimator is the ONLY remaining lever and this
lever is worth measuring.

Method matches keepraw_structural_prior.py exactly -- same loader, same structural fit, same Monte
Carlo over candidate sets -- so the `plain` and `EB-model` columns here are directly comparable to
that script's. The new arm is `EB-xpd`.

  plain     argmin over the raw noisy means            (what generation ships today)
  EB-model  argmin over means shrunk to a structural prior  (what MTG_KEEP_BOTTOM_SHRINK does)
  EB-xpd    same, but the posterior also conditions on the other pd's observation of that cell

rho is MEASURED here, not assumed: the two pds' observed residuals share no noise (independent
rollouts), so cov(u0,u1) estimates cov(r0,r1) directly and rho_r = cov(u0,u1)/(tau0*tau1).

usage: keepraw_crosspd_prior.py [raw.json] [sample] [trials] [bottom_floor]

bottom_floor=2 (default) scores the BOTTOMING argmin (refined-only, what best_sub sees).
bottom_floor=1 removes the filter, which is the candidate set the KEEP argmin
(KeepVal/ArgminSub) actually faces -- it has no refined-only filter at all.
"""
import json, math, os, random, re, sys
from collections import defaultdict

RAW = sys.argv[1] if len(sys.argv) > 1 else \
    'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json'
SAMPLE = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
TRIALS = int(sys.argv[3]) if len(sys.argv) > 3 else 200
BOTTOM_FLOOR = int(sys.argv[4]) if len(sys.argv) > 4 else 2
# SE_SCALE multiplies every standard error, so SE_SCALE=sqrt(30/R) simulates the table at
# effective R rollouts/cell (se ~ 1/sqrt(R)). Used to answer 'would more R fix this?'.
SE_SCALE = float(os.environ.get('SE_SCALE', '1'))
# SE_CAP caps every standard error, simulating a raised generation FLOOR (no cell may be
# less precise than this) as distinct from SE_SCALE, which raises the cap for every cell.
SE_CAP = float(os.environ.get('SE_CAP', '0')) or None

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
                se[pd] = ((max(0.0, sq[pd] / c - mu * mu) / c) ** 0.5) * SE_SCALE
                if SE_CAP is not None and se[pd] > SE_CAP: se[pd] = SE_CAP
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


MODEL, TAU_R = {}, {}
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
        sw = rv = mse = 0.0
        for comp, y, se_i, c in rows:
            x = (1.0,) + comp
            yh = sum(beta[i] * x[i] for i in range(d) if x[i])
            w = float(c)
            sw += w
            rv += w * (y - yh) ** 2
            mse += w * se_i * se_i
        tau_r = max(0.0, rv / sw - mse / sw) ** 0.5
        MODEL[(H, pd)] = beta
        TAU_R[(H, pd)] = tau_r


def vhat(H, pd, comp):
    beta = MODEL[(H, pd)]
    x = (1.0,) + comp
    return sum(beta[i] * x[i] for i in range(len(x)) if x[i])


# ---- measure rho: correlation of the two pds' TRUE residuals from their structural models ----
# The pds' rollouts are independent draws, so their observation noises are uncorrelated and
# cov(u0,u1) is an unbiased estimate of cov(r0,r1) with no noise term to subtract. Only the
# VARIANCES need de-noising, which is what TAU_R already is.
print()
print('=== 0. CROSS-pd CORRELATION of the cell VALUES, disattenuated (measured) ===')
print(f'{"size":>4} {"cells":>8} {"tau_r(0)":>9} {"tau_r(1)":>9} {"cov":>9} {"rho":>7}')
RHO = {}
for H in sorted(cells, reverse=True):
    if (H, 0) not in MODEL or (H, 1) not in MODEL:
        continue
    tab = cells[H]
    n = 0
    a0 = a1 = q0 = q1 = q01 = ms0 = ms1 = 0.0
    for comp, (mean, se, cnt) in tab.items():
        if cnt[0] <= 1 or cnt[1] <= 1:
            continue
        n += 1
        a0 += mean[0]; a1 += mean[1]
        q0 += mean[0] ** 2; q1 += mean[1] ** 2; q01 += mean[0] * mean[1]
        ms0 += se[0] ** 2; ms1 += se[1] ** 2
    if n < 50:
        continue
    m0, m1 = a0 / n, a1 / n
    v0, v1 = q0 / n - m0 * m0, q1 / n - m1 * m1
    cov = q01 / n - m0 * m1
    # Disattenuate: observed variance = true variance + mean sampling variance. The two pds'
    # rollouts are independent, so the COVARIANCE needs no correction -- only the variances.
    t0, t1 = max(0.0, v0 - ms0 / n * n / n), max(0.0, v1 - ms1 / n * n / n)
    t0, t1 = max(0.0, v0 - ms0 / n), max(0.0, v1 - ms1 / n)
    rho = cov / math.sqrt(t0 * t1) if t0 > 0 and t1 > 0 else 0.0
    if rho > 0.995 or rho < -0.995:
        print(f'  WARNING size {H}: disattenuated rho={rho:.3f} out of range -- capping at 0.95')
    rho = max(-0.95, min(0.95, rho))
    RHO[H] = rho
    print(f'{H:>4} {n:>8} {math.sqrt(t0):>9.4f} {math.sqrt(t1):>9.4f} {cov:>9.4f} {rho:>7.3f}')


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
print('=== REGRET: plain vs structural-prior EB vs cross-pd EB ===')
print(f'{"m":>2} {"size":>4} {"sets":>7} {"rho":>6} '
      f'{"plain":>8} {"EB-model":>9} {"EB-xpd":>8} {"xpd GAIN":>9} {"closes":>7}')
grand = defaultdict(float)
gn = 0
for m in range(1, 7):
    H = 7 - m
    tab = cells.get(H)
    if not tab or H not in RHO:
        continue
    rho = RHO[H]
    sets = []
    for comp in hands7:
        subs = subcomps(comp, H)
        for pd in (0, 1):
            if (H, pd) not in MODEL or (H, 1 - pd) not in MODEL:
                continue
            ref, anyc = [], []
            for s in subs:
                c = tab.get(s)
                if c is None:
                    continue
                mean, se, cnt = c
                # need BOTH pds observed to use the cross-pd term; fall back to own-pd only
                if cnt[pd] > 1:
                    has_other = cnt[1 - pd] > 1
                    rec = (se[pd], vhat(H, pd, s),
                           se[1 - pd] if has_other else None)
                    anyc.append(rec)
                    if cnt[pd] > BOTTOM_FLOOR:
                        ref.append(rec)
            cand = ref if ref else anyc
            if len(cand) >= 2:
                sets.append((cand, TAU_R[(H, pd)], TAU_R[(H, 1 - pd)]))
    if not sets:
        continue
    tot = defaultdict(float)
    n = 0
    r = random.Random(987654 + m)
    gauss = r.gauss
    for cand, tau0, tau1 in sets:
        N = len(cand)
        ses = [c[0] for c in cand]
        vh = [c[1] for c in cand]
        seo = [c[2] for c in cand]
        t2 = tau0 * tau0
        lam = [t2 / (t2 + s * s) if (t2 + s * s) > 0 else 1.0 for s in ses]
        # 2x2 posterior weights per candidate: E[r0 | u0,u1] = (Sigma (Sigma+D)^-1 u)[0]
        wts = []
        for i in range(N):
            if seo[i] is None or tau1 <= 0 or tau0 <= 0:
                wts.append((lam[i], 0.0))
                continue
            c01 = rho * tau0 * tau1
            a = t2 + ses[i] ** 2
            b = c01
            cc = c01
            dd = tau1 * tau1 + seo[i] ** 2
            det = a * dd - b * cc
            if abs(det) < 1e-15:
                wts.append((lam[i], 0.0))
                continue
            # row 0 of Sigma * inv(Sigma+D)
            w0 = (t2 * dd - c01 * cc) / det
            w1 = (-t2 * b + c01 * a) / det
            wts.append((w0, w1))
        for _ in range(TRIALS):
            theta, X, Xo = [0.0] * N, [0.0] * N, [0.0] * N
            for i in range(N):
                r0 = gauss(0.0, tau0)
                theta[i] = vh[i] + r0
                X[i] = theta[i] + gauss(0.0, ses[i])
                if seo[i] is not None and tau0 > 0:
                    # other pd's residual, correlated with this one at rho
                    mu_o = rho * (tau1 / tau0) * r0
                    sd_o = tau1 * math.sqrt(max(0.0, 1.0 - rho * rho))
                    Xo[i] = mu_o + gauss(0.0, sd_o) + gauss(0.0, seo[i])
            bt = min(theta)
            bi = min(range(N), key=lambda i: X[i])
            tot['plain'] += theta[bi] - bt
            bi = min(range(N), key=lambda i: vh[i] + lam[i] * (X[i] - vh[i]))
            tot['model'] += theta[bi] - bt
            bi = min(range(N), key=lambda i: vh[i] + wts[i][0] * (X[i] - vh[i])
                                                   + wts[i][1] * Xo[i])
            tot['xpd'] += theta[bi] - bt
            n += 1
    pl, md, xp = tot['plain'] / n, tot['model'] / n, tot['xpd'] / n
    gain = md - xp
    print(f'{m:>2} {H:>4} {len(sets):>7} {rho:>6.3f} '
          f'{pl:>8.4f} {md:>9.4f} {xp:>8.4f} {gain:>9.4f} {(gain/pl if pl else 0):>6.1%}')
    for k_ in ('plain', 'model', 'xpd'):
        grand[k_] += (tot[k_] / n) * len(sets)
    gn += len(sets)

print()
if gn:
    pl, md, xp = grand['plain'] / gn, grand['model'] / gn, grand['xpd'] / gn
    print(f'set-weighted: plain {pl:.4f} t   EB-model {md:.4f} t   EB-xpd {xp:.4f} t')
    print(f'  structural prior alone: {pl-md:.4f} t ({(pl-md)/pl:.1%} of the curse)')
    print(f'  + cross-pd            : {md-xp:.4f} t ({(md-xp)/pl:.1%} more)  '
          f'-> total {(pl-xp)/pl:.1%}')
    print()
    print('Interpretation: the confounded in-game A/B measured k=0 (no deferral) losing to k=1')
    print('by +0.0142 t. The offline gate sweep predicted that gap at 0.0171 t, so this regret')
    print('scale is calibrated to roughly that ratio -- read the xpd GAIN as the fraction of the')
    print('deferral benefit an estimator improvement can recover WITHOUT deferring anything.')
