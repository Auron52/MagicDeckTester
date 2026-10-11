#!/usr/bin/env bash
# onoff.sh <bin> <LEVER> <depth|ship> <seed> <gi> [turn] -- one game, lever on vs off: win turn + that turn's casts
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"; lever="$2"; depth="$3"; seed="$4"; gi="$5"; turn="${6:-0}"
if [ "$depth" = ship ]; then dargs=(--budget-ms 20); else dargs=(--depth "$depth" --budget-ms 10 --ignore-play-profile); fi
for v in 1 0; do
  out="$U/onoff/$lever.$seed.$v"; mkdir -p "$out"
  env "$lever=$v" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
      --seed "$seed" --game-index "$gi" --games 1 "${dargs[@]}" --threads 1 --log-dir "$out" > "$out/stdout.txt" 2> "$out/stderr.txt"
  echo "== $lever=$v: $(grep 'avg (turns)' "$out/stdout.txt" | awk '{print $4}')"
  if [ "$turn" != 0 ]; then
    for f in "$out"/*.json; do python3 "$U/showlog.py" "$f" | grep -E "^$turn MAIN"; done
  fi
done
