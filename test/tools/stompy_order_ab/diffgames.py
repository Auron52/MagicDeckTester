#!/usr/bin/env python3
"""Per-turn old-vs-new diff of single games from two --game-trace-dir directories.

usage: diffgames.py <old_trace_dir> <new_trace_dir> <slower.txt> [--max N]
Trace files are <job>_gi<N>.json where job = <deck>.<cell>.x.g<gi>.
"""
import glob
import json
import os
import sys

old_dir, new_dir, slower = sys.argv[1], sys.argv[2], sys.argv[3]
mx = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 10 ** 9


def lines(path):
    j = json.load(open(path))
    out = {}
    hand = j.get("openingHand", [])
    for t in j["turns"]:
        key = t["turn"]
        segs = out.setdefault(key, [])
        for a in t["actions"]:
            ty = a["type"]
            if ty == "DRAW":
                segs.append("draw:" + str(a.get("cardName")))
            elif ty == "PLAY_LAND":
                segs.append("land:" + str(a.get("cardName")))
            elif ty == "CAST_SPELL":
                segs.append("cast:" + str(a.get("cardName")))
            elif ty == "ATTACK":
                segs.append("atk:%s->%s" % (a.get("damage"), a.get("oppLife")))
            elif ty in ("ACTIVATE", "ABILITY", "ACTIVATE_ABILITY"):
                segs.append("act:" + str(a.get("cardName")) + ":" + str(a.get("effect", a.get("detail", ""))))
            elif ty == "REVEAL":
                segs.append("rev:" + str(a.get("source", "")) + ":" + ",".join(
                    "%s%s" % (n, "*" if k else "") for n, k in zip(a.get("names", []), a.get("keptFlags", []))))
            else:
                segs.append(ty.lower() + ":" + str(a.get("cardName", "")))
    return j.get("mulliganSequence"), hand, out


n = 0
for row in open(slower):
    f = row.split()
    if not f:
        continue
    deck, cell, gi, wo, wn = f[0], f[1], int(f[2]), f[3], f[4]
    pat = "%s.%s.*.g%d_gi%d.json" % (deck, cell, gi, gi)
    po = glob.glob(os.path.join(old_dir, pat))
    pn = glob.glob(os.path.join(new_dir, pat))
    if not po or not pn:
        print("== %s %s gi%d: missing trace (%d/%d)" % (deck, cell, gi, len(po), len(pn)))
        continue
    mo, ho, lo = lines(po[0])
    mn, hn, ln = lines(pn[0])
    print("== %s %s gi%d  old T%s -> new T%s   mull old=%s new=%s %s" %
          (deck, cell, gi, wo, wn, mo, mn, "" if ho == hn else "KEPT HAND DIFFERS"))
    for t in sorted(set(lo) | set(ln)):
        a, b = lo.get(t, []), ln.get(t, [])
        mark = "  " if a == b else "**"
        print("%s T%d old: %s" % (mark, t, " ".join(a)))
        if a != b:
            print("%s T%d new: %s" % (mark, t, " ".join(b)))
    n += 1
    if n >= mx:
        break
