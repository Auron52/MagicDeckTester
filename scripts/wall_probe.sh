#!/usr/bin/env bash
# UNCONTENDED wall-cost probe. Supersedes scripts/ef_cost_probe.sh (same method, arm set now
# driven by ARMS= so one script serves every "what does this lever cost in wall time" question).
#
# WHY THIS IS SERIAL AND PINNED (and not a pooled --batch, which the repo otherwise mandates):
# the number under test IS wall time. A pooled batch measures per-job wall under 24-way
# self-contention, which once read one EF arm as 38% FASTER than its own control -- uninterpretable.
# So every run here is single-threaded, pinned to ONE logical CPU, and runs alone on an idle box;
# arms are interleaved rep-major so any drift hits all arms alike.
#
# WHY ALL ARMS SHARE ONE PIN rather than getting a core each (which would be 5x faster): the host
# is a 12900K, a HYBRID part whose P- and E-cores differ by ~2x, and WSL2 exposes a SYNTHETIC flat
# topology (12x2) that does not say which is which. Spreading arms across cores could therefore
# compare a P-core arm against an E-core one and call the difference a lever cost. Measured
# 2026-09-01: cpus 0/2/6/10/14/18/22 all ran the same 12 games in 3.87-4.00s, i.e. this container
# sees a homogeneous set -- but that is a property of the current VM, not a guarantee, and one pin
# is immune to it either way.
#
# WHAT THE PAIR OF NUMBERS MEANS: the search budget is VIRTUAL (units charged at ConsumeAt() sites,
# all inside the rollout/plan loops). Enumerator work -- the EF walk, the BP_NODE option expansion --
# charges NO units, so at matched --budget-ms an arm can do strictly more real work for the same
# virtual spend. units_total/id_depth (stats pass) says whether the virtual side moved; wall says
# what the uncharged overhead actually costs. A lever that is quality-neutral at matched budget_ms
# but spends more units is NOT free -- see docs/design/bp-node-partition.md.
set -u

BIN=./build/Release/mtg
CPU=${CPU:-2}
OUT=${OUT:-logs/wall_probe}
REPS=${REPS:-3}
DECKS=${DECKS:-hinata}
ARMS=${ARMS:-"base recipe node ef credit"}
mkdir -p "$OUT/stats"
RES="$OUT/results.tsv"
[ -f "$RES" ] || printf 'rep\tdeck\tarm\twall_s\tavg_turns\n' > "$RES"

HIN=(decks/Hinata2/Hinata2.cod --profile decks/Hinata2/Hinata2.profile.json
     --games ${HGAMES:-200} --seed 6600001 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)
DRG=(decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json
     --games ${DGAMES:-200} --seed 2002 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)
# The two MTG_BP_NODE_D56 movers (site 5 / site 6). d5/20ms is not an imposed setting here: the
# batch's own [play] lines report both decks resolving to exactly depth=5 budget=20ms source=default,
# so --ignore-play-profile reproduces their shipped settings rather than overriding them.
MIR=(decks/"Mirrorwing Dragon"/"Mirrorwing Dragon".cod
     --profile decks/"Mirrorwing Dragon"/"Mirrorwing Dragon".profile.json
     --games ${MGAMES:-200} --seed 6600001 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)
KIT=(decks/KittyEquipment/KittyEquipment.cod --profile decks/KittyEquipment/KittyEquipment.profile.json
     --games ${KGAMES:-200} --seed 6600001 --depth 5 --budget-ms 20 --ignore-play-profile --threads 1)

