#!/usr/bin/env python3
"""Manifest generator: cheaper budget-staged escalation beams (wip/beam-staged, 2026-11-25).

USER (2026-11-25): "Are not all decks using the beam? Do we have any numbers on what (ii) would look like? It might
make sense to get them before we choose. But yeah, I don't think I want to drop the beam as-is. The cost is a bit on
the high side."  (ii) = replace the fixed-width beam by a budget-staged admission (complete at unlimited budget).

ARMS (one binary = wip/beam-staged; every lever per job, so ONE pooled batch carries every arm):
  A      today: the deck's d5 value beam (W3 ld2) / the d3 static W20 ld1 beam.
  AK     A + MTG_LEAF_KILL_ONLY_NOM2 (sound identity).
  NK     no beam + K.
  SK     MTG_ESC_BEAM_STAGED (plans beyond W while the escalation budget is not Exhausted) + K.
  SF50K  staged, plans beyond W only while Used < 0.50 x Limit (esc_beam_stage_frac 0.5) + K.
  SF25K  staged, share 0.25 + K.
  SG50K  staged ORDERED re-pricing: the k-th plan beyond W while Used < 0.5^k x Limit (esc_beam_stage_geo) + K.
All staged arms are complete at an unlimited budget (no limit => no share is ever reached).

Rows: AL / CG / DS at play (d5, value_play budget), std and 2HG (starting_life 30, opponent_heads 2), plus a d3 b10
row (ignore_play_profile, the suite's d3 shape) where the static W20 beam is live. Pairing: every arm of a row plays the
same (seed, game_index=0) chunks; seed = BASE + 1000*k. Seeds 46,100,000+ (unused under build/scratch 2026-11-25).
Report: python3 logs/beam-gate/ab_report.py <outdir>.
"""
import argparse, json, os

