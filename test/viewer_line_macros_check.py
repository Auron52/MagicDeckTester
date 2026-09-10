#!/usr/bin/env python3
"""LINE MACROS: a queued CONTINUATION steers the untap pick, and a fused gesture's halves each land.

USER, EldraziDisplacerFlicker 2026-09: "a shortcut for the create clue + sacrifice and maybe even a
way to multistack draw + untap + draw + untap since it is really slow doing it manually, especially
when the engine slows down."

WHY THIS IS A CHECK AND NOT A DOC NOTE. Both features are deliberately built out of machinery that
already exists -- the macro expands client-side into ordinary segments, the fused clue is two
ordinary entries -- so almost everything about them is invisible to the engine by design. What is
NOT invisible is the one piece that had to be new, and it is invisible to everything else:

  * `need=<COLOURS>` diverts an ETB-untap pick toward the colours the human's REMAINING QUEUE wants.
    The regression digests and the reference sweep never declare one (it is human-play-only and
    default-inert), so both stay green whatever it does. And the board alone cannot witness it: the
    yield order and the demand order agree most of the time, so "the continuation moved this pick"
    and "the yield ranking happened to pick it" end the turn identically.
  * the failure that matters most is SILENT REVERSION -- the promotion quietly not firing (a gate
    flipped, the mask lost in the token lift) leaves every outcome test green while the macro goes
    back to untapping whatever has the biggest Overgrowth on it.

So the assertions are about WHICH LANDS UNTAP, read off the next frame's board and cross-checked
against MTG_UNTAP_DEMAND_TRACE -- the mechanism, not the outcome.

WHAT IT DRIVES. EldraziDisplacerFlicker `claude_s1_gi0`, the turn-6 pre-combat main (the same frame
human_line_order_check.py and manual_tap_check.py drive). That board is the right shape by
construction: FOUR hand-tappable lands with different faces -- Azorius Chancery {W}{U}, Conservatory
{G}{W}, Mariposa Military Base {C}, Yavimaya Coast {G}{U}{C} -- an Emiel that blinks a Cloud of
Faeries for "untap up to two lands", and NO {C} sink on board or in hand, so the pre-existing
starved-{C} promotion (MTG_UNTAP_C_STARVED) is dormant and anything that moves here moved because of
the declared continuation. The line pre-taps all four by hand (the manual tap/pay fallback, which
rides the same --cast-order list) so there is a real choice of tapped lands to untap.

ASSERTS
  a) with NO `need=`, the untap takes the yield order -- the historical behaviour, no trace;
  b) `need=C` promotes the board's only {C} source into the untapped set, and says so in the trace;
  c) exactly ONE pick moves (the promotion is bounded, not a re-ranking);
  d) `need=U` does NOT divert, because a {U} source is already in the chosen set -- demand that is
     already served is not demand, which is what stops a standing need bleeding yield every loop;
  e) MTG_UNTAP_LINE_DEMAND=0 reproduces arm (a), so the flag is a real off switch;
  f) FUSED "investigate & crack": the investigate half really creates a Clue, and the crack half is
     enumerable on the frame that commit produces -- which is the whole reason the crack is deferred;
  g) MACRO sequencing: a repeated block's second iteration validates against the frame its own first
     iteration produced, i.e. LB.repeatBlock's segments are each individually committable.

SKIPS (exit 0) when the binary or the reference is missing, or when the board has moved so the frame
this drives is no longer reachable -- the same policy as human_line_order_check.py.

Usage: MTG_BIN=./build/Release/mtg python3 test/viewer_line_macros_check.py
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import viewer_protocol_check as V   # flatten_choices / force_arg / side_channel_args  # noqa: E402

MTG  = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
DECK = os.path.join("decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.cod")
PROF = os.path.join("decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.profile.json")
REF  = os.path.join("references", "EldraziDisplacerFlicker", "claude_s1_gi0.json")

UPTO = 21   # -> the turn-6 pre-combat main

fails = []


def skip(why):
    print("SKIP: line macros (%s)" % why)
    sys.exit(0)


def check(label, got, want):
    if got == want:
        print("  PASS  %s: %r" % (label, got))
    else:
        fails.append("%s: got %r, expected %r" % (label, got, want))
        print("  FAIL  %s: got %r, expected %r" % (label, got, want))


def merge_cast_order(side, entry):
    """The reference's own --cast-order pins PLUS one more for the decision under test. Replacing
    the argument would drop every earlier pin and replay a different game -- the trap
    `side_channel_args` exists to avoid (see manual_tap_check.py's twin)."""
    s = list(side)
    if not entry:
        return s
    if "--cast-order" in s:
        i = s.index("--cast-order")
        s[i + 1] = s[i + 1] + ";" + entry
    else:
        s += ["--cast-order", entry]
    return s


def invoke(choices, side, line=None, env_extra=None):
    args = [MTG, DECK, "--claude-play",
            "--seed", str(REFJ["seed"]), "--game-index", str(REFJ["game_index"]),
            "--max-turns", "8", "--depth", "0", "--profile", PROF,
            "--choices", ",".join(str(c) for c in choices)] + side
    if FORCE is not None:
        args += ["--force-mulligan", FORCE]
    if line is not None:
        args += ["--validate-line", line]
    # MTG_PLAY_PLANS_CAP=0 uncaps the plan list exactly as the GUI does; the trace is what tells a
    # diverted pick from a coincidentally-equal one.
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_UNTAP_DEMAND_TRACE="1")
    if env_extra:
        env.update(env_extra)
    return subprocess.run(args, capture_output=True, text=True, env=env, cwd=ROOT)


