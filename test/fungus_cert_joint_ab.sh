#!/usr/bin/env bash
# MTG_FUNGUS_CERT_JOINT: does the joint Saproling budget pay, and are the LABELS unchanged?
#
# WHAT THE LEVER IS. The Fungus winless certificate bounds this turn's reachable damage. Today it
# credits "attack with every Saproling" AND "sacrifice every Saproling for a card" from the same
# pool -- admissible, but loose, because a token cannot both attack and be eaten. The joint budget
# prices the pool once. Being <= today's bound at every node, it can only make the certificate fire
# MORE; it can never certify a node today's bound refused.
#
# WHY THAT MATTERS HERE. Measured on the phase-A label path (docs/design/fungus-token-search-cost.md,
# and the straggler read of 2026-09-19), the certificate already refutes 77-84% of horizon-edge
# nodes, and the survivors are what the labeller's whole cost is: 99.7% of ApplyPlanDirect calls are
# `fsw-plans` -- plans applied one at a time at a node asking only "can I win THIS turn?". The
# decline attribution says the survivors are one class almost exclusively:
#
#     FUNGUS WINLESS CERT reasons: fired=21142 combat-lethal=10671
#     combat-lethal breakdown: lord-library=9982   <- 93.5% of the declines
#
# so a tightening that bites on the lord terms is aimed at the right place.
#
# TWO PROCESSES, NOT ONE POOLED BATCH -- and that is deliberate. The repo's rule is to pool every
# arm into one `mtg --batch`, because a per-arm wave strands cores on each wave's tail. It cannot be
# done here: `FungusCertJointOn()` is a `static const bool`, read once per PROCESS, so one batch
# cannot carry both arms. The rule's purpose is preserved the way test/subset_filter_ab.sh preserves
# it -- both arms are launched CONCURRENTLY and share the box at the same instant, and each arm is
# itself a pooled batch, so there is exactly one tail per arm rather than one per game.
#
# THE CORRECTNESS ASSERTION IS THE LABEL FILE, and it is the load-bearing half. An UNSOUND
# certificate certifies a node that was really a win, the search drops that turn, and the label
# comes back LATER than the truth -- a quality loss, which is the one direction this repo will not
# trade for wall clock. Rows are compared SORTED because the dump is multi-threaded and row order
# varies run to run (the same reason phase_split sorts).
#
# The cost column is ApplyPlanDirect calls (`LABEL WORK`), not wall: it is the labeller's own work
# counter, so it does not move with how the scheduler happened to slice the box.
#
#   bash test/fungus_cert_joint_ab.sh <tag> [jobs] [games-per-job] [seed-base]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

TAG=${1:?usage: fungus_cert_joint_ab.sh <tag> [jobs] [games-per-job] [seed-base]}
JOBS=${2:-16}
GPJ=${3:-2}
BASE=${4:-62000}
THREADS=${THREADS:-12}          # per arm; two arms => 2 x THREADS on the box
BIN=${BIN:-build/Release/mtg}
DECK=decks/Fungus/Fungus.cod
PROF=decks/Fungus/Fungus.profile.json

OUT=logs/fungus_joint/$TAG
mkdir -p "$OUT"

# Seeds spaced by `games` so no two jobs replay the same shuffle (an overlap inflates agreement).
python3 - "$OUT/manifest.json" "$JOBS" "$GPJ" "$BASE" <<'PY'
import json, sys
out, jobs, gpj, base = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
json.dump({"jobs": [{"name": f"fj_{i:03d}", "deck": "decks/Fungus/Fungus.cod",
                     "profile": "decks/Fungus/Fungus.profile.json",
                     "games": gpj, "seed": base + i * gpj, "game_index": 0}
                    for i in range(jobs)]}, open(out, "w"), indent=1)
PY
echo "$JOBS jobs x $GPJ games = $((JOBS * GPJ)) games per arm, seeds $BASE..$((BASE + JOBS * GPJ - 1))"

for J in 0 1; do
    rows="$OUT/rows.J$J"; rm -f "$rows"
    ( st=$(date +%s.%N)
      MTG_DUMP_VALUE_ROWS="$rows" MTG_EVAL_ROWS_K=3 MTG_EVAL_ROWS_ROLLOUT=0 \
      MTG_FUNGUS_CERT_JOINT="$J" MTG_WINLESS_STATS=1 \
          "$BIN" --batch "$OUT/manifest.json" --threads "$THREADS" \
          > "$OUT/arm.J$J.log" 2>&1
      date +%s.%N | awk -v a="$st" '{printf "%.1f\n", $1-a}' > "$OUT/sec.J$J" ) &
done
echo "both arms launched concurrently; waiting"
wait

applies() { grep -o 'LABEL WORK: ApplyPlanDirect calls=[0-9]*' "$1" | tail -1 | grep -o '[0-9]*$'; }
fired()   { grep -o 'WINLESS CERT\[m1\]: checks=[0-9]* fired=[0-9]* ([0-9.]*%)' "$1" | tail -1; }

a0=$(applies "$OUT/arm.J0.log"); a1=$(applies "$OUT/arm.J1.log")
s0=$(cat "$OUT/sec.J0" 2>/dev/null); s1=$(cat "$OUT/sec.J1" 2>/dev/null)
r0=$(grep -vc '^#' "$OUT/rows.J0" 2>/dev/null || echo 0)
r1=$(grep -vc '^#' "$OUT/rows.J1" 2>/dev/null || echo 0)

echo
echo "arm OFF : applies=${a0:-?}  wall=${s0:-?}s  rows=$r0   $(fired "$OUT/arm.J0.log")"
echo "arm ON  : applies=${a1:-?}  wall=${s1:-?}s  rows=$r1   $(fired "$OUT/arm.J1.log")"
[ -n "${a0:-}" ] && [ -n "${a1:-}" ] && [ "${a1:-0}" -gt 0 ] && \
    awk -v a="$a0" -v b="$a1" 'BEGIN{printf "\nlabel work: %.3fx fewer ApplyPlanDirect calls\n", a/b}'
[ -n "$s0" ] && [ -n "$s1" ] && \
    awk -v a="$s0" -v b="$s1" 'BEGIN{if(b>0) printf "wall:       %.3fx (contention-shared, both arms concurrent)\n", a/b}'

# The half that decides adoption.
if cmp -s <(grep -v '^#' "$OUT/rows.J0" | sort) <(grep -v '^#' "$OUT/rows.J1" | sort); then
    echo "LABELS: IDENTICAL on $r0 rows (the tightening pruned only provably-winless turns)"
else
    echo "LABELS: *** DIFFER *** -- the joint bound is UNSOUND or the rows are incomplete."
    diff <(grep -v '^#' "$OUT/rows.J0" | sort) <(grep -v '^#' "$OUT/rows.J1" | sort) | head -20
fi
grep -h "JOINT reduced-vs-exhaustive" "$OUT/arm.J1.log" | tail -1