# arm -> env assignments (empty = shipped defaults). "recipe" is the LOSSY greedy-free form
# (EnumeratePlans drops the empty combination, so cands[0] can never be "cast nothing"); "node"
# adds kBpEmptyChoice back and is the SOUND one. Cost of both is reported because the difference
# between them is the price of that one restored option.
declare -A ARMENV=(
  [base]=""
  [recipe]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NO_GREEDY_CONT=1"
  [node]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NO_GREEDY_CONT=1 MTG_BP_NODE=1"
  [ef]="MTG_EXEC_FEAS=1"
  [credit]="MTG_HINATA_SUBSET_CREDIT=1"
  [creditef]="MTG_HINATA_SUBSET_CREDIT=1 MTG_EXEC_FEAS=1"
  # MTG_BP_NODE_D56 (stage 1): the node hosting the other two DEFERRED breakpoint classes. Priced
  # against BOTH the plain node and node+D0ONLY, since D0ONLY is the form it would ship with.
  [nodeonly]="MTG_BP_NODE=1"
  [d56]="MTG_BP_NODE=1 MTG_BP_NODE_D56=1"
  [node0]="MTG_BP_NODE=1 MTG_BP_NODE_D0ONLY=1"
  [d560]="MTG_BP_NODE=1 MTG_BP_NODE_D0ONLY=1 MTG_BP_NODE_D56=1"
  # The SOUND recipe (2026-09-02 adoption gate) and its EF extension. Every flag is pinned
  # EXPLICITLY in both directions so the arms mean the same thing on a default-OFF and a
  # default-ON (adopted) binary -- [base]="" does not have that property once defaults flip.
  [off]="MTG_BP_SITE3=0 MTG_BP_SITE3_DEFER=0 MTG_BP_NODE=0 MTG_BP_NODE_ROOTTURN=0 MTG_BP_CANON_CONT=0"
  [screcipe]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=1"
  # Sound-recipe WALL DECOMPOSITION arms (2026-09-02: recipe wall = hinata +31% / ds +50% while
  # units read +2.6% / -1.1% -- find which lever burns the uncharged time). Cumulative ladder
  # plus canon isolated; every flag pinned both ways as above.
  [s3d]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=0 MTG_BP_NODE_ROOTTURN=0 MTG_BP_CANON_CONT=0"
  [noderoot]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=0"
  [canononly]="MTG_BP_SITE3=0 MTG_BP_SITE3_DEFER=0 MTG_BP_NODE=0 MTG_BP_NODE_ROOTTURN=0 MTG_BP_CANON_CONT=1"
  # canon-everywhere (MTG_BP_CANON_ROLLOUT=1) on top of the recipe: the UNSCOPED form the
  # 2026-09-02 quality gates measured, kept as the wall-regression reference for the scoped fix.
  [canonall]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=1 MTG_BP_CANON_ROLLOUT=1"
  # TIGHT scope: canon at root/resume/capture only, rec-rollouts back to greedy.
  [tightrecipe]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=1 MTG_BP_CANON_REC=0"
  # RECROOT: tight + rec applies on the ROOT TURN only (the quality-vs-wall middle arm).
  [recroot]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=1 MTG_BP_CANON_REC=0 MTG_BP_CANON_RECROOT=1"
  [efrecipe]="MTG_BP_SITE3=1 MTG_BP_SITE3_DEFER=1 MTG_BP_NODE=1 MTG_BP_NODE_ROOTTURN=1 MTG_BP_CANON_CONT=1 MTG_EXEC_FEAS=1"
)

run_one() {   # $1=rep $2=deck $3=arm $4=stats(0/1) [$5..=extra argv]
  local rep="$1" deck="$2" arm="$3" stats="$4"; shift 4
  local -a argv
  case "$deck" in hinata) argv=("${HIN[@]}") ;; dragonstorm) argv=("${DRG[@]}") ;;
    mirrorwing) argv=("${MIR[@]}") ;; kitty) argv=("${KIT[@]}") ;; esac
  argv+=("$@")
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
  echo "[wall] rep=$rep $deck/$arm wall=${wall}s avg=$avg stats=$stats"
  rm -f "$tmp.out" "$tmp.err"
}

# Startup calibration (games=1): profile + exhaustive-keep parse is arm-independent (~2.2 s on
# Hinata), so subtracting it turns totals into honest per-game costs.
for rep in c1 c2; do
  for deck in $DECKS; do
    case "$deck" in hinata) HS=${HIN[4]}; HIN[4]=1 ;; dragonstorm) DS=${DRG[4]}; DRG[4]=1 ;;
      mirrorwing) MS=${MIR[4]}; MIR[4]=1 ;; kitty) KS=${KIT[4]}; KIT[4]=1 ;; esac
    run_one "cal-$rep" "$deck" base 0
    case "$deck" in hinata) HIN[4]=$HS ;; dragonstorm) DRG[4]=$DS ;;
      mirrorwing) MIR[4]=$MS ;; kitty) KIT[4]=$KS ;; esac
  done
done

# Timed passes first (no stats counters in the hot path), rep-major.
for rep in $(seq 1 "$REPS"); do
  for deck in $DECKS; do for arm in $ARMS; do run_one "$rep" "$deck" "$arm" 0; done; done
done
# One instrumented pass per cell for the virtual-side read.
for deck in $DECKS; do for arm in $ARMS; do run_one stats "$deck" "$arm" 1; done; done
echo "[wall] DONE -> $RES"
