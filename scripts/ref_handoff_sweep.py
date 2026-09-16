#!/usr/bin/env python3
"""REFERENCE HAND-OFF SWEEP -- hand the shipped search EVERY main-phase frame of every reference.

scripts/ref_bench.py answers "does the search match the reference from turn 1"; scripts/ref_handoff.py
answers "from THIS frame". This sweeps the second question over the whole game: each reference is
validated once (viewer_protocol_check.check_reference), its resolved pick stream is walked ONCE in a
persistent child to enumerate every main-phase decision frame (turn, phase, ordinal-in-turn, the
picks before it), and each frame with turn <= the recorded win turn is handed to the autonomous
search exactly as ref_handoff.py does (`--choices <prefix> --choices-then-auto`, the deck's shipped
play settings). A frame is SHORT when the search's win turn from there is later than the human's
recorded win turn (or it does not win inside the horizon). The last frame of the winning turn is the
executor test: can the engine finish the human's own go-off from one decision before the end?

    python3 scripts/ref_handoff_sweep.py --deck EldraziDisplacerFlicker --log-root logs/ref_sweep/edf
    python3 scripts/ref_handoff_sweep.py references/EldraziDisplacerFlicker/claude_s6_gi5.json \\
        --env MTG_HOLD_C_FOR_LINE=1 --jobs 6 --log-root logs/ref_sweep/edf_holdc

Each hand-off is ONE single-threaded engine game; --jobs runs that many at once (the search's budget
is in deterministic work units, so contention does not move a result). Set MTG_MEM_BUDGET_MB for the
per-game memo cap before launching (the sweep does not touch it).
"""
import argparse
import concurrent.futures
import glob
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))
import viewer_protocol_check as vpc          # noqa: E402
import combo_off_replay_hunt as hunt         # noqa: E402  -- Session (persistent child walk)

MTG = os.path.join(ROOT, "build", "Release", "mtg")


def board_line(d):
    me = d.get("me") or {}
    bf = ", ".join(("%s%s%s" % (p.get("name"), "(T)" if p.get("tapped") else "",
                                ("->%s" % p["attached_to"]) if p.get("attached_to") else ""))
                   for p in me.get("battlefield", []))
    hand = ", ".join(x.get("name") if isinstance(x, dict) else str(x) for x in me.get("hand", []))
    pool = me.get("floating_mana") or ""
    return "bf=[%s] hand=[%s] pool=%s life=%s" % (bf, hand, pool, me.get("life"))


def enumerate_frames(c):
    """Walk the resolved pick stream once; every main_phase decision becomes a frame record holding
    the picks that precede it (the hand-off prefix). Returns (frames, terminal-or-error)."""
    res = c["resolved"]
    s = hunt.Session(c["seed"], c["gi"], c["force"], c["side"], c["mt"])
    frames, per_turn = [], {}
    try:
        for i in range(len(res) + 8):
            kind, obj = s.read()
            if kind == "result":
                return frames, obj
            if kind == "eof":
                return frames, {"error": "replay ended without a terminal: %s" % str(obj)[-200:]}
            if obj.get("type") == "main_phase":
                key = (obj.get("turn"), obj.get("phase"))
                n = per_turn.get(key, 0)
                per_turn[key] = n + 1
                frames.append({"turn": obj.get("turn"), "phase": obj.get("phase"), "frame": n,
                               "prefix": list(s.picks), "board": board_line(obj)})
            s.send(res[i] if i < len(res)
                   else (-1 if obj.get("type") == "main_phase" else vpc.engine_default(obj)[0]))
        return frames, {"error": "walked %d picks without reaching a terminal" % len(res)}
    finally:
        s.close()


