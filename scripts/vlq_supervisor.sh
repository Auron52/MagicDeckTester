#!/usr/bin/env bash
# Supervisor for the value-leaf regeneration queue -- keeps an unattended multi-day run going.
#
# WHY. The driver exits the whole queue when any per-deck stage fails, so a failure on deck 3 would
# silently cost decks 4-8 overnight. The driver is fully resumable (stage markers), so the right
# response to a dead driver is: work out which deck it died in, drop that ONE deck, and relaunch on
# the rest. This runs OUTSIDE the driver rather than patching it, because the driver was already
# running when this need appeared and bash reads a script lazily -- editing a running script can make
# it execute garbage.
#
# WHAT IT WILL NOT DO. It never relaunches after a FREEZE VIOLATION: that means src/ moved mid-queue,
# so every later measurement would describe a different engine than the earlier ones, and silently
# continuing would produce exactly the internally-inconsistent table this whole queue exists to fix.
# It also stops if logs/vlq/STOP exists -- `touch logs/vlq/STOP` to end the run deliberately without
# racing the supervisor.
#
#   setsid bash scripts/vlq_supervisor.sh > logs/vlq_supervisor.out 2>&1 &
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

VLQ=logs/vlq
DECKS="hinata antilife dragonstorm slivers th knights burn auras"
FAILED=""

slog() { echo "[$(date '+%m-%d %H:%M:%S')] [supervisor] $*" | tee -a "$VLQ/supervisor.log"; }

driver_alive() { pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1; }

remaining() {   # decks with no completed play stage, minus the ones we have given up on
    local d out=""
    for d in $DECKS; do
        [ -e "$VLQ/done/${d}_60_play" ] && continue
        case " $FAILED " in *" $d "*) continue ;; esac
        out="$out $d"
    done
    echo "$out"
}

# Which deck did the driver die in? The driver logs ">>> DECK <key>" on entry, so the last one named
# is where it stopped. (It logs "STOPPED in <key>" too, but only on a stage returning non-zero -- a
# crash or an OOM kill leaves no such line, and this still works.)
last_deck() { grep -o '>>> DECK [a-z]*' "$VLQ/driver.log" 2>/dev/null | tail -1 | awk '{print $3}'; }

slog "watching; decks:$(remaining)"
while :; do
    while driver_alive; do sleep 60; done
    sleep 5                                  # let the driver's final log lines flush

    if [ -e "$VLQ/STOP" ]; then slog "STOP file present -- standing down"; exit 0; fi
    if grep -q "FREEZE VIOLATION" "$VLQ/driver.log" 2>/dev/null; then
        slog "FREEZE VIOLATION in the driver log -- NOT relaunching. src/ moved mid-queue, so"
        slog "  anything measured after it would describe a different engine. Human call."
        exit 1
    fi
    if grep -q "QUEUE COMPLETE" "$VLQ/driver.log" 2>/dev/null; then
        slog "queue completed normally"; exit 0
    fi

    local_failed=$(last_deck)
    if [ -n "$local_failed" ] && [ ! -e "$VLQ/done/${local_failed}_60_play" ]; then
        FAILED="$FAILED $local_failed"
        slog "driver died in deck '$local_failed' -- dropping it and continuing"
    fi
    rest=$(remaining)
    if [ -z "$rest" ]; then slog "nothing left to run (failed:$FAILED)"; exit 0; fi

    slog "relaunching for:$rest   (given up on:${FAILED:- none})"
    ONLY="$rest" setsid bash scripts/valueleaf_regen_queue.sh run >> "$VLQ/run.out" 2>&1 < /dev/null &
    sleep 30
done
