#!/usr/bin/env python3
"""Classify held-out slower games for one lever: rerun each game with the lever on (A, all defaults) and
off (B), find the first turn where play differs, and print a one-line summary per game.
usage: classify.py <LEVER_ENV> <tag> <gi...>"""
import glob, json, os, subprocess, sys

W = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix"
S = "/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/div"
BIN = "/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/mtg_final"

def run(gi, arm, env):
    out = "%s/%s_g%d_%s" % (S, tag, gi, arm)
    e = dict(os.environ); e["ENV_EXTRA"] = env
    subprocess.run(["bash", W + "/run1.sh", "d0", str(gi), str(5300001 + gi), "new", out, BIN],
                   env=e, stdout=subprocess.DEVNULL, check=False)
    return json.load(open(glob.glob(out + "/*.json")[0]))

lever, tag = sys.argv[1], sys.argv[2]
for gi in map(int, sys.argv[3:]):
    a, b = run(gi, "A", ""), run(gi, "B", lever + "=0")
    inv = {}
    for k, v in a["cardNumbering"].items():
        for n in (v if isinstance(v, list) else [v]): inv[n] = k
    def casts(t): return [x.get("cardName") or str(inv.get(x.get("card"), x.get("card")))
                          for x in t["actions"] if x.get("type") in ("CAST_SPELL", "PLAY_LAND", "ABILITY")]
    line = "gi=%d A=%s B=%s" % (gi, a["result"]["turn"], b["result"]["turn"])
    for i, (ta, tb) in enumerate(zip(a["turns"], b["turns"])):
        if [x.get("type") for x in ta["actions"]] != [x.get("type") for x in tb["actions"]] or casts(ta) != casts(tb):
            hand = [inv.get(h, h) for h in a["turns"][i - 1]["boardAfter"]["hand"]] if i else []
            line += " | T%d %s  A:%s  B:%s  hand:%s" % (ta["turn"], ta["phase"], casts(ta)[:6], casts(tb)[:6], hand)
            break
    print(line, flush=True)
