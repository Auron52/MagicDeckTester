#!/bin/bash
# Replay d0 games under every arm, with per-game logs.
#   named_games.sh <binary> <outdir> [gi:seed ...]   (default: the five named games, seed 9310001 + gi)
# Writes <outdir>/<gi>_<arm>/*.json (game logs) and <outdir>/<gi>_<arm>.out.
BIN=$1; OUT=$2; shift 2
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
declare -A ARM
ARM[generic]="MTG_BURN_BUCKET_DISCARD=0"
ARM[cur]=""
ARM[k1]="MTG_BURN_VOLLEY_KEEP1=1"
ARM[k2]="MTG_BURN_VOLLEY_KEEP2=1"
ARM[f1]="MTG_BURN_VOLLEY_KEEP1=1 MTG_BURN_VOLLEY_KEEP_FLOOR=1"
ARM[f2]="MTG_BURN_VOLLEY_KEEP2=1 MTG_BURN_VOLLEY_KEEP_FLOOR=1"
ARM[rank]="MTG_BURN_VOLLEY_NOLAND_RANK=1"
ARM[late]="MTG_BURN_VOLLEY_NOLAND_LATE=1"
ARM[k1late]="MTG_BURN_VOLLEY_KEEP1=1 MTG_BURN_VOLLEY_NOLAND_LATE=1"
ARM[k2late]="MTG_BURN_VOLLEY_KEEP2=1 MTG_BURN_VOLLEY_NOLAND_LATE=1"
GAMES=("$@")
if [ ${#GAMES[@]} -eq 0 ]; then
  for gi in 1048 8292 12620 17376 20102; do GAMES+=("$gi:$((9310001 + gi))"); done
fi
mkdir -p "$OUT"
for spec in "${GAMES[@]}"; do
  gi=${spec%%:*}; seed=${spec##*:}
  for arm in generic cur k1 k2 f1 f2 rank late k1late k2late; do
    d="$OUT/${gi}_${arm}"
    mkdir -p "$d"
    # shellcheck disable=SC2086
    env -C "$WT" ${ARM[$arm]} MTG_PGO=0 "$BIN" decks/burn/burn.txt --profile decks/burn/burn.profile.json \
      --games 1 --seed "$seed" --game-index "$gi" --depth 0 --budget-ms 0 --ignore-play-profile \
      --log-dir "$d" > "$d.out" 2>&1
  done
done
