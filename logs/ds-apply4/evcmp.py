import sys,re,os
S='/workspaces/MagicDeckTester/build/scratch/ds-apply4'
a,b=sys.argv[1],sys.argv[2]
ev=open(S+'/events.txt').read().split()
def stat(tag,s):
    f=S+'/rep_%s_%s'%(tag,s)
    if not os.path.exists(f+'.err'): return None
    k=n=0
    for l in open(f+'.err',errors='ignore'):
        if not l.startswith('[mana-oracle] APPLY-DROP seed') : continue
        n+=1
        d=re.search(r'engine dropped (\[[^\]]*\])',l)
        if d and 'Kolaghan' in d.group(1).split(', cast ')[-1] and 'not declared' not in l: k+=1
    t=[x for x in open(f+'.out') if x.startswith('avg')]
    return k,n,(t[0].split()[3] if t else '?')
tot=[0,0,0,0]
for e in ev:
    s=e.split(':')[0]; x=stat(a,s); y=stat(b,s)
    if x is None or y is None: continue
    tot[0]+=x[0];tot[1]+=y[0];tot[2]+=x[1];tot[3]+=y[1]
    print('%s %-6s KolD %3d -> %3d  allAD %3d -> %3d  win %s -> %s'%(s,e.split(':')[1],x[0],y[0],x[1],y[1],x[2],y[2]))
print('total KolD %d -> %d, all AD %d -> %d'%tuple(tot))
