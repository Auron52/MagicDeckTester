#!/usr/bin/env bash
# turnord2.sh <seed> <gi> <depth> <turn> [ENV=VAL...] -- the [uo-order]/[uo-cast] trace of one Dragonstorm turn,
# run from the build volume under lowprio.sh; scratch under build/scratch (9p rule, 2026-10-10).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
S=/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6
seed="$1"; gi="$2"; depth="$3"; turn="$4"; shift 4
out="$S/to_${seed}"; mkdir -p "$out"
env MTG_USER_ORDER_TRACE=2 "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$WT/build/Release/mtg" \
    "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
    --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 \
    > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "== win turn: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
grep -E "\[uo-(order|cast)\] gs=$seed t=$turn m=1" "$out/stderr.txt" | sed -E 's/ gs=[0-9]+//; s/staged=0 //; s/splice=0 //' | cut -c1-220
