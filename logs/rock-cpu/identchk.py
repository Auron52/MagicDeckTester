# identchk.py : dev (flags off) vs base, per job digest + units (ident.out vs ../m1.out)
import re
S="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu"
def rd(p):
    d={}
    for l in open(p):
        m=re.match(r'(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=\d+ units=(\d+)',l)
        if m: d[m.group(1)]=(m.group(2),m.group(3),m.group(4),m.group(5))
    return d
a=rd(S+"/m1.out"); b=rd(S+"/ab2/ident.out")
bad=[n for n in b if a.get(n)!=b[n]]
print(f"ident: {len(b)} jobs compared, {len(bad)} differ")
for n in bad: print("DIFF",n,a.get(n),b[n])
raise SystemExit(1 if bad or len(b)!=64 else 0)
