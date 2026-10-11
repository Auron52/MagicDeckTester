#!/usr/bin/env bash
# trace2.sh <bin> <seed> <gi> <depth> <tag> <env-set> -- one game with trace=2 + game log; prints the casts per turn
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"; seed="$2"; gi="$3"; depth="$4"; tag="$5"; set="$6"
envs=(MTG_NONE=0)
if [ "$set" != "-" ]; then IFS=',' read -r -a envs <<< "$set"; fi
out="$U/tr/$tag"; mkdir -p "$out"
env MTG_USER_ORDER_TRACE=2 "${envs[@]}" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
    --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 --log-dir "$out" \
    > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "== $tag [$set]: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
for f in "$out"/*.json; do python3 "$U/showlog.py" "$f" | grep -E "MAIN|CLEANUP" | grep -v "MAIN_[12] $"; done
