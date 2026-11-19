import re,collections,sys
stacks=[];cur=[]
for line in open(sys.argv[1]):
    line=line.rstrip()
    if not line.strip():
        if cur: stacks.append(cur); cur=[]
        continue
    parts=line.strip().split(None,1)
    cur.append(parts[1] if len(parts)>1 else '?')
if cur: stacks.append(cur)
N=len(stacks)
def short(s):
    s=s.replace('(anonymous namespace)::','').replace('manaoracle::','')
    return re.sub(r'\(.*','',s)[-60:]
for target in sys.argv[2:]:
    child=collections.Counter(); tot=0
    for st in stacks:
        idx=[i for i,f in enumerate(st) if target in f]
        if not idx: continue
        tot+=1; i=idx[0]
        child[short(st[i-1]) if i>0 else 'SELF']+=1
    print(target, tot, f"{100*tot/N:.1f}% of {N}")
    for k,v in child.most_common(12): print(f"   {v:5d} {k}")
