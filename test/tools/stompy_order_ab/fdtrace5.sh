#!/usr/bin/env bash
# One StompySurprise game under two arms (A = the given flags, B = off), any cell, 20-life or 2HG, traced
# (MTG_FD_TRACE, user-order trace, prepay hold trace, per-game traces), threads 1, nice 10.
# usage: fdtrace5.sh <binary> <gi> <out_dir> <ship|d3|d0> <2hg|20> '<A flags as JSON>'
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; GI=$2; OUT=$3; CELL=$4; HG=$5; AFL=$6
mkdir -p "$OUT"
python3 - "$GI" "$OUT" "$CELL" "$HG" "$AFL" <<'EOF'
import json, os, sys
gi, out, cell, hg, afl = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4], json.loads(sys.argv[5])
WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164"
CELLS = {"ship": {}, "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
         "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}
for arm, fl in (("A", afl), ("B", None)):
    j = {"name": "stompy.%s.g%d" % (arm, gi), "deck": WT + "/decks/StompySurprise/StompySurprise.cod",
         "profile": WT + "/decks/StompySurprise/StompySurprise.profile.json", "games": 1,
         "seed": 10900001 + gi, "game_index": gi}
    j.update(CELLS[cell])
    if hg == "2hg":
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
  grep -E "^\[fd\] T[0-9] LINE|uo-order.*m=1" "$OUT/$arm.err" | sed 's/pool=.*seq=/seq=/' | grep -v 'route=clean seq=$' | head -14 | cut -c1-330
  python3 /workspaces/MagicDeckTester/build/scratch/stompy-order/trsum.py "$OUT"/trace_$arm/*.json \
      | grep -E '^T[0-9] (MAIN_1|COMBAT)' | cut -c1-420
done
