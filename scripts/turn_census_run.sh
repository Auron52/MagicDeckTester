#!/usr/bin/env bash
# Produce a per-decision work census (MTG_TURN_CENSUS) at full machine utilisation.
#
#   bash scripts/turn_census_run.sh <deck-dir-or-file> <depth> <budget_ms> <games> [outdir] [jobs]
#
# e.g. bash scripts/turn_census_run.sh decks/Snow 3 10 320 logs/turncensus/snow_d3
#      python3 scripts/turn_census.py summary logs/turncensus/snow_d3/*.tsv
#
# WHY THIS IS MANY PROCESSES AND NOT ONE `mtg --batch`.  The repo's batching rule
# (CLAUDE.md: "long / multi-item runs MUST batch into ONE pooled work queue") exists because a loop
# of small invocations strands cores on every invocation's tail.  It is not violated here, it is
# INAPPLICABLE: the census works by diffing PROCESS-GLOBAL atomic counters across one decision, so
# two games played concurrently INSIDE one process make every delta the sum of two decisions.  That
# is why `--batch`, whose workers are threads, cannot host this instrument at all -- the engine
# detects it and stamps `contended=1` on every affected row, and the analyzer then refuses to rank
# them.  Separate single-threaded PROCESSES have no shared counters, so N of them are N clean
# censuses.  The cores stay saturated because each process is given a whole chunk of games (one
# tail per process, and the chunks are equal-sized), which is the property the batching rule is
# actually protecting.
#
# Seed allocation: chunk i plays `games` consecutive seeds starting at base + i*games, so no two
# chunks measure the same game.  (Memory `batch-game-repro-seed`: consecutive per-job seeds in an
# ad-hoc manifest silently re-measure the same games unless the stride exceeds games/job.)
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${MTG_BIN:-$root/build/Release/mtg}"
[ -f "$BIN" ] || BIN="$root/build/Release/mtg.exe"
if [ ! -f "$BIN" ]; then echo "ERROR: no binary -- run ./build.sh first" >&2; exit 2; fi

deck_in="${1:?deck dir or file}"
depth="${2:?lookahead depth}"
budget="${3:?budget ms (per-decision virtual ms; 0 = greedy, which emits no rows)}"
games="${4:?games per job}"
outdir="${5:-$root/logs/turncensus/run}"
jobs="${6:-$(nproc)}"
base="${CENSUS_SEED_BASE:-910000}"

# Deck resolution mirrors the per-deck folder layout (CLAUDE.md): decks/<name>/<name>.{cod,txt}
# plus the sibling profile.  Passing the folder is the documented form.
if [ -d "$deck_in" ]; then
  name="$(basename "$deck_in")"
  deck=""
  for ext in cod txt; do [ -f "$deck_in/$name.$ext" ] && deck="$deck_in/$name.$ext" && break; done
  [ -n "$deck" ] || { echo "ERROR: no $name.cod/.txt in $deck_in" >&2; exit 2; }
  prof="$deck_in/$name.profile.json"
else
  deck="$deck_in"
  prof="${deck%.*}.profile.json"
fi
prof_arg=()
[ -f "$prof" ] && prof_arg=(--profile "$prof")

if [ "$budget" -eq 0 ]; then
  echo "NOTE: budget_ms=0 is the greedy (d0) path.  Both census roots require a real bounded" >&2
  echo "      budget, so a d0 run emits ZERO rows -- that is correct, not a failure: there is no" >&2
  echo "      search decision to account for.  Use the deck's searched cell instead." >&2
fi

mkdir -p "$outdir"
rm -f "$outdir"/chunk_*.tsv
echo "census: deck=$deck depth=$depth budget_ms=$budget games=$games x jobs=$jobs"
echo "        seeds $base .. $((base + jobs*games - 1))  ->  $outdir"

pids=()
for ((i=0; i<jobs; i++)); do
  s=$((base + i*games))
  # --threads 1 is the correctness requirement, not a tuning choice (see the header note).
  MTG_TURN_CENSUS="$outdir/chunk_$i.tsv" \
  MTG_TURN_CENSUS_MIN_UNITS="${CENSUS_MIN_UNITS:-0}" \
    "$BIN" "$deck" "${prof_arg[@]}" --games "$games" --seed "$s" \
      --depth "$depth" --budget-ms "$budget" --threads 1 \
      > "$outdir/chunk_$i.log" 2>&1 &
  pids+=($!)
done

# Wait on captured PIDs only (memory `never-pkill-by-name-in-a-waiter` / `never-chain-runs-on-pgrep`:
# a pgrep -f waiter matches its own command line, and a name-based kill in a waiter has taken out
# unrelated work in this repo before).
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=$((fail+1)); done

rows=$(cat "$outdir"/chunk_*.tsv 2>/dev/null | grep -vc '^[#s]' || true)
cont=$(cat "$outdir"/chunk_*.tsv 2>/dev/null | awk -F'\t' '$9==1' | wc -l)
echo "done: $rows decision rows, $cont contended, $fail job(s) exited non-zero"
if [ "$cont" -gt 0 ]; then
  echo "*** $cont CONTENDED rows -- a chunk ran more than one thread.  Those rows' counter" >&2
  echo "    columns are not attributable and the analyzer drops them." >&2
fi
echo "read it with:  python3 scripts/turn_census.py summary $outdir/chunk_*.tsv"
