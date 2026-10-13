#!/usr/bin/env python3
"""Byte-identity manifest: every dig deck + three digless decks, d3 b10 and the shipped cell, 50 games each.
Run it under (a) the base binary, (b) the new binary with MTG_DIG_SEARCHED_CHOICE=0, (c) the new binary as
shipped; (a) == (b) everywhere and (a) == (c) on the digless decks is the lever's identity contract.
  python3 make_ident.py --out DIR
  python3 make_ident.py --compare DIR_A DIR_B
"""
import argparse, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_dig_ab as M

EXTRA = [("burn", "decks/burn/burn.txt", "decks/burn/burn.profile.json"),
         ("hinata", "decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
         ("goblins", "decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json")]

ap = argparse.ArgumentParser()
ap.add_argument("--out")
ap.add_argument("--compare", nargs=2)
a = ap.parse_args()

if a.compare:
    def dig(d):
        out = {}
        for line in open(os.path.join(d, "batch.out"), errors="ignore"):
            m = re.match(r"^(\S+): played=\d+ avg=(\S+) digest=(\S+)", line)
            if m:
                out[m.group(1)] = (m.group(2), m.group(3))
        return out
    x, y = dig(a.compare[0]), dig(a.compare[1])
    same = diff = 0
    for k in sorted(set(x) & set(y)):
        if x[k][1] == y[k][1]:
            same += 1
        else:
            diff += 1
            print("DIFF %-28s %s %s  vs  %s %s" % (k, x[k][0], x[k][1], y[k][0], y[k][1]))
    print("identical %d, different %d, missing %d" % (same, diff, len(set(x) ^ set(y))))
    sys.exit(0)

jobs = []
for key, deck, prof in M.ALL_DECKS + EXTRA:
    for cell, depth, bud in (("d3", 3, 10), ("ship", None, 20)):
        j = {"name": "%s_%s" % (key, cell), "deck": os.path.join(M.PRIMARY, deck),
             "profile": os.path.join(M.PRIMARY, prof), "games": 50, "seed": 8600001, "game_index": 0,
             "budget_ms": bud}
        if depth is not None:
            j["depth"] = depth
            j["ignore_play_profile"] = True
        jobs.append(j)
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
print("wrote %d jobs" % len(jobs))
