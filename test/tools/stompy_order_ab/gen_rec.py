#!/usr/bin/env python3
"""Recovery (stage 1 + stage 2) manifest for a list of slower games under named arms.
usage: gen_rec.py <out.json> <arm>[,<arm>...] <slower-file> [<slower-file> ...]
Each slower-file line: deck cell gi old new. Stage 1 = --depth max(shipped, old) --budget-ms 100, stage 2 =
--depth 8 --budget-ms 0, both ignore_play_profile. Job name <deck>.<cell>.<arm>-s<stage>.g<gi> (recov_v5.py's).
"""
import json
import os
import sys


WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
S0 = 10900001
NTR = {"MTG_STOMPY_NO_TARGET_RANK": True}
SVN = {"MTG_SAC_VICTIM_SAME_NAME": True}
PD = {"MTG_EXEC_PUT_DEFERRED": True}
PF = {"MTG_HOLD_PRODUCER_FIRST": True}
OF = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}
FLAGS = {"fix": {"MTG_HOLD_RELEASE_YIELD": True},
         "ofix": dict(OF),
         "onotop": dict(OF, MTG_STOMPY_TOP_RESOLVE=False),
         "ofixntr": dict(OF, **NTR),
         "onotopntr": dict(OF, MTG_STOMPY_TOP_RESOLVE=False, **NTR),
         "ofixsvn": dict(OF, **SVN),
         "onotopsvn": dict(OF, MTG_STOMPY_TOP_RESOLVE=False, **SVN),
         "svn": dict(SVN),
         "ofixsvnpd": dict(OF, **SVN, **PD),
         "onotopsvnpd": dict(OF, MTG_STOMPY_TOP_RESOLVE=False, **SVN, **PD),
         "ofixall": dict(OF, **SVN, **PD, **PF),
         "onotopall": dict(OF, MTG_STOMPY_TOP_RESOLVE=False, **SVN, **PD, **PF),
         "off": None}

import re
cases = open(os.path.join(WT, "test/regression_cases.sh")).read()


def deck_map(name):
    m = re.search(r"declare -A %s=\((.*?)\n\)" % name, cases, re.S)
    o = {}
    for line in m.group(1).splitlines():
        mm = re.match(r'\s*\[(\w+)\]=("?)(.*?)\2\s*$', line)
        if mm:
            o[mm.group(1)] = mm.group(3)
    return o


FILES = deck_map("DECK_FILE")
PROFS = deck_map("DECK_PROF")


def shipped_depth(deck_file):
    vp = os.path.splitext(os.path.join(WT, deck_file))[0] + ".value.json"
    try:
        return int(json.load(open(vp)).get("value_play", {}).get("target_depth", 5))
    except Exception:
        return 5


out, arms, files = sys.argv[1], sys.argv[2].split(","), sys.argv[3:]
jobs = []
for fn in files:
    for row in open(fn):
        f = row.split()
        if not f:
            continue
        deck, cell, gi, wo = f[0], f[1], int(f[2]), int(f[3])
        b = deck[:-3] if deck.endswith("2hg") else deck
        for arm in arms:
            for stage in (1, 2):
                j = {"name": "%s.%s.%s-s%d.g%d" % (deck, cell, arm, stage, gi),
                     "deck": os.path.join(WT, FILES[b]), "profile": os.path.join(WT, PROFS[b]),
                     "games": 1, "seed": S0 + gi, "game_index": gi, "ignore_play_profile": True,
                     "depth": max(shipped_depth(FILES[b]), wo) if stage == 1 else 8,
                     "budget_ms": 100 if stage == 1 else 0}
                if deck.endswith("2hg"):
                    j["starting_life"] = 30
                    j["opponent_heads"] = 2
                if FLAGS[arm]:
                    j["flags"] = FLAGS[arm]
                jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(len(jobs), "jobs")
