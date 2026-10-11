#!/usr/bin/env bash
# tr3.sh <DeckFolder> <seed> <gi> <depth> <tag> [ENV=VAL...] -- one game, build/Release/mtg under lowprio.sh,
# game log + traces to build/scratch; prints every turn's actions (all phases) and the FB / tap traces.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
S=/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6/tr3
deck="$1"; seed="$2"; gi="$3"; depth="$4"; tag="$5"; shift 5
bud=0; if [ "$depth" != 0 ]; then bud=10; fi
out="$S/$tag"; mkdir -p "$out"
env MTG_PGO=0 MTG_FB_TRACE=1 MTG_USER_ORDER_TRACE=2 "$@" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh \
    "$WT/build/Release/mtg" "$WT/decks/$deck/$deck.cod" --profile "$WT/decks/$deck/$deck.profile.json" \
    --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms "$bud" --ignore-play-profile --threads 1 \
    --log-dir "$out" > "$out/stdout.txt" 2> "$out/stderr.txt"
echo "== $tag: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
for f in "$out"/*.json; do python3 "$WT/logs/userorder/showlog.py" "$f" | grep -v "^\[" ; done
grep -iE "fb|firebreath" "$out/stderr.txt" | head -20
