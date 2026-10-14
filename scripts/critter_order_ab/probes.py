#!/usr/bin/env python3
"""Re-run the draft's CritterLifegain probe boards (build/scratch/castorder/probe_CritterLifegain/scen*/)
on two binaries, plus the Heliod two-way boards this change adds.

  python3 -I probes.py make            -> build/scratch/critter-order/probes/<arm>/*.json (paths rewritten)
  python3 -I probes.py show            -> per scenario: turn-4 main-1 casts and counters, base vs new
"""
import glob, json, os, sys

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26"
SRC = "/workspaces/MagicDeckTester/build/scratch/castorder/probe_CritterLifegain"
OUT = "/workspaces/MagicDeckTester/build/scratch/critter-order/probes"
ARMS = ("base", "new")

EXTRA = [
    # Heliod two-way: devotion 1 on board, hand Heliod + Ocelot + Serra + Ranger-Captain (8 Plains).
    ("H_twoway_ranger", 8, ["Soul Warden"], ["Heliod, Sun-Crowned", "Ocelot Pride", "Serra Ascendant",
                                             "Ranger-Captain of Eos"]),
    # Heliod two-way on an empty board: Soul Warden + Heliod + Ocelot (5 Plains).
    ("H_twoway_empty", 5, [], ["Heliod, Sun-Crowned", "Soul Warden", "Ocelot Pride"]),
    # Heliod walks past two: devotion 2, hand Heliod + Ocelot + Serra (5 Plains).
    ("H_walk2", 5, ["Soul Warden", "Soul's Attendant"], ["Heliod, Sun-Crowned", "Ocelot Pride", "Serra Ascendant"]),
    # Ajani split: Pridemate + Ajani + Heliod on 9 Plains with Warden + Champion out.
    ("J_ajani_minus2", 9, ["Soul Warden", "Auriok Champion"], ["Ajani, Strength of the Pride", "Ajani's Pridemate",
                                                               "Heliod, Sun-Crowned"]),
]


def make():
    for arm in ARMS:
        os.makedirs(os.path.join(OUT, arm), exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(SRC, "scen*", "*.json")))
    srcs = [s for s in srcs if not s.endswith(".log.json")]
    for s in srcs:
        j = json.load(open(s))
        tag = os.path.basename(os.path.dirname(s)) + "_" + os.path.basename(s)[:-5]
        for arm in ARMS:
            k = dict(j)
            k["deck"] = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.cod")
            k["profile"] = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.profile.json")
            k["cards_json"] = os.path.join(WT, "src/cards/data/cards.json")
            k["log_out"] = os.path.join(OUT, arm, tag + ".log.json")
            json.dump(k, open(os.path.join(OUT, arm, tag + ".json"), "w"), indent=1)
    for name, lands, board, hand in EXTRA:
        for depth, budget in ((3, 50), (5, 0)):
            tag = "x_%s_d%d_b%d" % (name, depth, budget)
            for arm in ARMS:
                bf = [{"name": "Plains"} for _ in range(lands)] + [{"name": n} for n in board]
                k = {"deck": os.path.join(WT, "decks/CritterLifegain/CritterLifegain.cod"),
                     "profile": os.path.join(WT, "decks/CritterLifegain/CritterLifegain.profile.json"),
                     "cards_json": os.path.join(WT, "src/cards/data/cards.json"),
                     "turn": 4, "on_the_play": True, "active_life": 20, "opponent_life": 30,
                     "library_filler": "Plains", "library_size": 30, "depth": depth, "budget_ms": budget,
                     "max_turns": 7, "battlefield": bf, "hand": hand,
                     "log_out": os.path.join(OUT, arm, tag + ".log.json")}
                json.dump(k, open(os.path.join(OUT, arm, tag + ".json"), "w"), indent=1)
    for arm in ARMS:
        with open(os.path.join(OUT, arm, "list.txt"), "w") as f:
            for p in sorted(glob.glob(os.path.join(OUT, arm, "*.json"))):
                if not p.endswith(".log.json"):
                    f.write(p + "\n")


def turn4(path):
    if not os.path.exists(path):
        return None
    d = json.load(open(path))
    for t in d["turns"]:
        if t.get("turn") == 4 and t.get("phase") == "MAIN_1":
            casts = [a["cardName"] for a in t["actions"] if a.get("type") == "CAST_SPELL"]
            acts = [a.get("type") + ":" + a.get("cardName", "") for a in t["actions"]
                    if a.get("type") not in ("CAST_SPELL", "PLAY_LAND", "TAP_LAND", "TAP")]
            bf = t["boardAfter"]["battlefield"]
            cnt = sum(sum(c["count"] for c in p.get("counters", []) if c.get("kind") != "depletion")
                      for p in bf if not p.get("isLand"))
            return casts, cnt, t["boardAfter"].get("playerLife"), acts
    return ([], 0, None, [])


def show():
    names = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(OUT, "base", "*.json"))
                   if not p.endswith(".log.json"))
    for n in names:
        print("=== " + n)
        for arm in ARMS:
            r = turn4(os.path.join(OUT, arm, n + ".log.json"))
            res = ""
            rp = os.path.join(OUT, arm, n + ".result.txt")
            if os.path.exists(rp):
                res = open(rp).read().strip().replace("\n", " | ")
            if r is None:
                print("   %-4s (no log) %s" % (arm, res))
            else:
                print("   %-4s casts=%s counters=%d life=%s other=%s  %s" % (arm, r[0], r[1], r[2], r[3][:6], res))


if __name__ == "__main__":
    {"make": make, "show": show}[sys.argv[1]]()
