#!/usr/bin/env bash
# Hourly progress trail for the pooled value-leaf regeneration run.
#
# Separate from the supervisor on purpose: the supervisor keeps the run alive, this leaves a durable
# record of what happened while nobody was watching. Keeping them apart also means neither must be
# edited to change the other -- and a long-running .sh must not be edited at all, since bash reads a
# script lazily and can end up executing garbage.
#
# Reports the pooled row count WITH an hourly delta, plus the per-deck breakdown by seed bucket, and
# the process CPU%. Those two together are what distinguish "slow" from "stalled": in a tail the row
# delta goes flat AND cpu falls to ~100% (one thread of 24). That is the signature the pooled design
# exists to confine to one occurrence per phase.
#
#   setsid bash scripts/vlq_heartbeat.sh > /dev/null 2>&1 &
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

VLQ=logs/vlq
HB=$VLQ/heartbeat.log
ALL=$VLQ/rows/all.rows
INTERVAL=${INTERVAL:-3600}
PREV=0

while :; do
    ts=$(date '+%m-%d %H:%M:%S')
    alive=$(pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1 && echo yes || echo NO)
    pid=$(pgrep -x mtg | head -1)
    cpu=$([ -n "$pid" ] && ps -p "$pid" -o %cpu --no-headers | tr -d ' ' || echo -)
    phase=$(ls "$VLQ/done" 2>/dev/null | tr '\n' ',' | sed 's/,$//')
    n=$(grep -vc '^#' "$ALL" 2>/dev/null || echo 0)
    line="[$ts] driver=$alive cpu=${cpu}% rows=$n(+$(( n - PREV ))/h) phases=[${phase:-none}]"
    PREV=$n
    if [ "$n" -gt 0 ]; then
        line="$line |$(awk '!/^#/{c[int($(NF-1)/100000)]++}
            END{split("_ hinata antilife dragon slivers th knights burn auras",nm," ");
                for(b=1;b<=8;b++) if(c[b]) printf " %s=%d", nm[b+1], c[b]}' "$ALL" 2>/dev/null)"
    fi
    [ "$cpu" != "-" ] && awk -v c="$cpu" 'BEGIN{exit !(c<150)}' && line="$line  <- TAIL (one thread busy)"
    echo "$line" >> "$HB"

    [ -e "$VLQ/STOP" ] && { echo "[$ts] STOP -- heartbeat standing down" >> "$HB"; exit 0; }
    if grep -q "=== COMPLETE" "$VLQ/driver.log" 2>/dev/null && [ "$alive" = NO ]; then
        echo "[$ts] run complete -- heartbeat standing down" >> "$HB"; exit 0
    fi
    sleep "$INTERVAL"
done
