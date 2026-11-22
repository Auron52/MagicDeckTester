#!/bin/bash
# SMALL-JOB LANE beside the one big run (USER 2026-11-20).
#
# The box runs ONE big engine process at a time under build/scratch/claude_box.lock (USER 2026-11-04, after an
# OOM container kill: MTG_RSS_CAP_GB is per PROCESS, so several big processes at once have no memory protection).
# Small work -- a single game or a few, a d8 b0 replay, a unit-test run, a worktree build -- used to queue behind
# that lock for hours. The USER (2026-11-20): "Sure, I'm up for this. I wouldn't try 2 32-core processes, but your
# suggestion makes sense." So up to LANE_SLOTS (4) cores of small jobs may run beside the big run, each slot
# memory-capped and admitted only when the memory is actually free.
#
# usage: scripts/box_lane.sh [--slots N] [--cap-gb G] -- <command...>
#   --slots N   cores the job uses (1..4, default 1). An mtg batch must pass --threads <= N; a build gets
#               MTG_BUILD_JOBS=N. The job waits until N slots are free at once.
#   --cap-gb G  memory cap PER SLOT (default 6). The job runs with MTG_RSS_CAP_GB = N*G (the engine aborts past it)
#               and is admitted only when MemAvailable >= N*G + 2 GB.
# Not for a big run: anything that wants more than 4 cores goes under claude_box.lock as before.
set -u
slots=1; cap=6
while [ $# -gt 0 ]; do
  case "$1" in
    --slots) slots="$2"; shift 2 ;;
    --cap-gb) cap="$2"; shift 2 ;;
    --) shift; break ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { echo "usage: $0 [--slots N] [--cap-gb G] -- <command...>" >&2; exit 2; }
max=4
if [ "$slots" -lt 1 ] || [ "$slots" -gt "$max" ]; then echo "box_lane: --slots must be 1..$max" >&2; exit 2; fi
dir=/workspaces/MagicDeckTester/build/scratch/lane
mkdir -p "$dir"
need_gb=$(( slots * cap + 2 ))
# A request the box can never satisfy would hold queue.lock forever and block every other lane job (2026-11-22:
# a 4-slot build at the default cap needs 26 GB on a 23 GB box; it waited 81 min). Shrink the per-slot cap so the
# job needs at most MemTotal - 4 GB, and say so.
total_gb=$(awk '/MemTotal/ {printf "%d", $2/1048576}' /proc/meminfo)
if [ "$need_gb" -gt $(( total_gb - 4 )) ]; then
  cap=$(( (total_gb - 6) / slots ))
  if [ "$cap" -lt 1 ]; then echo "box_lane: ${total_gb} GB box cannot host ${slots} slot(s)" >&2; exit 2; fi
  need_gb=$(( slots * cap + 2 ))
  echo "box_lane: ${total_gb} GB box -- per-slot cap lowered to ${cap} GB (needs ${need_gb} GB free)" >&2
fi

# FIFO-ish admission: only the waiter holding queue.lock tries for slots, so a 4-slot build is not starved by a
# stream of 1-slot jobs grabbing each slot as it frees (2026-11-20: two agents' 4-slot builds waited 1 h 45 min).
exec {qfd}>"$dir/queue.lock"
flock "$qfd"
while :; do
  held=()
  for i in $(seq 1 $max); do
    [ ${#held[@]} -ge "$slots" ] && break
    exec {fd}>"$dir/slot$i.lock"
    if flock -n "$fd"; then held+=("$fd"); else exec {fd}>&-; fi
  done
  if [ ${#held[@]} -ge "$slots" ]; then
    avail=$(awk '/MemAvailable/ {printf "%d", $2/1048576}' /proc/meminfo)
    if [ "$avail" -ge "$need_gb" ]; then break; fi
    echo "box_lane: $(date -u +%H:%M) waiting for memory (${avail} GB available, need ${need_gb})" >&2
  fi
  for f in "${held[@]}"; do exec {f}>&-; done   # never sit on a partial set (no deadlock between waiters)
  sleep 30
done

exec {qfd}>&-   # admitted: let the next waiter try
echo "box_lane: $(date -u +%H:%M) running on ${slots} slot(s), MTG_RSS_CAP_GB=$(( slots * cap )): $*" >&2
env MTG_RSS_CAP_GB=$(( slots * cap )) MTG_BUILD_JOBS="$slots" MTG_LANE_SLOTS="$slots" "$@"
