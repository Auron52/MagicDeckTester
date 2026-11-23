#!/usr/bin/env python3
"""Check Blood Sun from the first main phase of each saved winning turn.

Each reference is replayed exactly through its preceding turns, then its first
pre-combat main phase on the winning turn is handed to autonomous search. This
checks the go-off decision before any of that round's extra turns have started.

    python3 test/bloodsun_reference_firstturn.py --out build/scratch/bloodsun/ref_firstturn/default.json
    python3 test/bloodsun_reference_firstturn.py --env MTG_BLOODSUN_REV_PRIORITY=1 \
        --out build/scratch/bloodsun/ref_firstturn/priority.json
"""
import argparse
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "test"))
import viewer_protocol_check as vpc  # noqa: E402
import combo_off_replay_hunt as hunt  # noqa: E402
import ref_handoff_sweep as rhs  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    ap.add_argument("--env", action="append", default=[], help="K=V engine environment override")
    args = ap.parse_args()
    rows = []
    refs = sorted(glob.glob(os.path.join(ROOT, "references", "Blood_Sun", "claude_s*_gi*.json")))
    if not refs:
        print("no Blood Sun references", file=sys.stderr)
        return 2
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    for ref in refs:
        c = {}
        replay_ok, replay_kind, detail = vpc.check_reference(ref, collect=c)
        hunt.DECK, hunt.PROF = c["deck"], c["prof"]
        recorded = json.load(open(ref))["win_turn"]
        frames, terminal = rhs.enumerate_frames(c)
        first = next((f for f in frames if f["turn"] == recorded and f["phase"] == "pre_main"), None)
        row = {"reference": os.path.relpath(ref, ROOT), "human_win_turn": recorded,
               "replay_ok": replay_ok, "replay": replay_kind, "replay_detail": detail}
        if first is not None:
            row["board"] = first["board"]
            row["prefix_len"] = len(first["prefix"])
            row["handoff"] = rhs.run_handoff(c, first, args.env, None, None)
            h = row["handoff"]
            row["match"] = (replay_ok and h["rc"] == 0 and h["engine_won"]
                            and h["engine_win_turn"] <= recorded)
        else:
            row["match"] = False
            row["error"] = "no first main-phase frame on recorded win turn"
        rows.append(row)
        with open(args.out, "w") as fh:
            json.dump({"env": args.env, "results": rows}, fh, indent=2)
            fh.write("\n")
        print("%-25s human T%s search T%s %s" % (
            os.path.basename(ref)[:-5], recorded,
            row.get("handoff", {}).get("engine_win_turn"),
            "MATCH" if row["match"] else "MISS"), flush=True)
    passed = sum(r["match"] for r in rows)
    print("%d/%d first-turn go-offs matched; %s" % (passed, len(rows), args.out))
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
