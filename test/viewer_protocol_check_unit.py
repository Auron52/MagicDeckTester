#!/usr/bin/env python3
"""Unit checks for test/viewer_protocol_check.py's side-channel reconstruction (no engine needed).

1. CAST-AND-TAPPED (Angels/claude_s5_gi4): a mana rock the line cast from hand and then tapped is a
   payment tap and must be pinned with the lands; a land that arrives tapped from the LIBRARY (a
   fetch -- an ETB state, not a payment) must not be.
2. ORDINAL REMAP (StompySurprise/v1-arborelf-worldspine4/claude_s11_gi10): ordinal-keyed pins
   (--tap-pref, --cast-order) follow the REPLAY's numbering once a recorded frame comes back without
   an ordinal, instead of addressing frames by the recording's stale numbers.
Run: python3 test/viewer_protocol_check_unit.py   (exit 1 on failure)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import viewer_protocol_check as v

fails = []


def chk(ok, msg):
    print(("  ok: " if ok else "  FAIL: ") + msg)
    if not ok:
        fails.append(msg)


def bf(*cards):
    return [{"name": n, "idx": i, "num": num, **({"tapped": True} if t else {})}
            for i, (n, num, t) in enumerate(cards)]


def frame(turn, ordinal, battlefield, hand=(), phase="pre_main", chosen=-1, cast_order=None):
    d = {"chosen": chosen, "decision": {"type": "main_phase", "turn": turn, "phase": phase,
                                        "main_ordinal": ordinal,
                                        "me": {"battlefield": battlefield,
                                               "hand": [{"num": n, "name": nm} for nm, n in hand]}}}
    if cast_order:
        d["cast_order"] = cast_order
    return d


# 1. cast-and-tapped
before = bf(("Plains", 22, False), ("Plains", 34, False), ("Seraph Sanctuary", 50, False))
after = bf(("Plains", 22, True), ("Plains", 34, True), ("Seraph Sanctuary", 50, True),
           ("Sol Ring", 54, True), ("Evolving Wilds land", 77, True))
decs = [frame(4, 5, before, hand=[("Sol Ring", 54), ("Youthful Valkyrie", 60)], chosen=3),
        frame(4, 6, after)]
tp = v.recorded_tap_prefs(decs)
chk(tp == {(4, "pre_main", 5): [22, 34, 50, 54]},
    f"the cast-and-tapped rock is pinned with the lands; the library arrival is not ({tp})")

# 2. ordinal remap: recorded ordinals 7 (now ordinal-less), 8 and 11 with pins
b0 = bf(("Forest", 25, False), ("Wirewood Lodge", 52, False))
b1 = bf(("Forest", 25, True), ("Wirewood Lodge", 52, False))
decs = [frame(5, 8, b0, chosen=1, cast_order=["A", "B"]), frame(5, 9, b1),
        frame(6, 11, b0, chosen=2), frame(6, 12, b1)]
plain = v.side_channel_args(decs)
chk("--tap-pref" in plain and "5:pre:8:25" in plain[plain.index("--tap-pref") + 1]
    and "--cast-order" in plain and plain[plain.index("--cast-order") + 1] == "8:A|B",
    f"without a map the keys are the recorded ordinals ({plain})")
om = v.OrdinalMap()
om.learn(6, 6)
om.learn(7, None)                 # a recorded frame the engine now shows WITHOUT an ordinal
chk(om(8) == 7 and om(11) == 10, f"later ordinals shift down by one ({om(8)}, {om(11)})")
moved = om.learn(8, 7)
chk(moved, "learning a non-identity mapping asks for a rebuild")
side = v.side_channel_args(decs, ordmap=om)
tps = side[side.index("--tap-pref") + 1]
chk("5:pre:7:25" in tps and "6:pre:10:25" in tps and ":8:" not in tps and ":11:" not in tps,
    f"--tap-pref keys follow the replay's numbering ({tps})")
chk(side[side.index("--cast-order") + 1] == "7:A|B", f"--cast-order keys too ({side})")
idm = v.OrdinalMap()
chk(not idm.learn(3, 3) and idm(9) == 9, "an identity alignment changes nothing")

# 3. find_plan: a per-copy target label (" #1") added after the recording must not drop the match
#    to the order-insensitive tier, and that tier must keep the recorded CAST ORDER
#    (Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28 T4).
def plan(i, casts, summary):
    return {"index": i, "summary": summary, "land": None, "casts": casts,
            "actions": [{"card": c} for c in casts]}
rec = plan(11, ["Ignoble Hierarch", "Expedite"],
           "land=none; cast: Ignoble Hierarch, Expedite → Zada, Hedron Grinder")
now = [plan(17, ["Expedite", "Ignoble Hierarch"],
            "land=none; cast: Expedite → Zada, Hedron Grinder #1, Ignoble Hierarch"),
       plan(18, ["Ignoble Hierarch", "Expedite"],
            "land=none; cast: Ignoble Hierarch, Expedite → Zada, Hedron Grinder #1")]
chk(v.find_plan(rec, now, recorded_index=11) == 1, "the copy-labelled summary matches in the recorded order")
now2 = [dict(now[0], summary="land=none; cast: X"), dict(now[1], summary="land=none; cast: Y")]
chk(v.find_plan(rec, now2, recorded_index=11) == 1, "the (land, casts) tier keeps the recorded cast order")
chk(v._strip_copy_labels("land=none; cast: blink #0") == "land=none; cast: blink #0",
    "a legacy 'blink #0' spawn reference is not a copy label")

print("FAIL (%d)" % len(fails) if fails else "PASS")
sys.exit(1 if fails else 0)
