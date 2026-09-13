#!/usr/bin/env python3
"""Pick the bottoming rule that actually beats the lookahead, offline. No rollouts, no regeneration.

ESTABLISHED SO FAR (all measured from the raw sidecar + the failed A/B):
  * table bottoming regret, weighted by the real mulligan mix        ~0.0495 t
  * P(bottoming fires) = P(mulligan_count>0)                          f = 0.586
  * measured A/B deficit                                             +0.0184 t
  * => implied CONFOUNDED-LOOKAHEAD regret  L = 0.0495 - 0.0184/f    ~0.0185 t
  * shrinking to the candidate-set mean recovers 6% of the curse; shrinking to an additive
    STRUCTURAL model recovers 24%. Neither is enough on its own (+0.0184 -> +0.0133).

THE RULE THIS TESTS. The table is not uniformly worse than the lookahead -- it is worse ON AVERAGE
because near-ties are decided by noise. Where its margin is decisive it is close to perfect. So
emit a bottoming target ONLY when the margin clears the noise, and otherwise emit nothing:
ExhaustiveKeepPolicy::DecideBottom returns false for a slot whose target vector is empty, and
AIEngine::BottomCards then falls through to exactly the lookahead path the A/B's arm A measured.
That makes the hybrid expressible with NO engine change and NO schema change -- purely in how
BuildPolicyFromTables emits bottom_keep.

    Z_i    = Vhat_i + lam_i (X_i - Vhat_i)        posterior mean   (lam_i = tau_r^2/(tau_r^2+se_i^2))
    sig_i  = se_i * sqrt(lam_i)                   posterior sd
    margin = Z_(2) - Z_(1),  sd_diff = sqrt(sig_(1)^2 + sig_(2)^2)
    emit the target iff margin >= k * sd_diff, else defer to the lookahead

Sweeps k. For each k reports the deferral rate g, the blended regret, and the PREDICTED A/B delta
    delta_pred = f * (regret_hybrid - L)
which is the number the real A/B will report. k=0 is "always trust the table" (today's behaviour,
plus shrinkage); k=inf is "always defer" (delta -> 0 by construction, i.e. equals the lookahead).
A useful k must land delta_pred comfortably BELOW 0.

CAVEAT stated plainly: L is DERIVED from this same A/B plus the regret model, not independently
measured. The ranking of k values is robust to L, but the absolute delta_pred inherits L's error --
the real confounded A/B remains the gate.
"""
import re
import random
import sys
from collections import defaultdict

RAW = 'decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json'
SAMPLE = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
TRIALS = int(sys.argv[2]) if len(sys.argv) > 2 else 200
BOTTOM_FLOOR = 2
F_FIRE = 0.586
L_LOOK = 0.0185
# conditional mulligan mix among games that mulliganed (measured, keepraw_mullrate.py)
MIX = {1: 0.616, 2: 0.303, 3: 0.0725, 4: 0.0085}

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
        tab[comp] = (mean, se, cnt)
        n += 1
    if H != 7:
        cells[H] = tab
del text


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


def quad_feats(c, K):
    out = [1.0]
    out.extend(c)
    for i in range(K):
        ci = c[i]
        for j in range(i, K):
            out.append(ci * c[j])
    return tuple(out)


print('fitting structural priors (quadratic where supported) ...', flush=True)
MODEL, TAU_R, FEAT = {}, {}, {}
for H in sorted(cells, reverse=True):
    tab = cells[H]
    for pd in (0, 1):
        rows = [(c, mean[pd], se[pd], cnt[pd])
                for c, (mean, se, cnt) in tab.items() if cnt[pd] > 1]
        if len(rows) < K + 10:
            continue
        dq = 1 + K + K * (K + 1) // 2
        use_quad = len(rows) >= 5 * dq
        FEAT[(H, pd)] = 'quad' if use_quad else 'add'
        feats = (lambda c: quad_feats(c, K)) if use_quad else (lambda c: (1.0,) + c)
        d = dq if use_quad else K + 1
        A = [[0.0] * d for _ in range(d)]
        bv = [0.0] * d
        for comp, y, se_i, c in rows:
            w = float(c)
            x = feats(comp)
            nz = [i for i in range(d) if x[i]]
            for i in nz:
                xi = x[i] * w
                Ai = A[i]
                for j in nz:
                    Ai[j] += xi * x[j]
                bv[i] += xi * y
        tr = sum(A[i][i] for i in range(d)) / d
        beta = solve(A, bv, ridge=max(1e-9, tr * 1e-7))
        sw = rv = mse = 0.0
        for comp, y, se_i, c in rows:
            x = feats(comp)
            yh = sum(beta[i] * x[i] for i in range(d) if x[i])
            w = float(c)
            sw += w
            rv += w * (y - yh) ** 2
            mse += w * se_i * se_i
        MODEL[(H, pd)] = beta
        print(f'  size {H} pd {pd}: {FEAT[(H,pd)]:>4} dim={d:<4} tau_r={max(0.0, rv/sw - mse/sw)**0.5:.4f}', flush=True)
        TAU_R[(H, pd)] = max(0.0, rv / sw - mse / sw) ** 0.5


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


