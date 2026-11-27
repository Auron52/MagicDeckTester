import re,sys
S='/workspaces/MagicDeckTester/build/scratch/ds-apply4'
def load(f):
    d={}
    for l in open(f):
        m=re.match(r'(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=\d+ units=(\d+) cpu=(\d+)',l)
        if m: d[m.group(1)]=(m.group(3),m.group(4),int(m.group(5)),int(m.group(6)))
    return d
new=load(sys.argv[1]); ref=load(sys.argv[2])
ks=[k for k in new if k in ref]
print('jobs',len(ks),'digest/avg diffs',[k for k in ks if new[k][:2]!=ref[k][:2]],'units diffs',[(k,ref[k][2],new[k][2]) for k in ks if new[k][2]!=ref[k][2]])
