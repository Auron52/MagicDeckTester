#!/usr/bin/env bash
# One game, the new binary under two arms (new defaults / MTG_STOMPY_ORDER=0), MTG_FD_TRACE on.
# usage: fdtrace.sh <deck: stompy|stompy2hg> <gi> <out_dir> [depth budget]   (default: shipped cell)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
DK=$1; GI=$2; OUT=$3; DEP=${4:-}; BUD=${5:-}
mkdir -p "$OUT"
python3 - "$DK" "$GI" "$OUT" "$DEP" "$BUD" <<'EOF'
import json, os, sys
dk, gi, out, dep, bud = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164"
for arm, fl in (("new", None), ("off", {"MTG_STOMPY_ORDER": False})):
    j = {"name": "%s.%s.g%d" % (dk, arm, gi), "deck": WT + "/decks/StompySurprise/StompySurprise.cod",
         "profile": WT + "/decks/StompySurprise/StompySurprise.profile.json", "games": 1,
         "seed": 10900001 + gi, "game_index": gi}
    if dep:
        j.update({"depth": int(dep), "budget_ms": int(bud), "ignore_play_profile": True})
    if dk.endswith("2hg"):
        j.update({"starting_life": 30, "opponent_heads": 2})
    if fl:
        j["flags"] = fl
    json.dump({"jobs": [j]}, open(os.path.join(out, "m_%s.json" % arm), "w"))
EOF
cd "$WT" || exit 1
for arm in new off; do
  MTG_FD_TRACE=1 MTG_USER_ORDER_TRACE=1 nice -n 10 "$SC/bin/mtg.new" --batch "$OUT/m_$arm.json" --threads 1 \
      --cards-json "$WT/src/cards/data/cards.json" --game-trace-dir "$OUT/trace_$arm" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  grep played "$OUT/$arm.out"
done
