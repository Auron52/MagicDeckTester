#!/usr/bin/env bash
# UNCONTENDED cost probe for MTG_EXEC_FEAS / MTG_HINATA_SUBSET_CREDIT.
#
# WHY THIS IS SERIAL AND PINNED (and not a pooled --batch, which the repo otherwise mandates):
# the number under test IS wall time. The prior ladder measured per-job wall inside a contended
# 24-thread batch and read one EF arm as 38% FASTER than its own control -- uninterpretable. So
# every run here is single-threaded, pinned to ONE logical CPU, and runs alone on an idle box;
# arms are interleaved rep-major so any drift hits all arms alike.
#
# WHAT THE PAIR OF NUMBERS MEANS: the search budget is VIRTUAL (units charged at ConsumeAt()
# sites, all inside the rollout/plan loops). The EF walk lives in the ENUMERATOR's subset gate and
# charges NO units -- so at matched --budget-ms an EF arm can do strictly more real work for the
# same virtual spend. units_total/id_depth (stats pass) says whether the virtual side moved; wall
# says what the uncharged overhead actually costs.
set -u

BIN=./build/Release/mtg
CPU=${CPU:-2}
OUT=logs/ef_cost
REPS=${REPS:-3}
mkdir -p "$OUT/stats"
RES="$OUT/results.tsv"
[ -f "$RES" ] || printf 'rep\tdeck\tarm\twall_s\tavg_turns\n' > "$RES"

HIN=(decks/Hinata2/Hinata2.cod --profile decks/Hinata2/Hinata2.profile.json
     --games ${HGAMES:-200} --seed 6600001 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)
DRG=(decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json
     --games ${DGAMES:-1000} --seed 2002 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)

# arm -> env assignments (empty = shipped defaults)
declare -A ARMENV=(
  [base]=""
  [credit]="MTG_HINATA_SUBSET_CREDIT=1"
  [ef]="MTG_EXEC_FEAS=1"
  [creditef]="MTG_HINATA_SUBSET_CREDIT=1 MTG_EXEC_FEAS=1"
)

run_one() {   # $1=rep $2=deck $3=arm $4=stats(0/1)
  local rep="$1" deck="$2" arm="$3" stats="$4"
  local -a argv
  case "$deck" in hinata) argv=("${HIN[@]}") ;; dragonstorm) argv=("${DRG[@]}") ;; esac
  local envs="${ARMENV[$arm]}"
  [ "$stats" = 1 ] && envs="$envs MTG_ROLLOUT_STATS=1"
  local tmp="$OUT/.run.$$"
  # shellcheck disable=SC2086
  env $envs /usr/bin/time -f "__WALL %e" taskset -c "$CPU" "$BIN" "${argv[@]}" > "$tmp.out" 2> "$tmp.err"
  local wall avg
  wall=$(grep -o '__WALL .*' "$tmp.err" | awk '{print $2}')
  avg=$(grep -o 'avg (turns)   : [0-9.]*' "$tmp.out" | awk '{print $4}')
  if [ "$stats" = 1 ]; then
    { grep -E 'units_total|units\.|id_depth|committed_depth|id_pass' "$tmp.err"; } > "$OUT/stats/$deck.$arm.stats.txt"
    printf 'stats\t%s\t%s\t%s\t%s\n' "$deck" "$arm" "$wall" "$avg" >> "$RES"
  else
    printf '%s\t%s\t%s\t%s\t%s\n' "$rep" "$deck" "$arm" "$wall" "$avg" >> "$RES"
  fi
  echo "[ef-cost] rep=$rep $deck/$arm wall=${wall}s avg=$avg stats=$stats"
  rm -f "$tmp.out" "$tmp.err"
}

# Startup calibration (games=1): profile + exhaustive-keep parse is ~4.5 s on Hinata and is
# arm-independent, so subtracting it turns totals into honest per-game costs.
CAL=1
for rep in c1 c2; do
  HG_SAVE=${HIN[4]}; DG_SAVE=${DRG[4]}
  HIN[4]=$CAL; DRG[4]=$CAL
  run_one "cal-$rep" hinata      base 0
  run_one "cal-$rep" dragonstorm base 0
  HIN[4]=$HG_SAVE; DRG[4]=$DG_SAVE
done

# Timed passes first (no stats counters in the hot path), rep-major.
for rep in $(seq 1 "$REPS"); do
  for arm in base credit ef creditef; do run_one "$rep" hinata      "$arm" 0; done
  for arm in base ef;                  do run_one "$rep" dragonstorm "$arm" 0; done
done
# One instrumented pass per cell for the virtual-side read.
for arm in base credit ef creditef; do run_one stats hinata      "$arm" 1; done
for arm in base ef;                  do run_one stats dragonstorm "$arm" 1; done
echo "[ef-cost] DONE -> $RES"
