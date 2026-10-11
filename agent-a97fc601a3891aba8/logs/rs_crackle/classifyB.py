#!/usr/bin/env python3
"""classifyB.py <LBdir> <slowB.txt> : mechanism of every lever-B slower game (bon slower than base).

For each game: the first turn the two arms' casts differ, whether the B-on arm cast a Crackle there that
the base arm held, Hinata's status at that point (in play / in hand), and what the base arm later did with
the held Crackle (turn, X, Hinata in play)."""
import glob, json, os, sys
from collections import Counter

HIN = "Hinata, Dawn-Crowned"
CRK = "Crackle with Power"


def load(d):
    p = sorted(glob.glob(os.path.join(d, "*.json")))
    return json.load(open(p[0])) if p else None


def turns(g):
    """turn -> dict(casts=[(name,X)], start_bf=[names], start_hand=[names])"""
    num = {}
    for nm, ids in g.get("cardNumbering", {}).items():
        for i in (ids if isinstance(ids, list) else [ids]):
            num[i] = nm
    out = {}
    for ph in g["turns"]:
        t = ph["turn"]
        r = out.setdefault(t, {"casts": [], "start_bf": None, "start_hand": None})
        b = ph.get("boardAfter", {})
        if ph.get("phase") == "DRAW" or r["start_bf"] is None:
            r["start_bf"] = [x.get("cardName") for x in b.get("battlefield", [])]
            r["start_hand"] = [num.get(h, h) for h in b.get("hand", [])]
        for a in ph.get("actions", []):
            if a.get("type") == "CAST_SPELL":
                r["casts"].append((a.get("cardName"), a.get("chosenX") or 0))
    return out


def main():
    root, lst = sys.argv[1], sys.argv[2]
    cats = Counter()
    rows = []
    for line in open(lst):
        _, cell, gi, seed = line.split()
        on, off = load(os.path.join(root, "%s_%s_bon" % (cell, gi))), load(os.path.join(root, "%s_%s_base" % (cell, gi)))
        if not on or not off:
            rows.append("%s gi=%s: missing log" % (cell, gi)); cats["missing"] += 1; continue
        T_on, T_off = turns(on), turns(off)
        wt_on = (on.get("result") or {}).get("turn"); wt_off = (off.get("result") or {}).get("turn")
        div = None
        for t in sorted(set(T_on) | set(T_off)):
            if T_on.get(t, {}).get("casts") != T_off.get(t, {}).get("casts"):
                div = t; break
        if div is None:
            rows.append("%s gi=%s: no cast divergence (on=%s off=%s)" % (cell, gi, wt_on, wt_off)); cats["no-cast-divergence"] += 1; continue
        a, b = T_on.get(div, {"casts": [], "start_bf": [], "start_hand": []}), T_off.get(div, {"casts": [], "start_bf": [], "start_hand": []})
        crk_on = [x for n, x in a["casts"] if n == CRK]
        crk_off = [x for n, x in b["casts"] if n == CRK]
        hin_play = HIN in (a["start_bf"] or [])
        hin_hand = HIN in (a["start_hand"] or [])
        if len(crk_on) > len(crk_off):
            # what did base do with the held Crackle later?
            later = None
            for t in sorted(T_off):
                if t < div:
                    continue
                for n, x in T_off[t]["casts"]:
                    if n == CRK and (t > div or len([1 for nn, _ in T_off[t]["casts"] if nn == CRK]) > len(crk_off)):
                        later = (t, x, HIN in (T_off[t]["start_bf"] or [])); break
                if later:
                    break
            if hin_play:
                cat = "chip-with-Hinata-in-play(?)"
            elif later and later[2]:
                cat = "B1 chip spent before Hinata; base fired the held Crackle WITH Hinata later"
            elif later:
                cat = "B2 chip spent; base fired the held Crackle later, still no Hinata (bigger X)"
            else:
                cat = "B3 chip spent; base never cast it (won another way)"
            cats[cat] += 1
            rows.append("%s gi=%s T%d: on casts Crackle X=%s (Hinata %s); base %s -> win T%s vs T%s [%s]" % (
                cell, gi, div, crk_on, "in hand" if hin_hand else "absent",
                ("fires it T%d X=%d Hinata in play=%s" % later) if later else "never casts it", wt_on, wt_off, cat.split(" ")[0]))
        else:
            cat = "B4 first divergence is not a Crackle cast (lookahead / order)"
            cats[cat] += 1
            rows.append("%s gi=%s T%d: on=%s off=%s -> win T%s vs T%s [B4]" % (cell, gi, div, a["casts"], b["casts"], wt_on, wt_off))
    for r in rows:
        print(r)
    print("\nSUMMARY")
    for k, v in cats.most_common():
        print("  %3d  %s" % (v, k))


main()
