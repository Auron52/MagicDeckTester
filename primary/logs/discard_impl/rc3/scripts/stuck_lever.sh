#!/bin/bash
# stuck_lever.sh <arm new|old> <tag> VAR=VAL...  -- the snow gi=221 ship game with lever overrides
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
ARM=$1; TAG=$2; shift 2
OUT=$R/snow/stuck221/${ARM}_${TAG}
export ENV_EXTRA="MTG_DECISION_PROGRESS=1 MTG_TURN_CENSUS=$OUT/census.tsv $*"
start=$(date +%s)
$R/scripts/run1.sh snow ship 221 1540222 "$ARM" "$OUT"
echo "wall_s=$(( $(date +%s) - start ))" >> "$OUT/stdout.txt"
