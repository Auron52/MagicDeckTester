#!/usr/bin/env python3
"""Mill / K boundary-fix A/B manifest (2026-11-26). ONE pooled batch, one binary (wip/mill-k).
Arms (per-job flags):  T = today (K default: provider opt-in only);  K = MTG_LEAF_KILL_ONLY_NOM2=1;
                       KF = K + MTG_KO_BOUNDARY_SIM=1 (the fix).
Mill: T, K, KF at --mill-games std + --mill-games-2hg 2HG (seed base 59,000,000).
Every other K-armed deck: K and KF only at --games std + --games-2hg 2HG (seed base 59,500,000) -- the fix charges no
unit, so KF == K byte-for-byte (digest + units) wherever no boundary win exists; this row set checks that.
Pairing: all arms of a row play the same (seed, game_index=0) chunks, seed = base + 1000*k (game i: seed+i, gi i).
usage: mk.py <out>"""
import argparse, json, os, re
R = "/workspaces/MagicDeckTester/.claude/worktrees/mill-k"
ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--mill-games", type=int, default=8000); ap.add_argument("--mill-games-2hg", type=int, default=4000)
ap.add_argument("--games", type=int, default=500); ap.add_argument("--games-2hg", type=int, default=250)
ap.add_argument("--chunk", type=int, default=125)
ap.add_argument("--mbase", type=int, default=59000000); ap.add_argument("--obase", type=int, default=59500000)
ap.add_argument("--no-others", action="store_true")
a = ap.parse_args()
txt = open(os.path.join(R, "test/regression_cases.sh")).read()
blk = txt[txt.index("declare -A DECK_FILE=("):]; blk = blk[:blk.index("\n)")]
F = {m.group(1): m.group(3) for m in re.finditer(r'^\s*\[(\w+)\]=("?)([^"\n]+)\2\s*$', blk, re.M)}
ARMED = ("alchemist,auras,breaching,creature_giving,critter,dragons,dragonstorm,edf,elementals,fluctuator,galadriel,"
         "gods,jumbo,kithkin,kittyv3,knights,landfall,lhc,mazesend,merfolk,mill,minotaur,mirrorwing,pd,pirates,"
         "selesnyav2,slivers,soldiers,stompy,th,whiteknights").split(",")
FL = {"T": None, "K": {"MTG_LEAF_KILL_ONLY_NOM2": True},
      "KF": {"MTG_LEAF_KILL_ONLY_NOM2": True, "MTG_KO_BOUNDARY_SIM": True}}
jobs = []; rows = []
def emit(key, games, games2, base, arms):
    k = 0; f = F[key]; stem = os.path.splitext(f)[0]
    for suffix, hg, n in (("", False, games), ("2hg", True, games2)):
        row = f"{key}{suffix}"
        rows.append(dict(row=row, deck=key, games=n, hg=hg, arms=arms))
        for c in range(0, n, a.chunk):
            seed = base + 1000 * k; k += 1
            for arm in arms:
                j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(R, f), "profile": os.path.join(R, stem + ".profile.json"),
                     "games": min(a.chunk, n - c), "seed": seed, "game_index": 0}
                if hg: j.update({"starting_life": 30, "opponent_heads": 2})
                if FL[arm]: j["flags"] = dict(FL[arm])
                jobs.append(j)
    return k
emit("mill", a.mill_games, a.mill_games_2hg, a.mbase, ("T", "K", "KF"))
if not a.no_others:
    b = a.obase
    for key in ARMED:
        if key == "mill": continue
        b += 1000 * emit(key, a.games, a.games_2hg, b, ("K", "KF"))
# Heaviest decks first (the k-fleet ms/game for the K arm), so the tail is short.
cost = {}
for fn in ("/workspaces/MagicDeckTester/build/scratch/k-fleet/ab/batch_O.out",):
    for ln in open(fn):
        m = re.match(r"^(\w+?)(2hg)?_O1_s\d+: played=(\d+).* cpu=(\d+)", ln)
        if m: cost.setdefault(m.group(1), []).append(int(m.group(4)) / int(m.group(3)))
cm = {k: sum(v) / len(v) for k, v in cost.items()}
def est(j):
    key = re.match(r"^(\w+?)(2hg)?_(T|K|KF)_s", j["name"]).group(1)
    return cm.get(key, 1000) * j["games"] * (1.8 if "opponent_heads" in j else 1.0)
jobs.sort(key=est, reverse=True)
os.makedirs(a.out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(a.out, "ab.json"), "w"), indent=1)
json.dump({"rows": rows}, open(os.path.join(a.out, "rows.json"), "w"), indent=1)
print(f"{len(jobs)} jobs, {sum(j['games'] for j in jobs)} games, est {sum(est(j) for j in jobs)/1000:.0f} core-s; {len(rows)} rows -> {a.out}")
