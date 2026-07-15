#!/usr/bin/env python3
"""Escalation-outcome separability: can rich features predict a NO-OP escalation better than
committed-depth alone? If not, a confidence gate collapses to value_trust_depth (already shipped).

Row: taken wt_changed turn committed gap est_wt <46 midgame feats>
Target y = 1 if NO-OP (wt_changed==0, safe to skip), 0 if REAL (must escalate).
Compares DEPTH-ONLY {committed,gap,turn,est_wt} vs FULL {+46 feats} by 5-fold rank-AUC, and prints
a precision-at-skip curve (of the escalations we flag skippable, what frac were actually REAL = the
false-negative cost) on out-of-fold predictions.  Pure python (no numpy/sklearn).
"""
import sys, math, random

def load(path):
    X, y = [], []
    for ln in open(path):
        p = ln.split()
        if len(p) < 7: continue
        v = list(map(float, p))
        wt_changed = int(v[1])
        turn, committed, gap, est = v[2], v[3], v[4], v[5]
        feats = v[6:]
        X.append([committed, gap, turn, est] + feats)
        y.append(1 - wt_changed)          # 1 = no-op (skippable)
    return X, y

DEPTH_IDX = [0, 1, 2, 3]                    # committed, gap, turn, est_wt
def full_idx(n): return list(range(n))

def standardize(cols, idx):
    # returns (mean,std) per selected idx computed on given rows
    m = [0.0]*len(idx); s = [0.0]*len(idx)
    n = len(cols)
    for j,i in enumerate(idx):
        vals = [r[i] for r in cols]
        mu = sum(vals)/n
        var = sum((x-mu)**2 for x in vals)/max(1,n-1)
        m[j] = mu; s[j] = math.sqrt(var) if var > 1e-12 else 1.0
    return m, s

def design(rows, idx, m, s):
    out = []
    for r in rows:
        out.append([1.0] + [ (r[i]-m[j])/s[j] for j,i in enumerate(idx) ])
    return out

def train_logistic(X, y, iters=400, lr=0.3, l2=1.0):
    d = len(X[0]); w = [0.0]*d; n = len(X)
    for _ in range(iters):
        g = [0.0]*d
        for xi, yi in zip(X, y):
            z = sum(w[k]*xi[k] for k in range(d))
            p = 1.0/(1.0+math.exp(-max(-30,min(30,z))))
            e = p - yi
            for k in range(d): g[k] += e*xi[k]
        for k in range(d):
            reg = 0.0 if k==0 else l2*w[k]
            w[k] -= lr*(g[k]/n + reg/n)
    return w

def predict(X, w):
    out=[]
    for xi in X:
        z = sum(w[k]*xi[k] for k in range(len(w)))
        out.append(1.0/(1.0+math.exp(-max(-30,min(30,z)))))
    return out

def auc(scores, labels):
    pairs = sorted(zip(scores, labels))
    # rank-sum for positive class (label==1)
    npos = sum(labels); nneg = len(labels)-npos
    if npos==0 or nneg==0: return float('nan')
    # assign average ranks
    ranks=[0.0]*len(pairs); i=0
    while i < len(pairs):
        j=i
        while j+1 < len(pairs) and pairs[j+1][0]==pairs[i][0]: j+=1
        avg=(i+j)/2.0 + 1
        for k in range(i,j+1): ranks[k]=avg
        i=j+1
    rsum = sum(ranks[k] for k in range(len(pairs)) if pairs[k][1]==1)
    return (rsum - npos*(npos+1)/2.0)/(npos*nneg)

def cv(X, y, idx, folds=5, seed=0):
    n=len(X); order=list(range(n)); random.Random(seed).shuffle(order)
    oof_s=[0.0]*n; oof_y=[0]*n
    for f in range(folds):
        te=[order[k] for k in range(n) if k%folds==f]; tr=[order[k] for k in range(n) if k%folds!=f]
        Rtr=[X[i] for i in tr]; Rte=[X[i] for i in te]
        m,s=standardize(Rtr, idx)
        Xtr=design(Rtr, idx, m, s); Xte=design(Rte, idx, m, s)
        w=train_logistic(Xtr, [y[i] for i in tr])
        ps=predict(Xte, w)
        for k,i in enumerate(te): oof_s[i]=ps[k]; oof_y[i]=y[i]
    return oof_s, oof_y

def prec_at_skip(scores, labels):
    # sort by predicted no-op prob desc; skip top-k; report false-neg (real skipped) rate
    order=sorted(range(len(scores)), key=lambda i:-scores[i])
    out=[]
    for frac in (0.2,0.4,0.5,0.6,0.8):
        k=int(len(order)*frac)
        sk=order[:k]
        real_skipped=sum(1 for i in sk if labels[i]==0)
        out.append((frac, k, real_skipped, real_skipped/max(1,k)))
    return out

def main():
    for path in sys.argv[1:]:
        X,y=load(path)
        n=len(X); npos=sum(y)
        nfeat=len(X[0])
        print(f"\n=== {path}  rows={n}  no-op={npos} ({npos/n:.0%})  real={n-npos} ===")
        if n < 40 or npos in (0,n):
            print("  too few / degenerate -> skip"); continue
        sd,syd=cv(X,y,DEPTH_IDX)
        sf,syf=cv(X,y,full_idx(nfeat))
        print(f"  AUC depth-only {{committed,gap,turn,est}} = {auc(sd,syd):.3f}")
        print(f"  AUC full       {{+46 feats}}              = {auc(sf,syf):.3f}")
        print("  precision-at-skip (full model): frac  skipped  real-skipped  false-neg-rate")
        for frac,k,rs,fr in prec_at_skip(sf,syf):
            print(f"     skip {frac:.0%}:  {k:4d}   {rs:4d}   {fr:.1%}")

if __name__=="__main__":
    main()
