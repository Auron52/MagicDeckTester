#!/usr/bin/env python3
"""Heath fetchland hand hint, Worldfire mitigation A/B (wip/heath-wf, 2026-11-24). NOT RUN by the agent: the coordinator
runs run.sh under the box lock.

Three arms, every game paired on (seed = BASE + seed_off + off, game_index = off):
  base    -- 249db2aa7 (the branch's parent; no deck-derived hint). Binary mtg.base2, run in worktree heath-base2.
  derived -- wip/heath-wf with MTG_ROLLOUT_FETCH_STATIC_HINT off == wip/heath-derived as measured in r8/r9.
  wf      -- wip/heath-wf with MTG_ROLLOUT_FETCH_STATIC_HINT on (the playout reads a fetchland's static produces).
derived and wf share one binary (build/scratch/heath/mtg.wf, run in .claude/worktrees/heath-wf) and one pooled batch (per-job "flags"); base needs its own binary and
cards.json, so it is a second pooled batch. Each batch pools every game of every row.

Rows (each deck's play setting = profile depth + the r8 budget):
  * Worldfire play, normal AND decoupled (shuffle_salt_search 1: the search plans against a reshuffle the real game does
    not deal), 3000 each -- the deck the mitigation is for;
  * the other nine r8 play rows at 2000 each -- the regression check that the mitigation does not undo the fix's gains.

Seeds: BASE = 9930000 (decoupled rows +5000). Used before by this work: 960000, 2000000, 3000000, 5000000-6047500,
7000000-7100000, 8400000-8510000 (r8), 8600000 (r9); a scan of build/scratch + logs for "seed": 99xxxxx found no 993xxxx.

usage: gen_manifest.py [--out DIR] [--scale F]   (writes manifest_base.json, manifest_fix.json, rows.json)
"""
import json, os, argparse

BASE = 9930000
CHUNK = 100
LEVER = "MTG_ROLLOUT_FETCH_STATIC_HINT"

# name, deck dir, budget_ms, games, extra job fields
ROWS = [
    ("worldfire_play",       "Worldfire",       20, 3000, {}),
    ("worldfire_play_dec",   "Worldfire",       20, 3000, {"shuffle_salt_search": 1, "_seed_off": 5000}),
    ("modernrwburn_play",    "Modern RW Burn",  20, 2000, {}),
    ("morph_play",           "Morph",           20, 2000, {}),
    ("fivecolour_play",      "Five Colour",     40, 2000, {}),
    ("antilife2hg_play",     "Anti-Lifegain",   20, 2000, {"starting_life": 30, "opponent_heads": 2}),
    ("sea_play",             "Sea Creatures",   20, 2000, {}),
    ("spirits_play",         "Spirits",         20, 2000, {}),
    ("creature_giving_play", "Creature Giving", 30, 2000, {}),
    ("boars_play",           "Boars",           40, 2000, {}),
    ("stuffy_play",          "Stuffy Doll",     20, 2000, {}),
]
ARMS_FIX = [("derived", {LEVER: False}), ("wf", {LEVER: True})]

def jobs_for(arm, flags, scale):
    out = []
    for name, deck, b, n, extra in ROWS:
        n = max(CHUNK, int(round(n * scale / CHUNK)) * CHUNK)
        so = extra.get("_seed_off", 0)
        for off in range(0, n, CHUNK):
            j = {"name": f"{name}_{arm}_o{off}", "deck": f"decks/{deck}/{deck}.cod",
                 "profile": f"decks/{deck}/{deck}.profile.json", "games": CHUNK,
                 "seed": BASE + so + off, "game_index": off, "budget_ms": b}
            j.update({k: v for k, v in extra.items() if not k.startswith("_")})
            if flags is not None: j["flags"] = flags
            out.append(j)
    # Worldfire first: its games are ~10x the others', so the pooled queue starts them first and ends on a short tail.
    out.sort(key=lambda j: 0 if j["name"].startswith("worldfire") else 1)
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--scale", type=float, default=1.0); a = ap.parse_args()
    base = jobs_for("base", None, a.scale)
    fix = [j for arm, fl in ARMS_FIX for j in jobs_for(arm, fl, a.scale)]
    fix.sort(key=lambda j: 0 if j["name"].startswith("worldfire") else 1)
    json.dump({"jobs": base}, open(os.path.join(a.out, "manifest_base.json"), "w"), indent=1)
    json.dump({"jobs": fix}, open(os.path.join(a.out, "manifest_fix.json"), "w"), indent=1)
    json.dump([r[0] for r in ROWS], open(os.path.join(a.out, "rows.json"), "w"))
    g = lambda js: sum(j["games"] for j in js)
    print(f"manifest_base.json: {len(base)} jobs / {g(base)} games; manifest_fix.json: {len(fix)} jobs / {g(fix)} games")

if __name__ == "__main__":
    main()
