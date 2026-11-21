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
#   * the job ITSELF uses at most nproc/3 cores (its own utime+stime over the sample, all threads), and
#   * MemAvailable >= MIN_AVAIL_GB (a default-budget job, ~RAM/2 of cache, still fits), and
#   * no OTHER released tail is still alive (at most ONE extra job, never a third), and
#   * the command is ONE `mtg --batch` run, not a shell script, build or chain (see TAIL-ELIGIBLE below).
# The released command keeps running to completion; only the lock moves on. Plain
# `flock build/heavy.lock <cmd>` waiters are fully compatible (same lock file).
#
# Why the job's own CPU, not the load average (USER 2026-11-21: "an ~8 core average should not hold up all
# others on a 32-core machine"): the rule used to be "1-minute load < nproc/4". A d8 b0 batch with exactly 8
# one-core games left sat at load ~8.3 on 32 cores for over an hour, never under 8, and held the lock while
# a landing validation waited (Maze's End batch 3, 2026-11-21). The load average also counts every other
# process on the box, so a small-lane job could keep a tail from releasing. The job's own CPU is what says
# it is in a tail; memory is the separate MemAvailable check below.
set -u
PRIMARY=/workspaces/MagicDeckTester
LOCK=${MTG_HEAVY_LOCK:-$PRIMARY/build/heavy.lock}
TAILS=$(dirname "$LOCK")/heavy.tails
SAMPLE_S=${MTG_HEAVY_SAMPLE_S:-30}
TAIL_SAMPLES=${MTG_HEAVY_TAIL_SAMPLES:-10}          # 10 x 30 s = 5 min of sustained tail
MIN_AVAIL_GB=${MTG_HEAVY_MIN_AVAIL_GB:-13}
NPROC=$(nproc)
CPU_MAX=${MTG_HEAVY_TAIL_CPU_MAX:-$(awk -v n="$NPROC" 'BEGIN{printf "%.2f", n/3}')}   # cores
CLK_TCK=$(getconf CLK_TCK)
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

# Total CPU ticks (utime+stime, every thread) of the job's process TREE: the command and all its live
# descendants (`env VAR=x cmd` execs, so $pid is the command itself; a multi-process job is summed too).
# The comm field may hold spaces, so fields are read after the last ')'. Prints 0 once the tree is gone.
job_ticks() {
  cat /proc/[0-9]*/stat 2>/dev/null | awk -v root="$pid" '
    { i = match($0, /\) [A-Za-z] /); if (!i) next; split(substr($0, i + 2), f, " ")
      p = $1; pp[p] = f[2]; t[p] = f[12] + f[13] }
    END { for (p in t) { q = p; while (q != "" && q != root && q in pp && q > 1) q = pp[q]; if (q == root) s += t[p] }
          print s + 0 }'
}

released=0 streak=0 prev_ticks=$(job_ticks)
while kill -0 "$pid" 2>/dev/null; do
  sleep "$SAMPLE_S"
  [ "$released" = 1 ] && continue
  [ "$tail_ok" = 1 ] || continue
  kill -0 "$pid" 2>/dev/null || break
  ticks=$(job_ticks)
  cores=$(awk -v a="$prev_ticks" -v b="$ticks" -v hz="$CLK_TCK" -v s="$SAMPLE_S" 'BEGIN{printf "%.2f", (b - a) / hz / s}')
  # A descendant exiting takes its ticks with it, so a NEGATIVE delta is unknown, not idle: count it busy.
  awk -v a="$prev_ticks" -v b="$ticks" 'BEGIN{exit !(b < a)}' && cores=$NPROC
  prev_ticks=$ticks
  avail_kb=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo)
  if awk -v c="$cores" -v m="$CPU_MAX" 'BEGIN{exit !(c <= m)}' \
     && [ "$avail_kb" -ge $(( MIN_AVAIL_GB * 1024 * 1024 )) ] && ! other_tail_alive; then
    streak=$(( streak + 1 ))
  else
    streak=0
  fi
  if [ "$streak" -ge "$TAIL_SAMPLES" ]; then
    echo "$(date -u +%FT%TZ) $* (job cores $cores, avail $(( avail_kb / 1048576 )) GB)" > "$TAILS/$pid"
    flock -u 9; exec 9>&-
    released=1
    echo "[heavy_run] $(date -u +%T) tail detected (job using $cores cores <= $CPU_MAX); heavy lock handed on, pid $pid keeps running" >&2
  fi
done
wait "$pid"; rc=$?
rm -f "$TAILS/$pid"
exit "$rc"
