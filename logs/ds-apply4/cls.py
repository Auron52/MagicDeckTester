import sys,re,glob
from collections import Counter
S='/workspaces/MagicDeckTester/build/scratch/ds-apply4'
def compact(l):
    m=re.match(r'\[ds2pay\] (.*?) paid=(\d) fc=\d cost (g\d+ B\d+ R\d+ G\d+) lu (g\d+ B\d+ R\d+ G\d+) nc=\S+ bound=\S+ unseen=(\d) hook=0 before (B\d+ R\d+ G\d+) fl (B\d+ R\d+ G\d+) C\d+ W\d+ ->(.*)',l)
    if not m: return l[:200]
    name,paid,cost,lu,un,bef,fl,board=m.groups()
    unt=[b for b in board.split() if not b.endswith('T') and not b.startswith('Ruby')]
    return '%-18s p%s u%s cost[%s] lu[%s] fl %s -> %s | untapped %s'%(name[:18],paid,un,cost,lu,bef,fl,' '.join(unt))
for f in sorted(glob.glob(S+'/rep_dbg_*.err')):
    L=[l.rstrip('\n') for l in open(f,errors='ignore') if l.startswith('[ds2pay]') and ' hook=0 ' in l]
    c=Counter()
    for i,l in enumerate(L):
        if l.startswith('[ds2pay] Dragonlord Kolaghan paid=0'):
            seq=[];j=i
            while j>=0 and i-j<=12:
                seq.append(L[j])
                if j<i and re.search(r'before B0 R0 G0',L[j]): break
                j-=1
            c[tuple(reversed(seq))]+=1
    print('#####',f.split('_')[-1][:-4],'failing Kolaghan applies:',sum(c.values()),'distinct',len(c))
    for k,v in c.most_common(int(sys.argv[1]) if len(sys.argv)>1 else 1):
        print('  x%d'%v)
        for s in k: print('    '+compact(s))
