#!/usr/bin/env python3
"""ONE pooled d0 census for the two payment levers (paired on/off per lever, per deck).

    python3 make_census.py <outdir> <games_per_deck>      -- writes <outdir>/manifest.json
    python3 make_census.py --analyse <outdir>             -- per-deck faster/slower per lever

MTG_FB_HOLD_BANKED: every deck that holds a firebreathing / team-pump card (firebreathing_cost or
team_pump_cost). MTG_LINE_FLOAT_TAP_FIRST: every deck (the payer is shared). Seeds: lever 1 at
8700001 + 10000*deck_idx, lever 2 at 8900001 + 10000*deck_idx (fresh). d0, no budget, profile attached.
"""
import glob, json, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6"
CHUNK = 25
MAX_TURNS = 8
LEVERS = (("fb", "MTG_FB_HOLD_BANKED", 8700001), ("ft", "MTG_LINE_FLOAT_TAP_FIRST", 8900001))


def decks():
    out = []
    for d in sorted(glob.glob(WT + "/decks/*/")):
        name = os.path.basename(d.rstrip("/"))
        lst = [f for f in glob.glob(d + "*.cod") + glob.glob(d + "*.txt") if os.path.basename(f).startswith(name + ".")]
        if not lst:
            continue
        prof = d + name + ".profile.json"
        out.append((name, lst[0], prof if os.path.exists(prof) else None))
    return out


def pump_cards():
    db = json.load(open(WT + "/src/cards/data/cards.json"))
    cards = db["cards"] if isinstance(db, dict) and "cards" in db else db
    return {c["name"] for c in cards
            if (c.get("parameters") or {}).get("firebreathing_cost") or (c.get("parameters") or {}).get("team_pump_cost")}


def holds_any(path, names):
    txt = open(path, errors="replace").read()
    return any(re.search(r"(^|[\s\"'>])" + re.escape(n) + r"($|[\s\"'<])", txt, re.M) for n in names)


def build(out, games):
    pumps = pump_cards()
    jobs, meta = [], {"decks": [], "games": games}
    for di, (name, lst, prof) in enumerate(decks()):
        tag = re.sub(r"[^A-Za-z0-9]", "", name)
        levers = [lv for lv in LEVERS if lv[0] == "ft" or holds_any(lst, pumps)]
        meta["decks"].append({"tag": tag, "name": name, "levers": [lv[0] for lv in levers]})
        for key, lever, base in levers:
            seed = base + 10000 * di
            for c in range(0, games, CHUNK):
                for arm, on in (("on", True), ("off", False)):
                    j = {"name": "%s_%s_%s.c%d" % (key, tag, arm, c), "deck": lst, "games": min(CHUNK, games - c),
                         "seed": seed + c, "game_index": c, "depth": 0, "budget_ms": 0,
                         "ignore_play_profile": True, "flags": {lever: on}}
                    if prof:
                        j["profile"] = prof
                    jobs.append(j)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs, "meta": meta}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs, %d games, %d decks (%d with a pump)" % (
        len(jobs), sum(j["games"] for j in jobs), len(meta["decks"]),
        sum(1 for d in meta["decks"] if "fb" in d["levers"])))


def analyse(out):
    meta = json.load(open(os.path.join(out, "manifest.json")))["meta"]
    pat = re.compile(r"\[win\] job=(fb|ft)_(\w+)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(dict)
    for line in open(os.path.join(out, "batch.err"), errors="replace"):
        m = pat.search(line)
        if m:
            key, tag, arm, gi, wt = m.group(1), m.group(2), m.group(3), int(m.group(5)), int(m.group(6))
            res[(key, tag, gi)][arm] = wt if wt > 0 else MAX_TURNS + 1
    names = {"fb": "MTG_FB_HOLD_BANKED", "ft": "MTG_LINE_FLOAT_TAP_FIRST"}
    for key in ("fb", "ft"):
        print("== %s (on vs off, paired, d0; loss = %d)" % (names[key], MAX_TURNS + 1))
        print("%-26s %5s %8s %8s %6s %6s %6s" % ("deck", "games", "on", "off", "delta", "faster", "slower"))
        tot = [0, 0, 0, 0, 0]
        for d in meta["decks"]:
            if key not in d["levers"]:
                continue
            ks = [k for k in res if k[0] == key and k[1] == d["tag"] and len(res[k]) == 2]
            on = sum(res[k]["on"] for k in ks)
            off = sum(res[k]["off"] for k in ks)
            fa = [k for k in ks if res[k]["on"] < res[k]["off"]]
            sl = [k for k in ks if res[k]["on"] > res[k]["off"]]
            print("%-26s %5d %8d %8d %+6d %6d %6d" % (d["name"][:26], len(ks), on, off, on - off, len(fa), len(sl)))
            for k in sl:
                print("     SLOWER gi=%d: on %d off %d" % (k[2], res[k]["on"], res[k]["off"]))
            tot = [tot[0] + len(ks), tot[1] + on, tot[2] + off, tot[3] + len(fa), tot[4] + len(sl)]
        print("%-26s %5d %8d %8d %+6d %6d %6d" % ("TOTAL", tot[0], tot[1], tot[2], tot[1] - tot[2], tot[3], tot[4]))


if __name__ == "__main__":
    if sys.argv[1] == "--analyse":
        analyse(sys.argv[2])
    else:
        build(sys.argv[1], int(sys.argv[2]))
