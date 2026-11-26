import json,glob,sys
def acts(p):
    d=json.load(open(glob.glob(p+"/*.json")[0]))
    out=[]
    for t in d["turns"]:
        for a in t.get("actions",[]):
            s=" ".join(f"{k}={v}" for k,v in a.items() if k not in("card",))
            out.append(f"T{t['turn']} {t.get('phase')}: {s}")
        b=t.get("boardAfter",{})
        if isinstance(b,dict):
            opp=b.get("opponentLibrary", b.get("opponentLibrarySize"))
            out.append(f"T{t['turn']} {t.get('phase')} --board keys {sorted(b.keys())}" if len(out)<3 else f"   [lib? {opp}]")
    return d["result"],out
r0,a0=acts(sys.argv[1]); r1,a1=acts(sys.argv[2])
print(r0,r1)
import difflib
for l in difflib.unified_diff(a0,a1,lineterm="",n=40): print(l)
