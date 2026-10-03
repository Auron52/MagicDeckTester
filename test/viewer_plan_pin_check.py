#!/usr/bin/env python3
"""The "play it anyway" override must stay BOUNDED -- and must still work.

USER, 2026-10-03, on the first cut of the override:
    "Hit an error when using the new feature to run the line anyway ...
     [rss-cap] mtg: rss=17.78G EXCEEDS the cap 17.60G -- aborting this process so the box survives"

WHAT BROKE AND WHY IT NEEDS A GATE. The override's first mechanism SUSPENDED the viewer plan-space
valve for the overridden frame, i.e. enumerated it in full. "In full" is not a bounded quantity: the
very next frame of the user's own seed-15 game was priced at 768,000,000 positions, the process
reached 17.78 GB, and the RSS cap killed it mid-click. The replacement PINS the cards the player's
line names into the valve's keep set instead, so the menu is bounded by the ordinary 65,536-position
cap while still containing the line (viewerplancap::PinScope, src/ai/EngineFlags.h).

Nothing else in the suite can witness either half:
  * the valve arms only under HumanPlayActive(), so no deck case and no reference replay reaches it;
  * the failure was a MEMORY bound, which no verdict or digest records -- the only way to see it is
    to run the frame under a cap and look at whether the process survives.

So this drives the user's own frames and asserts BOTH halves, with the suspend route as the negative
control. Two fixtures:

  FRAME A (narrow, main ordinal 13) -- the frame originally reported. The line is rules-legal and the
      valve had dropped the group carrying it (a card's SECOND hand copy, which the cover rule
      cannot protect by construction). Asserts: refused without the pin, ACCEPTED with it.

  FRAME B (wide, main ordinal 14) -- the frame that broke, reached by playing frame A's pinned line.
      The valve prices it in the 1e9 positions range. Asserts: with the valve OFF (what suspending
      it did) the process is RSS-killed under a small cap; with the PIN it completes under that same
      cap. The negative control is the point -- without it, "the pinned run fits" says nothing about
      whether anything ever did not.

SKIPS ITSELF when the frames are not reachable (an engine change that moves this deck's play), in
the same spirit as manual_tap_check.py and pay_line_color_check.py: a shifted board must not read as
a regression in the thing being guarded.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("MTG_BIN", os.path.join(ROOT, "build/Release/mtg"))
DECK = "decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod"
PROF = "decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.profile.json"

# The user's own s15/gi14 prefix (from logs/play/rejections/..._s15_gi14_t5.json), plus the pinned
# pick that frame A's override makes playable -- which is how frame B is reached.
PREFIX_A = "0,0,0,1,5,3,1,1,-1,-1,-1,-1,13,-1,-1,17,8,214,-1,-1"
CAST_ORDER = "8:*|Sram, Senior Edificer|Kite Shield"
LINE_A = ("land=Ancient Den;cast=Cid, Freeflier Pilot;cast=Golem-Skin Gauntlets;"
          "cast=Golem-Skin Gauntlets;cast=Shadowspear")
# What the viewer's playAnywayNames() sends for that queue: the queued entries' card names.
PINS_A = "13:Ancient Den|Cid, Freeflier Pilot|Golem-Skin Gauntlets|Shadowspear"
PLAN_A = 135                                 # the index the pinned menu puts the line at
PREFIX_B = PREFIX_A + "," + str(PLAN_A)
LINE_B = "cast=Colossus Hammer;cast=Colossus Hammer"
# Small enough that the suspend route dies in seconds, large enough that the pinned route (measured
# at ~0.85 GB on frame B) has real headroom. The gap between the two arms is ~5x, not marginal.
CAP_GB = 2

fails = []


def run(prefix, line=None, pins=None, cap_gb=None, valve_off=False):
    args = [BIN, DECK, "--profile", PROF, "--cards-json", "src/cards/data/cards.json",
            "--claude-play", "--seed", "15", "--game-index", "14", "--max-turns", "8",
            "--depth", "0", "--choices", prefix, "--cast-order", CAST_ORDER]
    if pins:
        args += ["--full-enum", pins]
    if line:
        args += ["--validate-line", line]
    env = dict(os.environ)
    if cap_gb:
        env["MTG_RSS_CAP_GB"] = str(cap_gb)
    if valve_off:
        env["MTG_VIEWER_PLAN_CAP"] = "0"
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, env=env)
    return r


def verdict_of(out):
    """The validation block is followed by the rest of the run's stdout, so decode exactly one
    object off the front rather than parsing the tail (which is not valid JSON)."""
    i = out.find("<<<CLAUDE_VALIDATION>>>")
    if i < 0:
        return None
    tail = out[i + len("<<<CLAUDE_VALIDATION>>>"):]
    j = tail.find("{")
    if j < 0:
        return None
    try:
        obj, _ = json.JSONDecoder().raw_decode(tail[j:])
        return obj
    except Exception:
        return None


# ---- reachability -----------------------------------------------------------------------------
probe = run(PREFIX_A)
if "<<<CLAUDE_DECISION>>>" not in probe.stdout:
    print("SKIP viewer_plan_pin_check: frame A is not reachable (no decision dump) -- "
          "the deck's play has moved; nothing to assert.")
    sys.exit(0)
ord_a = re.search(r'"main_ordinal"\s*:\s*(-?\d+)', probe.stdout)
if not ord_a or ord_a.group(1) != "13":
    print("SKIP viewer_plan_pin_check: frame A is main ordinal %s, not 13 -- the prefix no longer "
          "lands on the reported frame." % (ord_a.group(1) if ord_a else "?"))
    sys.exit(0)

# ---- FRAME A: the override must still do its job ----------------------------------------------
r = run(PREFIX_A, line=LINE_A)
v = verdict_of(r.stdout)
if not v:
    print("SKIP viewer_plan_pin_check: frame A emitted no validation block.")
    sys.exit(0)
if v.get("verdict") != "legal_not_enumerated":
    print("SKIP viewer_plan_pin_check: frame A now grades '%s', not 'legal_not_enumerated' -- the "
          "valve no longer drops the group carrying this line, so there is no override to test."
          % v.get("verdict"))
    sys.exit(0)
print("frame A, no pin : legal_not_enumerated  (the refusal the player sees)  OK")

r = run(PREFIX_A, line=LINE_A, pins=PINS_A)
v = verdict_of(r.stdout) or {}
if v.get("verdict") != "accept":
    fails.append("frame A WITH the pin graded '%s', expected 'accept' -- the override no longer "
                 "makes the player's rules-legal line playable." % v.get("verdict"))
else:
    print("frame A, pinned : accept at plan_index %s  OK" % v.get("plan_index"))

# ---- FRAME B: the override must not be able to un-bound the enumeration -----------------------
probe = run(PREFIX_B, pins=PINS_A)
trunc = re.search(r'"plans_truncated".{0,80}?\((\d+) -> (\d+) positions\)', probe.stdout, re.S)
if "<<<CLAUDE_DECISION>>>" not in probe.stdout or not trunc:
    print("SKIP viewer_plan_pin_check: frame B is not reachable as a TRUNCATED frame -- the wide "
          "board this guards is gone, so there is no unbounded path to close.")
    sys.exit(1 if fails else 0)
full_pos = int(trunc.group(1))
print("frame B         : valve prices it at %d -> %s positions" % (full_pos, trunc.group(2)))
if full_pos < 10_000_000:
    print("SKIP viewer_plan_pin_check: frame B is only %d positions -- too narrow to distinguish a "
          "bounded re-enumeration from an unbounded one." % full_pos)
    sys.exit(1 if fails else 0)

ord_b = re.search(r'"main_ordinal"\s*:\s*(-?\d+)', probe.stdout)
pins_b = PINS_A + ";" + ord_b.group(1) + ":Colossus Hammer"

def blew_cap(r):
    """Did the RSS cap stop this run? NOT `returncode != 0`: a --validate-line run exits 71 by
    design ("validation verdict emitted", main.cpp), so a clean override run is a NON-ZERO exit and
    testing the code alone reports every success as a failure. The cap announces itself on stderr
    and then SIGKILLs, so test for either."""
    return "[rss-cap]" in r.stderr or r.returncode < 0 or r.returncode == 137


# NEGATIVE CONTROL: the suspend route. Enumerating this frame in full is what took the user's
# process to 17.78 GB; under a small cap it must still die, or this fixture proves nothing.
r = run(PREFIX_B, line=LINE_B, pins=PINS_A, cap_gb=CAP_GB, valve_off=True)
if not blew_cap(r):
    print("SKIP viewer_plan_pin_check: the NEGATIVE CONTROL survived -- enumerating frame B in full "
          "fits in %d GB now, so this frame can no longer tell a bounded override from an "
          "unbounded one. Find a wider frame rather than trusting the pinned arm below." % CAP_GB)
    sys.exit(1 if fails else 0)
print("frame B, valve OFF (what SUSPENDING it did): rc=%s, rss-cap announced=%s  "
      "(negative control: the unbounded path really is unbounded)  OK"
      % (r.returncode, "[rss-cap]" in r.stderr))

# THE ASSERTION: the same frame, same cap, with the override expressed as a PIN.
r = run(PREFIX_B, line=LINE_B, pins=pins_b, cap_gb=CAP_GB)
if blew_cap(r):
    fails.append("frame B WITH the pin hit the %d GB cap (rc=%s) -- the override is still able to "
                 "un-bound the enumeration, which is the defect this check exists for."
                 % (CAP_GB, r.returncode))
else:
    v = verdict_of(r.stdout) or {}
    print("frame B, pinned : completed under the %d GB cap, graded '%s'  OK"
          % (CAP_GB, v.get("verdict")))
    if v.get("verdict") not in ("accept", "choose"):
        fails.append("frame B WITH the pin graded '%s': the pin bounded the menu but the player's "
                     "line is still unplayable." % v.get("verdict"))

if fails:
    print("\nFAIL viewer_plan_pin_check:")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("\nPASS viewer_plan_pin_check: the override makes the line playable AND cannot un-bound the "
      "enumeration (negative control confirms the unbounded path would die).")
