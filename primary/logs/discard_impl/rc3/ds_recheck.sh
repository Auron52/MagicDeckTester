#!/bin/bash
# Dragonstorm's 22 ab3-slower games re-played on the CURRENT tree (USER order on) vs the old arm.
#   old = every ab3 FIX lever =0 + MTG_DRAGONSTORM_USER_ORDER=0 (the committed-GT behaviour)
#   new = head defaults (USER order, payable-order rituals ...)
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/rc3/dragonstorm
mkdir -p $O
python3 - "$O" <<'EOF'
import json, re, sys
o = sys.argv[1]
DK = ("decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json")
OFF = {l: False for l in ["MTG_DRAGONSTORM_ORDER_OPAQUE", "MTG_PAYABLE_ORDER_RITUALS", "MTG_COLOR_EXACT_SCALED",
       "MTG_ATTACK_TAP_NEED_EXACT", "MTG_ATTACK_TAP_ABILITY_DISCOUNT", "MTG_ROLLOUT_LAND_ACT_COLOR",
       "MTG_SOLVE_VERIFIED_WIN", "MTG_EDF_GORGE_RED_SIZING", "MTG_SOLVE_PENDING_AURA_EXACT",
       "MTG_DRAGONSTORM_USER_ORDER"]}
jobs = []
for line in open("/workspaces/MagicDeckTester/logs/discard_impl/rc3/slower_dragonstorm.txt"):
    m = re.search(r"dragonstorm\s+(\S+)\s+gi=(\d+)\s+old=(\d+) new=(\d+) seed=(\d+)", line)
    cell, gi, seed = m.group(1), int(m.group(2)), int(m.group(5))
    for arm, fl in (("old", OFF), ("new", None)):
        j = {"name": "ds_%s_%s.g%d" % (cell, arm, gi), "deck": DK[0], "profile": DK[1], "games": 1,
             "seed": seed, "game_index": gi, "budget_ms": {"ship": 20, "d3": 10, "d0": 0, "2hg": 10}[cell]}
        if cell != "ship": j["depth"] = {"d3": 3, "d0": 0, "2hg": 3}[cell]; j["ignore_play_profile"] = True
        if cell == "2hg": j["starting_life"] = 30; j["opponent_heads"] = 2
        if fl: j["flags"] = fl
        jobs.append(j)
json.dump({"jobs": jobs}, open(o + "/recheck.json", "w"), indent=1)
EOF
MTG_DUMP_WINS=1 nice -n 10 $R/logs/discard_impl/rc3/mtg_head_ccacda4e --batch $O/recheck.json --threads 4 > $O/recheck.out 2> $O/recheck.err
python3 - "$O" <<'EOF'
import re, sys
o = sys.argv[1]
res = {}
for line in open(o + "/recheck.err", errors="ignore"):
    m = re.search(r"\[win\] job=ds_(\S+)_(old|new)\.g(\d+) gi=\d+ wt=(-?\d+)", line)
    if m: res[(m.group(1), int(m.group(3)), m.group(2))] = 9 if int(m.group(4)) < 0 else int(m.group(4))
keys = sorted({(c, g) for (c, g, a) in res})
tot = 0
for c, g in keys:
    od, nw = res.get((c, g, "old")), res.get((c, g, "new"))
    tot += (nw - od) if od is not None and nw is not None else 0
    print("%-4s gi=%-5d old=%s new(head)=%s %s" % (c, g, od, nw, "STILL SLOWER" if nw > od else ("faster" if nw < od else "equal")))
print("sum(new-old) over these games:", tot)
EOF
