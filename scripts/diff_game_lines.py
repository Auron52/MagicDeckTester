#!/usr/bin/env python3
"""Side-by-side per-turn action diff of two single-game logs (same game, two arms).

The arms differ by a FLAG, not a binary, so test/explain_game.py (which diffs two binaries)
does not apply. Usage:

    python3 scripts/diff_game_lines.py <logdirA> <logdirB> [labelA labelB]

Prints one line per (turn, phase) with the casts/land drops/attacks each arm took, marking
rows where the arms differ with '*'.
"""
import glob
import json
import sys


def load(d):
    return json.load(open(sorted(glob.glob(f"{d}/*game_*.json"))[0]))


def acts(t):
    out = []
    for a in t.get("actions", []):
        k = a.get("type")
        n = a.get("cardName", "")
        if k == "CAST_SPELL":
            tg = ",".join(x.get("cardName", "") for x in a.get("targets", []))
            out.append(f"cast {n}" + (f"->{tg}" if tg else "") + f" [{a.get('manaPaid','')}]")
        elif k == "PLAY_LAND":
            out.append(f"land {n}")
        elif k == "ATTACK":
            out.append(f"ATTACK {a.get('damage')} (opp {a.get('oppLife')})")
        elif k == "DRAW":
            out.append(f"draw {n}")
        else:
            out.append(f"{k} {n}".strip())
    return "; ".join(out)


def rows(g):
    return [((t.get("turn"), t.get("phase")), acts(t), t.get("boardAfter", {}).get("opponentLife"))
            for t in g["turns"]]


def main():
    A, B = load(sys.argv[1]), load(sys.argv[2])
    la, lb = (sys.argv[3], sys.argv[4]) if len(sys.argv) > 4 else ("A", "B")
    ra, rb = rows(A), rows(B)
    print(f"result: {la}={A['result']}  {lb}={B['result']}")
    print(f"opening hand identical: {A['openingHand'] == B['openingHand']}")
    keys = []
    for k, _, _ in ra + rb:
        if k not in keys:
            keys.append(k)
    da = {k: (a, l) for k, a, l in ra}
    db = {k: (a, l) for k, a, l in rb}
    print(f"\n{'turn/phase':16s} {'':1s} {la[:48]:48s} | {lb[:48]:48s}")
    for k in keys:
        a, la_ = da.get(k, ("--", ""))
        b, lb_ = db.get(k, ("--", ""))
        if not a and not b:
            continue
        mark = "*" if a != b else " "
        print(f"{str(k[0])+' '+str(k[1]):16s} {mark} {a[:48]:48s} | {b[:48]:48s}"
              + (f"   opp {la_}/{lb_}" if la_ != lb_ else ""))


if __name__ == "__main__":
    main()
