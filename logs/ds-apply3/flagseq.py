import sys,re,glob
from collections import Counter
def compact(l):
    m=re.match(r'\[ds2pay\] (.*?) paid=(\d) fc=\d cost (g\d+ B\d+ R\d+ G\d+) lu (g\d+ B\d+ R\d+ G\d+) nc=\S+ bound=\S+ unseen=(\d) hook=0 before (B\d+ R\d+ G\d+) fl (B\d+ R\d+ G\d+) C\d+ W\d+ ->(.*)',l)
    if not m: return l[:200]
    name,paid,cost,lu,un,bef,fl,board=m.groups()
    unt=[b for b in board.split() if not b.endswith('T') and not b.startswith('Ruby')]
    return '%-18s p%s u%s cost[%s] lu[%s] fl %s -> %s | untapped %s'%(name[:18],paid,un,cost,lu,bef,fl,' '.join(unt))
S='/workspaces/MagicDeckTester/build/scratch/ds-apply3'
files=sys.argv[1:] or sorted(glob.glob(S+'/rep_dbg_*.err'))
for f in files:
    L=[l.rstrip('\n') for l in open(f,errors='ignore')]
    out=Counter()
    for i,l in enumerate(L):
        if not l.startswith('[mana-oracle] APPLY-DROP seed'): continue
        if 'Kolaghan' not in l.split('engine dropped')[1].split(']')[0] or 'not declared' in l: continue
        plan=re.search(r'plan=\{([^}]*)\}',l).group(1).replace('cast:','')
        j=i-1
        while j>=0 and not (L[j].startswith('[ds2pay]') and ' hook=0 ' in L[j]): j-=1
        blk=[]
        while j>=0 and L[j].startswith('[ds2pay]') and ' hook=0 ' in L[j]: blk.append(L[j]); j-=1
        blk.reverse()
        out[(plan,tuple(compact(b) for b in blk))]+=1
    print('#####',f.split('_')[-1][:-4])
    for (plan,blk),v in out.most_common(2):
        print('  x%d plan %s'%(v,plan))
        for b in blk[-8:]: print('     '+b)
