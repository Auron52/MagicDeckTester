#!/usr/bin/env python3
"""casts.py <logdir|json> ... : one line per turn -- the spells cast (X), opp life after, hand at end."""
import glob, json, os, sys


def load(p):
    if os.path.isdir(p):
        p = sorted(glob.glob(os.path.join(p, "*.json")))[0]
    return json.load(open(p))


for p in sys.argv[1:]:
    g = load(p)
    print("==", os.path.basename(p.rstrip("/")), "result", g.get("result", {}).get("turn"))
    rows = {}
    for ph in g["turns"]:
        r = rows.setdefault(ph["turn"], {"casts": [], "opp": None, "hand": None})
        for a in ph.get("actions", []):
            if a.get("type") == "CAST_SPELL":
                nm = a.get("cardName", "?")
                x = a.get("chosenX")
                r["casts"].append("%s%s" % (nm.split(",")[0].replace(" with Power", ""), ("(X=%s)" % x) if x else ""))
        b = ph.get("boardAfter", {})
        r["opp"] = b.get("opponentLife", r["opp"])
        num = {}
        for nm, ids in g.get("cardNumbering", {}).items():
            for i in (ids if isinstance(ids, list) else [ids]):
                num[i] = nm
        r["hand"] = [num.get(h, h) for h in b.get("hand", [])]
    for t in sorted(rows):
        r = rows[t]
        print("  T%-2d opp=%-4s %-70s hand=%s" % (t, r["opp"], ", ".join(r["casts"]), r["hand"]))
