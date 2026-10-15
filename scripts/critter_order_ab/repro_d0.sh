#!/usr/bin/env bash
# Single-game repro of a slower game on both arms (new default vs MTG_CRITTER_ORDER=0), --threads 1, niced, logs
# on the build volume. Usage: repro_d0.sh <deck-key critter|critter2hg> <gi> <seed> <depth> [budget]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
key=$1; gi=$2; seed=$3; depth=$4; bud=${5:-0}
extra=""
if [ "$key" = critter2hg ]; then extra="--starting-life 30 --opponent-heads 2"; fi
for arm in new old; do
  d="$S/repro/${key}_gi${gi}_d${depth}_$arm"
  mkdir -p "$d"
  if [ "$arm" = old ]; then ord=0; else ord=1; fi
  env -C "$WT" MTG_CRITTER_ORDER=$ord MTG_DUMP_WINS=1 MTG_USER_ORDER_TRACE=1 nice -n 10 "$S/bin/mtg.diag" \
    decks/CritterLifegain/CritterLifegain.cod --profile decks/CritterLifegain/CritterLifegain.profile.json \
    --seed "$seed" --games 1 --game-index "$gi" --depth "$depth" --budget-ms "$bud" --threads 1 $extra \
    --log-dir "$d" > "$d/out.txt" 2> "$d/err.txt"
  echo "$arm: $(grep -o 'wt=-\?[0-9]*' "$d/err.txt" | head -1)"
  grep "uo-order" "$d/err.txt" | sed -E 's/.*t=([0-9]+) m=([0-9]).*seq=(.*)/  t\1 m\2 \3/'
done
