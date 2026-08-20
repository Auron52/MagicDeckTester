#!/usr/bin/env python3
"""Does the Kemba park LOOP actually round-trip -- and did it ever get the CHANCE?

The park is half a loop (USER, 2026-08-19): free gear moves onto Kemba in main 2 so the upkeep
counts it and makes Cats, then moves BACK onto a double-strike host in main 1 before combat. A park
that never un-parks is a misplay that would barely move the average -- so the average cannot judge
this lever. This reads the actual played games instead.

RUNWAY IS THE FIRST QUESTION, AND IT IS EASY TO SKIP. Measured 2026-08-19, all 8 park events on
KittyEquipment sat on the last or second-to-last turn of their game: the "next turn" was either the
winning turn or did not exist. Reported without that check, 0-of-8 round-trips reads as "the un-park
is broken"; with it, the correct reading is "the un-park was never given a turn on which it could
matter". A park whose next turn is the final turn is NOT evidence about the un-park either way, so
it is counted separately here and excluded from the round-trip rate.

Usage: park_roundtrip.py <dir-of-game-json> [--verbose]
"""
import json
import pathlib
import sys
from collections import Counter

KEMBA = "Kemba, Kha Regent"
# Hosts the un-park targets: the double-strikers the doctrine wants carrying the gear.
DOUBLE_STRIKERS = {"Kor Duelist", "Balan, Wandering Knight"}


def attachments(board):
    """equipment-name -> host-name for everything attached."""
    by_num = {p.get("card"): p.get("cardName", "") for p in board}
    return {p.get("cardName", ""): by_num.get(p.get("attachedTo"))
            for p in board if p.get("attachedTo")}


def main():
    root = pathlib.Path(sys.argv[1])
    verbose = "--verbose" in sys.argv
    files = sorted(root.glob("*.json"))
    if not files:
        sys.exit(f"no game json under {root}")

    parks = 0
    with_runway = 0          # a following turn exists AND is not the turn the game ends
    unparked = 0             # ...of those, gear had left Kemba by that turn's pre-combat
    still_parked = 0         # ...of those, it had not -- THIS is the trap
    no_runway = 0            # next turn is the winning turn or does not exist: says nothing
    trap_with_ds = 0         # stuck WITH a double-striker on board (the USER's stated condition)
    park_turns = Counter()
    games_with_park = 0

    for f in files:
        g = json.loads(f.read_text())
        final = (g.get("result") or {}).get("turn")
        turns = g.get("turns", [])
        if not turns:
            continue
        last_logged = max(e["turn"] for e in turns)
        end_of, main1_of, bf_of = {}, {}, {}
        for e in turns:
            bf = e.get("boardAfter", {}).get("battlefield", [])
            end_of[e["turn"]] = attachments(bf)
            if e.get("phase") == "MAIN_1":
                main1_of[e["turn"]] = attachments(bf)
                bf_of[e["turn"]] = bf

        saw = False
        for t in sorted(end_of):
            on_kemba = [eq for eq, host in end_of[t].items() if host == KEMBA]
            if not on_kemba:
                continue
            saw = True
            parks += len(on_kemba)
            park_turns[t] += len(on_kemba)
            nxt = t + 1
            # No runway: the loop cannot close on a turn that does not exist, and closing it on the
            # turn the game is already won cannot change anything.
            if nxt > last_logged or (final is not None and final <= nxt):
                no_runway += len(on_kemba)
                continue
            with_runway += len(on_kemba)
            after = main1_of.get(nxt, {})
            ds_out = [p.get("cardName") for p in bf_of.get(nxt, [])
                      if p.get("cardName") in DOUBLE_STRIKERS]
            for eq in on_kemba:
                if after.get(eq) != KEMBA:
                    unparked += 1
                else:
                    still_parked += 1
                    if ds_out:
                        trap_with_ds += 1
                    if verbose:
                        print(f"  STUCK {f.name} T{t}: {eq} still on Kemba at T{nxt} pre-combat"
                              f"  (game ends T{final}; double-strikers out: {ds_out or 'NONE'})")
        if saw:
            games_with_park += 1

    print(f"games                    : {len(files)}")
    print(f"games with a park        : {games_with_park}")
    print(f"park events              : {parks}")
    print(f"  NO RUNWAY (next turn is the win, or absent): {no_runway}"
          f"  -- says nothing about the un-park")
    print(f"  with runway            : {with_runway}")
    if with_runway:
        print(f"    round-tripped        : {unparked} ({100.0*unparked/with_runway:.1f}%)")
        print(f"    STILL parked         : {still_parked} ({100.0*still_parked/with_runway:.1f}%)"
              f"  -- of which {trap_with_ds} WITH a double-striker on board <-- the trap")
    if park_turns:
        print("park events by turn      : "
              + ", ".join(f"T{t}={n}" for t, n in sorted(park_turns.items())))


if __name__ == "__main__":
    main()
