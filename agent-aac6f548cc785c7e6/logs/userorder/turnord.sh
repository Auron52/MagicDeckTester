#!/usr/bin/env bash
# turnord.sh <bin-tag> <seed> <gi> <depth> <turn> -- the [uo-order]/[uo-cast] trace lines of one turn (main 1)
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
tag="$1"; seed="$2"; gi="$3"; depth="$4"; turn="$5"
out="$U/tr/to_${tag}_${seed}"; mkdir -p "$out"
env MTG_USER_ORDER_TRACE=2 nice -n 19 "$U/mtg.$tag" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
    --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 \
    > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "== $tag: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
grep -E "\[uo-(order|cast)\] gs=$seed t=$turn m=1" "$out/stderr.txt" | sed -E 's/ gs=[0-9]+//; s/staged=0 //; s/splice=0 //' | cut -c1-220
