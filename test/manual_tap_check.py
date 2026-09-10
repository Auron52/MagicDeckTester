#!/usr/bin/env python3
"""MANUAL TAP/PAY: the human's `tap=` tokens are honoured, and ONLY the human's.

USER, EldraziDisplacerFlicker 2026-09: "I think we probably need an alternative fallback for the
user, so they can tap and pay mana as they desire. It's a bit tricky perhaps, but we could have it
available just for fixing poorly allocated taps or mana usage while still being able to give good
feedback on the mana usage by the engine."

WHY THIS IS A CHECK AND NOT A DOC NOTE. A pre-tap is invisible to every other layer:

  * the regression digests and the reference sweep never set one (the fallback is human-play-only
    and default-inert), so both stay green whatever it does;
  * the board alone cannot witness it -- the float a pre-tap makes is spent by the very next
    payment, so "the human dictated this tap" and "the allocator happened to pick the same land"
    end the turn identically;
  * and the failure that matters most is SILENT REPAIR: an engine that quietly re-allocates around
    an awkward hand-forced tap passes every outcome test there is while doing exactly the thing the
    fallback exists to stop.

So the assertions are about the TAPS, not the outcome, and they are read off `MTG_PRE_TAP_TRACE`
plus the next frame's board.

WHAT IT DRIVES. EldraziDisplacerFlicker `claude_s1_gi0`, the turn-3 pre-combat main whose recorded
line is `land=Mariposa Military Base; cast: Trace of Abundance -> Aether Hub`. That board is the
right shape by luck and by construction: TWO hand-tappable sources with different faces (Aether Hub
{C}, Conservatory {G}/{W}), one of them carrying a Wild Growth whose bonus must ride the tap, and a
spell the engine pays for by choosing a face itself -- so "the human chose" and "the engine chose"
are separable. A second phase drives the turn-6 frame (Clue crack + Emiel blink) to pin the
POSITION half, and skips itself when that board is not reachable.

ASSERTS
  a) a line with explicit pre-taps validates, and executes with EXACTLY those sources tapped for
     EXACTLY those colours (aura riders included);
  b) the SAME line with no pre-taps takes the engine's own allocation -- no forced taps at all, and
     a demonstrably different tapped set;
  c) an impossible pre-tap (already tapped / a colour the source cannot make / a permanent that is
     not there) is REJECTED with a reason, never silently fixed;
  d) MTG_HUMAN_PRE_TAP=0 turns the whole fallback off, so arm (a) reduces to arm (b);
  e) the engine's `taps` affordance names exactly the hand-tappable sources (the viewer offers from
     it, so an over-broad list is an offer the engine will refuse);
  f) a tap declared BETWEEN two queued entries fires between them, not before or after both.

SKIPS (exit 0) when the binary or the reference is missing, or when the board has moved so the
frame this drives is no longer reachable -- the same policy as human_line_order_check.py.

Usage: MTG_BIN=./build/Release/mtg python3 test/manual_tap_check.py
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

UPTO = 7    # prefix decisions -> the turn-3 pre-combat main that plays Mariposa + Trace
UPTO6 = 21  # ...and the turn-6 main human_line_order_check.py drives (Clue crack + Emiel blink)

fails = []


def skip(why):
    print("SKIP: manual tap/pay (%s)" % why)
    sys.exit(0)


def merge_cast_order(side, entry):
    """The reference's own --cast-order pins PLUS one more entry for the decision under test.
    Replacing the argument instead of merging would drop every earlier pin and replay a different
    game -- the same trap `side_channel_args` exists to avoid."""
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
    # MTG_PLAY_PLANS_CAP=0 uncaps the plan list exactly as the GUI does; the two traces are what
    # this check reads (pre-tap: the forced taps; line-order: the realised action sequence).
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_PRE_TAP_TRACE="1",
               MTG_LINE_ORDER_TRACE="1")
    if env_extra:
        env.update(env_extra)
    return subprocess.run(args, capture_output=True, text=True, env=env, cwd=ROOT)


def block(out, tag):
    m = re.search(r"<<<%s>>>\n(.*?)\n<<<END_[A-Z_]+>>>" % tag, out, re.S)
    return json.loads(m.group(1)) if m else None


def forced_taps(proc):
    """The taps the HUMAN forced, in the order the apply performed them: (name, colour)."""
    out = []
    for l in proc.stderr.split("\n"):
        m = re.match(r"\[pre-tap\] tap (.+)#(\d+) as (\w) ->", l)
        if m:
            out.append((m.group(1), m.group(3)))
    return out


def rejected_taps(proc):
    return [l.split(": ", 1)[1] for l in proc.stderr.split("\n")
            if l.startswith("[pre-tap] REJECT ")]


def tapped_set(dec):
    return sorted(o["name"] for o in dec["me"]["battlefield"] if o.get("tapped"))


def check(label, got, want):
    if got == want:
        print("  PASS  %s: %r" % (label, got))
    else:
        fails.append("%s: got %r, expected %r" % (label, got, want))
        print("  FAIL  %s: got %r, expected %r" % (label, got, want))


def check_contains(label, got, needle):
    if needle in (got or ""):
        print("  PASS  %s: %s" % (label, got))
    else:
        fails.append("%s: %r does not mention %r" % (label, got, needle))
        print("  FAIL  %s: %r does not mention %r" % (label, got, needle))


# ---- resolve the board without hardcoding any index -------------------------------------------
if not os.path.exists(MTG):
    skip("binary %s missing" % MTG)
if not os.path.exists(os.path.join(ROOT, REF)):
    skip("reference %s missing" % REF)

REFJ  = json.load(open(os.path.join(ROOT, REF)))
FORCE = V.force_arg(REFJ)
PRE   = V.flatten_choices(REFJ["decisions"][:UPTO], drop_mulligan=FORCE is not None)
SIDE  = V.side_channel_args(REFJ["decisions"])

probe = invoke(PRE, SIDE)
DEC = block(probe.stdout, "CLAUDE_DECISION")
if DEC is None or DEC.get("type") != "main_phase":
    skip("the turn-3 main is no longer the frame at prefix %d" % UPTO)
ORDN = DEC.get("main_ordinal")
if ORDN is None:
    skip("that frame carries no main_ordinal (it cannot hold a pin)")

# The plan by CONTENT, never by index: "play Mariposa, enchant the Hub with Trace of Abundance".
PI = next((p["index"] for p in DEC.get("plans", [])
           if p.get("land") == "Mariposa Military Base"
           and (p.get("casts") or []) == ["Trace of Abundance"]
           and any(a.get("enchant_target_name") == "Aether Hub" for a in (p.get("actions") or []))),
          None)
if PI is None:
    skip("the Mariposa + Trace-on-the-Hub plan is no longer enumerated here")

BF   = {o["name"]: o for o in DEC["me"]["battlefield"]}
HUB  = BF.get("Aether Hub")
CONS = BF.get("Conservatory")
if not HUB or not CONS or HUB.get("tapped") or CONS.get("tapped"):
    skip("the two-untapped-source board this pins is not the board any more")

print("manual tap/pay: turn %d %s (main_ordinal %d, %d plans); plan %d = %s"
      % (DEC["turn"], DEC["phase"], ORDN, len(DEC["plans"]), PI, DEC["plans"][PI]["summary"]))

# ---- e) the affordance the viewer offers from -------------------------------------------------
# `taps` is HumanPreTapFaces verbatim, so this pins the OFFER against the engine's own legality
# test: an over-broad list is a click the engine would refuse, a missing one is a source the human
# cannot reach at all. The Wild Growths are the negative control -- an Aura is not a mana source,
# it only makes its HOST produce more.
check("Aether Hub's tappable faces",  HUB.get("taps"),  "C")
check("Conservatory's tappable faces", CONS.get("taps"), "GW")
check("an Aura offers no tap of its own",
      sorted(o["name"] for o in DEC["me"]["battlefield"] if o.get("taps")),
      ["Aether Hub", "Conservatory"])

TAP_HUB  = "tap=Aether Hub#%d:C" % HUB["num"]
TAP_CONS = "tap=Conservatory#%d:W" % CONS["num"]

# ---- b) NO pre-taps: the engine allocates, exactly as it always has ----------------------------
base = invoke(PRE + [PI], SIDE)
base_dec = block(base.stdout, "CLAUDE_DECISION")
if base_dec is None:
    skip("the commit produced no following frame (the game ended here)")
check("no pre-taps -> no forced taps at all", forced_taps(base), [])
ENGINE_TAPPED = tapped_set(base_dec)

# ---- a) WITH pre-taps: exactly those sources, exactly those colours ----------------------------
armed = invoke(PRE + [PI], merge_cast_order(SIDE, "%d:%s|%s" % (ORDN, TAP_HUB, TAP_CONS)))
armed_dec = block(armed.stdout, "CLAUDE_DECISION")
if armed_dec is None:
    fails.append("the pre-tapped commit produced no following frame")
    print("  FAIL  the pre-tapped commit produced no following frame")
else:
    check("declared taps are performed, in order, with the declared faces",
          forced_taps(armed), [("Aether Hub", "C"), ("Conservatory", "W")])
    # Both pre-tapped sources really are tapped afterwards. Aether Hub is the discriminator: the
    # engine's own allocation (arm b) leaves it UNTAPPED, so its being tapped here can only be the
    # human's doing.
    for nm in ("Aether Hub", "Conservatory"):
        check("%s is tapped after the pre-tapped line" % nm, nm in tapped_set(armed_dec), True)
    # ...and the allocation genuinely MOVED. Identical tapped sets would mean the fallback changed
    # nothing measurable and every assertion above could be passing vacuously.
    check("the human's allocation differs from the engine's",
          tapped_set(armed_dec) != ENGINE_TAPPED, True)
    # The float the human banked but the line did not need SURVIVES (CR 500.4) rather than being
    # re-allocated behind their back -- the Wild Growth rider on Conservatory is part of it.
    check("the unspent forced mana is still floating",
          bool(armed_dec["me"].get("floating_mana")), True)

# ---- d) the flag really is the off switch -----------------------------------------------------
off = invoke(PRE + [PI], merge_cast_order(SIDE, "%d:%s|%s" % (ORDN, TAP_HUB, TAP_CONS)),
             env_extra={"MTG_HUMAN_PRE_TAP": "0"})
off_dec = block(off.stdout, "CLAUDE_DECISION")
check("MTG_HUMAN_PRE_TAP=0 forces no taps", forced_taps(off), [])
if off_dec is not None:
    check("MTG_HUMAN_PRE_TAP=0 reproduces the engine's allocation",
          tapped_set(off_dec), ENGINE_TAPPED)

# ---- c) an impossible pre-tap is REJECTED, with a reason ---------------------------------------
# Through --validate-line, i.e. the path the viewer actually asks on, so the human is told before
# they commit. Every arm asserts the REASON text too: "illegal" alone would also be produced by an
# unrelated failure, and the point of the fallback is that it explains itself.
LINE = "land=Mariposa Military Base;cast=Trace of Abundance"
for label, taps, needle in [
    ("a colour the source cannot make", "tap=Conservatory#%d:U" % CONS["num"],
     "cannot produce {U}"),
    ("a source already tapped earlier in the line",
     "%s;tap=Conservatory#%d:G" % (TAP_CONS, CONS["num"]), "is already tapped"),
    ("a permanent that is not on the battlefield", "tap=Kitchen#0:G",
     "you control no untapped 'Kitchen'"),
    ("a copy id that is not in play", "tap=Conservatory#999:G",
     "no copy of 'Conservatory' with that id"),
    ("no colour stated at all", "tap=Conservatory#%d" % CONS["num"], "names no colour"),
    ("an Aura is not a mana source", "tap=Wild Growth#0:G",
     "cannot be tapped by hand"),
]:
    p = invoke(PRE, SIDE, line=taps + ";" + LINE)
    vj = block(p.stdout, "CLAUDE_VALIDATION")
    if vj is None:
        fails.append("%s: no validation verdict was emitted" % label)
        print("  FAIL  %s: no validation verdict was emitted" % label)
        continue
    check("%s -> illegal" % label, vj["verdict"], "illegal")
    check_contains("%s -> reason" % label, vj.get("reason"), needle)

# ...and the accepting twin, so "illegal" above is not just this line being unplayable anyway.
# `choose` counts: this line names no enchant host, so the engine rightly offers the host variants.
ok = invoke(PRE, SIDE, line=TAP_CONS + ";" + LINE)
okj = block(ok.stdout, "CLAUDE_VALIDATION")
check("a legal pre-tap still validates",
      (okj or {}).get("verdict") in ("accept", "choose"), True)

# ---- f) POSITION: a tap declared between two entries fires between them ------------------------
# The turn-6 frame human_line_order_check.py drives: crack the Clue, then blink Emiel. A soft skip
# (not a failure) when that board is not reachable -- this arm is about ordering, and phases a-e
# have already established the mechanism.
PRE6 = V.flatten_choices(REFJ["decisions"][:UPTO6], drop_mulligan=FORCE is not None)
LINE6 = "cast=Clue Token;blink=Emiel the Blessed@10*1"
p6 = invoke(PRE6, SIDE, line=LINE6)
v6 = block(p6.stdout, "CLAUDE_VALIDATION")
if v6 is None or v6.get("verdict") != "accept":
    print("  SKIP  position arm: the turn-6 Clue+blink frame is not reachable here")
else:
    ORD6 = v6["decision"].get("main_ordinal")
    src6 = [o for o in v6["decision"]["me"]["battlefield"]
            if o.get("taps") and not o.get("tapped")]
    if ORD6 is None or not src6:
        print("  SKIP  position arm: no untapped hand-tappable source on that board")
    else:
        s = src6[0]
        tok = "tap=%s#%d:%s" % (s["name"], s["num"], s["taps"][0])
        turn6 = v6["decision"]["turn"]

        def realised(pin):
            """The two traces interleaved by stderr order -- the ONE observable that can tell where
            in the line a tap ran. A REJECTED tap counts as an occurrence: this arm is about
            POSITION, and a tap the human put after the action that spends its land is dropped
            where it stands (the same deal the human line order makes for an activation)."""
            pp = invoke(PRE6 + [v6["plan_index"]], merge_cast_order(SIDE, pin))
            seq, ok_tap = [], False
            for l in pp.stderr.split("\n"):
                m = re.match(r"\[line-order\] turn=%d human_seq=\d+ (?:cast|activate) (.+)$"
                             % turn6, l)
                if m:
                    seq.append(m.group(1))
                    continue
                # `tap ...#N as C ->` and `REJECT ...#N: <why>` -- a space or a colon after the id.
                m = re.match(r"\[pre-tap\] (tap|REJECT) (.+?)#\d+[ :]", l)
                if m:
                    seq.append("<tap %s>" % m.group(2))
                    ok_tap = ok_tap or m.group(1) == "tap"
            return seq, ok_tap

        # THE SAME token, moved. Two pins whose only difference is where the tap sits in the
        # declared list -- if position were ignored the two sequences would be identical, which is
        # exactly what a "flush every tap up front" implementation would produce.
        first, first_ok = realised("%d:*|%s|Clue Token|Emiel the Blessed" % (ORD6, tok))
        mid,   _        = realised("%d:*|Clue Token|%s|Emiel the Blessed" % (ORD6, tok))
        check("a tap declared FIRST runs first",
              first, ["<tap %s>" % s["name"], "Clue Token", "Emiel the Blessed"])
        check("the same tap declared BETWEEN runs between",
              mid, ["Clue Token", "<tap %s>" % s["name"], "Emiel the Blessed"])
        # ...and the first pin's tap actually happened, so the pair above is a statement about
        # ORDER rather than about a tap that could never work on this board at all.
        check("the source really was tappable at the head of the line", first_ok, True)

if fails:
    print("manual tap/pay: FAIL (%d)" % len(fails))
    for f in fails:
        print("  - %s" % f)
    sys.exit(1)
print("manual tap/pay: PASS")
