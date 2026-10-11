#!/usr/bin/env python3
"""Summarise [edf-loop] traces: per loop, iterations, draw-sink payments, other sink events."""
import re
import sys

loops = []
cur = None
for line in open(sys.argv[1]):
    if line.startswith("[edf-loop] begin"):
        cur = {"hdr": line.strip(), "draws": 0, "dmg_paid": 0, "events": {}, "enter": []}
        loops.append(cur)
        continue
    if cur is None:
        continue
    if line.startswith("[edf-loop] end"):
        cur["end"] = line.strip()
        continue
    m = re.match(r"\[edf-loop\]\s+(\S.*?)(?:\(|:|$)", line)
    if "draw-sink" in line and "paid" in line:
        cur["draws"] += 1
    key = re.sub(r"\d+", "#", line.strip()[:70])
    if "k=" not in line[:16]:
        cur["events"][key] = cur["events"].get(key, 0) + 1
    if " enter " in line:
        cur["enter"].append(line.strip()[:220])
for i, l in enumerate(loops):
    print(f"loop {i}: {l['hdr']} | {l.get('end')} | draw-sink paid={l['draws']}")
    for k, v in sorted(l["events"].items(), key=lambda kv: -kv[1])[:6]:
        print(f"      {v:3d}  {k}")
    if l["enter"]:
        print("      first enter:", l["enter"][0])
