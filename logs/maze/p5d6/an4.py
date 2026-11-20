import re,collections
R={}
for f in ['/workspaces/MagicDeckTester/build/scratch/maze/p5d5/d8.out','s4.out']:
    for l in open(f):
        m=re.match(r"(\S+): played=\d+ avg=([\d.]+) digest=(\S+) ms=(\d+) units=(\d+)",l)
        if m: R[m.group(1)]=(float(m.group(2)) or 9.0, int(m.group(5)))
for g in ['mazesend-d5_g219','mazesend2hg-d5_g108','mazesend2hg-d5_g161','mazesend2hg-d5_g21']:
    row=[]; tr=tc=0; ok=True
    for s in (1,2,3,4):
        k=f"D8B_{g}" + ("" if s==1 else f"_s{s}")
        r=R.get(k+"_rev"); c=R.get(k+"_cand")
        row.append(f"s{s} rev {r[0] if r else '?'} cand {c[0] if c else '?'}")
        if r and c: tr+=r[0]; tc+=c[0]
        else: ok=False
    print(g, ' | '.join(row), f"| total rev {tr:.0f} cand {tc:.0f}{'' if ok else ' (partial)'}")
