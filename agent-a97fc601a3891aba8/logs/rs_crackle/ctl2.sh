#!/usr/bin/env bash
# CASTABLE_MANA where the same-subset credit is actually consumed -- the SEARCH (d3 b10), not the d0 runner:
# 150 games x {on, off}, seeds 8360001+; plus logs of the two nhh-control slower games.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
python3 - <<'EOF'
import json, os
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
S = "/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8/ctl2"
jobs = []
for c in range(0, 150, 25):
    for arm, v in (("on", True), ("off", False)):
        jobs.append({"name": "ctld3_%s.c%d" % (arm, c), "deck": WT + "/decks/Hinata2/Hinata2.cod",
                     "profile": WT + "/decks/Hinata2/Hinata2.profile.json", "games": 25, "seed": 8360001 + c,
                     "game_index": c, "budget_ms": 10, "depth": 3, "ignore_play_profile": True,
                     "flags": {"MTG_HINATA_SUBSET_CREDIT": True, "MTG_HINATA_CASTABLE_MANA": v}})
os.makedirs(S, exist_ok=True)
json.dump({"jobs": jobs}, open(S + "/manifest.json", "w"), indent=1)
EOF
bash "$R/vbatch.sh" "$S/mtg_final" "$S/ctl2" 6
python3 - <<'EOF'
import re, math
from collections import defaultdict
w = defaultdict(dict); dg = {}
for line in open("/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8/ctl2/batch.err"):
    m = re.search(r"\[win\] job=ctld3_(on|off)\.c\d+ gi=(\d+) wt=(-?\d+)", line)
    if m:
        t = int(m.group(3)); w[m.group(1)][int(m.group(2))] = 9 if t < 0 else t
for line in open("/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8/ctl2/batch.out"):
    m = re.match(r"^ctld3_(on|off)\.c(\d+): played=\d+ avg=\S+ digest=(\S+) ms=\d+ units=(\d+)", line)
    if m: dg[(m.group(1), m.group(2))] = (m.group(3), int(m.group(4)))
gis = sorted(set(w["on"]) & set(w["off"]))
d = [w["on"][g] - w["off"][g] for g in gis]
print("d3 b10 SUBSET_CREDIT=1: n=%d on-off=%+d faster=%d slower=%d" % (len(d), sum(d), sum(1 for v in d if v < 0), sum(1 for v in d if v > 0)))
print("  slower:", [(g, w["off"][g], w["on"][g], 8360001 + g) for g in gis if w["on"][g] > w["off"][g]])
ch = sorted({c for (_, c) in dg}, key=int)
print("  digest-differing chunks:", sum(1 for c in ch if dg[("on", c)][0] != dg[("off", c)][0]), "of", len(ch),
      " units on/off: %.4f" % (sum(dg[("on", c)][1] for c in ch) / max(1, sum(dg[("off", c)][1] for c in ch))))
EOF
for pair in 322:8340323 779:8340780; do
  gi=${pair%%:*}; seed=${pair##*:}
  on=$(bash "$R/vgame.sh" "$S/mtg_final" d0 "$gi" "$seed" "$S/NH/${gi}_on" MTG_HINATA_HOLD_NEEDS_HINATA=1)
  off=$(bash "$R/vgame.sh" "$S/mtg_final" d0 "$gi" "$seed" "$S/NH/${gi}_off" MTG_HINATA_HOLD_NEEDS_HINATA=1 MTG_HINATA_CASTABLE_MANA=0)
  echo "nhh gi=$gi: on=$on off=$off"
done
