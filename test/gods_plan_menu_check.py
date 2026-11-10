#!/usr/bin/env python3
"""Seed 120022: capped play menus retain basic Gods plays behind Loam variants."""

import json
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build/Release/mtg"
DECK = ROOT / "decks/Gods/Gods.cod"
PROFILE = ROOT / "decks/Gods/Gods.profile.json"
CHOICES = [
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 5, 0, 0, 1, 0, 2, 3, 2, 3,
    3, 3, 2, 3, 0, 0, 0, 3, 1498, 0, 0, 4, 0, 1, 2, 4, 578, -1, -1, 4,
]
CASES = [
    (29, "land=none; cast: Mystic Retrieval (flashback)"),
    (37, "land=City of Brass; cast: (nothing)"),
    (41, "land=none; cast: Replenish"),
]


for prefix_len, summary in CASES:
    cmd = [str(BIN), str(DECK), "--profile", str(PROFILE), "--claude-play",
           "--seed", "120022", "--max-turns", "8", "--choices",
           ",".join(map(str, CHOICES[:prefix_len]))]
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="200")
    proc = subprocess.run(cmd, env=env, cwd=ROOT, capture_output=True, text=True)
    assert proc.returncode == 70, (prefix_len, proc.returncode, proc.stderr[-500:])
    raw = proc.stdout.split("<<<CLAUDE_DECISION>>>", 1)[1].split("<<<END_DECISION>>>", 1)[0]
    decision = json.loads(raw)
    assert decision.get("plans_total", 0) > 200, (prefix_len, decision.get("plans_total"))
    assert len(decision["plans"]) <= 200, (prefix_len, len(decision["plans"]))
    assert any(plan["summary"] == summary for plan in decision["plans"]), (prefix_len, summary)
    print(f"T{decision['turn']} choice {prefix_len}: visible {summary}")