def run_handoff(c, fr, env_kv, max_turns, stderr_path):
    mt = max_turns if max_turns is not None else c["mt"]
    argv = [MTG, c["deck"], "--claude-play", "--seed", str(c["seed"]), "--game-index", str(c["gi"]),
            "--max-turns", str(mt), "--profile", c["prof"],
            "--choices", ",".join(str(p) for p in fr["prefix"]), "--choices-then-auto"]
    if c["force"] is not None:
        argv += ["--force-mulligan", c["force"]]
    if c["side"]:
        argv += list(c["side"])
    env = dict(os.environ)
    env.update(hunt.REPLAY_ENV)
    for kv in env_kv:
        k, _, v = kv.partition("=")
        env[k] = v
    t0 = time.time()
    p = subprocess.run(argv, capture_output=True, text=True, cwd=ROOT, env=env)
    if stderr_path:
        with open(stderr_path, "w") as fh:
            fh.write(p.stderr)
    res = hunt.result_of(p.stdout)
    out = {"rc": p.returncode, "secs": round(time.time() - t0, 1),
           "engine_win_turn": (res or {}).get("win_turn") if isinstance(res, dict) else None,
           "engine_won": (res or {}).get("won") if isinstance(res, dict) else None,
           "cmd": " ".join(env_kv) + (" " if env_kv else "")
                  + " ".join(os.path.relpath(a, ROOT) if a.startswith(ROOT) else a for a in argv)}
    if not isinstance(res, dict):
        out["stderr_tail"] = "\n".join(p.stderr.splitlines()[-8:])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("references", nargs="*", help="reference JSON paths (or use --deck)")
    ap.add_argument("--deck", help="sweep every references/<Deck>/claude_s*_gi*.json")
    ap.add_argument("--phase", default="any", help="pre_main | post_main | any (default)")
    ap.add_argument("--max-turns", type=int, default=None, help="default: each reference's own")
    ap.add_argument("--env", action="append", default=[], help="K=V for the engine (repeatable)")
    ap.add_argument("--jobs", type=int, default=4, help="concurrent hand-off games")
    ap.add_argument("--all-turns", action="store_true",
                    help="also hand off frames AFTER the recorded win turn (default: turn <= win turn)")
    ap.add_argument("--keep-stderr", action="store_true", help="keep each hand-off's engine stderr")
    ap.add_argument("--log-root", required=True)
    args = ap.parse_args()

    refs = list(args.references)
    if args.deck:
        refs += sorted(glob.glob(os.path.join(ROOT, "references", args.deck, "claude_s*_gi*.json")))
    if not refs:
        print("no references given", file=sys.stderr)
        return 2
    os.makedirs(args.log_root, exist_ok=True)
    phase = None if args.phase == "any" else args.phase

    # 1) validate + enumerate frames, one reference at a time (each is one persistent child).
    plan = []      # (ref_rel, c, frame)
    per_ref = {}
    for ref in refs:
        rel = os.path.relpath(ref, ROOT)
        c = {}
        ok, kind, detail = vpc.check_reference(ref, collect=c)
        hunt.DECK, hunt.PROF = c["deck"], c["prof"]
        recorded = json.load(open(ref)).get("win_turn")
        frames, term = enumerate_frames(c)
        if phase is not None:
            frames = [f for f in frames if f["phase"] == phase]
        if not args.all_turns and recorded is not None:
            frames = [f for f in frames if f["turn"] is not None and f["turn"] <= recorded]
        per_ref[rel] = {"reference": rel, "replay": kind, "replay_detail": detail,
                        "recorded_win_turn": recorded, "replay_terminal": term,
                        "n_frames": len(frames), "frames": frames}
        for f in frames:
            plan.append((rel, c, f))
        print("[sweep] %-40s replay=%-9s recorded T%s  frames=%d" % (rel, kind, recorded, len(frames)),
              flush=True)

    # 2) hand-offs, --jobs at a time.
    print("[sweep] %d hand-offs, %d at a time" % (len(plan), args.jobs), flush=True)
    t0 = time.time()

    def work(item):
        rel, c, f = item
        tag = "%s_T%d_%s_%d" % (os.path.basename(rel)[:-5], f["turn"], f["phase"], f["frame"])
        err = os.path.join(args.log_root, tag + ".err") if args.keep_stderr else None
        r = run_handoff(c, f, args.env, args.max_turns, err)
        f.update(r)
        return rel, f

    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        for rel, f in ex.map(work, plan):
            done += 1
            rec = per_ref[rel]["recorded_win_turn"]
            w = f.get("engine_win_turn")
            verdict = ("SHORT" if (w is None or not f.get("engine_won") or (rec is not None and w > rec))
                       else ("early" if (rec is not None and w < rec) else "ok"))
            f["verdict"] = verdict
            print("[%3d/%d] %-28s T%d %-9s #%-2d picks=%-3d -> win %s (%s) %5.1fs %s" % (
                done, len(plan), os.path.basename(rel)[:-5], f["turn"], f["phase"], f["frame"],
                len(f["prefix"]), w, "won" if f.get("engine_won") else "no", f["secs"],
                verdict.upper() if verdict != "ok" else ""), flush=True)

    # 3) summary.
    print("\n=== hand-off sweep: %d references, %d frames, wall %.0fs, env %s" % (
        len(refs), len(plan), time.time() - t0, " ".join(args.env) or "(default)"))
    print("%-28s human | frames  ok  early  SHORT | short frames" % "reference")
    total_short = 0
    for rel in sorted(per_ref):
        r = per_ref[rel]
        fs = r["frames"]
        short = [f for f in fs if f.get("verdict") == "SHORT"]
        early = [f for f in fs if f.get("verdict") == "early"]
        ok = len(fs) - len(short) - len(early)
        total_short += len(short)
        desc = ", ".join("T%d%s#%d->%s" % (f["turn"], "" if f["phase"] == "pre_main" else "/" + f["phase"],
                                           f["frame"], f.get("engine_win_turn")) for f in short)
        print("%-28s %5s | %6d %3d %6d %6d | %s" % (
            os.path.basename(rel)[:-5], r["recorded_win_turn"], len(fs), ok, len(early), len(short), desc))
    print("TOTAL SHORT FRAMES: %d of %d" % (total_short, len(plan)))
    with open(os.path.join(args.log_root, "sweep.json"), "w") as fh:
        json.dump({"env": args.env, "references": per_ref}, fh, indent=1)
    print("json: %s" % os.path.join(args.log_root, "sweep.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
