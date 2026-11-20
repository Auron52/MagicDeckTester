#!/usr/bin/env python3
"""TRICK BEFORE ITS HAND TARGET: no offered main-phase plan casts a targeted trick before the same-plan creature it
targets is cast.

Stuffy Doll claude-play sweep (2026-11-20, seeds 930006 / 930009 / 930014 / 930015): the full-order human menu
offered `Loran's Escape -> Mogg Maniac, Mogg Maniac` with the Maniac still in HAND. Its realised order
(`cast_order_canonical`) casts the Escape first, when it has no target (CR 601.2c); committing it silently spent
the Escape and its {W} on a fizzle (no target prompt, no scry). The legal twin (Maniac first) was offered too.
TurnSolver's ordering expansion filtered that shape only for rfx plans; it now filters every plan.

WHAT IT CHECKS. It walks each recorded sweep game through the real `--claude-play` protocol, and at every
main-phase decision along the CSV checks every offered plan: for each action with an `enchant_target` that is a
card in HAND, a cast of that card's name must come BEFORE the trick in `cast_order_canonical` (the realised order).

Usage: python3 test/claude_play_trick_order_check.py [--bin build/Release/mtg]
Exit 0 = no violation; 1 = a violation (each printed); 2 = harness error.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECK = ROOT / "decks/Stuffy Doll/Stuffy Doll.cod"
PROF = ROOT / "decks/Stuffy Doll/Stuffy Doll.profile.json"
# (seed, full sweep CSV) -- each reached at least one violating menu on the unfixed tree.
GAMES = [
    (930006, "0,1,0,1,-1,-1,9,-1,-1,3,-1,-1,-1,1,1,1,-1,0,0,0,0,0,0"),
    (930009, "0,1,0,2,0,-1,-1,0,0,-1,-1,0,1,-1,-1,41,0,1,-1,0,0,0,0,0,0,0,0,0"),
    (930014, "1,0,-1,-1,3,-1,-1,3,-1,-1,18,6,1,-1,0,0,0,0,0,0,0,0"),
    (930015, "0,1,5,0,1,-1,-1,0,0,-1,-1,0,1,-1,-1,0,-1,-1,0,-1"),
]


def decision(binary, seed, choices):
    cmd = [str(binary), str(DECK), "--profile", str(PROF), "--claude-play", "--seed", str(seed),
           "--max-turns", "8", "--reveal", "6", "--choices", ",".join(choices)]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode == 0:
        return None
    if p.returncode != 70 or "<<<CLAUDE_DECISION>>>" not in p.stdout:
        raise RuntimeError(f"seed {seed} choices {choices}: rc={p.returncode} {p.stderr[-400:]}")
    return json.loads(p.stdout.split("<<<CLAUDE_DECISION>>>", 1)[1].split("<<<END_DECISION>>>", 1)[0])


def violations(d):
    hand = {h["num"]: h["name"] for h in d.get("me", {}).get("hand", [])}
    out = []
    for p in d.get("plans", []):
        order = p.get("cast_order_canonical") or p.get("casts") or []
        for a in p.get("actions", []):
            t = a.get("enchant_target")
            if not t or t not in hand:
                continue
            name = hand[t]
            if a.get("card") not in order or name not in order:
                continue
            if order.index(name) > order.index(a["card"]):
                out.append(f"plan {p['index']}: {p.get('summary')} (order {order})")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.environ.get("MTG_BIN", str(ROOT / "build/Release/mtg")))
    args = ap.parse_args()
    bad, menus = 0, 0
    try:
        for seed, csv in GAMES:
            picks = csv.split(",")
            for k in range(len(picks) + 1):
                d = decision(args.bin, seed, picks[:k])
                if d is None:
                    break
                if d.get("type") != "main_phase":
                    continue
                menus += 1
                for v in violations(d):
                    bad += 1
                    print(f"FAIL seed {seed} decision {d.get('decision_index')} T{d.get('turn')}: {v}")
    except RuntimeError as e:
        print(f"HARNESS ERROR: {e}")
        return 2
    print(f"{menus} main-phase menus checked; {bad} trick-before-hand-target plan(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
