#!/bin/bash
# Commit-the-line fidelity (MTG_FD_ORACLE): count [fd-diverge] lines (a committed line that did not realise its
# predicted win) with the lever ON vs =0. Env-level arms (the oracle line carries no job name), two batches.
B=/workspaces/MagicDeckTester/build/scratch/dig_agent
BIN=${1:-$B/mtg.v6}
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6
mkdir -p "$B/fd_on" "$B/fd_off"
python3 - "$B/fd_on/manifest.json" <<'EOF'
import json, os, sys
sys.path.insert(0, "/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6/logs/dig_agent")
import make_dig_ab as M
jobs = []
for key, deck, prof in M.ALL_DECKS:
    if key not in ("fivecolour", "pirates", "th", "soldiers", "auras", "fluctuator"):
        continue
    for c in range(0, 100, 25):
        jobs.append({"name": "%s_fd.c%d" % (key, c), "deck": os.path.join(M.PRIMARY, deck),
                     "profile": os.path.join(M.PRIMARY, prof), "games": 25, "seed": 8660001 + c,
                     "game_index": c, "budget_ms": 10, "depth": 3, "ignore_play_profile": True})
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
EOF
cp "$B/fd_on/manifest.json" "$B/fd_off/manifest.json"
for arm in on off; do
  lv=1; [ "$arm" = off ] && lv=0
  env -C "$WT" MTG_FD_ORACLE=1 MTG_DIG_SEARCHED_CHOICE=$lv taskset -c 0-5 nice -n 19 "$BIN" --batch "$B/fd_$arm/manifest.json" --threads 6 > "$B/fd_$arm/batch.out" 2> "$B/fd_$arm/batch.err"
  echo "lever=$lv fd-diverge lines: $(grep -c 'fd-diverge' "$B/fd_$arm/batch.err")"
done
