import json, os, sys, collections
from multiprocessing import Pool
T = "logs/overnight/traces"; MT = 8
SLOT = {41,42,43,44}                     # Ancestral Anger -> Fortifying Draught
OTHER = {47,48,49,54,55,59,60}           # every other substituted slot
def wt(g):
    t=(g.get("result") or {}).get("turn",-1)
    return MT+1 if (t is None or t<0 or t>MT) else t
def scan(args):
    arm,gi=args
    p=f"{T}/{arm}_gi{gi}.json"
    if not os.path.exists(p): return None
    g=json.load(open(p))
    seen={c["card"] for c in g.get("openingHand",[])}
    fists=0
    for t in g["turns"]:
        for a in t.get("actions",[]):
            ty=a.get("type")
            if ty=="DRAW": seen.add(a["card"])
            elif ty=="CAST_SPELL" and a.get("cardName")=="Fists of Flame": fists+=1
    return gi,wt(g),seen,fists
def load(arm,gis):
    with Pool(24) as pool:
        return {r[0]:r[1:] for r in pool.imap_unordered(scan,[(arm,g) for g in gis],chunksize=200) if r}
N=60000; gis=range(N)
A=load(sys.argv[1],gis); B=load(sys.argv[2],gis)
strata=collections.defaultdict(list)
for gi in A:
    if gi not in B: continue
    wa,sa,fa=A[gi]; wb,sb,fb=B[gi]
    both=sa|sb
    if not (both & SLOT): continue          # must draw the Anger/Draught slot
    if both & OTHER: continue               # and NO other substitution -> clean contrast
    strata[min(max(fa,fb),3)].append(wb-wa)
import statistics as st
print(f"{sys.argv[1]} vs {sys.argv[2]}   (games drawing ONLY the Ancestral Anger -> Fortifying Draught slot)")
print(f"{'Fists of Flame cast':>22s} {'games':>8s} {'delta':>9s} {'se':>8s} {'t':>7s}   {'favours':>8s}")
tot=[]
for k in sorted(strata):
    d=strata[k]; tot+=d
    m=st.mean(d); se=st.stdev(d)/len(d)**.5 if len(d)>1 else 0
    lab=f"{k}" if k<3 else "3+"
    print(f"{lab:>22s} {len(d):>8,} {m:>+9.4f} {se:>8.4f} {m/se if se else 0:>7.2f}   {'Draught' if m<0 else 'Anger':>8s}")
m=st.mean(tot); se=st.stdev(tot)/len(tot)**.5
print(f"{'ALL':>22s} {len(tot):>8,} {m:>+9.4f} {se:>8.4f} {m/se:>7.2f}")
