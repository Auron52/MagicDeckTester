#!/usr/bin/env bash
# logcmp.sh <tagA> <tagB>: per event, are the two repro game logs identical (runId stripped)?
S=/workspaces/MagicDeckTester/build/scratch/ds-apply4; cd $S
for s in $(tr ' ' '\n' < events.txt | grep . | cut -d: -f1); do
  fa=$(ls rep_$1_$s/*.json 2>/dev/null | head -1); fb=$(ls rep_$2_$s/*.json 2>/dev/null | head -1)
  [ -n "$fa" ] && [ -n "$fb" ] || { echo "$s missing"; continue; }
  if diff -q <(grep -v '"runId"' "$fa") <(grep -v '"runId"' "$fb") > /dev/null; then echo "$s same"; else echo "$s DIFF"; fi
done
