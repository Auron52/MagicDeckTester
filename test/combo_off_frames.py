#!/usr/bin/env python3
"""Replay the USER'S OWN FRAMES through the COMBO OFF rule table.

Every board the user actually sat in front of -- the saved play sessions under logs/play/ and the
rejection dumps under logs/play/rejections/ -- carries a full `me`/`opponent` snapshot in its
decision JSON.  This turns each main-phase frame into a `--scenario` fixture and asks the one
question the user is complaining about: *would the button have appeared here?*

It is a REPORT, not a gate.  The synthesized board is deliberately a LOWER BOUND on the real one:

  * floating mana cannot be staged by the scenario harness, so a frame the user reached with
    {G:86} banked is re-asked with an empty pool -- if the button appears anyway, it would have
    appeared there too;
  * `opponent_library_size` is not recorded, so it is estimated as 53 - (turn - 1) (their opening
    seven plus one draw at the end of each of our turns).  The deck-out route is the only thing
    that reads it;
  * a land Aura is re-attached BY HOST NAME, so with two same-named lands it may land on the other
    copy -- which can only move the yield ordering, never the inventory the rules read.

Usage:  python3 test/combo_off_frames.py                 # every EDF frame we have
        python3 test/combo_off_frames.py --deck Foo      # restrict by deck name substring
        MTG_BIN=path python3 test/combo_off_frames.py
"""
import json, os, re, subprocess, sys, glob, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MTG  = os.environ.get("MTG_BIN", os.path.join(ROOT, "build/Release/mtg"))
DECK = os.path.join(ROOT, "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod")
# The play logs live in the MAIN checkout (logs/ is gitignored, so a worktree has none of its own).
LOGDIRS = [os.path.join(ROOT, "logs/play"), "/workspaces/MagicDeckTester2/logs/play"]


def frames_from_session(path):
    """Saved --claude-play session: one entry per decision, each with its own board snapshot."""
    try:
        d = json.load(open(path))
    except Exception:
        return []
    out = []
    for i, ent in enumerate(d.get("decisions", [])):
        dec = ent.get("decision") or {}
        if dec.get("type") != "main_phase" or "me" not in dec:
            continue
        out.append(("%s#%d" % (os.path.basename(path), i), dec))
    return out


def frames_from_rejection(path):
    """Rejection dump: a single frame, plus the line the user tried to play."""
    try:
        d = json.load(open(path))
    except Exception:
        return []
    st = d.get("state")
    if not st or "me" not in st:
        return []
    st = dict(st)
    st["turn"] = d.get("turn", 4)
    return [(os.path.basename(path), st)]


def build_fixture(dec, turn_default=4):
    me = dec.get("me") or {}
    opp = dec.get("opponent") or {}
    turn = dec.get("turn", turn_default)
    bf = me.get("battlefield") or []
    by_num = {c.get("num"): c.get("name") for c in bf}
    perms = []
    for c in bf:
        e = {"name": c["name"], "controller": 0, "tapped": bool(c.get("tapped"))}
        host = c.get("attached_to")
        if host is not None and host in by_num:
            e["equips"] = by_num[host]
        perms.append(e)
    # Auras must be constructed AFTER their hosts (the harness resolves `equips` by name against
    # the permanents already built).
    perms.sort(key=lambda e: 1 if "equips" in e else 0)
    hand = [c["name"] for c in (me.get("hand") or []) if c.get("name")]
    return {
        "deck": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
        "env": {"MTG_HUMAN_PLAY": "1"},
        "turn": turn,
        "on_the_play": False,
        "active_life": me.get("life", 20),
        "opponent_life": opp.get("life", 20),
        "opponent_library_size": max(1, 53 - (turn - 1)),
        "library_filler": "Forest",
        "library_size": max(1, me.get("library_size", 40)),
        "depth": 3,
        "budget_ms": 20,
        "battlefield": perms,
        "hand": hand,
        "expect_combo_off": True,
    }


def run(fix):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(fix, f)
        p = f.name
    try:
        out = subprocess.run([MTG, "--scenario", p], capture_output=True, text=True,
                             cwd=ROOT).stdout
    finally:
        os.unlink(p)
    m = re.search(r"^scenario: combo_off .*$", out, re.M)
    line = m.group(0) if m else "(no combo_off line)"
    won = "combo_off verified finish (opponent lost)" in out
    fail = re.search(r"^scenario: FAIL .*$", out, re.M)
    return line, won, (fail.group(0) if fail else "")


def main():
    want = None
    if "--deck" in sys.argv:
        want = sys.argv[sys.argv.index("--deck") + 1]
    frames = []
    seen = set()
    for d in LOGDIRS:
        if not os.path.isdir(d) or d in seen:
            continue
        seen.add(d)
        for p in sorted(glob.glob(os.path.join(d, "claude_s*.json"))):
            frames += frames_from_session(p)
        for p in sorted(glob.glob(os.path.join(d, "rejections", "*.json"))):
            if "EldraziDisplacerFlicker" not in os.path.basename(p):
                continue
            frames += frames_from_rejection(p)
    if want:
        frames = [(n, f) for (n, f) in frames if want in n]
    if not frames:
        print("no frames found under", LOGDIRS)
        return 0
    shown = 0
    for name, dec in frames:
        line, won, fail = run(build_fixture(dec))
        mark = "SHOWN " if " offered=1" in line else "absent"
        if " offered=1" in line:
            shown += 1
        print("  %-6s %-44s %s%s" % (mark, name, line.replace("scenario: combo_off ", ""),
                                     ("   [" + fail.replace("scenario: FAIL ", "") + "]")
                                     if fail else ""))
    print("Frames: %d, button shown on %d" % (len(frames), shown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
