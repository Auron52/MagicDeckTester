#!/usr/bin/env bash
# Hourly progress trail for the value-leaf regeneration queue.
#
# Separate from the supervisor on purpose: the supervisor's job is to keep the run alive, this one's
# is to leave a durable record of what happened while nobody was watching. Keeping them apart also
# means neither has to be edited to change the other -- and a long-running .sh must not be edited at
# all, since bash reads a script lazily and can end up executing garbage.
#
# Appends ONE line per deck per hour to logs/vlq/heartbeat.log, plus a rate so a stall is obvious:
# rows that stop climbing while the driver is still alive is the signature of the pathological-game
# tail, which is exactly what the pooled-batch design exists to avoid.
#
#   setsid bash scripts/vlq_heartbeat.sh > /dev/null 2>&1 &
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

VLQ=logs/vlq
HB=$VLQ/heartbeat.log
INTERVAL=${INTERVAL:-3600}
declare -A PREV

while :; do
    ts=$(date '+%m-%d %H:%M:%S')
    alive=$(pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1 && echo yes || echo NO)
    mtg=$(pgrep -c -x mtg 2>/dev/null || echo 0)
    cur=$(grep -o '>>> DECK [a-z]*' "$VLQ/driver.log" 2>/dev/null | tail -1 | awk '{print $3}')
    line="[$ts] driver=$alive mtg=$mtg deck=${cur:-none}"
    for stem in Hinata2 Anti-Lifegain Dragonstorm slivers_vial treasure_hunt Knights burn Auras; do
        n=$(grep -vc '^#' "$VLQ/rows/$stem.rows" 2>/dev/null || echo 0)
        [ "$n" -eq 0 ] && continue
        d=$(( n - ${PREV[$stem]:-0} ))
        PREV[$stem]=$n
        line="$line | $stem=$n(+$d/h)"
    done
    stages=$(ls "$VLQ/done" 2>/dev/null | grep -c . || echo 0)
    echo "$line | stages_done=$stages" >> "$HB"

    if [ -e "$VLQ/STOP" ]; then echo "[$ts] STOP file -- heartbeat standing down" >> "$HB"; exit 0; fi
    if grep -q "QUEUE COMPLETE" "$VLQ/driver.log" 2>/dev/null && [ "$alive" = NO ]; then
        echo "[$ts] queue complete -- heartbeat standing down" >> "$HB"; exit 0
    fi
    sleep "$INTERVAL"
done
