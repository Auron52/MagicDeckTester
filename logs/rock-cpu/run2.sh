#!/bin/bash
# run2.sh : carried-residual price. build dev2 (full core, box lock), identity vs base, batch C (MILLI 85), batch D (MILLI 43), perf
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; A=$S/ab2; W=/workspaces/MagicDeckTester/.claude/worktrees/rock-cpu-dev
st() { echo "$* $(date -u +%FT%TZ)" >> $A/status2; }
st sleeping-for-9p-clock; sleep 210
st waiting-for-box-lock
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
flock 9
st lock-acquired
cd $W && ./build.sh > $S/build_dev2.log 2>&1; rc=$?
st build rc=$rc
[ $rc -eq 0 ] || { echo STOPPED > $A/STOPPED2; exit 1; }
cp $W/build/Release/mtg $S/mtg.dev2
bin=$S/mtg.dev2; export MTG_PGO=0
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
mkdir -p $A/logs_ident2 $A/logs_C $A/logs_D $A/perf2/logs
env MTG_RSS_CAP_GB=24 $bin --batch $A/ident.json --threads 32 --game-log-dir $A/logs_ident2 > $A/ident.out 2> $A/ident2.err
python3 $A/identchk.py > $A/ident2.txt 2>&1; ri=$?
st identity rc=$ri
[ $ri -eq 0 ] || { echo STOPPED > $A/STOPPED2; exit 1; }
env MTG_RSS_CAP_GB=40 MTG_LKR_PRICE_MILLI=85 $bin --batch $A/C.json --threads $(nproc) --game-log-dir $A/logs_C > $A/C.out 2> $A/C.err
st batchC rc=$?
env MTG_RSS_CAP_GB=40 MTG_LKR_PRICE_MILLI=43 $bin --batch $A/D.json --threads $(nproc) --game-log-dir $A/logs_D > $A/D.out 2> $A/D.err
st batchD rc=$?
# instruction counts, price arms, same seeds / sizes as ab2/perf
for k in niv extraturns hinata galadriel stuffy dragons; do for m in 85 43; do
  sed "s/\"name\": \"price__/\"name\": \"pc${m}__/" $A/perf/price__${k}__s.json > $A/perf2/pc${m}__${k}__s.json
  echo "$m pc${m}__${k}__s"; done; done > $A/perf2/list.txt
run1() { m=$1; nm=$2; env MTG_LKR_PRICE_MILLI=$m MTG_RSS_CAP_GB=3 perf stat -x, -e instructions:u -o $A/perf2/$nm.perf -- $bin --batch $A/perf2/$nm.json --threads 1 --game-log-dir $A/perf2/logs > $A/perf2/$nm.out 2> $A/perf2/$nm.err; }
export -f run1; export A bin
xargs -P 12 -L 1 bash -c 'run1 $0 $1' < $A/perf2/list.txt
st perf done
echo DONE > $A/DONE2
