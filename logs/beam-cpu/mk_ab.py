#!/usr/bin/env python3
"""Manifest generator: CPU alternatives to the escalation beam (wip/beam-cpu, 2026-11-25). NOT RUN here.

USER (2026-11-25): "That's kind of rough. What exactly are the implications of removing the beam? Do we have any
alternatives to lower the cost? Creature Giving is not a fast deck as far as I know, so this is a tough sell as-is."
See logs/beam-cpu/report.md for what each arm is and the small-sample numbers behind it.

ARMS (one binary = wip/beam-cpu; everything per job, so ONE pooled batch carries every arm):
  A    origin: the deck's d5 value beam (W3 ld2).
  N    MTG_ESC_BEAM_NONE: no beam.
  NK   N + MTG_LEAF_KILL_ONLY_NOM2: the SOUND rollout kill-only turn for no-second-main decks (a re-spend under a
       budget, exact per leaf at b0). Inert on a deck that can open a main 2 (the parity gate).
  NR   N + esc_deck_r = R(deck): the single-path escalation's frozen cost-per-leaf re-priced for UNBEAMED passes
       (the shipped R was fitted with the beam on). Only decks given with --deck-r (default: CG 114, measured
       MTG_HYBRID_STATS Rsample on 45 unbeamed first-fit passes).
  NKR  NK + NR.
  S    MTG_ESC_BEAM_STAGED: the beam as a budget-staged admission (top W always, the rest while the escalation
       budget is not Exhausted; complete at unlimited budget). Lossy under a budget.
  SK   S + MTG_LEAF_KILL_ONLY_NOM2.
  AK   A + MTG_LEAF_KILL_ONLY_NOM2 (isolates the kill-only turn's own effect with the beam kept).

Rows: every deck whose value_play block DRIVES at target_depth 5 with beam_width > 0 (today: Anti-Lifegain,
Creature Giving, Dragonstorm) at its play setting, standard and 2HG (starting_life 30, opponent_heads 2), plus
optional extra budgets (--budgets 15,20). Pairing: every arm of a (deck, format, budget) row plays the same
(seed, game_index) blocks; each chunk gets its own seed = BASE + 1000*k with game_index 0.
Seeds: 37,100,000+ (nothing under build/scratch uses 35M-39M as of 2026-11-25).

Report: python3 logs/beam-gate/ab_report.py <outdir> pairs per game vs arm A and sums cpu/units per arm
(job names are <row>_<arm>_s<seed>, the beam-gate convention).

usage: mk_ab.py [--out DIR] [--base N] [--games N] [--games-2hg N] [--chunk N] [--arms A,N,NK,...]
                [--budgets 15,20] [--deck-r creature_giving=114,antilife=..] [--decks creature_giving,...]
"""
import argparse, json, os

DECKS = {   # suite key -> (deck dir/stem); the value_play block is read from the sidecar
    "antilife": "decks/Anti-Lifegain/Anti-Lifegain",
    "creature_giving": "decks/Creature Giving/Creature Giving",
    "dragonstorm": "decks/Dragonstorm/Dragonstorm",
}
KO = {"MTG_LEAF_KILL_ONLY_NOM2": True}
ARMS = {
    "A":   ({}, False),
    "N":   ({"MTG_ESC_BEAM_NONE": True}, False),
    "NK":  ({"MTG_ESC_BEAM_NONE": True, **KO}, False),
    "NR":  ({"MTG_ESC_BEAM_NONE": True}, True),
    "NKR": ({"MTG_ESC_BEAM_NONE": True, **KO}, True),
    "S":   ({"MTG_ESC_BEAM_STAGED": True}, False),
    "SK":  ({"MTG_ESC_BEAM_STAGED": True, **KO}, False),
    "AK":  (dict(KO), False),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--out", default="/workspaces/MagicDeckTester/build/scratch/beam-cpu/ab")
    ap.add_argument("--base", type=int, default=37100000)
    ap.add_argument("--chunk", type=int, default=250)
    ap.add_argument("--games", type=int, default=2000)
    ap.add_argument("--games-2hg", type=int, default=1000)
    ap.add_argument("--arms", default="A,N,NK,NR,NKR,S,SK")
    ap.add_argument("--budgets", default="", help="extra budgets beyond the play budget, e.g. 15,20")
    ap.add_argument("--deck-r", default="creature_giving=114")
    ap.add_argument("--decks", default=",".join(DECKS))
    a = ap.parse_args()
    arms = [x for x in a.arms.split(",") if x]
    for x in arms:
        if x not in ARMS:
            raise SystemExit(f"unknown arm {x}; known: {','.join(ARMS)}")
    deck_r = {}
    for kv in filter(None, a.deck_r.split(",")):
        k, v = kv.split("=")
        deck_r[k] = float(v)
    extra = [int(b) for b in a.budgets.split(",") if b]
    jobs, rows, k = [], [], 0
    for key in a.decks.split(","):
        stem = DECKS[key]
        vp = json.load(open(os.path.join(a.repo, stem + ".value.json"))).get("value_play") or {}
        if not (vp.get("enabled") and vp.get("target_depth") == 5 and vp.get("beam_width", 0) > 0):
            print(f"skip {key}: no live d5 beam at play"); continue
        play_b = vp.get("budget_ms", 0)
        for hg in (False, True):
            games = a.games_2hg if hg else a.games
            for b in [play_b] + [x for x in extra if x != play_b]:
                row = f'{key}{"2hg" if hg else ""}_d5b{b}'
                row_arms = [x for x in arms if not (ARMS[x][1] and key not in deck_r)]
                rows.append(dict(row=row, deck=key, depth=5, budget=b, games=games, run=row_arms, hg=hg,
                                 regimes={}, deck_r=deck_r.get(key)))
                for c in range(0, games, a.chunk):
                    n = min(a.chunk, games - c)
                    seed = a.base + 1000 * k
                    k += 1
                    for arm in row_arms:
                        flags, use_r = ARMS[arm]
                        j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(a.repo, stem + ".cod"),
                             "profile": os.path.join(a.repo, stem + ".profile.json"), "games": n,
                             "seed": seed, "game_index": 0}
                        if b != play_b:
                            j["budget_ms"] = b   # depth stays the value_play block's (5)
                        if hg:
                            j.update({"starting_life": 30, "opponent_heads": 2})
                        if flags:
                            j["flags"] = dict(flags)
                        if use_r:
                            j["esc_deck_r"] = deck_r[key]
                        jobs.append(j)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "ab.json"), "w"), indent=1)
    # logs/beam-gate/ab_report.py's format: every arm here is run (no identical-by-construction substitution).
    json.dump({"rows": rows, "equiv": {r["row"]: {x: x for x in r["run"]} for r in rows}},
              open(os.path.join(a.out, "rows.json"), "w"), indent=1)
    ng = sum(j["games"] for j in jobs)
    print(f"{len(rows)} rows, {len(jobs)} jobs, {ng} games -> {a.out}/ab.json")
    print("run (coordinator, under build/scratch/claude_box.lock):")
    print(f"  cd {a.repo} && flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_PGO=0 "
          f"build/Release/mtg --batch {a.out}/ab.json --threads $(nproc) --game-log-dir {a.out}/wins "
          f"> {a.out}/batch.out 2> {a.out}/batch.err")


if __name__ == "__main__":
    main()
