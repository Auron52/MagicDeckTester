#!/bin/bash
# hc/run.sh : Hinata + Niv 2HG + Extra Turns 2HG confirm of MTG_LKR_PLAYOUT_OFF, one pooled batch, then perf counts
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; H=$S/hc; bin=$S/mtg.dev2
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
st() { echo "$* $(date -u +%FT%TZ)" >> $H/status; }
st waiting-for-box-lock
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
flock 9
st lock-acquired
export MTG_PGO=0; mkdir -p $H/logs $H/perf/logs
env MTG_RSS_CAP_GB=40 $bin --batch $H/H.json --threads $(nproc) --game-log-dir $H/logs > $H/H.out 2> $H/H.err
st batch rc=$?
# instructions: 20 games per (shape, arm), 1 thread, first chunk seeds of each shape
python3 - <<'PY'
import json
H="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu/hc"
m=json.load(open(H+"/H.json"))["jobs"]; lst=open(H+"/perf/list.txt","w")
for j in m:
    if j["game_index"]==0:
        j=dict(j); j["games"]=20 if "hinata" not in j["name"] else 12
        nm=j["name"]; json.dump({"jobs":[j]},open(f"{H}/perf/{nm}.json","w")); lst.write(nm+"\n")
PY
run1() { env MTG_RSS_CAP_GB=3 perf stat -x, -e instructions:u -o $H/perf/$1.perf -- $bin --batch $H/perf/$1.json --threads 1 --game-log-dir $H/perf/logs > $H/perf/$1.out 2> $H/perf/$1.err; }
export -f run1; export H bin
xargs -P 8 -I{} bash -c 'run1 {}' < $H/perf/list.txt
st perf done
echo DONE > $H/DONE
