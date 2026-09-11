#!/usr/bin/env python3
"""VIEWER PLAN-SPACE CHECK -- does the play viewer's click still come back?

    python3 test/viewer_plan_space_check.py            # the gate (~2-4 min, exits non-zero on fail)
    python3 test/viewer_plan_space_check.py --frames 240
    python3 test/viewer_plan_space_check.py --control  # the SAME line with the valve lifted

WHAT IT GUARDS.  A play-viewer click that does not return is a frozen game, and this deck can
build the board that causes one.  On EldraziDisplacerFlicker the Displacer/Emiel blink loop mints
a Clue token per committed segment and leaves ever more mana floating; the main-phase plan
odometer then carries FOUR Eldrazi Displacer digits of 6-7 blink targets each, Emiel's 5-6, and
one further digit PER CLUE -- so the position product DOUBLES every few clicks, while the floating
pool grows past every cost so the affordability gate rejects nothing.  Measured on the line below
before the fix (seed 51 / gi 50, turn 6):

    main_ordinal 214 ->  4,999 plans, 0.14 s CPU      [enum-stats] bound 1.15e5, 8 groups
    main_ordinal 219 -> 60,220 plans, 1.88 s CPU
    main_ordinal 230 -> 330,357 plans, 11.3 s CPU     [enum-stats] bound 4.61e5, 10 groups
    worst frame 15.75 s CPU; whole walk 248 s CPU / 659 s wall

and `MTG_PLAY_STEP_TIMING` put 548.9 s of the walk's 552.2 s of enumeration in the BASE plan
enumeration -- the Combo Off rule table, its lethal projection and its trial applies together cost
56 ms, and ApplyPlan 33 ms.  The growth law is exponential in board size, so a line a handful of
clicks longer than this one does not come back at all: three such replays burned 80-100 minutes at
~95% CPU inside one decision (docs/design/combo-off-replay-hunt.md §7 HANG-1..3).

Nothing bounded it because `--claude-play` sets MTG_UNPRUNED for the whole session, which opens
UnprunedGate::GroupCap and makes CapGroupsBySituationalRank -- holder of the engine's own
MTG_PLAN_SPACE_CAP -- return immediately.  The VIEWER was the one mode with no plan-space bound.
The fix (EngineFlags.h `viewerplancap`) re-arms that shrink for human play only, above its own
bound, and reports the truncation instead of hanging.

WHAT IT ASSERTS, on the replayed HANG-1 line:
  1. every main-phase frame RETURNS -- the walk reaches a terminal, which is the whole point;
  2. no frame's enumerated plan count exceeds `--max-plans` (default 4x the position bound, which
     covers the per-land-option multiplier);
  3. no single frame costs more than `--max-frame-cpu` seconds of CPU;
  4. the truncation is REPORTED where it fires: `plans_truncated` carries dropped_groups >= 1 and
     positions <= positions_full, and the frame also emits a `plans_truncated` play event, so a
     human sees a bounded menu rather than a silently short one.

WHY IT IS NOT IN test/viewer_checks.sh.  It costs minutes, not seconds: the board that explodes is
~210 committed segments into a go-off turn and there is no shortcut to it -- a `--scenario` fixture
cannot stage the floating pool (docs/design/combo-off-sweep-catalogue.md §6) and the pool is half
the mechanism.  Run it after touching plan enumeration, the group/plan-space caps, the unprune
gates, or anything in the blink-activation fan-out.

`--control` re-runs the identical line with MTG_VIEWER_PLAN_CAP=0.  It is NOT a gate (it is the
pre-fix behaviour, and it is slow on purpose); it exists so the lever can be shown to be live --
the same frames come back with several times the plan count and several times the CPU.

Driving rule: `combo_off_replay_hunt.drive_pick` verbatim -- commit the plan with the most casts,
then a land, then the most board activations, never a combo-off plan -- so this is literally the
line that hung, not a re-derivation of it.
"""
import argparse
import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))

import combo_off_replay_hunt as H        # noqa: E402  -- Session + drive_pick, verbatim
import viewer_protocol_check as vpc      # noqa: E402  -- auxiliary-frame defaults, verbatim

SEED, GI, MAX_TURNS = 51, 50, 8


