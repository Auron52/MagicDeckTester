#!/bin/bash
# Run the doctest unit binary SHARDED across cores (USER 2026-11-21: "Nothing should be single-threaded if we
# can avoid it..."). A plain `build/Release/mtg-test` runs every test case serially on ONE core (~1.5-4 min); this
# splits the case list into ranges with doctest's --first/--last and runs them in parallel, then checks that every
# case ran exactly once and that every shard passed.
#
# usage: scripts/unit_par.sh [mtg-test binary] [jobs] [-- extra doctest args...]
#   binary  default build/Release/mtg-test
#   jobs    default `nproc` (the affinity-honouring core count; never a constant)
# Exit status: 0 when every shard passed and the shard counts add up to the full count; non-zero otherwise.
# Per-shard output: $UNIT_PAR_DIR (default build/scratch/unit_par/<pid>); a failing shard's log is printed.
set -u
BIN=${1:-build/Release/mtg-test}
JOBS=${2:-$(nproc)}
shift $(( $# >= 2 ? 2 : $# ))
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
[ -x "$BIN" ] || { echo "unit_par: no executable $BIN" >&2; exit 2; }
DIR=${UNIT_PAR_DIR:-build/scratch/unit_par/$$}
mkdir -p "$DIR"
TOTAL=$("$BIN" --count "${EXTRA[@]}" 2>&1 | grep -oE '^\[doctest\] .*passing the current filters: *[0-9]+' | grep -oE '[0-9]+$')
[ -n "$TOTAL" ] && [ "$TOTAL" -gt 0 ] || { echo "unit_par: could not read the test count" >&2; exit 2; }
# Many more shards than cores, so an expensive case does not leave the rest of the box idle behind it.
SHARDS=$(( JOBS * 4 )); [ "$SHARDS" -gt "$TOTAL" ] && SHARDS=$TOTAL
PER=$(( (TOTAL + SHARDS - 1) / SHARDS ))
seq 0 $(( SHARDS - 1 )) | xargs -P "$JOBS" -I{} bash -c '
  i={}; f=$(( i * '"$PER"' + 1 )); l=$(( (i + 1) * '"$PER"' ))
  [ "$f" -le '"$TOTAL"' ] || exit 0
  "'"$BIN"'" --first=$f --last=$l "${@}" > "'"$DIR"'/shard_$i.log" 2>&1; echo $? > "'"$DIR"'/shard_$i.rc"' _ "${EXTRA[@]}"
fail=0; ran=0
for rc in "$DIR"/shard_*.rc; do
  i=${rc##*_}; i=${i%.rc}; log="$DIR/shard_$i.log"
  # "[doctest] test cases: <all> | <p> passed | <f> failed | <s> skipped": count what this shard RAN (p + f).
  line=$(grep -E '^\[doctest\] test cases:' "$log" | tail -1)
  p=$(echo "$line" | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+'); f=$(echo "$line" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+')
  ran=$(( ran + ${p:-0} + ${f:-0} ))
  if [ "$(cat "$rc")" != 0 ]; then fail=1; echo "=== shard $i FAILED (rc=$(cat "$rc")) ==="; grep -nE 'ERROR|FATAL|failed' "$log" | head -40; fi
done
echo "[unit_par] $ran / $TOTAL test cases in $SHARDS shards on $JOBS cores; $( [ $fail = 0 ] && echo PASS || echo FAIL )"
[ "$ran" = "$TOTAL" ] || { echo "[unit_par] shard counts do not add up to the full count" >&2; exit 3; }
exit $fail
