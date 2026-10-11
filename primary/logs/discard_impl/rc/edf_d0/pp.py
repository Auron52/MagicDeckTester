#!/usr/bin/env python3
"""Pretty-print an mtg trace JSON: one line per phase entry, actions + hand/board after."""
import json
import sys


def name_map(d):
    m = {}
    for h in d.get("mulliganSequence", []):
        for c in h.get("hand", []):
            m[c["card"]] = c["cardName"]
    for t in d["turns"]:
        for a in t.get("actions", []):
            if "card" in a and "cardName" in a:
                m[a["card"]] = a["cardName"]
        b = t.get("boardAfter", {})
        for p in b.get("battlefield", []):
            m[p["card"]] = p["cardName"]
    return m


def fmt_action(a, m):
    s = a.get("type", "?")
    if "cardName" in a:
        s += " " + a["cardName"]
    extra = {k: v for k, v in a.items() if k not in ("type", "card", "cardName")}
    if extra:
        s += " " + json.dumps(extra, separators=(",", ":"))
    return s


def main(path):
    d = json.load(open(path))
    m = name_map(d)
    print("result", d["result"])
    for h in d.get("mulliganSequence", []):
        print("mull", h["attempt"], [c["cardName"] for c in h["hand"]], "kept" if h.get("kept") else "")
    for t in d["turns"]:
        b = t.get("boardAfter", {})
        acts = "; ".join(fmt_action(a, m) for a in t.get("actions", []) if a.get("type") != "UNTAP_SOURCES")
        nunt = sum(1 for a in t.get("actions", []) if a.get("type") == "UNTAP_SOURCES")
        if nunt: acts += f" [+{nunt} UNTAP_SOURCES]"
        print(f"T{t['turn']} {t['phase']}: {acts}")
        hand = [m.get(c, str(c)) for c in b.get("hand", [])]
        bf = []
        for p in b.get("battlefield", []):
            s = p["cardName"]
            if p.get("tapped"):
                s += "(T)"
            for k, v in p.items():
                if k not in ("card", "cardName", "isLand", "tapped"):
                    s += f"[{k}={v}]"
            bf.append(s)
        print(f"    hand({len(hand)}): {sorted(hand)}")
        print(f"    bf: {bf}")
        if b.get("graveyard"):
            print(f"    gy: {[m.get(c, str(c)) for c in b['graveyard']]}")
        print(f"    opp life {b.get('opponentLife')}")


if __name__ == "__main__":
    main(sys.argv[1])
