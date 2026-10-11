#!/bin/bash
# Run one held-out EDF game under two lever settings and print the first turn where play differs.
# usage: diverge.sh <cell> <gi> <seed> <tag> "<envA>" "<envB>"
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3/logs/edf_fix
S=/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/div
BIN=/workspaces/MagicDeckTester/build/scratch/agent-a090b37745a53c3a3/mtg_final
cell=$1; gi=$2; seed=$3; tag=$4; A=$5; B=$6
ENV_EXTRA="MTG_EXEC_TAP_TRACE=1 $A" bash $W/run1.sh "$cell" "$gi" "$seed" new "$S/${tag}_g${gi}_A" "$BIN" | sed 's/^/A: /'
ENV_EXTRA="MTG_EXEC_TAP_TRACE=1 $B" bash $W/run1.sh "$cell" "$gi" "$seed" new "$S/${tag}_g${gi}_B" "$BIN" | sed 's/^/B: /'
python3 - "$S/${tag}_g${gi}_A" "$S/${tag}_g${gi}_B" <<'EOF'
import json, glob, sys
def load(d):
    f = glob.glob(d + "/*.json")[0]
    return json.load(open(f))
a, b = load(sys.argv[1]), load(sys.argv[2])
num = a["cardNumbering"]; inv = {}
for k, v in num.items():
    for n in (v if isinstance(v, list) else [v]): inv[n] = k
def acts(t): return [(x.get("type"), x.get("cardName") or inv.get(x.get("card"), x.get("card"))) for x in t["actions"]]
for ta, tb in zip(a["turns"], b["turns"]):
    if acts(ta) != acts(tb) or ta["phase"] != tb["phase"]:
        print("first divergence T%d %s" % (ta["turn"], ta["phase"]))
        prev = a["turns"][a["turns"].index(ta) - 1]["boardAfter"] if a["turns"].index(ta) > 0 else None
        if prev:
            print("  board before:", [(x["cardName"], "T" if x.get("tapped") else "", x.get("attachedTo")) for x in prev["battlefield"] if True])
            print("  hand before :", [inv.get(h, h) for h in prev["hand"]])
        print("  A:", acts(ta)[:14])
        print("  B:", acts(tb)[:14])
        break
print("result A", a["result"], " B", b["result"])
EOF
