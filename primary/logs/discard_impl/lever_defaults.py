#!/usr/bin/env python3
"""List every heurarm slot added since origin (plus extra names given on the command line) with the default
its EnvOn reader(s) use. Prints NAME DEFAULT(S) FILE:LINE... so the final A/B can build an "all new levers off"
arm (default-ON levers -> false; default-OFF proof arms left alone).
Usage: lever_defaults.py [--base origin/phase-1-2-deck-analyzer] [EXTRA_MTG_NAME ...]"""
import re, subprocess, sys, glob, json

R = "/workspaces/MagicDeckTester"
args = sys.argv[1:]
base = "origin/phase-1-2-deck-analyzer"
if args[:1] == ["--base"]:
    base, args = args[1], args[2:]
diff = subprocess.run(["git", "-C", R, "diff", base + "..HEAD", "--", "src/ai/HeuristicArm.h"],
                      capture_output=True, text=True).stdout
names = re.findall(r'^\+\s+"(MTG_[A-Z0-9_]+)",', diff, re.M) + args
src = {}
for f in glob.glob(R + "/src/**/*.cpp", recursive=True) + glob.glob(R + "/src/**/*.h", recursive=True):
    src[f] = open(f, encoding="utf-8", errors="replace").read().split("\n")
out = {}
for n in names:
    hits = []
    for f, lines in src.items():
        for i, l in enumerate(lines):
            m = re.search(r'EnvOn\(\s*"%s"\s*(?:,\s*(true|false))?\s*\)' % n, l)
            if m:
                hits.append((m.group(1) or "false", "%s:%d" % (f.replace(R + "/", ""), i + 1)))
    defs = sorted(set(h[0] for h in hits))
    out[n] = defs
    print("%-40s %-12s %s" % (n, ",".join(defs) or "NO-READER", " ".join(h[1] for h in hits[:3])))
json.dump(out, open(R + "/logs/discard_impl/lever_defaults.json", "w"), indent=1)
