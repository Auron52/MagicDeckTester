#!/usr/bin/env python3
"""cpauto.py: replay game 814's FULL-arm line (scaling2 round 2) through claude-play to T4 pre-main; prints the CSV."""
import json, os, subprocess, sys
S = "/workspaces/MagicDeckTester/build/scratch/elves-scal2"
WANT = {1: ["Joraga Warcaller"], 2: ["Priest of Titania"], 3: ["Marwyn, the Nurturer", "Elvish Archdruid"]}
LAND = {1: "Forest", 2: "Forest", 3: "Forest"}
STOP = int(os.environ.get("STOP_TURN", 4))
env = dict(os.environ, MTG_START_LIFE="30", MTG_OPPONENT_HEADS="2", MTG_RSS_CAP_GB="2", MTG_ELVES_SCALING_PRUNE="0")
ch = ["0", "1", "1"]
done = {t: False for t in WANT}
while True:
    cmd = [S + "/ab/mtg3", "decks/Elves/Elves.cod", "--profile", "decks/Elves/Elves.profile.json", "--claude-play",
           "--seed", "9850815", "--max-turns", "8", "--choices", ",".join(ch)]
    r = subprocess.run(cmd, cwd="/workspaces/MagicDeckTester/.claude/worktrees/elves-scal2", env=env, capture_output=True, text=True)
    if "<<<CLAUDE_DECISION>>>" not in r.stdout: print("ended", r.returncode, r.stdout[-800:]); break
    d = json.loads(r.stdout.split("<<<CLAUDE_DECISION>>>")[1].split("<<<END_DECISION>>>")[0])
    T, ty, ph = d.get("turn"), d["type"], d.get("phase")
    if T >= STOP and ty == "main_phase" and ph == "pre_main":
        print("STOP at", T, ph, "csv:", ",".join(ch)); print(json.dumps(d)[:4000]); break
    if ty == "main_phase":
        plans = d.get("plans", [])
        pick = None
        if T in WANT and not done[T] and ph == "pre_main":
            for p in plans:
                if sorted(p.get("casts", [])) == sorted(WANT[T]) and ("land=" + LAND[T]) in p.get("summary", ""):
                    pick = p["index"]; done[T] = True; break
            if pick is None: print("no match T", T, [p.get("summary") for p in plans][:30]); break
        if pick is None:
            nothing = [p["index"] for p in plans if not p.get("casts")]
            pick = nothing[0] if nothing else -1
        print(f"T{T} {ph} pick {pick}", file=sys.stderr)
        ch.append(str(pick))
    else:
        print("decision", ty, T, json.dumps(d)[:600]); ch.append(str(d.get("ai_choice", {}).get("index", d.get("ai_choice", 0)) if isinstance(d.get("ai_choice"), dict) else d.get("ai_choice", 0)))
