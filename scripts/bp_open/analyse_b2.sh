#!/usr/bin/env bash
# Read batch 2 (build/scratch/bp_open/b2: the x3 = d3 b0 exactness cell + F5's stage-1 recovery) once it has
# finished, and write the tables into docs/design/bp-open-data/.
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
B=/workspaces/MagicDeckTester/build/scratch/bp_open/b2
O=$W/docs/design/bp-open-data
cat "$B/run.info"
python3 "$W/scripts/bp_open/make_ab.py" analyse "$B" --base off > "$O/exact_r2_vs_off.txt"
cat "$O/exact_r2_vs_off.txt"
cp -f "$B/batch.err" "$B/parts/rec1_on/batch.err"
python3 "$W/scripts/bp_open/make_recovery.py" analyse "$B/parts/rec1_on" > "$O/recovery_stage1_f5_r2.txt"
cat "$O/recovery_stage1_f5_r2.txt"
