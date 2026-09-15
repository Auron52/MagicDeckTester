#!/usr/bin/env python3
"""REFERENCE HAND-OFF -- put the shipped search on the human's exact mid-game board.

The reference bench (scripts/ref_bench.py) says WHETHER the search matches a hand-played reference;
it cannot say WHERE the line is lost. This tool replays a reference's recorded picks up to a chosen
main-phase frame (turn T, first pre-combat frame by default) at TRUE fidelity -- the real shuffle,
the real floating pool, the real exile zone -- and then hands the game to the autonomous search
(`--choices-then-auto`, AIEngine::kHandBackToSearch): human-play widening off, every human chooser
uninstalled, the deck's shipped play settings (value_play, else the built-in d5/20 ms) unless
--depth/--budget-ms override them.

Two questions it answers, one hand-off each:
  * hand the search the board AFTER the human's development turn(s): can it EXECUTE the human's
    go-off from there?  (no  => construct/executor gap;  yes => the gap is upstream)
  * hand it the board at turn 1: does it reproduce the bench's own win turn?  (the tool's own
    fidelity check -- it must)

Usage:
    python3 scripts/ref_handoff.py references/<Deck>/claude_s6_gi5.json --turn 4
    python3 scripts/ref_handoff.py references/<Deck>/claude_s6_gi5.json --turn 4 --budget-ms 100
    python3 scripts/ref_handoff.py references/<Deck>/claude_s8_gi7.json --turn 3 --frame 1  # 2nd frame of T3
    ... --env MTG_EDF_CO_WHY=1 --env MTG_FS_ROOT_DUMP=4 --stderr logs/edf_construct/s6_t4.err

Prints the frame it handed off on (the human's board there), the [play] line the search resolved,
the engine's win turn from that board, and the pasteable command.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))
import viewer_protocol_check as vpc          # noqa: E402
import combo_off_replay_hunt as hunt         # noqa: E402  -- Session (persistent child walk)

MTG = os.path.join(ROOT, "build", "Release", "mtg")


def walk_to_frame(ref_path, c, turn, frame_no, phase):
    """Drive the resolved pick stream in ONE persistent child until the requested main-phase frame
    appears; return (prefix_picks, frame_decision) or (None, reason)."""
    res = c["resolved"]
    s = hunt.Session(c["seed"], c["gi"], c["force"], c["side"], c["mt"])
    # Session hard-codes the EDF deck/profile in its argv; patch for any other deck.
    seen_in_turn = 0
    try:
        for i in range(len(res) + 8):
            kind, obj = s.read()
            if kind == "result":
                return None, "the replayed line ended (win_turn %s) before turn %d frame %d" % (
                    obj.get("win_turn"), turn, frame_no)
            if kind == "eof":
                return None, "replay ended without a terminal: %s" % str(obj)[-200:]
            if obj.get("type") == "main_phase" and obj.get("turn") == turn \
                    and (phase is None or obj.get("phase") == phase):
                if seen_in_turn == frame_no:
                    return list(s.picks), obj
                seen_in_turn += 1
            s.send(res[i] if i < len(res)
                   else (-1 if obj.get("type") == "main_phase" else vpc.engine_default(obj)[0]))
        return None, "walked %d picks without reaching turn %d" % (len(res), turn)
    finally:
        s.close()


def board_line(d):
    me = d.get("me") or {}
    bf = ", ".join(("%s%s%s" % (p.get("name"), "(T)" if p.get("tapped") else "",
                                ("->%s" % p["attached_to"]) if p.get("attached_to") else ""))
                   for p in me.get("battlefield", []))
    hand = ", ".join(x.get("name") if isinstance(x, dict) else str(x) for x in me.get("hand", []))
    pool = me.get("floating_mana") or ""
    return "bf=[%s] hand=[%s] pool=%s life=%s" % (bf, hand, pool, me.get("life"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("--turn", type=int, required=True, help="hand off at this turn's main phase")
    ap.add_argument("--frame", type=int, default=0,
                    help="which main-phase frame of that turn (0 = first; a go-off turn has many)")
    ap.add_argument("--phase", default="pre_main", help="pre_main (default) | post_main | any")
    ap.add_argument("--depth", type=int, default=None)
    ap.add_argument("--budget-ms", type=int, default=None)
    ap.add_argument("--max-turns", type=int, default=None, help="default: the reference's own")
    ap.add_argument("--env", action="append", default=[], help="K=V for the engine (repeatable)")
    ap.add_argument("--stderr", default=None, help="write the engine's stderr here (default: discard)")
    ap.add_argument("--log-dir", default=None, help="claude-play trace dir (final_events narrates the search's turns)")
    ap.add_argument("--json", action="store_true", help="machine-readable summary on stdout")
    args = ap.parse_args()

    c = {}
    ok, kind, detail = vpc.check_reference(args.reference, collect=c)
    if kind in ("play", "unresolvable", "mulligan", "shuffle-dead"):
        print("WARNING: reference replay is %s (%s) -- the prefix is the REPLAYED line" % (kind, detail),
              file=sys.stderr)
    # Session is EDF-bound in its argv; override deck/profile for any other reference.
    hunt.DECK, hunt.PROF = c["deck"], c["prof"]
    phase = None if args.phase == "any" else args.phase
    prefix, frame = walk_to_frame(args.reference, c, args.turn, args.frame, phase)
    if prefix is None:
        print("ERROR: %s" % frame, file=sys.stderr)
        return 2
    mt = args.max_turns if args.max_turns is not None else c["mt"]
    argv = [MTG, c["deck"], "--claude-play", "--seed", str(c["seed"]), "--game-index", str(c["gi"]),
            "--max-turns", str(mt), "--profile", c["prof"],
            "--choices", ",".join(str(p) for p in prefix), "--choices-then-auto"]
    if c["force"] is not None:
        argv += ["--force-mulligan", c["force"]]
    if c["side"]:
        argv += list(c["side"])
    if args.depth is not None:
        argv += ["--depth", str(args.depth), "--ignore-play-profile"]
    if args.budget_ms is not None:
        argv += ["--budget-ms", str(args.budget_ms)]
    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)
        argv += ["--log-dir", args.log_dir]
    env = dict(os.environ)
    env.update(hunt.REPLAY_ENV)
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k] = v
    # No address-space cap here (vpc.capped): the search's RAM-derived memo bounds size to the
    # machine, and a 4 GB ulimit would turn a legitimate solve into a bad_alloc.
    p = subprocess.run(argv, capture_output=True, text=True, cwd=ROOT, env=env)
    if args.stderr:
        os.makedirs(os.path.dirname(args.stderr) or ".", exist_ok=True)
        with open(args.stderr, "w") as fh:
            fh.write(p.stderr)
    res = hunt.result_of(p.stdout)
    play = [ln for ln in p.stderr.splitlines() if ln.startswith("[play]") or ln.startswith("[then-auto]")]
    out = {"reference": os.path.relpath(args.reference, ROOT), "turn": args.turn, "frame": args.frame,
           "recorded_win_turn": json.load(open(args.reference)).get("win_turn"),
           "prefix_len": len(prefix), "rc": p.returncode,
           "engine_win_turn": (res or {}).get("win_turn") if isinstance(res, dict) else None,
           "engine_won": (res or {}).get("won") if isinstance(res, dict) else None,
           "play": play, "board": board_line(frame),
           "cmd": " ".join(args.env) + (" " if args.env else "")
                  + " ".join(os.path.relpath(a, ROOT) if a.startswith(ROOT) else a for a in argv)}
    if args.json:
        print(json.dumps(out, indent=1))
    else:
        print("reference      : %s (recorded win turn %s)" % (out["reference"], out["recorded_win_turn"]))
        print("hand-off frame : T%d %s #%d after %d picks" % (args.turn, args.phase, args.frame, len(prefix)))
        print("board there    : %s" % out["board"])
        for ln in play:
            print("engine         : %s" % ln)
        print("search from here: win turn %s (won=%s, rc=%d)" % (out["engine_win_turn"], out["engine_won"], p.returncode))
        if not isinstance(res, dict):
            print("NO RESULT -- stderr tail:\n" + "\n".join(p.stderr.splitlines()[-15:]))
        print("cmd            : %s" % out["cmd"])
    return 0 if isinstance(res, dict) else 1


if __name__ == "__main__":
    sys.exit(main())
