#!/usr/bin/env bash
# A scenario under two arms (ofix = order + fix, off) with the per-turn log written to the out dir and
# summarised. usage: scn4.sh <binary> <scenario.json> <out_dir> [ENV=VAL ...]   (extra env: both arms)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
BIN=$1; F=$2; OUT=$3; shift 3
mkdir -p "$OUT"
cd "$WT" || exit 1
for arm in ofix off; do
  case $arm in
    ofix) E="MTG_STOMPY_ORDER=1 MTG_HOLD_RELEASE_YIELD=1" ;;
    off)  E="MTG_STOMPY_ORDER=0 MTG_HOLD_RELEASE_YIELD=0" ;;
  esac
  python3 - "$F" "$OUT/s_$arm.json" "$OUT/log_$arm.json" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1])); j["log_out"] = sys.argv[3]
json.dump(j, open(sys.argv[2], "w"))
EOF
  env "$@" $E MTG_USER_ORDER_TRACE=1 MTG_FD_TRACE=1 nice -n 10 "$BIN" --scenario "$OUT/s_$arm.json" \
      --cards-json "$WT/src/cards/data/cards.json" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  echo "== $arm: $(grep '^scenario: win_turn' "$OUT/$arm.out")"
  python3 "$SC/trsum.py" "$OUT/log_$arm.json" 2>&1 | grep -v -E 'COMBAT  \||MAIN_2  \||^mull' | cut -c1-330
done
