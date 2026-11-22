# perfsum.py <dir> <ref_arm> <arm>...: per deck/mode instruction ratio arm/ref, units ratio, cpu ratio, and wins identity
import sys,re,os,glob,collections
D=sys.argv[1]; ref=sys.argv[2]; arms=sys.argv[3:]
def ins(p):
    for l in open(p):
        f=l.split(',')
        if len(f)>3 and f[2].startswith('instructions'): return int(f[0])
def job(p):
    for l in open(p):
        m=re.search(r'played=(\d+).*units=(\d+) cpu=(\d+)',l)
        if m: return int(m.group(1)),int(m.group(2)),int(m.group(3))
def wins(nm):
    p=f"{D}/logs/{nm}__o0.wins"
    return open(p).read() if os.path.exists(p) else None
decks=sorted({os.path.basename(p).split('__')[1] for p in glob.glob(D+'/*.perf')})
for mode in ('s','d0'):
    print(f"\n## {mode}\n\n| deck | games | " + " | ".join(f"{a} instr ratio | {a} units ratio | {a} cpu ratio | {a} same games" for a in arms)+" | ref Ginstr/game |")
    print("|---|---|"+"---|---|---|---|"*len(arms)+"---|")
    tot={a:[0,0] for a in arms}
    for d in decks:
        r=f"{ref}__{d}__{mode}"
        if not os.path.exists(f"{D}/{r}.perf"): continue
        ri=ins(f"{D}/{r}.perf"); rj=job(f"{D}/{r}.out")
        row=f"| {d} | {rj[0]} |"
        for a in arms:
            n=f"{a}__{d}__{mode}"
            if not os.path.exists(f"{D}/{n}.perf"): row+=" - | - | - | - |"; continue
            ai=ins(f"{D}/{n}.perf"); aj=job(f"{D}/{n}.out")
            tot[a][0]+=ri; tot[a][1]+=ai
            same='yes' if wins(r)==wins(n) else 'no'
            row+=f" {ai/ri:.4f} | {aj[1]/max(1,rj[1]):.4f} | {aj[2]/max(1,rj[2]):.4f} | {same} |"
        row+=f" {ri/rj[0]/1e9:.2f} |"
        print(row)
    print("| ALL (instr) | |"+"".join(f" {tot[a][1]/max(1,tot[a][0]):.4f} | | | |" for a in arms)+" |")
