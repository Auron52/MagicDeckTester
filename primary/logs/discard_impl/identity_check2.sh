#!/bin/bash
# Byte-identity spot check after the Dragonstorm USER-order integration: the new Release binary vs the
# d6a760e5 PGO binary on decks the change must not touch, and on Dragonstorm with the lever =0.
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/identity2
mkdir -p $O
python3 - "$O" <<'EOF'
import json, sys
sys.path.insert(0, "/workspaces/MagicDeckTester/logs/discard_impl")
import make_ab
o = sys.argv[1]
maps = make_ab.suite_maps()
def dk(k): return make_ab.EXTRA_FILES.get(k) or (maps["DECK_FILE"][k], maps["DECK_PROF"][k])
def job(name, k, games, seed, bud, depth=None, flags=None):
    d = dk(k)
    j = {"name": name, "deck": d[0], "profile": d[1], "games": games, "seed": seed, "game_index": 0, "budget_ms": bud}
    if depth is not None: j["depth"] = depth; j["ignore_play_profile"] = True
    if flags: j["flags"] = flags
    return j
jobs = []
seed = 4200001
for k in ("hinata", "breaching", "mirrorwing", "burn", "fungus", "angels", "edf"):
    if k not in maps["DECK_FILE"] and k not in make_ab.EXTRA_FILES: print("skip", k); continue
    jobs.append(job(k + "_d0", k, 60, seed, 0, 0)); seed += 1000
    jobs.append(job(k + "_d3", k, 30, seed, 10, 3)); seed += 1000
old = jobs + [job("ds_d0", "dragonstorm", 120, 4300001, 0, 0), job("ds_d3", "dragonstorm", 40, 4310001, 10, 3)]
new = jobs + [job("ds_d0", "dragonstorm", 120, 4300001, 0, 0, {"MTG_DRAGONSTORM_USER_ORDER": False}),
              job("ds_d3", "dragonstorm", 40, 4310001, 10, 3, {"MTG_DRAGONSTORM_USER_ORDER": False})]
json.dump({"jobs": old}, open(o + "/old.json", "w"), indent=1)
json.dump({"jobs": new}, open(o + "/new.json", "w"), indent=1)
EOF
nice -n 10 $R/build/PGO/mtg --batch $O/old.json --threads 3 > $O/old.out 2> $O/old.err
nice -n 10 $R/build/Release/mtg --batch $O/new.json --threads 3 > $O/new.out 2> $O/new.err
python3 - "$O" <<'EOF'
import re, sys
o = sys.argv[1]
def rd(p):
    d = {}
    for line in open(p):
        m = re.match(r"^(\S+): played=(\d+) avg=([\d.]+) digest=([0-9a-f]+)", line)
        if m: d[m.group(1)] = (m.group(3), m.group(4))
    return d
a, b = rd(o + "/old.out"), rd(o + "/new.out")
for k in sorted(a):
    print("%-14s old %s %s | new %s %s  %s" % (k, a[k][0], a[k][1], b.get(k, ("-", "-"))[0], b.get(k, ("-", "-"))[1],
          "IDENTICAL" if b.get(k) == a[k] else "DIFFERS"))
EOF
