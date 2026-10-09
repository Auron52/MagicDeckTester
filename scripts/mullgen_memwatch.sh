#!/bin/bash
# Memory watchdog for the Snow mulligan generation.
#
# WHY. On 2026-10-02 this 10 GB / 24 GB-swap box became unresponsive to keyboard and mouse for
# minutes and had to be hard-powered-off, losing ~21 h of window. The gen was healthy at its last
# heartbeat (8 rollouts/s, frozen 70.4%, journal committing 0 s ago) and the log simply stops, so
# the engine did not fail -- the BOX went down, and swap thrashing is the prime suspect
# (docs/design/analysis-Snow.md section 7 records this engine's label path spiking 5 GB -> 23 GB).
#
# A killed gen costs only the handful of in-flight cells (the journal is durable and resumes), so
# trading the gen for a live box is always the right trade. This watchdog makes that trade.
#
# It runs under SCHED_FIFO (see launch_with_watchdog.sh) because a normal-priority watchdog is
# exactly what gets starved during the thrash it is supposed to catch -- the box would not schedule
# the user's keyboard, so it would not have scheduled this either.
set -u

LOG=${LOG:-logs/mullgen_memwatch.log}
# Thresholds are deliberately EARLY. This box denies both SCHED_FIFO and negative nice (verified
# 2026-10-03: chrt -> "Operation not permitted", nice -n -19 -> "Permission denied"), so this
# watchdog runs at NORMAL priority and can itself be starved by the thrash it guards against.
# The asymmetry decides the setting: a false trip costs one cheap resume (the journal is durable),
# a missed trip costs the whole box and ~21 h of window. So fire with plenty of headroom rather
# than close to the cliff. The engine's steady state is ~3.1 GB RSS with 5-8 GB available, so
# 2.5 GB available / 256 MB swap is far from normal operation yet well before thrash.
AVAIL_MIN_MB=${AVAIL_MIN_MB:-2500}   # trip if MemAvailable falls below this
SWAP_MAX_MB=${SWAP_MAX_MB:-256}      # trip if swap in use exceeds this
INTERVAL=${INTERVAL:-5}
STRIKES_NEEDED=${STRIKES_NEEDED:-2}  # consecutive bad samples, so a blip does not kill a good run
PATTERN=${PATTERN:-gen-mulligan fast}

# WHICH PHASE THIS IS GUARDING MATTERS, because the cost of a false trip is not symmetric.
#
#   GENERATION  -- journalled. A kill costs the handful of in-flight cells and resumes. Trip EARLY
#                  (the defaults above).
#   VALIDATION  -- NOT journalled, and worse: scripts/mullgen.sh line ~383 treats an A/B that
#                  "failed to run" EXACTLY like a reject and QUARANTINES the profile. So a false
#                  trip would quarantine a ~290 h artifact on a measurement that never happened.
#                  It is recoverable (rename the .DISABLED.json back, per mullgen.sh's own note)
#                  but it is a wrong verdict, which the skill calls worse than no verdict. So guard
#                  validation only at a TERMINAL level -- when the box is already going down and the
#                  batch is doomed regardless:
#                      PATTERN='build/Release/mtg --batch' AVAIL_MIN_MB=700 SWAP_MAX_MB=2048 \
#                      STRIKES_NEEDED=3 bash logs/Snow_mullgen/memwatch.sh
#                  Normal validation sits at ~2.8 GB RSS with 5-6 GB available and zero swap, so
#                  those numbers are far outside ordinary variance.

strikes=0
peak_rss=0

say() { echo "$(date -u '+%Y-%m-%dT%H:%M:%SZ') $*" >> "$LOG"; }

say "watchdog start: trip if MemAvailable<${AVAIL_MIN_MB}MB or swap>${SWAP_MAX_MB}MB (${STRIKES_NEEDED} strikes, ${INTERVAL}s)"

# Wait for the engine to appear. Without this the watchdog loses a startup race against
# mullgen.sh (which has its own preamble before it execs the binary), finds no process on its
# first sample, and exits immediately -- leaving the run unguarded. That happened on the first
# resume attempt 2026-10-03.
for _ in $(seq 120); do
    pgrep -f "$PATTERN" >/dev/null && break
    sleep 5
done
if ! pgrep -f "$PATTERN" >/dev/null; then
    say "engine never appeared after 10 min -- watchdog exiting"
    exit 0
fi
say "engine found: pid=$(pgrep -f "$PATTERN" | head -1)"

while :; do
    pid=$(pgrep -f "$PATTERN" | head -1 || true)
    if [ -z "${pid:-}" ]; then
        say "gen process gone -- watchdog exiting (peak RSS ${peak_rss}MB)"
        exit 0
    fi

    avail=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
    stot=$(awk '/SwapTotal/{print int($2/1024)}' /proc/meminfo)
    sfree=$(awk '/SwapFree/{print int($2/1024)}' /proc/meminfo)
    sused=$((stot - sfree))
    rss=$(awk '/VmRSS/{print int($2/1024)}' "/proc/$pid/status" 2>/dev/null || echo 0)
    [ "$rss" -gt "$peak_rss" ] && peak_rss=$rss

    # one line a minute keeps a usable history without flooding
    if [ $(( $(date +%s) % 60 )) -lt "$INTERVAL" ]; then
        say "rss=${rss}MB avail=${avail}MB swap_used=${sused}MB peak_rss=${peak_rss}MB"
    fi

    if [ "$avail" -lt "$AVAIL_MIN_MB" ] || [ "$sused" -gt "$SWAP_MAX_MB" ]; then
        strikes=$((strikes + 1))
        say "STRIKE ${strikes}/${STRIKES_NEEDED}: rss=${rss}MB avail=${avail}MB swap_used=${sused}MB"
        if [ "$strikes" -ge "$STRIKES_NEEDED" ]; then
            say "TRIPPED -- killing gen pid=$pid to keep the box alive. Journal is durable; resume with:"
            say "    nohup bash logs/Snow_mullgen/launch_with_watchdog.sh > /dev/null 2>&1 &"
            kill -TERM "$pid" 2>/dev/null
            for _ in $(seq 20); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
            kill -0 "$pid" 2>/dev/null && { say "SIGTERM ignored -- SIGKILL"; kill -KILL "$pid" 2>/dev/null; }
            say "gen stopped. peak RSS ${peak_rss}MB"
            exit 1
        fi
    else
        strikes=0
    fi
    sleep "$INTERVAL"
done
