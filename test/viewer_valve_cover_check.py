#!/usr/bin/env python3
"""VIEWER VALVE COVER CHECK -- can the player still see every card they can actually cast?

    python3 test/viewer_valve_cover_check.py          # the gate (seconds, non-zero on fail)
    python3 test/viewer_valve_cover_check.py -v       # print the whole ledger

WHAT IT GUARDS, and why a scenario fixture could NOT guard it.

The viewer plan valve (CapGroupsBySituationalRank, MTG_VIEWER_PLAN_CAP) bounds the main-phase plan
odometer by DROPPING whole groups -- one hand slot or one equip piece each.  A dropped cast group
removes that card from the menu entirely, so a rules-legal line comes back `legal_not_enumerated`
and the player simply cannot make the play.  That cost the user a T3 win twice on KittyEquipment v2
(2026-10-02): first because the covering key was one-per-Action::Kind, and then AGAIN because the
key was only (kind, free-or-paid) -- so the budget held a SECOND COPY of Cathar's Shield while
Colossus Hammer, equally affordable, had no representative at all.

THE FIXTURE WAS GREEN THROUGH BOTH BUGS.  `test/scenarios/kittyv2_floating_mana_cast_survives_valve
.json` asserts `validate_line: cast=Colossus Hammer` and passed the whole time, because the
`--scenario` path evaluates CheckLine with `menu=nullptr`, which RE-ENUMERATES instead of reading
the truncated menu.  So it tests rules-legality, which was never in doubt, and says nothing about
what the viewer offers.  This check reads the valve's own KEPT/DROPPED ledger instead, which is the
decision that actually reaches the player.

THE INVARIANT.  On the frame below, every distinct hand-cast card the turn's mana could pay for
must keep at least one group.  Affordability is the bound that makes this safe to demand: it is
what the pool can buy, so the set is small by construction, and an unaffordable card losing its
group costs the player nothing this turn.  Duplicates are explicitly NOT demanded -- a second copy
of an already-offered card is reachable on the next click, which is the same argument the valve
already uses to pool fungible equip copies.

The board is reached through the scenario harness only to STAGE it; the assertion is on the ledger,
and the ledger's inputs here are byte-identical to the live viewer frame that was rejected
(groups=30, equip_groups=18, fold=0.09145, folded_pay=2.17678e+11, pcap=65536 -- compare
logs/play/rejections/KittyEquipment_cod_v2-puresteel-hammer_s1_gi0_t3.json).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = [os.path.join(ROOT, "test", "scenarios", f) for f in (
    # Reduced form: the mechanism, no lands in hand.
    "kittyv2_floating_mana_cast_survives_valve.json",
    # FAITHFUL form: the user's rejected hand verbatim, three Plains included. Kept as a SEPARATE
    # board because hand-slot order decides the cover slot among equal-ranked casts -- the two
    # boards disagreed about WHICH paid cast survived under the pre-fix coarse key (reduced kept
    # the Hammer and dropped Shadowspear; the live frame did the reverse). Covering every
    # affordable card is what makes that order stop mattering, so both must pass.
    "kittyv2_valve_covers_every_affordable_cast.json",
)]
BIN = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
if not os.path.exists(BIN):
    BIN = os.path.join(ROOT, "build", "Release", "mtg.exe")
VERBOSE = "-v" in sys.argv

# The card the user was denied. Named explicitly so a regression names itself in the failure text
# rather than showing up as an anonymous count.
MUST_OFFER = "Colossus Hammer"

ROW = re.compile(r"^\[valve\]\s+(KEEP|DROP)\s+(.+?)\s+kind=(\d+)\s+cast=(\d+)\s+"
                 r"width=(\d+)\s+min_mv=(-?\d+)\s+afford=(\d+)\s*$")


def check_one(fixture):
    env = dict(os.environ)
    env["MTG_VIEWER_VALVE_DIAG"] = "1"
    r = subprocess.run([BIN, "--scenario", fixture], cwd=ROOT, env=env,
                       capture_output=True, text=True)

    # Several valve calls fire per game (one per land option / per re-solve). Take the ledger with
    # the MOST rows -- the widest frame, which is the one that truncates and the one the user hit.
    ledgers, cur = [], None
    for line in r.stderr.splitlines():
        if "KEPT/DROPPED ledger" in line:
            cur = []
            ledgers.append(cur)
            continue
        if cur is not None:
            m = ROW.match(line)
            if m:
                cur.append(m.groups())
            elif line.startswith("[valve] groups="):
                cur = None
    if not ledgers:
        print("ERROR: no valve ledger emitted -- did the valve run? (needs MTG_VIEWER_PLAN_CAP on)",
              file=sys.stderr)
        print(r.stderr[-2000:], file=sys.stderr)
        return 2
    rows = max(ledgers, key=len)

    if VERBOSE:
        for st, name, kind, cast, width, mv, aff in rows:
            print("  %-4s %-26s kind=%-3s cast=%s width=%s min_mv=%s afford=%s"
                  % (st, name, kind, cast, width, mv, aff))

    # Group the hand-cast rows by card, and ask whether the card kept ANY group.
    offered, present = {}, set()
    for st, name, _kind, cast, _w, _mv, aff in rows:
        if cast != "1" or aff != "1":
            continue
        present.add(name)
        offered[name] = offered.get(name, False) or (st == "KEEP")

    missing = sorted(n for n in present if not offered[n])
    print("affordable hand-cast cards on this frame : %d" % len(present))
    print("  offered : %s" % ", ".join(sorted(n for n in present if offered[n])))
    print("  DROPPED : %s" % (", ".join(missing) if missing else "(none)"))

    fails = []
    if MUST_OFFER not in present:
        fails.append("%r is not even an affordable cast on this frame -- the fixture board drifted, "
                     "so this check is no longer measuring the reported bug" % MUST_OFFER)
    elif not offered.get(MUST_OFFER):
        fails.append("%r is affordable but the valve dropped its only group -- this is the exact "
                     "regression that cost the user a T3 win" % MUST_OFFER)
    for n in missing:
        if n != MUST_OFFER:
            fails.append("affordable cast %r has NO group in the menu" % n)

    if fails:
        print("  FAIL (%d):" % len(fails))
        for f in fails:
            print("    * %s" % f)
        return 1
    print("  PASS -- every affordable hand cast keeps a group")
    return 0


def main():
    if not os.path.exists(BIN):
        print("ERROR: no engine binary -- build Release first", file=sys.stderr)
        return 2
    worst = 0
    for f in FIXTURES:
        if not os.path.exists(f):
            print("ERROR: missing fixture %s" % f, file=sys.stderr)
            worst = max(worst, 2)
            continue
        print("== %s" % os.path.basename(f))
        worst = max(worst, check_one(f))
    print("\n%s" % ("ALL BOARDS PASS" if worst == 0 else "FAILED (rc=%d)" % worst))
    return worst


if __name__ == "__main__":
    sys.exit(main())
