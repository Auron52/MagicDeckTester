#!/usr/bin/env bash
# g6trace.sh <bin> <seed> <gi> <tag> <arm-name> [env...] -- one d0 game with trace + game log, printed compactly
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"; seed="$2"; gi="$3"; tag="$4"; arm="$5"; shift 5
out="$U/g6/$tag/$arm"; mkdir -p "$out"
env MTG_USER_ORDER_TRACE=2 "$@" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
    --seed "$seed" --game-index "$gi" --games 1 --depth 0 --ignore-play-profile --threads 1 --log-dir "$out" \
    > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "== $arm: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
for f in "$out"/*.json; do python3 "$U/showlog.py" "$f" | grep -E "MAIN|CLEANUP" | grep -v "MAIN_[12] $"; done
