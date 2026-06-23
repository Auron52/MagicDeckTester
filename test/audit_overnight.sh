#!/bin/bash
# Per-game overnight audit vs committed gt_logs. For each overnight config prints:
# old/new win counts (+ win-rate delta), faster/slower/lost game counts, and a verdict.
# ACCEPT-safe only if every config preserves/raises win-rate and the slower/lost are
# explained (shuffle two-way churn / burn ordering). Read before --accept.
cd "$(dirname "$0")/.."
WINS=test/logs/overnight/wins
tot_f=0; tot_s=0; tot_l=0; any_drop=0
printf "%-32s %8s %8s %6s %6s %6s\n" "config" "oldWon" "newWon" "fast" "slow" "lost"
for new in $(ls $WINS/*.wins 2>/dev/null | sort); do
  key=$(basename "$new" .wins); gt=test/gt_logs/$key.wins
  [ -f "$gt" ] || { printf "%-32s  (no gt -- NEW)\n" "$key"; continue; }
  read oldw neww f s l < <(paste "$gt" "$new" | awk '
    {n=NF; ow=$2; nw=$4;
     if(ow>0)owon++; if(nw>0)nwon++;
     if(nw!=ow){ if(nw==-1)l++; else if(ow==-1)f++; else if(nw<ow)f++; else s++ } }
    END{printf "%d %d %d %d %d", owon+0, nwon+0, f+0, s+0, l+0}')
  flag=""; [ "$neww" -lt "$oldw" ] && { flag="  <-- WIN-RATE DROP"; any_drop=1; }
  printf "%-32s %8s %8s %6s %6s %6s%s\n" "$key" "$oldw" "$neww" "$f" "$s" "$l" "$flag"
  tot_f=$((tot_f+f)); tot_s=$((tot_s+s)); tot_l=$((tot_l+l))
done
echo "-----"
echo "TOTAL faster=$tot_f slower=$tot_s lost=$tot_l   win-rate-drop-configs=$any_drop"
[ "$any_drop" -eq 0 ] && echo "VERDICT: no config dropped win-rate -- accept-safe pending changed-game spot-check" \
                       || echo "VERDICT: a config DROPPED win-rate -- DO NOT auto-accept; inspect flagged configs"
