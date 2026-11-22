#!/bin/bash
# perf_lane.sh <tag> <binary> <ngames_s> <ngames_d0> <seed_s> <seed_d0> <arms_json> [env...]
# One process per (deck, mode, arm), --threads 1, two at a time; user-space instruction counts via perf stat.
set -u
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; tag=$1; bin=$2; ns=$3; nd=$4; ss=$5; sd=$6; arms=$7; shift 7
W=/workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
D=$S/perf_$tag; mkdir -p $D/logs
cd $W
python3 - "$D" "$ns" "$nd" "$ss" "$sd" "$arms" <<'PY'
import json,sys
D,ns,nd,ss,sd,arms=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5]),json.loads(sys.argv[6])
rd=json.load(open('/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu/rock_decks.json')); rd.pop('snow',None)
lst=open(D+'/list.txt','w')
for k,(deck,prof,_) in sorted(rd.items()):
    for mode,n,seed in (('s',ns,ss),('d0',nd,sd)):
        if n<=0: continue
        for arm,fl in arms.items():
            j={"name":f"{arm}__{k}__{mode}__o0","deck":deck,"profile":prof,"games":n,"seed":seed,"game_index":0,"flags":fl}
            if mode=='d0': j.update({"depth":0,"budget_ms":0,"ignore_play_profile":True})
            nm=f"{arm}__{k}__{mode}"
            json.dump({"jobs":[j]},open(f"{D}/{nm}.json","w"))
            lst.write(nm+"\n")
PY
run1() {
  nm=$1
  perf stat -x, -e instructions:u -o $D/$nm.perf -- "$bin" --batch $D/$nm.json --threads 1 --game-log-dir $D/logs > $D/$nm.out 2> $D/$nm.err
}
export -f run1; export D bin
for e in "$@"; do export "$e"; done
# heaviest first (niv, stuffy, ...) is not needed: two workers over the list
xargs -P 2 -I{} bash -c 'run1 {}' < $D/list.txt
echo DONE > $D/DONE
