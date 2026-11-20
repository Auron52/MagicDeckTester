#!/bin/bash
O=/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old
N=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58
J=$N/logs/overnight-accept/jdiff.py
for d in Hinata/Hinata Gods/Gods "Unpredictable Cyclone/Unpredictable Cyclone" "Five Colour/Five Colour" "Spirits/Spirits" "Midrange Burn/Midrange Burn"; do
  for s in profile value; do
    [ -f "$O/decks/$d.$s.json" ] && [ -f "$N/decks/$d.$s.json" ] || { echo "== $d.$s presence old=$([ -f "$O/decks/$d.$s.json" ] && echo y) new=$([ -f "$N/decks/$d.$s.json" ] && echo y)"; continue; }
    echo "== $d.$s"
    python3 "$J" "$O/decks/$d.$s.json" "$N/decks/$d.$s.json" 2>&1 | head -${LIM:-12}
  done
done
for d in Hinata Gods "Unpredictable Cyclone" "Five Colour" Spirits "Midrange Burn"; do diff -rq "$O/decks/$d" "$N/decks/$d"; done
