#!/usr/bin/env python3
"""Does the Kemba park LOOP actually round-trip?

The park is half a loop (USER, 2026-08-19): free gear moves onto Kemba in main 2 so the upkeep
counts it and makes Cats, then moves BACK onto a double-strike host in main 1 before combat. A park
that never un-parks is a misplay that would barely move the average -- so the average cannot judge
this lever. This reads the actual played games instead.

Per game it walks the phase snapshots (boardAfter.battlefield, with attachedTo) and reports, per
turn: the equipment sitting on Kemba at the END of the turn (post-park) and whether each of those
had left Kemba by the pre-combat snapshot of the NEXT turn (the un-park).

Usage: park_roundtrip.py <dir-of-game-json> [--verbose]
"""
import json
import pathlib
import sys
from collections import Counter

KEMBA = "Kemba, Kha Regent"


def attachments(board):
    """m_number -> name for every permanent, plus equipment -> host-name."""
    by_num = {}
    for p in board:
        by_num[p.get("card")] = p.get("cardName", "")
    out = {}
    for p in board:
        host = p.get("attachedTo")
        if host:
            out[p.get("cardName", "")] = by_num.get(host, "?")
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    verbose = "--verbose" in sys.argv
    files = sorted(root.glob("*.json"))
    if not files:
        sys.exit(f"no game json under {root}")

    parks = 0            # (turn, equipment) pairs sitting on Kemba at end of turn
    unparked = 0         # ...that had left Kemba by the next turn's pre-combat snapshot
    still_parked = 0     # ...that had not (and the game continued)
    no_next_turn = 0     # ...where the game ended, so the loop had no chance to close
    park_turns = Counter()
    games_with_park = 0

    for f in files:
        g = json.loads(f.read_text())
        turns = g.get("turns", [])
        # last snapshot per turn (end of turn) and the pre-combat snapshot per turn (MAIN_1)
        end_of = {}
        main1_of = {}
        for e in turns:
            t = e.get("turn")
            bf = e.get("boardAfter", {}).get("battlefield", [])
            end_of[t] = bf
            if e.get("phase") == "MAIN_1":
                main1_of[t] = bf     # last MAIN_1 entry of the turn = state entering combat

        saw = False
        for t in sorted(end_of):
            on_kemba = [eq for eq, host in attachments(end_of[t]).items() if host == KEMBA]
            if not on_kemba:
                continue
            saw = True
            parks += len(on_kemba)
            park_turns[t] += len(on_kemba)
            nxt = main1_of.get(t + 1)
            if nxt is None:
                no_next_turn += len(on_kemba)
                continue
            after = attachments(nxt)
            for eq in on_kemba:
                if after.get(eq) != KEMBA:
                    unparked += 1
                else:
                    still_parked += 1
                    if verbose:
                        print(f"  STUCK {f.name} T{t}: {eq} still on Kemba at T{t+1} pre-combat")
        if saw:
            games_with_park += 1

    print(f"games                : {len(files)}")
    print(f"games with a park    : {games_with_park}")
    print(f"park events          : {parks}")
    if parks:
        print(f"  round-tripped      : {unparked} ({100.0*unparked/parks:.1f}%)")
        print(f"  STILL parked next T: {still_parked} ({100.0*still_parked/parks:.1f}%)  <-- the trap")
        print(f"  game ended first   : {no_next_turn} ({100.0*no_next_turn/parks:.1f}%)")
        print("park events by turn  : " + ", ".join(f"T{t}={n}" for t, n in sorted(park_turns.items())))


if __name__ == "__main__":
    main()
