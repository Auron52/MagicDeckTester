#!/usr/bin/env bash
# resume_loop.sh <full ab manifest> <outdir> : ONE heavy_run invocation. Re-runs the remaining jobs with --resume until all
# have .wins (the RSS-cap self-abort loses only the in-flight games; finished games persist as partials). Max 40 attempts.
set -u
cd /workspaces/MagicDeckTester/.claude/worktrees/idgate
M=$1; O=$2; S=/workspaces/MagicDeckTester/build/scratch/idgate
for i in $(seq 1 40); do
  python3 logs/idgate/interleave.py "$M" logs/idgate/sr_ab.json $S/sr1/out logs/idgate/ovn_ab_rest.json --skip-done "$O/wins" > "$O/loop.$i.txt"
  python3 -c "import json,sys; sys.exit(0 if json.load(open('logs/idgate/ovn_ab_rest.json'))['jobs'] else 1)" || { echo "ALLDONE after $((i-1))" > "$O/LOOPDONE"; exit 0; }
  [ -f "$O/out" ] && mv "$O/out" "$O/out.loop$i"; [ -f "$O/err" ] && mv "$O/err" "$O/err.loop$i"; rm -f "$O/DONE"
  RESUME=1 bash logs/idgate/run_batch.sh logs/idgate/ovn_ab_rest.json "$O" MTG_RSS_CAP_GB=18
  echo "attempt $i $(cat $O/DONE) $(date -u +%H:%M)" >> "$O/loop.log"
done
echo "GAVE UP after 40" > "$O/LOOPDONE"