DECKS = {
    "antilife": "decks/Anti-Lifegain/Anti-Lifegain",
    "creature_giving": "decks/Creature Giving/Creature Giving",
    "dragonstorm": "decks/Dragonstorm/Dragonstorm",
}
KO = {"MTG_LEAF_KILL_ONLY_NOM2": True}
ST = {"MTG_ESC_BEAM_STAGED": True}
ARMS = {   # name -> (flags, extra job fields)
    "A":     ({}, {}),
    "AK":    (dict(KO), {}),
    "NK":    ({"MTG_ESC_BEAM_NONE": True, **KO}, {}),
    "S":     (dict(ST), {}),
    "SK":    ({**ST, **KO}, {}),
    "SF50K": ({**ST, **KO}, {"esc_beam_stage_frac": 0.5}),
    "SF25K": ({**ST, **KO}, {"esc_beam_stage_frac": 0.25}),
    "SG50K": ({**ST, **KO}, {"esc_beam_stage_frac": 0.5, "esc_beam_stage_geo": True}),
    "SF50":  (dict(ST), {"esc_beam_stage_frac": 0.5}),
    "SG50":  (dict(ST), {"esc_beam_stage_frac": 0.5, "esc_beam_stage_geo": True}),
    # FLEET arms (USER 2026-11-25: "If it is useful to those old decks it may be useful to newer ones"):
    "D":     ({"MTG_ESC_BEAM_D5_ALL": True}, {}),
    "DSF50": ({"MTG_ESC_BEAM_D5_ALL": True, **ST}, {"esc_beam_stage_frac": 0.5}),
    "DSG50": ({"MTG_ESC_BEAM_D5_ALL": True, **ST}, {"esc_beam_stage_frac": 0.5, "esc_beam_stage_geo": True}),
}
# On a deck that already ships W3 ld2 at d5 the D arms are the same game by construction.
D_EQUIV = {"D": "A", "DSF50": "SF50", "DSG50": "SG50"}
FLEET = {   # every other suite deck whose enabled value_play block plays at depth 5 (2026-11-25)
    "alchemist": "decks/Accomplished Alchemist/Accomplished Alchemist",
    "auras": "decks/Auras/Auras",
    "burn": "decks/Burn/Burn",
    "critter": "decks/Critter Lifegain/Critter Lifegain",
    "hinata": "decks/Hinata/Hinata",
    "knights": "decks/Knights/Knights",
    "melira": "decks/Melira Pod/Melira Pod",
    "slivers": "decks/Slivers/Slivers",
    "th": "decks/Treasure Hunt/Treasure Hunt",
    "vampires": "decks/Vampires/Vampires",
}
COST = {"creature_giving": 0.55, "antilife": 0.14, "dragonstorm": 0.13, "alchemist": 2.3, "auras": 0.15,
        "burn": 0.12, "critter": 0.14, "hinata": 19.0, "knights": 0.16, "melira": 2.6, "slivers": 0.11,
        "th": 0.25, "vampires": 2.3}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--out", default="/workspaces/MagicDeckTester/build/scratch/beam-staged/ab")
    ap.add_argument("--base", type=int, default=46100000)
    ap.add_argument("--chunk", type=int, default=250)
    ap.add_argument("--games", type=int, default=5000)
    ap.add_argument("--games-2hg", type=int, default=2000)
    ap.add_argument("--games-d3", type=int, default=2000, help="0 = no d3 rows")
    ap.add_argument("--d3-budget", type=int, default=10)
    ap.add_argument("--arms", default="A,AK,NK,SK,SF50,SF50K,SF25K,SG50,SG50K")
    ap.add_argument("--decks", default=",".join(DECKS))
    ap.add_argument("--fleet", default=",".join(FLEET), help="'' = no fleet rows")
    ap.add_argument("--fleet-arms", default="A,D,DSF50,DSG50")
    ap.add_argument("--fleet-games", type=int, default=1000)
    ap.add_argument("--fleet-games-2hg", type=int, default=500)
    ap.add_argument("--fleet-base", type=int, default=47100000)
    ap.add_argument("--fleet-chunk", type=int, default=125)
    a = ap.parse_args()
    arms = [x for x in a.arms.split(",") if x]
    farms = [x for x in a.fleet_arms.split(",") if x]
    for x in arms + farms:
        if x not in ARMS:
            raise SystemExit(f"unknown arm {x}; known: {','.join(ARMS)}")
    jobs, rows = [], []

    def emit(key, stem, shapes, run, equiv, base, chunk):
        k = 0
        for suffix, hg, depth, bb, games in shapes:
            row = f"{key}{suffix}_d{depth}b{bb}"
            rows.append(dict(row=row, deck=key, depth=depth, budget=bb, games=games, run=run, hg=hg, regimes={},
                             equiv=equiv))
            for c in range(0, games, chunk):
                n = min(chunk, games - c)
                seed = base + 1000 * k
                k += 1
                for arm in run:
                    flags, extra = ARMS[arm]
                    j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(a.repo, stem + ext(stem)),
                         "profile": os.path.join(a.repo, stem + ".profile.json"), "games": n,
                         "seed": seed, "game_index": 0, "_cost": COST[key]}
                    if depth != 5:
                        j.update({"depth": depth, "budget_ms": bb, "ignore_play_profile": True})
                    if hg:
                        j.update({"starting_life": 30, "opponent_heads": 2})
                    if flags:
                        j["flags"] = dict(flags)
                    j.update(extra)
                    jobs.append(j)
        return k

    def ext(stem):
        return ".cod" if os.path.exists(os.path.join(a.repo, stem + ".cod")) else ".txt"

    def vplay(stem):
        return json.load(open(os.path.join(a.repo, stem + ".value.json"))).get("value_play") or {}

    kbase = a.base
    for key in filter(None, a.decks.split(",")):
        stem = DECKS[key]
        vp = vplay(stem)
        if not (vp.get("enabled") and vp.get("target_depth") == 5 and vp.get("beam_width", 0) > 0):
            print(f"skip {key}: no live d5 beam at play"); continue
        pb = vp.get("budget_ms", 0)
        shapes = [("", False, 5, pb, a.games), ("2hg", True, 5, pb, a.games_2hg)]
        if a.games_d3 > 0:
            shapes.append(("", False, 3, a.d3_budget, a.games_d3))
        run = list(arms)
        equiv = {x: x for x in run}
        for d, rep in D_EQUIV.items():   # fleet arms on a W3 deck: the representative's games
            if d in farms and rep in run:
                equiv[d] = rep
        kbase += 1000 * emit(key, stem, shapes, run, equiv, kbase, a.chunk)
    fk = a.fleet_base
    for key in filter(None, a.fleet.split(",")):
        stem = FLEET[key]
        vp = vplay(stem)
        if not (vp.get("enabled") and vp.get("target_depth") == 5):
            raise SystemExit(f"{key}: value_play does not drive at d5")
        if vp.get("beam_width", 0) > 0:
            raise SystemExit(f"{key}: already beamed -- belongs in --decks")
        pb = vp.get("budget_ms", 0)
        shapes = [("", False, 5, pb, a.fleet_games), ("2hg", True, 5, pb, a.fleet_games_2hg)]
        fk += 1000 * emit(key, stem, shapes, list(farms), {x: x for x in farms}, fk, a.fleet_chunk)
    # Heaviest jobs first so the pooled queue ends on one short tail (CG 2HG ~1 s/game, d3 rows are cheap).
    def est(j):
        return j["_cost"] * j["games"] * (1.8 if "opponent_heads" in j else 1.0) * (0.3 if j.get("depth") == 3 else 1.0)
    jobs.sort(key=est, reverse=True)
    total = sum(est(j) for j in jobs)
    for j in jobs:
        del j["_cost"]
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "ab.json"), "w"), indent=1)
    json.dump({"rows": rows, "equiv": {r["row"]: r.pop("equiv") for r in rows}},
              open(os.path.join(a.out, "rows.json"), "w"), indent=1)
    ng = sum(j["games"] for j in jobs)
    print(f"{len(rows)} rows, {len(jobs)} jobs, {ng} games, est {total:.0f} core-s -> {a.out}/ab.json")


if __name__ == "__main__":
    main()
