# usage: cmp.py <batch.out> <logdir> <ref_arm> <arm> [--mt 8] [--list]
# per (deck, mode): pairs, faster, slower, net turns (arm - ref; unwon = mt+1), digest-only diffs, units & cpu ratio
import sys,re,os,collections
out,logdir,ref,arm=sys.argv[1:5]; mt=8; lst='--list' in sys.argv
if '--mt' in sys.argv: mt=int(sys.argv[sys.argv.index('--mt')+1])
job={}
for l in open(out):
    m=re.match(r'(\S+): played=(\d+) avg=\S+ digest=\S+ ms=(\d+) units=(\d+) cpu=(\d+)',l)
    if m: job[m.group(1)]=(int(m.group(2)),int(m.group(3)),int(m.group(4)),int(m.group(5)))
def wins(n):
    d={}
    p=os.path.join(logdir,n+'.wins')
    if not os.path.exists(p): return None
    for l in open(p):
        f=l.split()
        if len(f)>=2: d[int(f[0])]=(int(f[1]),f[2] if len(f)>2 else '')
    return d
agg=collections.defaultdict(lambda:{'n':0,'f':0,'s':0,'net':0,'dig':0,'ur':0,'ua':0,'cr':0,'ca':0,'slow':[],'fast':[]})
missing=0
for name in job:
    a,deck,mode,off=name.split('__')
    if a!=ref: continue
    on=arm+'__'+deck+'__'+mode+'__'+off
    if on not in job: missing+=1; continue
    A=wins(name); B=wins(on)
    if A is None or B is None: missing+=1; continue
    r=agg[(deck,mode)]
    r['ur']+=job[name][2]; r['ua']+=job[on][2]; r['cr']+=job[name][3]; r['ca']+=job[on][3]
    for g in A:
        if g not in B: continue
        x=A[g][0] if A[g][0]>0 else mt+1; y=B[g][0] if B[g][0]>0 else mt+1
        r['n']+=1; r['net']+=y-x
        if y<x: r['f']+=1; r['fast'].append((g,A[g][0],B[g][0]))
        elif y>x: r['s']+=1; r['slow'].append((g,A[g][0],B[g][0]))
        elif A[g][1]!=B[g][1]: r['dig']+=1
print(f"# {arm} vs {ref}  (missing jobs: {missing})")
for mode in ('s','d0'):
    print(f"\n## {'searched (play settings)' if mode=='s' else 'd0'}\n")
    print("| deck | pairs | faster | slower | net turns | digest-only | units ratio | CPU ratio |")
    print("|---|---|---|---|---|---|---|---|")
    T={'n':0,'f':0,'s':0,'net':0,'dig':0,'ur':0,'ua':0,'cr':0,'ca':0}
    for (deck,m),r in sorted(agg.items()):
        if m!=mode: continue
        for k in T: T[k]+=r[k]
        print(f"| {deck} | {r['n']} | {r['f']} | {r['s']} | {r['net']:+d} | {r['dig']} | {r['ua']/max(1,r['ur']):.4f} | {r['ca']/max(1,r['cr']):.4f} |")
    print(f"| ALL | {T['n']} | {T['f']} | {T['s']} | {T['net']:+d} | {T['dig']} | {T['ua']/max(1,T['ur']):.4f} | {T['ca']/max(1,T['cr']):.4f} |")
if lst:
    print()
    for (deck,m),r in sorted(agg.items()):
        for g,x,y in r['slow']: print('slower',deck,m,g,x,y)
        for g,x,y in r['fast']: print('faster',deck,m,g,x,y)
