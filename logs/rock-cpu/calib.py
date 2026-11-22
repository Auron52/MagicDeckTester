# calib.py : MILLI = 1000 * (rung ns per work event, charged contexts window+playout) / (ns per budget unit), per deck;
# prints per-deck figures to calib.txt and the median MILLI on stdout
import re,statistics
D="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu/ab2"
ms=[]; out=open(D+"/calib.txt","w")
for k in ("niv","extraturns","hinata"):
    ctx={}
    for l in open(f"{D}/stats_{k}.err"):
        m=re.match(r'\[line-keep-ctx\]\s+(\S+)\s+trials=(\d+) ms=([\d.]+) work=(\d+) tables=(\d+) fired=(\d+)',l)
        if m: ctx[m.group(1)]=(int(m.group(2)),float(m.group(3)),int(m.group(4)),int(m.group(5)),int(m.group(6)))
    for l in open(f"{D}/stats_{k}.out"):
        m=re.search(r'played=(\d+).*units=(\d+) cpu=(\d+)',l)
        if m: g,u,c=int(m.group(1)),int(m.group(2)),int(m.group(3))
    nsu=c*1e6/u
    cms=ctx['window'][1]+ctx['playout'][1]; cw=ctx['window'][2]+ctx['playout'][2]
    nsw=cms*1e6/max(1,cw)
    milli=1000*nsw/nsu
    allms=sum(v[1] for v in ctx.values())
    print(f"{k}: games={g} units={u} cpu_ms={c} ns/unit={nsu:.0f} rung_ms_all={allms:.0f} ({100*allms/c:.1f}% of cpu) "
          f"charged_ctx_ms={cms:.0f} work={cw} ns/work={nsw:.0f} milli_1x={milli:.0f} "
          f"implied_units_per_game={cw*milli/1000/g:.0f} (units/g {u/g:.0f}) ctx={ctx}",file=out)
    ms.append(milli)
med=statistics.median(ms); print(f"median milli_1x={med:.0f}",file=out); out.close()
print(max(1,round(med)))
