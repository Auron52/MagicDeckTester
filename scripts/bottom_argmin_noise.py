#!/usr/bin/env python3
"""Quantify how much of the exhaustive bottoming table's argmin is NOISE rather than signal.

The bottoming decision is an argmin over the (7-m)-subcompositions of a kept hand, each cell
estimated from only R rollouts. Selecting the minimum of many noisy estimates is winner's-curse
biased: the pick is the luckiest cell, not the best one. This measures that regret directly from
the raw sidecar (per-cell count/sum/sumsq), using the project's own method (Gaussian draws from
the stored mean+variance -- same as RunAdaptiveBottomRegretSim).

Usage: bottom_argmin_noise.py <deck-dir-stem> [trials] [hands]
"""
import json, sys, random, itertools, math, collections

stem = sys.argv[1]
TRIALS = int(sys.argv[2]) if len(sys.argv) > 2 else 64
NHANDS = int(sys.argv[3]) if len(sys.argv) > 3 else 20000

raw  = json.load(open(f"{stem}.keepmodel.exhaustive.raw.json"))
prof = json.load(open(f"{stem}.keepmodel.exhaustive.profile.DISABLED.json"))
ek   = prof.get("exhaustive_keep") or prof
buckets = [b[0] if isinstance(b, list) else b for b in ek["buckets"]]
K = len(buckets); max_mull = ek["max_mull"]

# cells[H][comp] = [(mean,se,n) per pd]
cells = {}
for s in raw["sizes"]:
    H = s["H"]; t = {}
    for e in s["entries"]:
        st = []
        for pd in (0, 1):
            n = e["count"][pd]
            if n <= 0: st.append(None); continue
            m = e["sum"][pd] / n
            var = max(0.0, e["sumsq"][pd] / n - m * m) * n / max(1, n - 1)
            st.append((m, math.sqrt(var / n), n, math.sqrt(var)))
        t[tuple(e["comp"])] = st
    cells[H] = t

keep = {tuple(e["comp"]): e["keep"] for e in ek["entries"]}

# deck counts per bucket, from the decklist embedded in meta or recomputed from the .cod
import re, os
cod = None
for cand in (f"{stem}.cod", f"{stem}.txt"):
    if os.path.exists(cand): cod = cand; break
counts = [0]*K
txt = open(cod).read()
for m_ in re.finditer(r'number="(\d+)" name="([^"]+)"', txt):
    n, nm = int(m_.group(1)), m_.group(2)
    if nm in buckets: counts[buckets.index(nm)] += n
if sum(counts) == 0:  # plain .txt "4 Name"
    for ln in txt.splitlines():
        m_ = re.match(r'\s*(\d+)\s+(.+?)\s*$', ln)
        if m_ and m_.group(2) in buckets: counts[buckets.index(m_.group(2))] += int(m_.group(1))
DECK = []
for b, c in enumerate(counts): DECK += [b]*c
print(f"deck: {sum(counts)} cards, K={K} buckets, max_mull={max_mull}, R={raw['meta']['R']}, "
      f"gen depth={raw['meta']['depth']} budget={raw['meta']['budget_ms']}ms")

rng = random.Random(12345)
def draw7():
    h = rng.sample(DECK, 7); c = [0]*K
    for b in h: c[b] += 1
    return tuple(c)

def subcomps(comp, size):
    """All subcompositions of `comp` summing to `size`."""
    idx = [i for i in range(K) if comp[i] > 0]
    out = []
    def rec(j, left, cur):
        if left == 0: out.append(tuple(cur)); return
        if j >= len(idx): return
        i = idx[j]
        rem = sum(comp[idx[t]] for t in range(j, len(idx)))
        if rem < left: return
        for take in range(min(comp[i], left), -1, -1):
            cur[i] = take; rec(j+1, left-take, cur); cur[i] = 0
    rec(0, size, [0]*K)
    return out

mull_hist = collections.Counter()
bottom_events = []   # (comp, m, pd)
for pd in (0, 1):
    for _ in range(NHANDS):
        m = 0
        while True:
            c = draw7()
            eff = 7 - m
            if eff <= 1: mull_hist[m] += 1; break
            kf = keep.get(c)
            k = kf[min(m, max_mull)*2 + pd] if kf else 1
            if k:
                mull_hist[m] += 1
                if m > 0: bottom_events.append((c, m, pd))
                break
            m += 1

tot = sum(mull_hist.values())
print(f"\n--- mulligan distribution over {tot} simulated hands (table keep policy) ---")
for m in sorted(mull_hist):
    print(f"  kept at mull {m}: {mull_hist[m]:7d}  ({100*mull_hist[m]/tot:5.1f}%)")
print(f"  => P(at least one mulligan) = {100*(tot-mull_hist[0])/tot:.1f}%")

# ---- winner's-curse regret on the bottoming argmin ----
print(f"\n--- bottoming argmin: signal vs noise ({len(bottom_events)} bottoming decisions) ---")
agg = collections.defaultdict(lambda: [0,0.0,0.0,0.0,0.0,0])
seen = {}
for comp, m, pd in bottom_events:
    key = (comp, m, pd)
    if key not in seen:
        subs = [s for s in subcomps(comp, 7-m) if s in cells.get(7-m, {}) and cells[7-m][s][pd]]
        if len(subs) < 2: seen[key] = None
        else:
            mu = [cells[7-m][s][pd][0] for s in subs]
            se = [cells[7-m][s][pd][1] for s in subs]
            seen[key] = (mu, se)
    v = seen[key]
    if not v: continue
    mu, se = v
    N = len(mu); tmin = min(mu)
    spread = max(mu) - tmin
    reg = 0.0
    for _ in range(TRIALS):
        noisy = [mu[i] + rng.gauss(0, se[i]) for i in range(N)]
        pick = min(range(N), key=lambda i: noisy[i])
        reg += mu[pick] - tmin
    reg /= TRIALS
    a = agg[m]
    a[0]+=1; a[1]+=reg; a[2]+=spread; a[3]+=sum(se)/N; a[4]+=N; a[5]+=1

print(f"{'mull':>5}{'decisions':>11}{'cands':>8}{'mean se/cell':>14}{'true spread':>13}{'ARGMIN REGRET':>15}")
tw = tr = 0.0
for m in sorted(agg):
    n,rg,sp,se,nc,_ = agg[m]
    print(f"{m:>5}{n:>11}{nc/n:>8.1f}{se/n:>14.4f}{sp/n:>13.4f}{rg/n:>15.4f}")
    tw += n; tr += rg
print(f"\nweighted mean regret over all bottoming decisions: {tr/tw:+.4f} turns")
print(f"diluted over ALL games (bottoming happens in {100*len(bottom_events)/tot:.1f}%): "
      f"{tr/tw*len(bottom_events)/tot:+.4f} turns")
