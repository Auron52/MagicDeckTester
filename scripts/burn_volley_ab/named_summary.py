#!/usr/bin/env python3
"""Summarise named_games.sh output: per game and arm, the win turn and every cleanup discard (turn, hand
before the discard, card shed). Usage: named_summary.py <outdir>"""
import glob, json, os, sys

SHORT = {"Mountain": "Mtn", "Lightning Bolt": "Bolt", "Shard Volley": "Volley", "Skullcrack": "Skull",
         "Searing Blaze": "Blaze", "Searing Blood": "Blood", "Light Up the Stage": "LUtS",
         "Goblin Guide": "GG", "Monastery Swiftspear": "Swift", "Eidolon of the Great Revel": "Eidolon"}
ARMS = ["generic", "cur", "k1", "k2", "f1", "f2", "rank", "late", "k1late", "k2late"]


def game(path):
    d = json.load(open(path))
    num = {}
    for name, ids in d["cardNumbering"].items():
        for i in ids:
            num[i] = SHORT.get(name, name)
    res = d["result"]
    wt = res.get("turn") if res.get("winner") == "player" else None
    sheds = []
    prev_hand = None
    for t in d["turns"]:
        b = t.get("boardAfter", {})
        if t.get("phase") == "CLEANUP":
            for a in t.get("actions", []):
                if a.get("type") == "DISCARD":
                    sheds.append("T%s[%s]-%s" % (t.get("turn"), " ".join(prev_hand or []),
                                                SHORT.get(a.get("cardName"), a.get("cardName"))))
        prev_hand = [num.get(c, str(c)) for c in b.get("hand", [])]
    return wt, sheds, d.get("seed")


def main(out):
    gis = sorted({int(os.path.basename(p).split("_")[0]) for p in glob.glob(os.path.join(out, "*_cur"))})
    for gi in gis:
        print("== gi %d" % gi)
        for arm in ARMS:
            f = glob.glob(os.path.join(out, "%d_%s" % (gi, arm), "*.json"))
            if not f:
                continue
            wt, sheds, seed = game(f[0])
            print("  %-8s seed=%s win=%-4s %s" % (arm, seed, wt if wt else "none", "  ".join(sheds)))


main(sys.argv[1])
