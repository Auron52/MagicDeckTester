#!/usr/bin/env python3
"""Slower-game follow-up manifest for the Maze hold-up A/B: slow_gen.py <ab_analyze output> <out.json> [nc]
Per slower game (fmt gi): d8 b0 on/off (recovery) and play-setting on/off with MTG_LINE_KEEP_CRIT=0 (does the
slowdown trace to the hold-up turning the line-keep rung off for its payments?). With `nc`: d8 b0 on/off only, to be
run under env MTG_NC_SEARCH=1 (not a per-job lever): the clairvoyance check for the games that do not recover."""
import json, re, sys
D = "decks/Maze's End/Maze's End.cod"; P = "decks/Maze's End/Maze's End.profile.json"
BASE = {"play": 31100000, "2hg": 31300000}
nc = len(sys.argv) > 3 and sys.argv[3] == "nc"
jobs = []
for l in open(sys.argv[1]):
    m = re.match(r'\s*SLOWER (\w+) gi=(\d+)', l)
    if not m: continue
    fmt, gi = m.group(1), int(m.group(2))
    extra = {"starting_life": 30, "opponent_heads": 2} if fmt == "2hg" else {}
    for arm in ("off", "on"):
        base = {"deck": D, "profile": P, "games": 1, "seed": BASE[fmt] + gi, "game_index": gi, **extra}
        fl = {} if arm == "on" else {"MTG_MAZE_HOLD_UP": False}
        if nc:
            jobs.append({**base, "name": f"maze_{fmt}_g{gi}_nc_{arm}", "depth": 8, "budget_ms": 0,
                         "ignore_play_profile": True, **({"flags": dict(fl)} if fl else {})})
            continue
        j = {**base, "name": f"maze_{fmt}_g{gi}_d8_{arm}", "depth": 8, "budget_ms": 0, "ignore_play_profile": True}
        if fl: j["flags"] = dict(fl)
        jobs.append(j)
        jobs.append({**base, "name": f"maze_{fmt}_g{gi}_lk0_{arm}", "flags": {**fl, "MTG_LINE_KEEP_CRIT": False}})
json.dump({"jobs": jobs}, open(sys.argv[2], "w"), indent=0)
print(f"{sys.argv[2]}: {len(jobs)} jobs")