KS = [0.0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0]
print(f'simulating (sample={SAMPLE}, trials={TRIALS}) ...', flush=True)
print()

# per-m results: regret[k] and defer-rate[k], for both plain-X and shrunk-Z scoring
res = {m: {} for m in MIX}
for m in sorted(MIX):
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
                    x = quad_feats(s, K) if FEAT[(H, pd)] == 'quad' else (1.0,) + s
                    vh = sum(beta[i] * x[i] for i in range(len(x)) if x[i])
                    rec = (se[pd], vh)
                    anyc.append(rec)
                    if cnt[pd] > BOTTOM_FLOOR:
                        ref.append(rec)
            cand = ref if ref else anyc
            if len(cand) >= 2:
                sets.append((cand, TAU_R[(H, pd)]))
    tot = defaultdict(float)
    dfr = defaultdict(float)
    n = 0
    r = random.Random(555 + m)
    gauss = r.gauss
    for cand, tau_r in sets:
        N = len(cand)
        ses = [c[0] for c in cand]
        vh = [c[1] for c in cand]
        t2 = tau_r * tau_r
        lam = [t2 / (t2 + s * s) if (t2 + s * s) > 0 else 1.0 for s in ses]
        sig = [ses[i] * lam[i] ** 0.5 for i in range(N)]
        for _ in range(TRIALS):
            theta = [vh[i] + gauss(0.0, tau_r) for i in range(N)]
            X = [theta[i] + gauss(0.0, ses[i]) for i in range(N)]
            bt = min(theta)
            Z = [vh[i] + lam[i] * (X[i] - vh[i]) for i in range(N)]
            order = sorted(range(N), key=lambda i: Z[i])
            i1, i2 = order[0], order[1]
            margin = Z[i2] - Z[i1]
            sd_diff = (sig[i1] ** 2 + sig[i2] ** 2) ** 0.5
            rt = theta[i1] - bt                      # regret if we trust the table
            for k in KS:
                if sd_diff > 0 and margin < k * sd_diff:
                    tot[k] += L_LOOK
                    dfr[k] += 1.0
                else:
                    tot[k] += rt
            n += 1
    res[m] = ({k: tot[k] / n for k in KS}, {k: dfr[k] / n for k in KS}, len(sets))

print(f'{"k":>5} {"defer%":>7} {"regret":>8} {"vs look":>8} {"delta_pred":>11}   verdict')
best = None
for k in KS:
    reg = 0.0
    dfrac = 0.0
    wsum = 0.0
    for m, w in MIX.items():
        if not res.get(m):
            continue
        rr, dd, _ = res[m]
        reg += w * rr[k]
        dfrac += w * dd[k]
        wsum += w
    if not wsum:
        continue
    reg /= wsum
    dfrac /= wsum
    delta = F_FIRE * (reg - L_LOOK)
    verdict = 'PASS (beats lookahead)' if delta < 0 else 'reject'
    print(f'{k:>5.2f} {dfrac:>6.1%} {reg:>8.4f} {L_LOOK:>8.4f} {delta:>+11.4f}   {verdict}')
    if best is None or delta < best[1]:
        best = (k, delta, dfrac, reg)

print()
if best:
    k, delta, dfrac, reg = best
    print(f'BEST GATE k={k}: defer {dfrac:.1%} of bottoming decisions to the lookahead,')
    print(f'  blended regret {reg:.4f} t vs lookahead {L_LOOK:.4f} t')
    print(f'  predicted A/B delta {delta:+.4f} t  (was +0.0184 t)')
    print()
    print('  Deferred slots ship as an EMPTY bottom_keep target -> DecideBottom returns false ->')
    print('  AIEngine::BottomCards falls through to the lookahead. No engine change required.')
