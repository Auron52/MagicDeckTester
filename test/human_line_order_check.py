#!/usr/bin/env python3
"""HUMAN LINE ORDER: the viewer's queued action order is applied AS-IS.

USER 2026-09-10, EldraziDisplacerFlicker: "the order is off for Emiel activations and Kitchen
activations. This means that I cannot draw and then untap the kitchen without going to an extra
breakpoint ... Ideally the order I provide would be followed as-is."

WHY THIS IS A CHECK AND NOT A NOTE IN A DOC. The realised order is invisible to every other layer.
viewer_protocol_check.py replays plan INDICES and compares win turns; viewer_client_check.js sees
the GUI's bookkeeping but never the apply; the regression suite's digests cover autonomous play,
which this feature is gated out of by construction. So a regression here -- the marker dropped from
the pin, a kind falling out of TurnSolver::IsTrailingActivation, the trailing pass running twice --
would be silent in every green gate while the player's line quietly ran in enumerator order again.

WHAT IT DRIVES. references/EldraziDisplacerFlicker/claude_s1_gi0.json replayed to its turn-6
pre-combat main (Emiel the Blessed, a Clue Token, a Cloud of Faeries and two Investigate lands all
in play), then ONE plan committed under several --cast-order pins, reading the realised sequence off
MTG_LINE_ORDER_TRACE. Every arm commits the SAME plan index and the enumerator emits that plan in
exactly ONE action order, so anything that differs between the arms was decided by the pin.

  * the declared order is applied verbatim, both ways round (activation vs activation);
  * a cast and two activations interleave in any declared sequence;
  * the un-pinned arm keeps enumerator order;
  * MTG_HUMAN_LINE_ORDER=0 reproduces the un-pinned arm (the documented opt-out);
  * a pin with no leading "*" reproduces it too (every reference saved before 2026-09-10).

SKIPS (exit 0) rather than failing when the board shape it needs is not reachable -- a reference
re-save or an enumeration change may legitimately move the decision, and a brittle red gate is
worse than none. It fails ONLY on a realised order that disagrees with the declared one.

Usage:  python3 test/human_line_order_check.py        (MTG_BIN=<path> to override the binary)
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import viewer_protocol_check as V   # flatten_choices / force_arg / side_channel_args

MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
DECK = os.path.join(ROOT, "decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.cod")
PROF = os.path.join(ROOT, "decks", "EldraziDisplacerFlicker",
                    "EldraziDisplacerFlicker.profile.json")
REF = os.path.join(ROOT, "references", "EldraziDisplacerFlicker", "claude_s1_gi0.json")
UPTO = 21                      # -> turn 6 pre-combat main
LINE = "cast=Clue Token;blink=Emiel the Blessed@10*1"
TURN = 6

fails = []


def skip(msg):
    print(f"SKIP: human line order ({msg})")
    sys.exit(0)


def invoke(choices, side, force, line=None, order=None, env_extra=None):
    args = [MTG, DECK, "--claude-play",
            "--seed", str(REFJ["seed"]), "--game-index", str(REFJ["game_index"]),
            "--max-turns", "8", "--depth", "0", "--profile", PROF,
            "--choices", ",".join(str(c) for c in choices)] + side
    if force is not None:
        args += ["--force-mulligan", force]
    if line is not None:
        args += ["--validate-line", line]
    if order is not None:
        args += ["--cast-order", order]
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_LINE_ORDER_TRACE="1")
    if env_extra:
        env.update(env_extra)
    return subprocess.run(args, capture_output=True, text=True, env=env, cwd=ROOT)


def block(out, tag):
    m = re.search(r"<<<%s>>>\n(.*?)\n<<<END_%s>>>" % (tag, tag.split("_")[-1]), out, re.S)
    return m.group(1) if m else None


def realised(proc):
    """The sequence the committed apply performed, from MTG_LINE_ORDER_TRACE."""
    return [l.split(maxsplit=4)[-1] for l in proc.stderr.splitlines()
            if l.startswith(f"[line-order] turn={TURN} ") and " human_seq=" in l]


def check(label, order, want, env_extra=None):
    got = realised(invoke(BASE, SIDE, FORCE, order=order, env_extra=env_extra))
    if got == want:
        print(f"  PASS  {label}: {got}")
    else:
        fails.append(f"{label}: applied {got}, declared {want}")
        print(f"  FAIL  {label}: applied {got}, expected {want}")


if not os.path.exists(MTG):
    skip(f"binary '{MTG}' missing -- build Release first")
if not os.path.exists(REF):
    skip("reference claude_s1_gi0.json is gone")

REFJ = json.load(open(REF))
FORCE = V.force_arg(REFJ)
PRE = V.flatten_choices(REFJ["decisions"][:UPTO], drop_mulligan=FORCE is not None)
SIDE = V.side_channel_args(REFJ["decisions"])

# Resolve the decision and the plan the line accepts -- nothing about either is hardcoded, so a
# re-enumeration that renumbers the menu moves this check with it instead of breaking it.
v = invoke(PRE, SIDE, FORCE, line=LINE)
vb = block(v.stdout, "CLAUDE_VALIDATION")
if not vb:
    skip(f"the turn-{TURN} decision is no longer reachable from this reference prefix")
vj = json.loads(vb)
if vj["verdict"] != "accept":
    skip(f"'{LINE}' now verdicts '{vj['verdict']}' -- the board shape moved")
dec = vj["decision"]
ORDN, PI = dec["main_ordinal"], vj["plan_index"]
plans = {p["index"]: p for p in dec["plans"]}
acts = [a["card"] for a in plans[PI]["actions"]]
if sorted(acts) != ["Clue Token", "Emiel the Blessed"]:
    skip(f"plan {PI} is now {acts}, not the blink+Clue pair this check reasons about")

print(f"human line order: turn {TURN} pre-combat main (main_ordinal {ORDN}, "
      f"{len(dec['plans'])} plans); '{LINE}' accepts plan {PI}, enumerated as {acts}")

# ---- activation vs activation: the user's report -------------------------------------------
# The enumerator emits this plan in ONE order and CheckLine matches by multiset, so before the
# fix BOTH spellings ran the blink first and "draw, then untap" needed a second committed line.
BASE = PRE + [PI]
ENUM = acts
check("draw THEN blink        ", f"{ORDN}:*|Clue Token|Emiel the Blessed",
      ["Clue Token", "Emiel the Blessed"])
check("blink THEN draw        ", f"{ORDN}:*|Emiel the Blessed|Clue Token",
      ["Emiel the Blessed", "Clue Token"])
check("no pin -> enumerator   ", None, ENUM)
check("MTG_HUMAN_LINE_ORDER=0 ", f"{ORDN}:*|Clue Token|Emiel the Blessed", ENUM,
      {"MTG_HUMAN_LINE_ORDER": "0"})
check("pin without the marker ", f"{ORDN}:Clue Token|Emiel the Blessed", ENUM)

# ---- cast vs activation, interleaved --------------------------------------------------------
# A plan holding one hand cast and two activations: the declared sequence must be honoured across
# the cast/activation boundary too, not just among the activations.
MIX = None
for i, p in enumerate(dec["plans"]):
    names = [a["card"] for a in p["actions"]]
    if (len(names) == 3 and "Wild Growth" in names
            and "Mariposa Military Base" in names and "Emiel the Blessed" in names):
        MIX = (p["index"], names)
        break
if MIX is None:
    print("  SKIP  cast/activation interleave: no cast+2-activation plan in this menu")
else:
    pi2, enum2 = MIX
    print(f"  ...cast+activation plan {pi2}, enumerated as {enum2}")
    BASE = PRE + [pi2]
    # Only the ACTIVATIONS are traced in the un-pinned (canonical) branch, so compare the pinned
    # arms against their own declarations and the un-pinned arm against its activation subset.
    for seq in (["Wild Growth", "Mariposa Military Base", "Emiel the Blessed"],
                ["Mariposa Military Base", "Emiel the Blessed", "Wild Growth"],
                ["Mariposa Military Base", "Wild Growth", "Emiel the Blessed"]):
        check("interleave " + " > ".join(s.split()[0] for s in seq),
              f"{ORDN}:*|" + "|".join(seq), seq)

if fails:
    print(f"human line order: FAIL ({len(fails)})")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("human line order: PASS")
