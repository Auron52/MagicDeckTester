#!/usr/bin/env bash
# One StompySurprise game under two arms (A = order + fix + same-name victim, B = off), any cell, 20-life or
# 2HG, with MTG_FD_TRACE, the user-order trace, the prepay hold trace and per-game traces.
# usage: fdtrace4.sh <binary> <gi> <out_dir> <ship|d3|d0> [2hg]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; GI=$2; OUT=$3; CELL=$4; HG=${5:-}
mkdir -p "$OUT"
python3 - "$GI" "$OUT" "$CELL" "$HG" <<'EOF'
import json, os, sys
gi, out, cell, hg = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164"
CELLS = {"ship": {}, "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
         "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}
A = {"MTG_HOLD_RELEASE_YIELD": True, "MTG_STOMPY_ORDER": True, "MTG_SAC_VICTIM_SAME_NAME": True}
for arm, fl in (("A", A), ("B", None)):
    j = {"name": "stompy.%s.g%d" % (arm, gi), "deck": WT + "/decks/StompySurprise/StompySurprise.cod",
         "profile": WT + "/decks/StompySurprise/StompySurprise.profile.json", "games": 1,
         "seed": 10900001 + gi, "game_index": gi}
    j.update(CELLS[cell])
    if hg:
        j.update({"starting_life": 30, "opponent_heads": 2})
    if fl:
        j["flags"] = fl
    json.dump({"jobs": [j]}, open(os.path.join(out, "m_%s.json" % arm), "w"))
EOF
cd "$WT" || exit 1
for arm in A B; do
  MTG_BATCH_HEARTBEAT="$OUT/hb.txt" MTG_FD_TRACE=1 MTG_USER_ORDER_TRACE=1 MTG_PREPAY_HOLD_TRACE=1 nice -n 10 "$BIN" \
      --batch "$OUT/m_$arm.json" --threads 1 --cards-json "$WT/src/cards/data/cards.json" \
      --game-trace-dir "$OUT/trace_$arm" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  echo "== $arm $(grep played "$OUT/$arm.out" | sed 's/ digest.*//')"
  grep -E "^\[fd\] T[0-9] LINE|uo-order.*m=1" "$OUT/$arm.err" | sed 's/pool=.*seq=/seq=/' | head -12 | cut -c1-330
  python3 /workspaces/MagicDeckTester/build/scratch/stompy-order/trsum.py "$OUT"/trace_$arm/*.json \
      | grep -E '^T[0-9] (MAIN_1|COMBAT)' | cut -c1-420
done
