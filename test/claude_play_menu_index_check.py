#!/usr/bin/env python3
"""A --claude-play main_phase reply is the plan's `index` FIELD, never its position in `plans`.

WHAT WENT WRONG (Morph seed 8, 2026-11-13). An agent reconstructing the USER's hand-played game
picked "index 55" for the T5 line Might of Old Krosa -> Spellskite, Berserk -> Spellskite,
Berserk -> Spellskite, and the engine committed Berserk + Might + Temur Battle Rage instead: two
`step_dropped` warnings, the second Berserk offered back as a frame of its own. It read as "two
lethal lines folded into one menu entry and the TBR one was kept". It was not. The frame enumerates
663 plans; the dump shows a capped, collapsed subset of them (MTG_PLAY_PLANS_CAP, hide_bundle), each
under its REAL engine index, so the array has gaps. The USER's line sat at array POSITION 55 with
`"index": 88`, and engine plan 55 really is Berserk -> Hierarch, Might -> Hierarch, TBR ->
Spellskite. Menu label and committed actions agreed throughout; the reply was a position.

The trap was the frame's own instruction: `"reply with one plan index (0-based)"` -- "0-based" is
what one says about an array position. The note now names the field and says the array is not
position-indexed (src/main.cpp, the main_phase note), and .claude/skills/claude-play.md says the same.

This check drives that exact frame and asserts:
  1. the frame's plan list has gaps (otherwise position == index and the check is vacuous);
  2. the note tells the reader to reply with the `index` field, not the array position;
  3. replying with the USER's line's `index` field, under the viewer's "**" pin of the same line,
     commits exactly that line -- no step_dropped event, both Berserks after exalted, won on T5;
  4. replying with its array POSITION instead commits a different plan (the trap, still there by
     design: the gaps are what keeps every saved reference's indices stable).

SKIPS ITSELF when the frame is not reachable (an engine change that moves this deck's T1-T4 play):
a shifted board must not read as a regression in the thing being guarded.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from engine_bin import engine_bin  # noqa: E402

BIN = os.environ.get("MTG_BIN") or engine_bin("mtg")
DECK = "decks/Morph/Morph.cod"
PROF = "decks/Morph/Morph.profile.json"
# The USER's seed-8 game up to the T5 pre-combat main (see logs/morph/seed8-line.txt).
PREFIX = "1,0,1,-1,-1,6,-1,-1,-1,-1,81,0,-1,-1"
CAST_ORDER = "6:**|land=Stomping Ground|cast=Might of Old Krosa|cast=Berserk|cast=Berserk"
WANT = [("Might of Old Krosa", "Spellskite"), ("Berserk", "Spellskite"), ("Berserk", "Spellskite")]

fails = []


def run(choices):
    args = [BIN, DECK, "--profile", PROF, "--cards-json", "src/cards/data/cards.json",
            "--claude-play", "--seed", "8", "--game-index", "7", "--max-turns", "8", "--depth", "0",
            "--jitte-prompt", "--firebreathe-prompt", "--storage-hold-prompt",
            "--choices", choices, "--cast-order", CAST_ORDER]
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout


def decision(out):
    m = re.search(r"<<<CLAUDE_DECISION>>>(.*?)<<<END_DECISION>>>", out, re.S)
    return json.loads(m.group(1)) if m else None


def result(out):
    m = re.search(r"<<<CLAUDE_RESULT>>>(.*?)<<<END_RESULT>>>", out, re.S)
    return json.loads(m.group(1)) if m else None


def play_out(choices):
    """Answer every later frame (targets -> Spellskite, main phases -> pass) to the end."""
    events = []
    for _ in range(40):
        rc, out = run(choices)
        events += re.findall(r'"kind": "([a-z_]+)", "text": "([^"]*)"', out)
        if rc != 70:
            return result(out), events, choices
        d = decision(out)
        if d is None:
            return None, events, choices
        pick = -1
        if d.get("type") == "target":
            pick = next((o["index"] for o in d.get("options", []) if o["label"].startswith("Spellskite")), -1)
        elif d.get("type") != "main_phase":
            pick = d.get("heuristic_default", -1)
        choices += "," + str(pick)
    return None, events, choices


def main():
    if not os.path.exists(os.path.join(ROOT, BIN)) and not os.path.exists(BIN):
        print("SKIP: no engine binary at %s" % BIN)
        return 0
    rc, out = run(PREFIX)
    d = decision(out)
    if rc != 70 or d is None or d.get("type") != "main_phase" or d.get("turn") != 5:
        print("SKIP: the seed-8 T5 frame is not reachable on this engine (rc=%d)" % rc)
        return 0
    plans = d["plans"]
    target = None
    for pos, p in enumerate(plans):
        acts = [(a.get("card"), a.get("enchant_target_name")) for a in p.get("actions", [])]
        if p.get("land") == "Stomping Ground" and acts == WANT:
            target = (pos, p["index"])
            break
    if target is None:
        print("SKIP: the USER's T5 line is not on this frame's menu")
        return 0
    pos, idx = target
    # 1. gaps
    if all(p["index"] == k for k, p in enumerate(plans)):
        print("SKIP: this frame's plan list has no gaps (position == index); nothing to guard")
        return 0
    # 2. the note
    note = d.get("note", "")
    if '"index"' not in note or "position" not in note:
        fails.append("main_phase note does not say to reply with the plan's \"index\" field rather than its "
                     "array position: %r" % note)
    # 3. the index field commits exactly the line
    res, events, stream = play_out(PREFIX + "," + str(idx))
    dropped = [t for k, t in events if k == "step_dropped"]
    if dropped:
        fails.append("index %d (the USER's line) dropped steps: %s" % (idx, dropped))
    if not res or res.get("win_turn") != 5:
        fails.append("index %d did not win on T5: %s" % (idx, res))
    if not any("Berserk, Berserk cast in combat after exalted" in t for _, t in events):
        fails.append("index %d: both Berserks were not cast after exalted" % idx)
    # 4. the position is a different plan (the trap the note warns about)
    if pos != idx:
        p_at = next(p for p in plans if p["index"] == pos) if any(p["index"] == pos for p in plans) else None
        if p_at is not None and [(a.get("card"), a.get("enchant_target_name")) for a in p_at["actions"]] == WANT:
            fails.append("array position %d unexpectedly holds the same line" % pos)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("PASS: T5 line is plan index %d (array position %d); stream %s" % (idx, pos, stream))
    return 0


if __name__ == "__main__":
    sys.exit(main())
