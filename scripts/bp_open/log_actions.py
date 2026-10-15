#!/usr/bin/env python3
"""Print a game log's turn-by-turn action list (compact), to diff two arms of one game.
  log_actions.py <game_log.json>"""
import json, sys

g = json.load(open(sys.argv[1]))
keys = [k for k in g.keys()]
turns = g.get("turns") or g.get("turnLog") or []
if not turns:
    print("top-level keys:", keys)
    sys.exit(0)
for t in turns:
    tn = t.get("turn", t.get("turnNumber", "?"))
    acts = t.get("actions") or t.get("events") or []
    out = []
    for a in acts:
        if isinstance(a, dict):
            s = a.get("description") or a.get("text") or a.get("action") or json.dumps(a, sort_keys=True)[:140]
        else:
            s = str(a)
        out.append(s)
    print("T%s: %s" % (tn, " | ".join(out)))
    if not acts:
        print("   (turn keys: %s)" % list(t.keys()))
