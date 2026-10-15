#!/usr/bin/env bash
# One StompySurprise game at the shipped cell on the fix binary under two arms (A = order + fix, B = off),
# MTG_FD_TRACE + the user-order trace + per-game traces. usage: fdtrace2.sh <gi> <out_dir> [2hg]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
GI=$1; OUT=$2; HG=${3:-}
mkdir -p "$OUT"
python3 - "$GI" "$OUT" "$HG" <<'EOF'
import json, os, sys
gi, out, hg = int(sys.argv[1]), sys.argv[2], sys.argv[3]
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164"
for arm, fl in (("A", {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True}), ("B", None)):
    j = {"name": "stompy.%s.g%d" % (arm, gi), "deck": WT + "/decks/StompySurprise/StompySurprise.cod",
         "profile": WT + "/decks/StompySurprise/StompySurprise.profile.json", "games": 1,
         "seed": 10900001 + gi, "game_index": gi}
    if hg:
        j.update({"starting_life": 30, "opponent_heads": 2})
    if fl:
        j["flags"] = fl
    json.dump({"jobs": [j]}, open(os.path.join(out, "m_%s.json" % arm), "w"))
EOF
cd "$WT" || exit 1
for arm in A B; do
  MTG_FD_TRACE=1 MTG_USER_ORDER_TRACE=1 nice -n 10 "$SC/bin/mtg.fix" --batch "$OUT/m_$arm.json" --threads 1 \
      --cards-json "$WT/src/cards/data/cards.json" --game-trace-dir "$OUT/trace_$arm" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  echo "== $arm $(grep played "$OUT/$arm.out")"
  grep -E "^\[fd\] T[0-9] LINE|uo-order.*m=1" "$OUT/$arm.err" | sed 's/pool=.*seq=/seq=/' | head -12
done
