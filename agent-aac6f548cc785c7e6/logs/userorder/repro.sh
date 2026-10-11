#!/usr/bin/env bash
# repro.sh <seed> <gi> <depth|ship> <tag> [extra env...] -- one game, both arms, trace + game log
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$WT/build/Release/mtg"
seed="$1"; gi="$2"; depth="$3"; tag="$4"; shift 4
if [ "$depth" = ship ]; then dargs=(--budget-ms 20); else dargs=(--depth "$depth" --budget-ms 10 --ignore-play-profile); fi
for arm in on off; do
  lv=1; [ "$arm" = off ] && lv=0
  out="$U/repro/$tag/$arm"
  mkdir -p "$out"
  env MTG_USER_ORDER_TRACE=1 MTG_DRAGONSTORM_USER_ORDER=$lv "$@" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" \
      --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" --seed "$seed" --game-index "$gi" --games 1 \
      "${dargs[@]}" --threads 1 --log-dir "$out" > "$out/stdout.txt" 2> "$out/stderr.txt"
  echo "== $arm: $(grep 'avg (turns)' "$out/stdout.txt")"
  for f in "$out"/*.json; do python3 "$U/showlog.py" "$f" | grep -v "^\[" ; done
done
