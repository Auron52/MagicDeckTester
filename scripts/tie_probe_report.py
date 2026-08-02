#!/usr/bin/env python3
"""Aggregate the indifference probe (MTG_TRACE=tie,tiescan) into a per-card free-rate.

The OFFER/PRUNE audit -- blind spot 1 of docs/design/searched-design-audit-blind-spots.md:
find actions the search takes because it is INDIFFERENT rather than because they help.

The engine emits, for every committed decision:

    [tiescan] turn=N scored=K win=W free=F chosen: <action keys of the plan it played>
    [tie]     turn=N win=W free=F/T extra: <action keys that a tied SUBSET plan did without>

A single `tie` line means little. At a bounded horizon most plays are free for the
PROJECTED win turn -- casting a lord on turn 2 does not move a turn-4 win, yet no player
would skip it. The signal is the RATE: a card that is free essentially every time it is
cast never contributes to the searched outcome, which is the duplicate-legend signature
(a correct rule plus an indifferent search producing a persistent misplay).

Read the output as a shortlist to inspect, never as a verdict. For each high-rate card ask
the question the rate cannot: would a human ever consider this play? A card can be free
because it is genuinely pointless (prune it) or because the horizon ends before it pays off
(leave it alone). Only the first is a bug.

Usage:
    MTG_TRACE=tie,tiescan build/Release/mtg <deck> --profile <p> --games N --threads 1 \
        2>trace.txt >/dev/null
    scripts/tie_probe_report.py trace.txt [--min-casts 20]
"""
import argparse
import collections
import re
import sys

TIE = re.compile(r"^\[tie\] turn=(\d+) win=(\d+) free=(\d+)/(\d+) extra: (.*)$")
SCAN = re.compile(r"^\[tiescan\] turn=(\d+) scored=(\d+) win=(\d+) free=(\d+) chosen: (.*)$")

# The action key is "<card name>#<kind>[/x3][/s1][/alt]..." -- strip the variant suffixes so
# the rate is per CARD, but keep the kind so a cast and an activation stay distinct.
KEY = re.compile(r"^(.*?)#(\d+)")


def card_of(key):
    m = KEY.match(key)
    return f"{m.group(1)}#{m.group(2)}" if m else key


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", nargs="?", default="-")
    ap.add_argument("--min-casts", type=int, default=20,
                    help="ignore cards played fewer than this many times (default 20)")
    args = ap.parse_args()

    src = sys.stdin if args.trace == "-" else open(args.trace, encoding="utf-8", errors="replace")

    played = collections.Counter()   # times the card appeared in a committed plan
    free = collections.Counter()     # ... of which it was in a tied subset's difference
    decisions = 0
    with_free = 0

    for line in src:
        m = SCAN.match(line.rstrip("\n"))
        if m:
            decisions += 1
            if int(m.group(4)):
                with_free += 1
            chosen = m.group(5)
            if chosen != "<pass>":
                for k in chosen.split():
                    played[card_of(k)] += 1
            continue
        m = TIE.match(line.rstrip("\n"))
        if m:
            for k in m.group(5).split():
                free[card_of(k)] += 1

    if not decisions:
        print("no [tiescan] lines -- was the run made with MTG_TRACE=tie,tiescan?", file=sys.stderr)
        return 1

    print(f"committed decisions : {decisions}")
    print(f"  with a free action: {with_free} ({100.0 * with_free / decisions:.1f}%)")
    print()
    print(f"{'action':<44} {'played':>7} {'free':>7} {'rate':>7}")
    print("-" * 68)

    rows = []
    for card, n in played.items():
        if n < args.min_casts:
            continue
        f = free.get(card, 0)
        rows.append((f / n, n, f, card))
    # Highest free-rate first; a rate near 1.0 over many casts is the shortlist.
    for rate, n, f, card in sorted(rows, reverse=True):
        print(f"{card:<44} {n:>7} {f:>7} {rate:>6.1%}")

    stray = sorted(set(free) - set(played))
    if stray:
        print("\nfree but never in a committed plan (variant-key mismatch, investigate):")
        for card in stray:
            print(f"  {card}  free={free[card]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
