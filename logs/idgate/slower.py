#!/usr/bin/env python3
"""slower.py <ab manifest> <wins dir>: per game off vs on (loss=9); prints every SLOWER game with its repro fields."""
import os,glob,json,sys,collections
man,O=sys.argv[1],sys.argv[2]
M={j['name']:j for j in json.load(open(man))['jobs']}
def rd(p):
    r={}
    for l in open(p):
        a=l.split(); w=int(a[1]); r[int(a[0])]=9 if w<=0 else w
    return r
s=f=0; per=collections.defaultdict(lambda:[0,0,0,0])
for p in sorted(glob.glob(O+'/off__*.wins')):
    n=os.path.basename(p)[5:-5]; q=O+'/on__'+n+'.wins'
    if not os.path.exists(q): continue
    a,b=rd(p),rd(q); j=M['on__'+n]; deck=n.split('_')[0]
    gi0=j.get('game_index',0)
    for g in a:
        if g not in b: continue
        d=per[deck]; d[0]+=b[g]-a[g]; d[3]+=1
        if b[g]>a[g]:
            s+=1; d[1]+=1
            print(f"SLOWER {n} gi={g} {a[g]}->{b[g]} seed={j['seed']-gi0+g} depth={j.get('depth')} b={j.get('budget_ms')} deck={j['deck']!r} profile={j.get('profile')!r} heads={j.get('opponent_heads')} life={j.get('starting_life')} ipp={j.get('ignore_play_profile')}")
        elif b[g]<a[g]: f+=1; d[2]+=1
for k,v in sorted(per.items(), key=lambda x:-x[1][0]): 
    if v[1] or v[2]: print(f"DECK {k:18} net={v[0]:+d} slower={v[1]} faster={v[2]} games={v[3]}")
print('slower',s,'faster',f)
