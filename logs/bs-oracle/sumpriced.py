import re,sys,collections
for t in sys.argv[1:]:
    tot=collections.Counter(); kinds=collections.Counter()
    for l in open('/workspaces/MagicDeckTester/build/scratch/bs-oracle/'+t+'.err',errors='replace'):
        m=re.search(r'job=(\S+?)__g\d+ .*priced_ms=(\d+)',l)
        if not m: continue
        tot[m.group(1)]+=int(m.group(2))
        for k,v in re.findall(r' (\w+)=(\d+)ms\(',l): kinds[k]+=int(v)
    print(t,dict(tot),'kinds',dict(kinds))
