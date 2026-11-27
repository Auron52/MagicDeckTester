#!/usr/bin/env python3
"""Task 3 census (2026-11-25): which suite decks does MTG_LEAF_KILL_ONLY_NOM2 (K) arm? One pooled batch on today's
binary: every test/regression_cases.sh deck, K forced off (flag 0) vs on (flag 1), d3 b10 ignore_play_profile, 20
games, seed 48,200,000+k. A deck whose K-on job spends different units from its K-off job armed the identity.
usage: mk_kcensus.py <out> [--verify --depth D --budget B --games N --decks k1,k2 --base S]"""
import argparse, json, os, re
R = "/workspaces/MagicDeckTester/.claude/worktrees/beam-staged"
ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("--verify", action="store_true", help="K-on arm only (for MTG_LEAF_KILL_ONLY_VERIFY)")
ap.add_argument("--depth", type=int, default=3); ap.add_argument("--budget", type=int, default=10)
ap.add_argument("--games", type=int, default=20); ap.add_argument("--decks", default="")
ap.add_argument("--base", type=int, default=48200000); ap.add_argument("--chunk", type=int, default=0)
a = ap.parse_args()
txt = open(os.path.join(R, "test/regression_cases.sh")).read()
blk = txt[txt.index("declare -A DECK_FILE=("):]
blk = blk[:blk.index("\n)")]
decks = {}
for m in re.finditer(r'^\s*\[(\w+)\]=("?)([^"\n]+)\2\s*$', blk, re.M):
    decks[m.group(1)] = m.group(3)
if a.decks:
    decks = {k: decks[k] for k in a.decks.split(",")}
arms = {"K1": {"MTG_LEAF_KILL_ONLY_NOM2": True}} if a.verify else {"K0": {"MTG_LEAF_KILL_ONLY_NOM2": False},
                                                                    "K1": {"MTG_LEAF_KILL_ONLY_NOM2": True}}
jobs, k = [], 0
chunk = a.chunk or a.games
for key, f in decks.items():
    stem = os.path.splitext(f)[0]
    prof = os.path.join(R, stem + ".profile.json")
    if not os.path.exists(prof):
        print("no profile:", key); continue
    for c in range(0, a.games, chunk):
        seed = a.base + 1000 * k; k += 1
        for arm, fl in arms.items():
            jobs.append({"name": f"{key}_d{a.depth}b{a.budget}_{arm}_s{seed}", "deck": os.path.join(R, f), "profile": prof,
                         "games": min(chunk, a.games - c), "seed": seed, "game_index": 0, "depth": a.depth,
                         "budget_ms": a.budget, "ignore_play_profile": True, "flags": dict(fl)})
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "m.json"), "w"), indent=1)
print(len(decks), "decks", len(jobs), "jobs")
