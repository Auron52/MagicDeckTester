#!/usr/bin/env bash
# Single-game repro of one A/B game on both arms (new default vs MTG_CRITTER_ORDER=0), --threads 1, niced (RULES
# item 4), all output on the build volume. Prints each arm's win turn and its realised executor cast lists.
# Usage: repro.sh <critter|critter2hg> <gi> <seed> <depth|ship> [budget]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
key=$1; gi=$2; seed=$3; depth=$4; bud=${5:-0}
d="$S/repro/${key}_gi${gi}_${depth}"
mkdir -p "$d"
python3 -I - "$d" "$key" "$gi" "$seed" "$depth" "$bud" "$WT" <<'PY'
import json, os, sys
d, key, gi, seed, depth, bud, wt = sys.argv[1:8]
jobs = []
for arm, fl in (("new", None), ("old", {"MTG_CRITTER_ORDER": False})):
    j = {"name": "%s_%s" % (key, arm), "deck": os.path.join(wt, "decks/CritterLifegain/CritterLifegain.cod"),
         "profile": os.path.join(wt, "decks/CritterLifegain/CritterLifegain.profile.json"), "games": 1,
         "seed": int(seed), "game_index": int(gi)}
    if depth != "ship":
        j.update({"depth": int(depth), "budget_ms": int(bud), "ignore_play_profile": True})
    if key == "critter2hg":
        j.update({"starting_life": 30, "opponent_heads": 2})
    if fl:
        j["flags"] = fl
    jobs.append(j)
json.dump({"jobs": jobs}, open(os.path.join(d, "manifest.json"), "w"), indent=1)
PY
env -C "$WT" MTG_DUMP_WINS=1 MTG_USER_ORDER_TRACE=1 nice -n 10 "$S/bin/mtg.diag" --batch "$d/manifest.json" --threads 1 \
  --game-log-dir "$d/logs" > "$d/out.txt" 2> "$d/err.txt"
grep "\[win\]" "$d/err.txt"
grep "uo-order" "$d/err.txt" | sed -E 's/.* t=([0-9]+) m=([0-9]) route=([a-z]+).*seq=(.*)/  t\1 \3 \4/'
ls "$d/logs" 2>/dev/null | head
