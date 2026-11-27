#!/usr/bin/env python3
"""Task 6 (2026-11-25): ONE pooled A/B of the final wip/beam-staged binary (F) against the baseline (B) -- the
binary the beam-staged report's A arm ran (b584c7a16, flags off: byte-identical to origin's committed tree).

Two binaries, so two manifests with the SAME jobs (names differ only in the arm tag), run back to back under one
hold of the box lock (run_ab.sh), each pooled over every deck. Rows: each deck at its play setting (value_play, no
depth / budget keys) std and 2HG (starting_life 30, opponent_heads 2). Pairing: both arms play the same
(seed, game_index=0) chunks, seed = BASE + 1000*k. Fresh seeds 49,100,000+ (CG/AL/DS) and 50,100,000+ (the decks
MTG_LEAF_KILL_ONLY_NOM2 newly enables); unused in logs/beam-staged and logs/beam-cpu.
usage: mk_ab.py <out> --kdecks key,key,...
"""
import argparse, json, os, re
R = "/workspaces/MagicDeckTester/.claude/worktrees/beam-staged"
ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("--kdecks", default="")
ap.add_argument("--games", type=int, default=5000); ap.add_argument("--games-2hg", type=int, default=2000)
ap.add_argument("--kgames", type=int, default=1000); ap.add_argument("--kgames-2hg", type=int, default=500)
ap.add_argument("--chunk", type=int, default=250); ap.add_argument("--kchunk", type=int, default=125)
ap.add_argument("--cost", default="", help="census dir: order jobs heaviest first by its K-off ms/game")
a = ap.parse_args()
txt = open(os.path.join(R, "test/regression_cases.sh")).read()
blk = txt[txt.index("declare -A DECK_FILE=("):]
blk = blk[:blk.index("\n)")]
F = {m.group(1): m.group(3) for m in re.finditer(r'^\s*\[(\w+)\]=("?)([^"\n]+)\2\s*$', blk, re.M)}
main = ["creature_giving", "antilife", "dragonstorm"]
kd = [k for k in a.kdecks.split(",") if k and k not in main]
jobs = {"B": [], "F": []}
rows = []
def emit(key, games, games2, chunk, base):
    k = 0
    f = F[key]; stem = os.path.splitext(f)[0]
    for suffix, hg, n in (("", False, games), ("2hg", True, games2)):
        row = f"{key}{suffix}"
        rows.append(dict(row=row, deck=key, games=n, hg=hg))
        for c in range(0, n, chunk):
            seed = base + 1000 * k; k += 1
            for arm in ("B", "F"):
                j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(R, f), "profile": os.path.join(R, stem + ".profile.json"),
                     "games": min(chunk, n - c), "seed": seed, "game_index": 0}
                if hg:
                    j.update({"starting_life": 30, "opponent_heads": 2})
                jobs[arm].append(j)
    return k
b = 49100000
for key in main:
    b += 1000 * emit(key, a.games, a.games_2hg, a.chunk, b)
b = 50100000
for key in kd:
    b += 1000 * emit(key, a.kgames, a.kgames_2hg, a.kchunk, b)
cost = {}
if a.cost:
    for ln in open(os.path.join(a.cost, "batch.out")):
        m = re.match(r"^(\w+?)_d3b10_K0_s\d+: played=(\d+) avg=\S+ digest=\S+ ms=(\d+)", ln)
        if m:
            cost[m.group(1)] = int(m.group(3)) / int(m.group(2))
def est(j):
    key = re.match(r"^(\w+?)(2hg)?_[BF]_s", j["name"]).group(1)
    return cost.get(key, 500) * j["games"] * (1.8 if "opponent_heads" in j else 1.0)
for arm in jobs:
    jobs[arm].sort(key=est, reverse=True)
print(f"est {sum(est(j) for j in jobs['B']) / 1000:.0f} core-s/arm (d3 b10 ms/game proxy)")
os.makedirs(a.out, exist_ok=True)
for arm, js in jobs.items():
    json.dump({"jobs": js}, open(os.path.join(a.out, f"ab_{arm}.json"), "w"), indent=1)
json.dump({"rows": rows}, open(os.path.join(a.out, "rows.json"), "w"), indent=1)
print(f"{len(rows)} rows, {len(jobs['B'])} jobs/arm, {sum(j['games'] for j in jobs['B'])} games/arm -> {a.out}")
