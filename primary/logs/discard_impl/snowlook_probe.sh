#!/bin/bash
# MTG_SNOW_LOOK_COLOR flip probe: lever ON (new default) vs =0 on the fresh Release build.
#   unbudgeted d3 -> must be BYTE-IDENTICAL (a sound prune changes no play when the budget is not a cap)
#   two other decks -> byte-identical by construction (only Snow holds Sheets / Augur)
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/snowlook
mkdir -p $O
python3 - "$O" <<'EOF'
import json, sys
sys.path.insert(0, "/workspaces/MagicDeckTester/logs/discard_impl")
import make_ab
o = sys.argv[1]
maps = make_ab.suite_maps()
def dk(k): return make_ab.EXTRA_FILES.get(k) or (maps["DECK_FILE"][k], maps["DECK_PROF"][k])
jobs = []
for k, games, depth, bud, seed in (("snow", 12, 3, 0, 5600001), ("snow", 40, 0, 0, 5610001),
                                   ("hinata", 30, 3, 10, 5620001), ("fivecolour", 30, 0, 0, 5630001)):
    for arm, fl in (("on", None), ("off", {"MTG_SNOW_LOOK_COLOR": False})):
        d = dk(k)
        j = {"name": "%s_d%d_b%d_%s" % (k, depth, bud, arm), "deck": d[0], "profile": d[1], "games": games,
             "seed": seed, "game_index": 0, "budget_ms": bud, "depth": depth, "ignore_play_profile": True}
        if fl: j["flags"] = fl
        jobs.append(j)
json.dump({"jobs": jobs}, open(o + "/probe.json", "w"), indent=1)
EOF
nice -n 19 $O/mtg_probe --batch $O/probe.json --threads 6 > $O/probe.out 2> $O/probe.err
python3 - "$O" <<'EOF'
import re, sys
o = sys.argv[1]
d = {}
for line in open(o + "/probe.out"):
    m = re.match(r"^(\S+)_(on|off): played=(\d+) avg=([\d.]+) digest=([0-9a-f]+)", line)
    if m: d.setdefault(m.group(1), {})[m.group(2)] = (m.group(4), m.group(5))
for k in sorted(d):
    a = d[k]
    print("%-22s on %s %s | off %s %s  %s" % (k, *a.get("on", ("-", "-")), *a.get("off", ("-", "-")),
          "IDENTICAL" if a.get("on") == a.get("off") else "DIFFERS"))
EOF