def block(out, tag):
    m = re.search(r"<<<%s>>>\n(.*?)\n<<<END_[A-Z_]+>>>" % tag, out, re.S)
    return json.loads(m.group(1)) if m else None


def promotions(proc):
    """The picks the declared continuation moved, from MTG_UNTAP_DEMAND_TRACE."""
    return [l.split("promote ", 1)[1] for l in proc.stderr.split("\n")
            if l.startswith("[untap-demand] ")]


def tapped_set(dec):
    return sorted(o["name"] for o in dec["me"]["battlefield"] if o.get("tapped"))


# ---- resolve the board without hardcoding any index -------------------------------------------
if not os.path.exists(MTG):
    skip("binary %s missing" % MTG)
if not os.path.exists(os.path.join(ROOT, REF)):
    skip("reference %s missing" % REF)

REFJ  = json.load(open(os.path.join(ROOT, REF)))
FORCE = V.force_arg(REFJ)
PRE   = V.flatten_choices(REFJ["decisions"][:UPTO], drop_mulligan=FORCE is not None)
SIDE  = V.side_channel_args(REFJ["decisions"])

DEC = block(invoke(PRE, SIDE).stdout, "CLAUDE_DECISION")
if DEC is None or DEC.get("type") != "main_phase":
    skip("the turn-6 main is no longer the frame at prefix %d" % UPTO)
ORDN = DEC.get("main_ordinal")
if ORDN is None:
    skip("that frame carries no main_ordinal (it cannot hold a pin)")

# The blink, by CONTENT: Emiel blinking the Cloud of Faeries ("untap up to two lands").
CLOUD = next((o for o in DEC["me"]["battlefield"] if o["name"] == "Cloud of Faeries"), None)
if CLOUD is None:
    skip("no Cloud of Faeries on this board -- nothing here untaps lands")
BLINK = "blink=Emiel the Blessed@%d*1" % CLOUD["num"]
vb = block(invoke(PRE, SIDE, line=BLINK).stdout, "CLAUDE_VALIDATION")
if vb is None or vb.get("verdict") != "accept":
    skip("'%s' no longer accepts here -- the board shape moved" % BLINK)
PI = vb["plan_index"]

# Every hand-tappable land, so the line can tap them all and leave the ETB untap a real choice.
SRCS = [o for o in DEC["me"]["battlefield"] if o.get("taps") and not o.get("tapped")]
if len(SRCS) < 3:
    skip("fewer than three untapped hand-tappable lands -- the untap has no choice to make")
C_SRC = [o["name"] for o in SRCS if "C" in o["taps"]]
if not C_SRC:
    skip("no {C}-capable land among them -- the need=C arm has nothing to promote")

TAPS = "|".join("tap=%s#%d:%s" % (o["name"], o["num"], o["taps"][0]) for o in SRCS)
BASE = "%d:*|%s|Emiel the Blessed" % (ORDN, TAPS)

print("line macros: turn %d %s (main_ordinal %d); blink plan %d; %d hand-tappable lands (%s)"
      % (DEC["turn"], DEC["phase"], ORDN, PI, len(SRCS),
         ", ".join("%s{%s}" % (o["name"], o["taps"]) for o in SRCS)))


def run(pin, env=None):
    p = invoke(PRE + [PI], merge_cast_order(SIDE, pin), env_extra=env)
    nd = block(p.stdout, "CLAUDE_DECISION")
    return (tapped_set(nd) if nd else None), promotions(p)


# ---- a) NO declared continuation: the yield order stands ---------------------------------------
BASE_TAPPED, base_promo = run(BASE)
if BASE_TAPPED is None:
    skip("the commit produced no following frame (the game ended here)")
check("no need= -> no pick is diverted", base_promo, [])

