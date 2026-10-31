#!/usr/bin/env python3
"""A bounded human menu must admit an explicitly pinned generic cycling family.

Runs a tiny deterministic seven-card test deck: first hand is the entire deck,
seed2 is on the play, so no library draw or game search is needed. The ordinary
32-position valve must omit combined cycling plans; naming a cycler and Bone Saw
in --full-enum must restore their combined plan while leaving the valve active.
The non-first cycler is pinned to cover family membership beyond g.front().
No server or references are written. Usage: python3 THIS.py /abs/path/to/mtg
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

if len(sys.argv) != 2:
    sys.exit("usage: viewer_cycle_pin_check.py MTG")
binary = Path(sys.argv[1]).resolve()
# Intended installed location is repo/test; when staged under build/scratch infer from binary's cwd supplied below.
root = Path(os.environ.get("MDT_TEST_ROOT", Path(__file__).resolve().parent.parent)).resolve()
scratch = root / "build/scratch/viewer-cycle-pin"
scratch.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_VIEWER_PLAN_CAP_POSITIONS="32",
           MTG_VIEWER_PLAN_CAP="1", MTG_UNPRUNED="1", MTG_HUMAN_PLAY="1")
with tempfile.TemporaryDirectory(dir=scratch) as work:
    deck = Path(work) / "cycle-pin.txt"
    deck.write_text("1 Ancient Tomb\n1 Bone Saw\n1 Paradise Mantle\n1 Kite Shield\n1 Cathar's Shield\n1 Cloud of Faeries\n1 Street Wraith\n")
    def frame(pins=None):
        cmd = [str(binary), str(deck), "--claude-play", "--seed", "2", "--game-index", "0",
               "--max-turns", "1", "--depth", "0", "--choices", "1", "--reveal", "0"]
        if pins:
            cmd += ["--full-enum", "0:" + "|".join(pins)]
        r = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True)
        if "<<<CLAUDE_DECISION>>>" not in r.stdout:
            sys.exit("FAIL no main frame: " + r.stderr[-2000:] + r.stdout[-2000:])
        d = json.loads(r.stdout.split("<<<CLAUDE_DECISION>>>")[1].split("<<<END_DECISION>>>")[0])
        if d.get("type") != "main_phase" or d.get("main_ordinal") != 0:
            sys.exit("FAIL did not reach deterministic first main: " + str(d.get("type")))
        return d
    ordinary = frame()
    cyclers = [c["name"] for c in ordinary["me"]["hand"] if c["name"] in ("Cloud of Faeries", "Street Wraith")]
    if len(cyclers) != 2:
        sys.exit("FAIL tiny fixture did not deal its two cyclers")
    cycler = cyclers[-1]
    def carries(d):
        return any("cycle " + cycler in p.get("summary", "") and "Bone Saw" in p.get("casts", [])
                   for p in d.get("plans", []))
    if carries(ordinary):
        sys.exit("FAIL negative control: ordinary frame already offers the combined line; use a tighter reachable fixture")
    pinned = frame([cycler, "Bone Saw"])
    if not carries(pinned):
        sys.exit("FAIL pinned generic cycle family still missing: " + cycler)
    print("PASS capped generic cycle pin: ordinary omits, pinned restores Bone Saw + cycle " + cycler)
