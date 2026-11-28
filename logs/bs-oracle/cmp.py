import re,sys,collections
def load(tag):
    S='/workspaces/MagicDeckTester/build/scratch/bs-oracle/'
    res={}
    for l in open(S+tag+'.out'):
        m=re.match(r'(\S+): played=\d+ .*digest=(\S+) ms=(\d+) .*cpu=(\d+)',l)
        if m: res[m.group(1)]=(m.group(2),int(m.group(3)),int(m.group(4)))
    fl=collections.Counter(); per=collections.defaultdict(collections.Counter)
    summ=[]
    try:
        for l in open(S+tag+'.err',errors='replace'):
            m=re.match(r'\[mana-oracle\] ([A-Z][A-Z-]+) seed=\d+ job=(\S+) ',l)
            if m and m.group(1) not in ('DECISION','DECISION-COST','PLAN-COST','GATE-COST'):
                fl[m.group(1)]+=1; per[m.group(2)][m.group(1)]+=1
            if l.startswith('=== MANA ORACLE'): summ.append(l.strip()[:400])
    except FileNotFoundError: pass
    return res,fl,per,summ
if __name__=='__main__':
    a,b=sys.argv[1],sys.argv[2]
    ra,fa,pa,sa=load(a); rb,fb,pb,sb=load(b)
    tot=[0,0,0,0]; dd=0
    for j in sorted(ra, key=lambda x:(x.split('__g')[0], int(x.split('__g')[1]))):
        if j not in rb: print(j,'missing in',b); continue
        da,ma,ca=ra[j]; db,mb,cb=rb[j]
        same='=' if da==db else 'DIGEST-DIFF'
        if da!=db: dd+=1
        fdiff={k:(pa[j][k],pb[j][k]) for k in set(pa[j])|set(pb[j]) if pa[j][k]!=pb[j][k]}
        print('%-26s %s cpu %8d -> %8d  %s'%(j,same,ca,cb,' '.join('%s:%d->%d'%(k,v[0],v[1]) for k,v in sorted(fdiff.items()))))
        tot[0]+=ca; tot[1]+=cb
    print('total cpu %s=%d %s=%d ratio=%.3f digest_diffs=%d'%(a,tot[0],b,tot[1],tot[1]/max(1,tot[0]),dd))
    print('flags',a,dict(fa)); print('flags',b,dict(fb))
    for s in sa: print(a,s)
    for s in sb: print(b,s)
