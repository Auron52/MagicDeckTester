"""Flatten a --log-dir game JSON into readable lines (turn/phase/actions/board)."""
import json, sys, glob

def flat(path):
    d = json.load(open(path))
    num = {}
    for name, ns in d["cardNumbering"].items():
        for n in ns:
            num[n] = name
    out = []
    out.append("opening: " + ", ".join(c["cardName"] for c in d["openingHand"]))
    for t in d["turns"]:
        hdr = f"T{t['turn']} {t['phase']}"
        for a in t.get("actions", []):
            s = dict(a)
            typ = s.pop("type", "?")
            nm = s.pop("cardName", "")
            s.pop("card", None)
            extra = " ".join(f"{k}={v}" for k, v in s.items())
            out.append(f"{hdr}: {typ} {nm} {extra}".rstrip())
        b = t.get("boardAfter")
        if b:
            bf = ", ".join(p["cardName"] + ("(T)" if p.get("tapped") else "") for p in b["battlefield"])
            hand = ", ".join(num.get(n, str(n)) for n in b["hand"])
            gy = ", ".join(num.get(n, str(n)) if isinstance(n, int) else str(n) for n in b.get("graveyard", []))
            out.append(f"{hdr}: [board] {bf}")
            out.append(f"{hdr}: [hand {len(b['hand'])}] {hand}")
            out.append(f"{hdr}: [gy] {gy}  life={b.get('playerLife')} opp={b.get('opponentLife')}")
    out.append(f"result: {d['result']}")
    return out

if __name__ == "__main__":
    for p in glob.glob(sys.argv[1] + "/*.json"):
        print("\n".join(flat(p)))
