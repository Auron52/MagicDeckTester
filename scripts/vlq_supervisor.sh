#!/usr/bin/env bash
# Supervisor for the pooled value-leaf regeneration run.
#
# The run is three pooled phases and every phase is resumable from its marker, so the only useful
# response to a driver that died unexpectedly (crash, OOM kill) is to start it again: finished phases
# are skipped, and a re-run of an unfinished row batch is harmless because rows dedupe on (seed,turn).
# Capped at 3 restarts so a deterministic failure cannot spin forever.
#
# WILL NOT RESTART after a FREEZE VIOLATION -- src/ moved mid-run, so measurements taken after it
# describe a different engine than those before, and continuing would build exactly the internally
# inconsistent table this run exists to replace. Also stands down on logs/vlq/STOP.
#
#   setsid bash scripts/vlq_supervisor.sh > /dev/null 2>&1 &
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

VLQ=logs/vlq
MAX_RESTARTS=${MAX_RESTARTS:-3}
n=0

slog() { echo "[$(date '+%m-%d %H:%M:%S')] [supervisor] $*" | tee -a "$VLQ/supervisor.log"; }
alive() { pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1; }

slog "watching (max $MAX_RESTARTS restarts)"
while :; do
    while alive; do sleep 60; done
    sleep 5                                   # let final log lines flush

    [ -e "$VLQ/STOP" ] && { slog "STOP file -- standing down"; exit 0; }
    if grep -q "FREEZE VIOLATION" "$VLQ/driver.log" 2>/dev/null; then
        slog "FREEZE VIOLATION -- NOT restarting; src/ moved mid-run. Human call."; exit 1
    fi
    if grep -q "=== COMPLETE" "$VLQ/driver.log" 2>/dev/null; then
        slog "run completed normally"; exit 0
    fi
    n=$(( n + 1 ))
    if [ "$n" -gt "$MAX_RESTARTS" ]; then
        slog "driver died $n times -- giving up rather than looping on a deterministic failure."
        slog "  Rows on disk are still usable: 'bash scripts/valueleaf_regen_queue.sh finish'"
        exit 1
    fi
    slog "driver died with work outstanding -- restart $n/$MAX_RESTARTS (finished phases are skipped)"
    setsid bash scripts/valueleaf_regen_queue.sh run >> "$VLQ/run.out" 2>&1 < /dev/null &
    sleep 30
done
