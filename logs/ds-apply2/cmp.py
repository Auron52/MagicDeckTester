import re,sys,json
from collections import Counter
S='/workspaces/MagicDeckTester/build/scratch/ds-apply2/'
want=set()
for t in [3,4,5,6,7,8]:
    for j in json.load(open(S+'miniT%d.json'%t))['jobs']: want.add((j['seed'],'T%d'%t))
def done(d):
    return {int(l.split(':')[0][5:]) for l in open(S+d+'/batch.out') if l.startswith('ev__')}
def ev(d, games):
    e={}; f=Counter()
    for l in open(S+d+'/flags.err',errors='ignore'):
        m=re.match(r'^\[mana-oracle\] APPLY-DROP seed=(\d+) \S+ \S+ (T\d+) m\d plan=\{[^}]*\} \| engine dropped (\[[^\]]*\])',l)
        if not m: continue
        k=(int(m.group(1)),m.group(2))
        if k not in want or k[0] not in games: continue
        f[k]+=1; e[k]=m.group(3).split(', cast ')[-1].strip('[]').replace('cast ','')+(' NL' if 'not declared' in l else ' D')
    return e,f
# usage: cmp.py <base dir> <new dir> (both under $S2)
b,n=sys.argv[1],sys.argv[2]
g=done(b) & done(n)
eb,fb=ev(b,g); en,fn=ev(n,g)
print('games in both',len(g),'| base events',len(eb),'flags',sum(fb.values()),'| new events',len(en),'flags',sum(fn.values()))
print('fixed',Counter(eb[k] for k in eb if k not in en).most_common())
print('new  ',Counter(en[k] for k in en if k not in eb).most_common())
print('kept ',Counter(eb[k] for k in eb if k in en).most_common())
