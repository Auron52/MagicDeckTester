#!/usr/bin/env python3
"""THE LINE'S COLOURS DECIDE THE GENERIC PIP: a committed multi-cast line must not spend the mana a
LATER cast in the same line needs.

USER, EldraziDisplacerFlicker, 2026-09-10: *"Still have the incorrect float issue on seed 6"*, on
the turn-4 line whose last committed segment is `cast: Eldrazi Displacer, Training Grounds`.

THE BOARD (seed 6, game-index 5, turn 4 -- the frame this drives):
  Kitchen + Overgrowth      -- taps for {G} or {U}, plus the aura's {G}{G}   (3 mana)
  Brushland + Fertile Ground-- TAPPED already (paid for Eladamri's Call)
  Adarkar Wastes            -- untapped, {W}/{U} (painful) or {C} (painless) (1 mana)
  float {W:1}; hand Eldrazi Displacer ({2}{W}) + Training Grounds ({U}); life 19

Kitchen alone can pay the whole segment: {U} for Training Grounds and the aura's {G}{G} for the
Displacer's {2}, with the floating {W} covering its pip. What the payment did instead was take
Kitchen's own unit as {G} -- `prod[0]`, i.e. decklist order -- so its three green paid the {2} with
one to SPARE, and Training Grounds' {U} then had to tap Adarkar Wastes for a coloured mode. Cost:
a point of pain, the board's LAST untapped land, and its last {C} source -- leaving a resolved
Eldrazi Displacer whose `{2}{C}` blink (a bare `{C}` under Training Grounds) could not be paid at
all, and a float of `{G:1}` that provably cannot pay a {C} pip (CR 107.4c). The user worked around
it by hand with the viewer's MANUAL TAP.

TWO INDEPENDENT DEFECTS PRODUCE IT AND EITHER ALONE IS SUFFICIENT -- that is why this check pins
both hatches, and why fixing one and calling it done would have looked green on the board:
  * MTG_PAY_LINE_TAP_COLOR   -- which colour a CHOICE SOURCE takes for a generic pip
                                (LineDemandAnyPipColor); prod[0] without it.
  * MTG_PAY_LINE_GENERIC_ORDER -- which colour a generic pip EATS out of the pool the payment has
                                just built (ConsumeFloatingAny); a fixed WUBRG order without it,
                                which spends the {U} first even when the tap chose it deliberately.

WHY A CHECK AND NOT A DOC NOTE -- nothing else in the suite can see this:
  * both levers are HumanPlayActive()-gated, so the regression digests, the scenario fixtures and
    every autonomous reference are byte-identical whatever they do (that is asserted, not assumed:
    `bash test/regression.sh --smoke`);
  * the reference sweep replays SAVED games, and no saved reference plays this line -- the user hit
    it live and hand-repaired it with `tap=` tokens, which is precisely the evidence a reference
    would then NOT contain;
  * and the outcome test is invisible too: the game is not lost, the line is not dropped and no
    error is printed. The only witnesses are a life total, a tapped bit, and which colour survived
    in the pool -- so those are what is asserted, exactly as manual_tap_check.py asserts taps
    rather than outcomes.

SKIPS (exit 0) when the binary/deck is missing or when the board this drives is no longer reachable
(a plan summary it steers by has gone) -- the same policy as human_line_order_check.py and
manual_tap_check.py. Content-driven throughout: every pick is chosen by matching a plan SUMMARY,
never by a recorded index, because every enumeration change renumbers the plan list.

Usage: MTG_BIN=./build/Release/mtg python3 test/pay_line_color_check.py
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
DECK = os.path.join("decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.cod")
PROF = os.path.join("decks", "EldraziDisplacerFlicker", "EldraziDisplacerFlicker.profile.json")
DEC_B, DEC_E = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"

SEED, GI = 6, 5

# The user's turn-1..4 line, as CONTENT. "keep" answers the mulligan; "-" passes; anything else is a
# case-insensitive substring of the plan summary to commit. `auto` takes the engine's own default
# (used only for the wish/tutor target frames, whose pick the plan summary has already declared).
LINE = [
    ("keep", None),
    ("plan", "land=Kitchen; cast: (nothing)"),                 # T1
    ("pass", None),
    ("pass", None),
    ("plan", "Fertile Ground → Brushland"),               # T2
    ("pass", None),
    ("pass", None),
    ("plan", "Overgrowth → Kitchen"),                     # T3
    ("pass", None),
    ("pass", None),
    # T4 -- the user's six committed segments
    ("plan", "Living Wish → Cloud of Faeries, Living Wish → Adarkar Wastes"),
    ("auto", None),                                            # wish target frame
    ("auto", None),                                            # wish target frame
    ("plan", "land=Adarkar Wastes; cast: Cloud of Faeries"),
    ("plan", "land=none; cast: Peregrine Drake"),
    ("plan", "land=none; cast: Peregrine Drake"),
    ("plan", "Eladamri's Call → Eldrazi Displacer"),
    ("auto", None),                                            # tutor target frame
    ("plan", "cast: Eldrazi Displacer, Training Grounds"),      # <-- the segment under test
]
UNDER_TEST = len(LINE) - 1          # index of the committed segment whose RESULT frame we assert

fails = []


def skip(why):
    print("SKIP: pay line colour (%s)" % why)
    sys.exit(0)


def extract(t, b, e):
    i, j = t.find(b), t.find(e)
    return None if i < 0 or j < 0 else t[i + len(b):j].strip()


def engine_default(d):
    ac = d.get("ai_choice")
    if isinstance(ac, int):
        return ac
    if isinstance(ac, dict) and isinstance(ac.get("index"), int):
        return ac["index"]
    hd = d.get("heuristic_default")
    if isinstance(hd, int):
        return hd
    return -1 if d.get("type") == "main_phase" else 0


def pick_for(step, d):
    kind, needle = step
    if kind == "keep":
        # The mulligan frame's own note: reply 1 to KEEP. ai_choice is the engine's hint, not a pin.
        return 1
    if kind == "pass":
        return -1
    if kind == "auto":
        return engine_default(d)
    plans = d.get("plans") or []
    want = needle.lower()
    hits = [i for i, p in enumerate(plans) if want in (p.get("summary") or "").lower()]
    if not hits:
        return None
    # Most exact = shortest summary containing the needle (a two-cast plan also contains the
    # one-cast plan's text, so "shortest" is what keeps `cast: Peregrine Drake` off a longer line).
    hits.sort(key=lambda i: len(plans[i].get("summary") or ""))
    return hits[0]


def board(d):
    me = d.get("me") or {}
    lands = {}
    for p in me.get("battlefield") or []:
        if p.get("is_land"):
            lands[p.get("name")] = bool(p.get("tapped"))
    plans = [(p.get("summary") or "") for p in (d.get("plans") or [])]
    return {
        "life": me.get("life"),
        "float": me.get("floating_mana") or {},
        "lands": lands,
        "blink_offered": any("Eldrazi Displacer: blink" in s for s in plans),
    }


def drive(env_extra):
    """Walk LINE through ONE --interactive child. Returns the list of per-step RESULT frames
    (frame i = the board the engine showed AFTER committing step i-1), or None if unreachable."""
    args = [MTG, DECK, "--claude-play", "--seed", str(SEED), "--game-index", str(GI),
            "--max-turns", "8", "--depth", "0", "--profile", PROF,
            "--choices", "", "--interactive"]
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0")
    env.update(env_extra)
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=1, cwd=ROOT, env=env)
    frames, buf = [], ""
    try:
        for n, step in enumerate(LINE + [("pass", None)]):
            while DEC_E not in buf:
                line = p.stdout.readline()
                if not line:
                    return None                     # game ended early -> board not reachable
                buf += line
            raw = extract(buf, DEC_B, DEC_E)
            buf = buf[buf.index(DEC_E) + len(DEC_E):]
            d = json.loads(raw)
            frames.append(board(d))
            if n >= len(LINE):
                break
            pick = pick_for(step, d)
            if pick is None:
                return None                         # a steering summary has gone -> unreachable
            p.stdin.write("%d\n" % pick)
            p.stdin.flush()
    finally:
        p.kill()
        p.wait()
    return frames


def want(cond, msg):
    if not cond:
        fails.append(msg)


def main():
    if not os.path.exists(MTG):
        skip("binary missing: %s" % MTG)
    if not os.path.exists(os.path.join(ROOT, DECK)):
        skip("deck missing")

    on = drive({})
    if on is None:
        skip("the seed-6 T4 board is no longer reachable through this line")
    # The frame the segment under test produced.
    res = on[UNDER_TEST + 1]
    pre = on[UNDER_TEST]

    # Sanity: this really is the board the check was written for. A shape change is a SKIP, not a
    # failure -- the assertions below are only meaningful on it.
    if sorted(pre["lands"]) != ["Adarkar Wastes", "Brushland", "Kitchen"] \
            or pre["lands"]["Adarkar Wastes"] or not pre["lands"]["Brushland"] \
            or pre["lands"]["Kitchen"] or pre["float"] != {"W": 1}:
        skip("pre-segment board moved (lands=%s float=%s)" % (pre["lands"], pre["float"]))

    # (a) Kitchen alone pays the segment: Adarkar Wastes stays UP, no pain, nothing stranded.
    want(res["lands"].get("Adarkar Wastes") is False,
         "Adarkar Wastes should stay UNTAPPED (Kitchen's {U}+{G}{G} pays the whole segment); got tapped")
    want(res["life"] == pre["life"],
         "no pain should be taken (Adarkar's painless {C} was never needed); life %s -> %s"
         % (pre["life"], res["life"]))
    want(res["float"] == {},
         "float should be EMPTY -- nothing was over-tapped; got %s" % (res["float"],))
    # (b) ...and the consequence the user actually cares about: the Displacer can blink.
    want(res["blink_offered"],
         "Eldrazi Displacer's blink must be offered ({C} from the still-untapped Adarkar Wastes)")

    # (c) Both hatches are REAL off switches, and EACH ALONE reproduces the reported bug --
    #     the two defects are independent, so a one-sided fix is not a fix.
    for flag in ("MTG_PAY_LINE_TAP_COLOR", "MTG_PAY_LINE_GENERIC_ORDER"):
        off = drive({flag: "0"})
        if off is None:
            fails.append("%s=0 arm did not reach the board" % flag)
            continue
        r = off[UNDER_TEST + 1]
        want(r["lands"].get("Adarkar Wastes") is True and r["float"] == {"G": 1}
             and r["life"] == pre["life"] - 1 and not r["blink_offered"],
             "%s=0 must restore the reported behaviour (Adarkar tapped, float {G:1}, -1 life, no "
             "blink); got lands=%s float=%s life=%s blink=%s"
             % (flag, r["lands"], r["float"], r["life"], r["blink_offered"]))
        # ...and it must be scoped to THIS segment: every earlier frame is untouched.
        want(off[:UNDER_TEST + 1] == on[:UNDER_TEST + 1],
             "%s=0 changed a frame BEFORE the segment under test -- the lever is out of scope" % flag)

    if fails:
        print("FAIL: pay line colour")
        for f in fails:
            print("  - %s" % f)
        sys.exit(1)
    print("PASS: pay line colour (seed 6 gi 5 T4: Adarkar stays up, float {}, blink offered; "
          "both hatches restore the bug)")


if __name__ == "__main__":
    main()
