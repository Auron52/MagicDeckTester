#!/usr/bin/env python3
"""K fleet + SG50/SG50+K A/B manifests (2026-11-25).
Two binaries -> two manifests run back to back under ONE hold of the box lock (run.sh):
  O = origin e430cc585 (today);  S = wip/beam-staged2 (SG50 beam, rebased onto e430cc585).
Arms (per-job "flags", K = MTG_LEAF_KILL_ONLY_NOM2):  O0 = O, K default;  O1 = O, K=1;  S0 = S, K default;  S1 = S, K=1.
Rows at the deck's play setting (no depth/budget keys), std and 2HG (starting_life 30, opponent_heads 2).
Pairing: every arm of a row plays the same (seed, game_index=0) chunks, seed = BASE + 1000*k.
Fresh seeds: 57,100,000+ (CG/AL/DS: O0 O1 S0 S1, n=5000 + 2000 2HG) and 57,500,000+ (the other 29 K-armed decks: O0 O1).
usage: mk.py <out> [--kgames 1000 --kgames-2hg 500]"""
import argparse, json, os, re
R = "/workspaces/MagicDeckTester/.claude/worktrees/k-fleet"
ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--games", type=int, default=5000); ap.add_argument("--games-2hg", type=int, default=2000)
ap.add_argument("--kgames", type=int, default=1000); ap.add_argument("--kgames-2hg", type=int, default=500)
ap.add_argument("--chunk", type=int, default=250); ap.add_argument("--kchunk", type=int, default=125)
ap.add_argument("--kbase", type=int, default=57500000); ap.add_argument("--mbase", type=int, default=57100000)
ap.add_argument("--kdecks", default="")
ap.add_argument("--no-main", action="store_true")
a = ap.parse_args()
txt = open(os.path.join(R, "test/regression_cases.sh")).read()
blk = txt[txt.index("declare -A DECK_FILE=("):]; blk = blk[:blk.index("\n)")]
F = {m.group(1): m.group(3) for m in re.finditer(r'^\s*\[(\w+)\]=("?)([^"\n]+)\2\s*$', blk, re.M)}
ARMED = ("alchemist,auras,breaching,creature_giving,critter,dragons,dragonstorm,edf,elementals,fluctuator,galadriel,"
         "gods,jumbo,kithkin,kittyv3,knights,landfall,lhc,mazesend,merfolk,mill,minotaur,mirrorwing,pd,pirates,"
         "selesnyav2,slivers,soldiers,stompy,th,whiteknights").split(",")
main = [] if a.no_main else ["creature_giving", "antilife", "dragonstorm"]
kd = [k for k in (a.kdecks.split(",") if a.kdecks else ARMED) if k not in main]
cost = {}  # d3 b10 ms/game from the K census (ordering proxy only)
for ln in open("/workspaces/MagicDeckTester/.claude/worktrees/beam-staged/logs/beam-adopt/kcensus.txt"):
    m = re.match(r"^(\w+?)_d3b10\s+\S+\s+units.*ms/g (\d+)", ln)
    if m: cost[m.group(1)] = int(m.group(2))
jobs = {"O": [], "S": []}; rows = []
KF = {"MTG_LEAF_KILL_ONLY_NOM2": True}
def emit(key, games, games2, chunk, base, arms):
    k = 0; f = F[key]; stem = os.path.splitext(f)[0]
    for suffix, hg, n in (("", False, games), ("2hg", True, games2)):
        row = f"{key}{suffix}"
        rows.append(dict(row=row, deck=key, games=n, hg=hg, arms=arms))
        for c in range(0, n, chunk):
            seed = base + 1000 * k; k += 1
            for arm in arms:
                j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(R, f), "profile": os.path.join(R, stem + ".profile.json"),
                     "games": min(chunk, n - c), "seed": seed, "game_index": 0}
                if hg: j.update({"starting_life": 30, "opponent_heads": 2})
                if arm.endswith("1"): j["flags"] = dict(KF)
                jobs[arm[0]].append(j)
    return k
b = a.mbase
for key in main:
    b += 1000 * emit(key, a.games, a.games_2hg, a.chunk, b, ("O0", "O1", "S0", "S1"))
b = a.kbase
for key in kd:
    b += 1000 * emit(key, a.kgames, a.kgames_2hg, a.kchunk, b, ("O0", "O1"))
def est(j):
    key = re.match(r"^(\w+?)(2hg)?_[OS][01]_s", j["name"]).group(1)
    return cost.get(key, 500) * j["games"] * (1.8 if "opponent_heads" in j else 1.0)
os.makedirs(a.out, exist_ok=True)
for arm, js in jobs.items():
    js.sort(key=est, reverse=True)
    if js:
        json.dump({"jobs": js}, open(os.path.join(a.out, f"ab_{arm}.json"), "w"), indent=1)
    print(f"{arm}: {len(js)} jobs, {sum(j['games'] for j in js)} games, est {sum(est(j) for j in js)/1000:.0f} core-s (d3b10 proxy)")
json.dump({"rows": rows}, open(os.path.join(a.out, "rows.json"), "w"), indent=1)
print(f"{len(rows)} rows -> {a.out}")
