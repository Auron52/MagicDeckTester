#!/bin/bash
# usage: watch_status.sh <tag> -- print new status lines as they appear; exit at the DONE marker.
F=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/$1.status
D=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/$1.done
prev=0
while true; do
  n=$(wc -l < "$F" 2>/dev/null || echo 0)
  if [ "$n" != "$prev" ]; then tail -n +$((prev+1)) "$F"; prev=$n; fi
  if [ -f "$D" ]; then echo "DONE marker: $(cat "$D")"; exit 0; fi
  sleep 15
done
