#!/usr/bin/env python3
"""cpstep.py <choices-csv> [extra args]: one claude-play step of Elves 2HG seed 9850815 (game 814, scaling2 round 2),
prints a compact summary of the next decision. Env: BIN, PRUNE (MTG_ELVES_SCALING_PRUNE, default 0)."""
import json, os, subprocess, sys
S = "/workspaces/MagicDeckTester/build/scratch/elves-scal2"
env = dict(os.environ, MTG_START_LIFE="30", MTG_OPPONENT_HEADS="2", MTG_RSS_CAP_GB="2",
           MTG_ELVES_SCALING_PRUNE=os.environ.get("PRUNE", "0"))
cmd = [os.environ.get("BIN", S + "/ab/mtg3"), "decks/Elves/Elves.cod", "--profile", "decks/Elves/Elves.profile.json",
       "--claude-play", "--seed", "9850815", "--max-turns", "8", "--choices", sys.argv[1]] + sys.argv[2:]
r = subprocess.run(cmd, cwd="/workspaces/MagicDeckTester/.claude/worktrees/elves-scal2", env=env, capture_output=True, text=True)
o = r.stdout
if "<<<CLAUDE_DECISION>>>" not in o:
    print("rc", r.returncode); print(o[-3000:]); print(r.stderr[-1500:]); sys.exit()
d = json.loads(o.split("<<<CLAUDE_DECISION>>>")[1].split("<<<END_DECISION>>>")[0])
print("idx", d.get("decision_index"), d["type"], "T", d.get("turn"), d.get("phase"), "opp", d.get("opponent", {}).get("life"))
print("hand", [h.get("name") for h in d.get("hand", [])])
print("bf", [(b.get("name"), b.get("tapped"), b.get("power")) for b in d.get("me", {}).get("battlefield", [])])
for k in ("note", "ai_choice", "options", "to_bottom"):
    if k in d: print(k, json.dumps(d[k])[:600])
for p in d.get("plans", [])[:60]:
    print("  plan", p.get("index"), json.dumps({k: v for k, v in p.items() if k in ("summary", "actions", "desc", "casts", "label")})[:300])
