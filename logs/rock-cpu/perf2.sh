#!/bin/bash
# perf2.sh <milli1> <milli05> : user-space instruction counts per (deck, arm), searched only, 1 thread per process, 16 at a time
set -u
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; D=$S/ab2/perf; mkdir -p $D/logs; bin=$S/mtg.dev
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
python3 - "$D" <<'PY'
import json,sys
D=sys.argv[1]; S="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu"
rd=json.load(open(S+"/rock_decks.json"))
N={"niv":40,"extraturns":30,"hinata":15,"stompy":30,"stuffy":30,"merfolkturns":30,"dragons":30,"galadriel":30,"fivecolour":30}
arms={"def":{},"poff":{"MTG_LKR_PLAYOUT_OFF":True},"pnt":{"MTG_LKR_PLAYOUT_NO_TABLE":True},"price":{"MTG_LKR_PRICE":True},"price05":{"MTG_LKR_PRICE":True}}
lst=open(D+"/list.txt","w")
for k,n in N.items():
    d,p,_=rd[k]
    for a,fl in arms.items():
        nm=f"{a}__{k}__s"
        json.dump({"jobs":[{"name":nm+"__o0","deck":d,"profile":p,"games":n,"seed":796000,"game_index":0,"flags":fl}]},open(f"{D}/{nm}.json","w"))
        lst.write(nm+"\n")
PY
run1() {
  nm=$1; m=$M1; case $nm in price05__*) m=$M05;; esac
  env MTG_LKR_PRICE_MILLI=$m MTG_RSS_CAP_GB=3 perf stat -x, -e instructions:u -o $D/$nm.perf -- $bin --batch $D/$nm.json --threads 1 --game-log-dir $D/logs > $D/$nm.out 2> $D/$nm.err
}
export -f run1; export D bin; export M1=$1 M05=$2
xargs -P 12 -I{} bash -c 'run1 {}' < $D/list.txt
