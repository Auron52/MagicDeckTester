#!/usr/bin/env python3
"""feat.py <dirA> <dirB>: features of the first divergence between two game logs (A = off, B = lever arm).
Prints: first diff turn/phase, B's casts, legendary-dup (B casts a legendary creature whose name is on its board
before that main), own creatures that LEFT the battlefield during B's diverging main (sacrificed) vs A's."""
import glob, json, sys
cj = json.load(open("src/cards/data/cards.json")); cs = cj if isinstance(cj, list) else cj.get("cards", cj)
cs = cs if isinstance(cs, list) else list(cs.values())
LEG = {c["name"] for c in cs if "Legendary" in (c.get("type_line") or c.get("type") or "") or c.get("legendary") or
       "Legendary" in str(c.get("supertypes", ""))}
def load(d):
    g = json.load(open(glob.glob(d + "/*.json")[0])); return g
A, B = load(sys.argv[1]), load(sys.argv[2])
def steps(g):
    out = []; prev = None
    for t in g["turns"]:
        out.append((t["turn"], t["phase"], [a.get("cardName") or a.get("type") for a in t["actions"]], prev, t["boardAfter"]))
        prev = t["boardAfter"]
    return out
sa, sb = steps(A), steps(B)
for x, y in zip(sa, sb):
    if (x[0], x[1], x[2]) != (y[0], y[1], y[2]):
        T, ph = y[0], y[1]
        pre = [p["cardName"] for p in (y[3] or {}).get("battlefield", [])]
        casts = [a.get("cardName") for t in B["turns"] if t["turn"] == T and t["phase"] == ph for a in t["actions"]
                 if a.get("type") == "CAST_SPELL"]
        dup = [c for c in casts if c in LEG and c in pre]
        def lost(s):
            pre_b = {(p["card"]) for p in (s[3] or {}).get("battlefield", []) if not p.get("isLand")}
            post = {(p["card"]) for p in s[4].get("battlefield", [])}
            return len(pre_b - post)
        wa = A["result"].get("turn"); wb = B["result"].get("turn")
        print(f"T{T} {ph} wins {wa}->{wb} casts={casts} legdup={dup} lostA={lost(x)} lostB={lost(y)} A={x[2]}")
        break
