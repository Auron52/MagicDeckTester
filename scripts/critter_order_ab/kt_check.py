#!/usr/bin/env python3
"""Which suite decks already have a valid keep table cached for the PRIMARY / the WORKTREE sidecar path."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

KT = os.path.expanduser("~/.cache/mtg/keeptable")
ROOTS = {"primary": "/workspaces/MagicDeckTester", "wt": make_ab.WT}


def name(canon):
    h = 1469598103934665603
    for c in canon.encode():
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    stem = os.path.basename(canon)
    stem = "".join(c if (c.isalnum() or c in "._-") else "_" for c in stem)[:64]
    return "%s-%016x.keeptable" % (stem, h)


for key, deck, prof in [("critter",) + make_ab.CRITTER] + make_ab.suite_decks():
    row = [key]
    for r, root in ROOTS.items():
        side = os.path.join(root, os.path.dirname(prof), os.path.basename(prof).replace(".profile.json", ".keepmodel.exhaustive.profile.json.gz"))
        if not os.path.exists(side):
            side2 = side[:-3]
            row.append("%s:no-sidecar" % r if not os.path.exists(side2) else "%s:%s" % (r, os.path.exists(os.path.join(KT, name(os.path.realpath(side2))))))
            continue
        row.append("%s:%s" % (r, os.path.exists(os.path.join(KT, name(os.path.realpath(side))))))
    print(" ".join(row))
