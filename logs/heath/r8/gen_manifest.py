#!/usr/bin/env python3
"""Heath fetchland hand hint: FRESH-SEED re-measure at each deck's PLAY setting (2026-11-24).

Three arms, every game paired on (seed = BASE + off, game_index = off):
  base    -- the branch's parent tree 249db2aa7 (no deck-derived hint), binary mtg.base2, run in worktree heath-base2
  derived -- wip/heath-derived as shipped in r4 (MTG_FETCH_AFTER_DUAL off), binary mtg.fad, run in heath-derived
  fad     -- derived + MTG_FETCH_AFTER_DUAL on (keep the fetch behind a real dual in the greedy ranker's multi pass)
derived and fad share one binary and one pooled batch (per-job "flags"); base needs its own binary + cards.json, so it
is a second batch. Each batch pools every game of every deck.

Rows: every deck the r4 play A/B moved (play setting = the deck profile's play depth + the r4/overnight budget), plus a
DECOUPLED row for Burn and Morph (shuffle_salt_search != shuffle_salt: the search plans against a reshuffle the real game
will not deal, so a fetch-timing win that needs the real reshuffle order collapses) -- the honest-reshuffle check of the
clairvoyance classification at scale.

Seeds: BASE = 8400000. Used before by this work: 960000 (r1), 2000000 (r2), 3000000 (r4 play), 5000000-6047500 (CG/big
d0), 7000000-7100000 (r4 d0); the suite uses < 1e6. 8.4M..8.41M is fresh.

usage: gen_manifest.py [--out DIR] [--scale F]   (writes manifest_base.json, manifest_fix.json, rows.json)
"""
import json, os, sys, argparse

BASE = 8400000
CHUNK = 100   # games per job: small enough that the pooled queue ends on one short tail

# name, deck dir, budget_ms, games, extra job fields
ROWS = [
    ("modernrwburn_play", "Modern RW Burn", 20, 10000, {}),
    ("morph_play",        "Morph",          20, 10000, {}),
    ("worldfire_play",    "Worldfire",      20,  3000, {}),
    ("fivecolour_play",   "Five Colour",    40,  3000, {}),
    ("antilife2hg_play",  "Anti-Lifegain",  20,  4000, {"starting_life": 30, "opponent_heads": 2}),
    ("sea_play",          "Sea Creatures",  20,  4000, {}),
    ("spirits_play",      "Spirits",        20,  4000, {}),
    ("creature_giving_play", "Creature Giving", 30, 6000, {}),
    ("boars_play",        "Boars",          40,  6000, {}),
    ("stuffy_play",       "Stuffy Doll",    20,  2000, {}),
    # decoupled reshuffle (honest about post-fetch library order); own seed block so it never pairs with a play row
    ("modernrwburn_play_dec", "Modern RW Burn", 20, 4000, {"shuffle_salt_search": 1, "_seed_off": 100000}),
    ("morph_play_dec",        "Morph",          20, 4000, {"shuffle_salt_search": 1, "_seed_off": 100000}),
]
ARMS_FIX = [("derived", {"MTG_FETCH_AFTER_DUAL": False}), ("fad", {"MTG_FETCH_AFTER_DUAL": True})]

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
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--scale", type=float, default=1.0); a = ap.parse_args()
    base = jobs_for("base", None, a.scale)
    fix = [j for arm, fl in ARMS_FIX for j in jobs_for(arm, fl, a.scale)]
    json.dump({"jobs": base}, open(os.path.join(a.out, "manifest_base.json"), "w"), indent=1)
    json.dump({"jobs": fix}, open(os.path.join(a.out, "manifest_fix.json"), "w"), indent=1)
    json.dump([r[0] for r in ROWS], open(os.path.join(a.out, "rows.json"), "w"))
    g = lambda js: sum(j["games"] for j in js)
    print(f"manifest_base.json: {len(base)} jobs / {g(base)} games; manifest_fix.json: {len(fix)} jobs / {g(fix)} games")

if __name__ == "__main__":
    main()