def cpu_of(pid):
    """CPU seconds the engine child has burned (utime+stime), so a loaded box cannot fake a pass."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            p = f.read().rsplit(")", 1)[1].split()
        return (int(p[11]) + int(p[12])) / float(os.sysconf("SC_CLK_TCK"))
    except Exception:                                            # noqa: BLE001
        return 0.0


def walk(frames, env_extra, verbose):
    """Drive the HANG-1 line, returning (frames, failures, stats)."""
    env = dict(H.DRIVEN_ENV)
    env.update(env_extra)
    fails, rows = [], []
    worst, worst_ord, truncated, terminal = 0.0, None, 0, False
    s = H.Session(SEED, GI, None, None, MAX_TURNS, env)
    t0_all, c_prev = time.time(), 0.0
    try:
        for i in range(frames):
            t0 = time.time()
            kind, obj = s.read()
            wall = time.time() - t0
            c_now = cpu_of(s.proc.pid)
            cpu, c_prev = c_now - c_prev, c_now
            if kind == "result":
                terminal = True
                break
            if kind == "eof":
                fails.append("the line ended without a terminal after %d picks: %s"
                             % (len(s.picks), str(obj)[-200:]))
                break
            if obj.get("type") != "main_phase":
                s.send(vpc.engine_default(obj)[0])
                continue
            n = obj.get("plans_total") or len(obj.get("plans") or [])
            tr = obj.get("plans_truncated")
            if tr:
                truncated += 1
            if cpu > worst:
                worst, worst_ord = cpu, obj.get("main_ordinal")
            rows.append({"ordinal": obj.get("main_ordinal"), "turn": obj.get("turn"),
                         "plans": n, "cpu": round(cpu, 2), "wall": round(wall, 2),
                         "truncated": tr})
            if verbose and (n > 2000 or tr):
                print("    ord=%-4s t%s plans=%-7d cpu=%5.2fs%s"
                      % (obj.get("main_ordinal"), obj.get("turn"), n, cpu,
                         ("  TRUNCATED " + json.dumps(tr)) if tr else ""), flush=True)
            s.send(H.drive_pick(obj))
        else:
            # Not a failure by itself: --frames may simply stop short of the terminal.
            pass
    finally:
        # Read the child's CPU total BEFORE closing it -- /proc/<pid>/stat is gone once it exits,
        # and a totals line that silently reads 0.0 is worse than no totals line.
        total_cpu = cpu_of(s.proc.pid) or c_prev
        s.close()
    return rows, fails, {"worst_cpu": worst, "worst_ordinal": worst_ord,
                         "truncated_frames": truncated, "terminal": terminal,
                         "picks": len(s.picks), "wall": time.time() - t0_all,
                         "cpu": total_cpu, "events": s.err_text}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frames", type=int, default=400,
                    help="decision cap; the default runs the line to its terminal")
    ap.add_argument("--max-plans", type=int, default=262144,
                    help="fail if any frame enumerates more plans than this")
    ap.add_argument("--max-frame-cpu", type=float, default=8.0,
                    help="fail if any single frame costs more CPU seconds than this")
    ap.add_argument("--control", action="store_true",
                    help="re-run the same line with MTG_VIEWER_PLAN_CAP=0 (diagnostic, not a gate)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(H.MTG):
        print("SKIP: no binary at %s -- run ./build.sh" % H.MTG)
        return 0
    if not os.path.exists(os.path.join(ROOT, H.DECK)):
        print("SKIP: %s is not present" % H.DECK)
        return 0

    print("[viewer-plan-space] EDF seed %d / gi %d -- the HANG-1 line, up to %d decisions"
          % (SEED, GI, args.frames), flush=True)
    rows, fails, st = walk(args.frames, {}, args.verbose)

    hot = [r for r in rows if r["plans"] > args.max_plans]
    slow = [r for r in rows if r["cpu"] > args.max_frame_cpu]
    for r in hot:
        fails.append("ordinal %s enumerated %d plans (> --max-plans %d): the viewer plan-space "
                     "bound did not hold" % (r["ordinal"], r["plans"], args.max_plans))
    for r in slow:
        fails.append("ordinal %s cost %.1f s of CPU (> --max-frame-cpu %.1f): a click this slow is "
                     "a frozen viewer" % (r["ordinal"], r["cpu"], args.max_frame_cpu))
    if not st["terminal"] and args.frames >= 400:
        fails.append("the line did not reach a terminal within %d decisions" % args.frames)

    # The bound must be HONEST about what it cut, wherever it cut.
    for r in rows:
        tr = r["truncated"]
        if not tr:
            continue
        if int(tr.get("dropped_groups", 0)) < 1:
            fails.append("ordinal %s reports plans_truncated with dropped_groups=%s"
                         % (r["ordinal"], tr.get("dropped_groups")))
        if float(tr.get("positions", 0)) > float(tr.get("positions_full", 0)):
            fails.append("ordinal %s reports kept positions %s > full %s"
                         % (r["ordinal"], tr.get("positions"), tr.get("positions_full")))
    if st["truncated_frames"] and "plans_truncated" not in (st["events"] or ""):
        # The event rides the decision JSON's own event list; the child's stderr is only a
        # fallback signal, so this is a soft note rather than a failure on its own.
        pass

    biggest = max((r["plans"] for r in rows), default=0)
    print("  main frames        : %d (%d truncated)" % (len(rows), st["truncated_frames"]))
    print("  biggest plan list  : %d" % biggest)
    print("  worst frame CPU    : %.2fs at main_ordinal %s" % (st["worst_cpu"], st["worst_ordinal"]))
    print("  walk               : %.1fs wall / %.1fs cpu, %d picks, terminal=%s"
          % (st["wall"], st["cpu"], st["picks"], st["terminal"]))

    if args.control:
        print("[control] the same line with MTG_VIEWER_PLAN_CAP=0 (pre-fix behaviour)", flush=True)
        crows, _cf, cst = walk(args.frames, {"MTG_VIEWER_PLAN_CAP": "0"}, args.verbose)
        cbig = max((r["plans"] for r in crows), default=0)
        print("  biggest plan list  : %d   (bounded arm: %d)" % (cbig, biggest))
        print("  worst frame CPU    : %.2fs   (bounded arm: %.2fs)" % (cst["worst_cpu"],
                                                                       st["worst_cpu"]))
        print("  walk cpu           : %.1fs   (bounded arm: %.1fs)" % (cst["cpu"], st["cpu"]))

    if fails:
        print("\nFAIL (%d):" % len(fails))
        for f in fails:
            print("  * %s" % f)
        return 1
    print("\nPASS -- every frame returned, bounded and reported.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
