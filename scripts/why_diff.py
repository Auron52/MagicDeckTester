#!/usr/bin/env python3
"""Side-by-side story of two replayed traces of the SAME game index under two decklists.

Reads the index.json that why_card.py writes and prints, per game: both opening hands, the turn the
two lines diverge, and every spell each arm cast up to its win -- so a screen delta can be read as a
sequence of plays rather than a number.

The two arms differ by a few cards, so the same seed does NOT deal the same hand: the swapped slots
change what sits at each library position. That is not a bug in the pairing -- it is what a paired
comparison of two decklists means -- but it does mean "arm A drew better here" is often the honest
summary of a single game, and only the aggregate over thousands says which list is better. The
per-card census at the end is the part that generalises.

Usage: why_diff.py <index.json> [--only A_BETTER|B_BETTER] [--games N] [--census]
"""
import argparse, collections, json, sys
from pathlib import Path

CAST = {"CAST_SPELL", "PLAY_LAND", "ACTIVATE_ABILITY"}


def load(p):
    return json.load(open(p)) if p and Path(p).exists() else None


def spells(trace, upto=None):
    """[(turn, cardName, type)] for everything the player actively did."""
    out = []
    for e in trace.get("turns", []):
        t = e.get("turn")
        if upto is not None and t > upto:
            break
        for a in e.get("actions", []):
            if a.get("type") in CAST:
                out.append((t, a.get("cardName"), a.get("type")))
    return out


def board_at(trace, turn):
    """Creature count on the battlefield at the end of `turn`."""
    n = None
    for e in trace.get("turns", []):
        if e.get("turn") == turn and e.get("boardAfter"):
            bf = e["boardAfter"].get("battlefield", [])
            n = sum(1 for p in bf if not p.get("isLand"))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("index")
    ap.add_argument("--only", default=None)
    ap.add_argument("--games", type=int, default=99)
    ap.add_argument("--census", action="store_true", help="only the aggregate per-card census")
    args = ap.parse_args()

    idx = json.load(open(args.index))
    a, b = idx["a"], idx["b"]
    rows = [r for r in idx["games"] if r["reproduced"]
            and (args.only is None or r["side"] == args.only)]

    cast_when = {a: collections.Counter(), b: collections.Counter()}
    won_with = {a: collections.Counter(), b: collections.Counter()}

    shown = 0
    for r in rows:
        ta = load(r["logs"][a]["path"])
        tb = load(r["logs"][b]["path"])
        if not ta or not tb:
            continue
        wa, wb = r["recorded"][a], r["recorded"][b]
        sa, sb = spells(ta, wa), spells(tb, wb)
        for tag, s, w in ((a, sa, wa), (b, sb, wb)):
            for _, name, ty in s:
                if ty == "CAST_SPELL":
                    cast_when[tag][name] += 1
            if w <= idx["max_turns"]:
                for _, name, ty in s:
                    if ty == "CAST_SPELL":
                        won_with[tag][name] += 1

        if args.census or shown >= args.games:
            continue
        shown += 1
        print("=" * 100)
        print(f"gi={r['gi']}   {r['side']}    {a} won T{wa}   |   {b} won T{wb}")
        ha = [c["cardName"] for c in ta.get("openingHand", [])]
        hb = [c["cardName"] for c in tb.get("openingHand", [])]
        print(f"  opening {a:22}: {', '.join(ha)}")
        print(f"  opening {b:22}: {', '.join(hb)}")
        only_a = sorted(set(ha) - set(hb)); only_b = sorted(set(hb) - set(ha))
        if only_a or only_b:
            print(f"  hand differs: {a} has [{', '.join(only_a)}]   {b} has [{', '.join(only_b)}]")
        mx = max(wa, wb)
        print(f"  {'turn':<5} {a[:34]:<36} {b[:34]}")
        for t in range(1, min(mx, idx["max_turns"]) + 1):
            la = "; ".join(f"{n}" for tt, n, ty in sa if tt == t and ty == "CAST_SPELL") or "-"
            lb = "; ".join(f"{n}" for tt, n, ty in sb if tt == t and ty == "CAST_SPELL") or "-"
            ca, cb = board_at(ta, t), board_at(tb, t)
            mark = "  <<<" if la != lb else ""
            print(f"  T{t:<4} {la[:30]:<30} {str(ca) if ca is not None else '.':>3}   "
                  f"{lb[:30]:<30} {str(cb) if cb is not None else '.':>3}{mark}")

    print("\n" + "=" * 100)
    print(f"CENSUS over {len(rows)} reproduced game pairs -- spells CAST (all replayed games)")
    names = sorted(set(cast_when[a]) | set(cast_when[b]))
    print(f"  {'card':<26} {a[:20]:>20} {b[:20]:>20}    diff")
    for n in names:
        x, y = cast_when[a][n], cast_when[b][n]
        if x or y:
            print(f"  {n:<26} {x:>20} {y:>20}   {x-y:+d}")


if __name__ == "__main__":
    main()
