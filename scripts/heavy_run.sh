#!/usr/bin/env bash
# heavy_run.sh -- run ONE heavy command (build, mtg --batch, tier) under the box-wide heavy lock,
# and hand the lock on early once the command has settled into a TAIL.
#
#   scripts/heavy_run.sh <cmd> [args...]          (exit status = the command's)
#
# The box runs one heavy process at a time, at full threads and the default cache budget,
# serialized through `flock build/heavy.lock`. A plain flock keeps the lock until the command exits,
# so a d8 / budget-0 batch whose last few games run for an hour leaves ~all cores idle while the
# queue waits. USER 2026-11-19 (asked "allow a second heavy job during d8 tails?"): "Yes, that seems fine."
#
# Release rule (all must hold for TAIL_SAMPLES consecutive samples, SAMPLE_S apart):
#   * the 1-minute load average is below nproc/4 (the job is using few cores), and
#   * MemAvailable >= MIN_AVAIL_GB (a default-budget job, ~RAM/2 of cache, still fits), and
#   * no OTHER released tail is still alive (at most ONE extra job, never a third), and
#   * the command is ONE `mtg --batch` run, not a shell script, build or chain (see TAIL-ELIGIBLE below).
# The released command keeps running to completion; only the lock moves on. Plain
# `flock build/heavy.lock <cmd>` waiters are fully compatible (same lock file).
set -u
PRIMARY=/workspaces/MagicDeckTester
LOCK=${MTG_HEAVY_LOCK:-$PRIMARY/build/heavy.lock}
TAILS=$(dirname "$LOCK")/heavy.tails
SAMPLE_S=${MTG_HEAVY_SAMPLE_S:-30}
TAIL_SAMPLES=${MTG_HEAVY_TAIL_SAMPLES:-10}          # 10 x 30 s = 5 min of sustained tail
MIN_AVAIL_GB=${MTG_HEAVY_MIN_AVAIL_GB:-13}
NPROC=$(nproc)
LOAD_MAX=$(( NPROC / 4 ))
[ "$#" -ge 1 ] || { echo "usage: heavy_run.sh <cmd> [args...]" >&2; exit 2; }
mkdir -p "$TAILS"

# TAIL-ELIGIBLE: only a single `--batch` run has a tail worth handing on. A multi-stage command (a chain
# script, `build_both.sh`, a build whose link step is single-threaded) has LIGHT STAGES that look exactly like
# a tail, and the stage after one then runs OUTSIDE the lock. 2026-11-20 07:16: an idgate chain was released
# during a light probe stage, then started a 32-thread batch beside a 12-job build -- MemAvailable 0.7 GB,
# 13 GB in swap. So: eligible only when an argument is `--batch` and the command is not a shell.
# MTG_HEAVY_TAIL=0 forces no release for any command. Wrap EACH heavy stage of a chain in its own heavy_run.
tail_ok=0
case "$(basename "$1")" in
  bash|sh|dash|zsh) ;;
  *) for a in "$@"; do [ "$a" = "--batch" ] && tail_ok=1; done ;;
esac
[ "${MTG_HEAVY_TAIL:-1}" = 0 ] && tail_ok=0

exec 9>>"$LOCK"
flock 9
"$@" 9>&- &
pid=$!

other_tail_alive() {
  local f p
  for f in "$TAILS"/*; do
    [ -e "$f" ] || continue
    p=${f##*/}
    [ "$p" = "$pid" ] && continue
    if kill -0 "$p" 2>/dev/null; then return 0; fi
    rm -f "$f"                                   # stale marker of a finished tail
  done
  return 1
}

released=0 streak=0
while kill -0 "$pid" 2>/dev/null; do
  sleep "$SAMPLE_S"
  [ "$released" = 1 ] && continue
  [ "$tail_ok" = 1 ] || continue
  kill -0 "$pid" 2>/dev/null || break
  load=$(cut -d' ' -f1 /proc/loadavg)
  avail_kb=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo)
  if awk -v l="$load" -v m="$LOAD_MAX" 'BEGIN{exit !(l < m)}' \
     && [ "$avail_kb" -ge $(( MIN_AVAIL_GB * 1024 * 1024 )) ] && ! other_tail_alive; then
    streak=$(( streak + 1 ))
  else
    streak=0
  fi
  if [ "$streak" -ge "$TAIL_SAMPLES" ]; then
    echo "$(date -u +%FT%TZ) $* (load $load, avail $(( avail_kb / 1048576 )) GB)" > "$TAILS/$pid"
    flock -u 9; exec 9>&-
    released=1
    echo "[heavy_run] $(date -u +%T) tail detected (load $load < $LOAD_MAX); heavy lock handed on, pid $pid keeps running" >&2
  fi
done
wait "$pid"; rc=$?
rm -f "$TAILS/$pid"
exit "$rc"
