#!/usr/bin/env bash
# onegame.sh BIN OUTDIR SEED GI HG(0/1) ORDER(0/1) [extra CLI args...]  -- one game with a full per-game log.
# Shipped settings unless extra args pin --depth/--budget-ms (then add --ignore-play-profile yourself).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a4d121dd522b89155
BIN=$1; OUT=$2; SEED=$3; GI=$4; HG=$5; ORD=$6; shift 6
mkdir -p "$OUT"
cd "$WT" || exit 9
envs=(MTG_DUMP_WINS=1 MTG_HINATA_ORDER_FULL="$ORD")
if [ "$HG" = 1 ]; then envs+=(MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2); fi
env "${envs[@]}" "$BIN" "$WT/decks/Hinata2/Hinata2.cod" --cards-json "$WT/src/cards/data/cards.json" \
    --seed "$SEED" --game-index "$GI" --games 1 --log-dir "$OUT" "$@" > "$OUT/out.txt" 2> "$OUT/err.txt"
echo "rc=$?"; grep -h "gi=" "$OUT/err.txt" | tail -2; ls "$OUT" | head
