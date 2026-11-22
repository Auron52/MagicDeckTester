import re,sys,collections
R={}
for l in open(sys.argv[1]):
    m=re.match(r"(\S+): played=\d+ avg=([\d.]+) digest=(\S+) ms=(\d+) units=(\d+) cpu=(\d+)",l)
    if m: R[m.group(1)]=(float(m.group(2)),m.group(3),int(m.group(4)),int(m.group(5)),int(m.group(6)))
rows=collections.OrderedDict()
for k in R:
    tag,g,arm=k.rsplit('_',2)[0],k.split('_')[1],k.rsplit('_',1)[1]
    rows.setdefault(tag,set()).add(g)
MT=9.0
ARM=sys.argv[2] if len(sys.argv)>2 else "on"
slow={}
for tag,gs in rows.items():
    net=0;f=s=same=0;u=[0,0];c=[0,0];n=0;zero=0;dig=0;sl=[]
    for g in sorted(gs,key=lambda x:int(x[1:])):
        a=R.get(f"{tag}_{g}_off"); b=R.get(f"{tag}_{g}_{ARM}")
        if not a or not b: continue
        ta=a[0] or MT; tb=b[0] or MT
        if a[0]==0 or b[0]==0: zero+=1
        n+=1; d=tb-ta; net+=d
        if d<0: f+=1
        elif d>0: s+=1; sl.append((g,ta,tb))
        if a[1]!=b[1]: dig+=1
        u[0]+=a[3];u[1]+=b[3];c[0]+=a[4];c[1]+=b[4]
    slow[tag]=sl
    print(f"{tag}: n={n} net={net:+.0f} faster={f} slower={s} digest_differs={dig} unwon_rows={zero} units_ratio={u[1]/max(1,u[0]):.4f} cpu_ratio={c[1]/max(1,c[0]):.4f} mean_off={sum((R[f'{tag}_{g}_off'][0] or MT) for g in gs)/n:.4f}")
for tag,sl in slow.items():
    print(tag,"slower:",' '.join(f"{g}:{a:g}->{b:g}" for g,a,b in sl))
