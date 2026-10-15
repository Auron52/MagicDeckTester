#!/usr/bin/env bash
# A scenario under the new binary: order ON (defaults) vs MTG_STOMPY_ORDER=0, with the user-order trace.
# usage: scn2.sh <scenario.json> <out_dir> [ENV=VAL ...]   (extra env applies to both arms)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
F=$1; OUT=$2; shift 2
mkdir -p "$OUT"
cd "$WT" || exit 1
for arm in new off; do
  if [ "$arm" = off ]; then O=0; else O=1; fi
  env "$@" MTG_STOMPY_ORDER=$O MTG_USER_ORDER_TRACE=1 MTG_FD_TRACE=1 nice -n 10 "$SC/bin/mtg.new" --scenario "$F" \
      > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  echo "== $arm: $(grep '^scenario: win_turn' "$OUT/$arm.out")"
  grep -E "uo-order" "$OUT/$arm.err" | sed 's/pool=.*seq=/seq=/' | head -8
  grep -E "^\[fd\] T[0-9] LINE" "$OUT/$arm.err" | head -2
done
