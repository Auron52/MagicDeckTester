#!/bin/bash
# run.sh : the whole rock-cpu A/B in ONE box-lock hold. Phases have genuine data dependencies only:
#  1 identity (dev flags-off vs base digests) + price calibration stats, concurrently (short)
#  2 batch A: def / poff / pnt / price@MILLI_1x  (one pooled batch)
#  3 batch B: price05@MILLI_1x/2 (same seeds; MILLI is a process env, so a separate process)
#  4 perf instruction counts per (deck, arm)
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu; A=$S/ab2; bin=$S/mtg.dev
cd /workspaces/MagicDeckTester/.claude/worktrees/rock-cpu
st() { echo "$* $(date -u +%FT%TZ)" >> $A/status; }
st waiting-for-box-lock
exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock
flock 9
st lock-acquired
export MTG_PGO=0
mkdir -p $A/logs_ident $A/logs_stats $A/logs_A $A/logs_B
env MTG_RSS_CAP_GB=24 $bin --batch $A/ident.json --threads 26 --game-log-dir $A/logs_ident > $A/ident.out 2> $A/ident.err &
p0=$!
pids=()
for k in niv extraturns hinata; do
  env MTG_RSS_CAP_GB=4 MTG_LINE_REDUCER_KEEP_STATS=1 $bin --batch $A/stats_$k.json --threads 1 --game-log-dir $A/logs_stats > $A/stats_$k.out 2> $A/stats_$k.err &
  pids+=($!)
done
wait $p0; r0=$?; wait "${pids[@]}"
st phase1-done ident_rc=$r0
python3 $A/identchk.py > $A/ident.txt 2>&1; ri=$?
st identity rc=$ri
if [ $ri -ne 0 ]; then st STOPPED identity-mismatch; echo STOPPED > $A/STOPPED; exit 1; fi
M1=$(python3 $A/calib.py); rc=$?
if [ $rc -ne 0 ] || [ -z "$M1" ]; then st STOPPED calib-failed; echo STOPPED > $A/STOPPED; exit 1; fi
M05=$(( (M1 + 1) / 2 ))
echo "M1=$M1 M05=$M05" > $A/milli.txt; st calib M1=$M1 M05=$M05
env MTG_RSS_CAP_GB=40 MTG_LKR_PRICE_MILLI=$M1 $bin --batch $A/A.json --threads $(nproc) --game-log-dir $A/logs_A > $A/A.out 2> $A/A.err
st batchA rc=$?
env MTG_RSS_CAP_GB=40 MTG_LKR_PRICE_MILLI=$M05 $bin --batch $A/B.json --threads $(nproc) --game-log-dir $A/logs_B > $A/B.out 2> $A/B.err
st batchB rc=$?
bash $A/perf2.sh $M1 $M05
st perf done
echo DONE > $A/DONE
