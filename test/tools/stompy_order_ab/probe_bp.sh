#!/usr/bin/env bash
# BP_NEW_ONLY x the tutor-to-top reset probe. usage: probe_bp.sh <binary> <scenario.json> <out_dir>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
BIN=$1; SCN=$2; OUT=$3
mkdir -p "$OUT"
cd "$WT" || exit 1
for depth in 0 3; do
  for bpno in 1 0; do
    python3 - "$SCN" "$OUT/scn_d${depth}.json" "$depth" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1])); j["depth"] = int(sys.argv[3])
j["log_out"] = sys.argv[2].replace(".json", ".log.json")
json.dump(j, open(sys.argv[2], "w"), indent=1)
EOF
    echo "=== depth=$depth MTG_BP_NEW_ONLY=$bpno"
    MTG_BP_NEW_ONLY=$bpno MTG_BP_NEW_ONLY_TRACE=4 MTG_ROLLOUT_STATS=1 taskset -c 0-5 nice -n 19 "$BIN" --scenario "$OUT/scn_d${depth}.json" \
        > "$OUT/d${depth}_bp${bpno}.out" 2> "$OUT/d${depth}_bp${bpno}.err"
    echo "exit=$?"
    grep -E "scenario:|win" "$OUT/d${depth}_bp${bpno}.out" | tail -3
    grep -c "^\[bp-newonly\]" "$OUT/d${depth}_bp${bpno}.err"
    grep -E "Turntimber" "$OUT/d${depth}_bp${bpno}.err" | grep -E "KEEP|DROP" | sort | uniq -c | head -8
    grep -E "bp_newonly" "$OUT/d${depth}_bp${bpno}.err" | tail -2
  done
done