# ---- b) need=C promotes the board's only {C} source --------------------------------------------
# The discriminator is that a {C}-capable land is left TAPPED by arm (a) and untapped here: it can
# only have got there by the declared demand.
c_tapped, c_promo = run(BASE + "|need=C")
still_tapped_a = [n for n in C_SRC if n in BASE_TAPPED]
if not still_tapped_a:
    print("  SKIP  need=C arm: the yield order already untaps every {C} land here (nothing to move)")
else:
    check("need=C diverts exactly one pick", len(c_promo), 1)
    check("...and it promotes a {C}-capable land",
          bool(c_promo) and c_promo[0].split(" over ")[0] in C_SRC, True)
    check("a {C} land the yield order left tapped is now untapped",
          any(n not in c_tapped for n in still_tapped_a), True)
    # ---- c) BOUNDED: exactly one land differs from the un-declared arm --------------------------
    # A re-ranking would move several. The bound is the whole reason this is safe to default ON.
    moved = set(BASE_TAPPED) ^ set(c_tapped)
    check("exactly one untap pick moved (2 names differ: one in, one out)", len(moved), 2)

# ---- d) demand that is ALREADY served does not divert -------------------------------------------
# `need=U` on a board whose chosen set already holds a {U} source must be a no-op -- otherwise a
# standing need would bleed yield on every iteration of a macro.
u_in_base = [o["name"] for o in SRCS if "U" in o["taps"] and o["name"] not in BASE_TAPPED]
if not u_in_base:
    print("  SKIP  need=U arm: no {U} source in the un-declared arm's untapped set")
else:
    u_tapped, u_promo = run(BASE + "|need=U")
    check("need=U with a {U} source already chosen -> no diversion", u_promo, [])
    check("...and the untapped set is unchanged", u_tapped, BASE_TAPPED)

# ---- e) the flag is a real off switch -----------------------------------------------------------
off_tapped, off_promo = run(BASE + "|need=C", {"MTG_UNTAP_LINE_DEMAND": "0"})
check("MTG_UNTAP_LINE_DEMAND=0 diverts nothing", off_promo, [])
check("MTG_UNTAP_LINE_DEMAND=0 reproduces the un-declared arm", off_tapped, BASE_TAPPED)

# ---- f) FUSED "investigate & crack": both halves land -------------------------------------------
# The crack is DEFERRED because the Clue does not exist while the investigate is being committed.
# This is the assertion that the deferral is what makes it reachable, not a workaround: the Clue
# count goes up, and the crack is enumerated on the frame the investigate produced.
INV = next((o["name"] for o in DEC["me"]["battlefield"]
            if any(a.get("makes_clue") and (a.get("activate_source") or a.get("card")) == o["name"]
                   for p in DEC.get("plans", []) for a in (p.get("actions") or []))), None)
if INV is None:
    print("  SKIP  fused clue: no Investigate source is enumerated on this board")
else:
    def clues(dec):
        return len([o for o in dec["me"]["battlefield"] if o["name"] == "Clue Token"])
    vi = block(invoke(PRE, SIDE, line="cast=%s" % INV).stdout, "CLAUDE_VALIDATION")
    if vi is None or vi.get("verdict") != "accept":
        print("  SKIP  fused clue: '%s' does not accept alone here" % INV)
    else:
        after = block(invoke(PRE + [vi["plan_index"]], SIDE).stdout, "CLAUDE_DECISION")
        if after is None:
            print("  SKIP  fused clue: the investigate produced no following frame")
        else:
            check("the investigate half creates a Clue", clues(after), clues(DEC) + 1)
            # The deferred half must be REACHABLE on that frame -- that is the whole claim. Asserted
            # as "a plan there sacrifices a Clue", not as a fixed line: whether the crack also needs
            # the turn's land drop is an affordability fact about this board, not about the feature.
            check("the crack half is enumerated on the frame it produced",
                  any(a.get("card") == "Clue Token"
                      for p in after.get("plans", []) for a in (p.get("actions") or [])), True)

# ---- g) MACRO sequencing: iteration 2 commits against iteration 1's frame ------------------------
# LB.repeatBlock emits N copies of the block, each deferred into its own segment. This is the engine
# half of that contract: the identical line still validates on the frame its own commit produced.
after_blink = block(invoke(PRE + [PI], SIDE).stdout, "CLAUDE_DECISION")
if after_blink is None or after_blink.get("type") != "main_phase":
    print("  SKIP  macro sequencing: the blink's commit did not re-prompt in a main phase")
else:
    v2 = block(invoke(PRE + [PI], SIDE, line=BLINK).stdout, "CLAUDE_VALIDATION")
    check("a repeated block's second iteration validates on the frame the first produced",
          (v2 or {}).get("verdict"), "accept")

if fails:
    print("line macros: FAIL (%d)" % len(fails))
    for f in fails:
        print("  - %s" % f)
    sys.exit(1)
print("line macros: PASS")
