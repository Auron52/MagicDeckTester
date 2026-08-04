#!/usr/bin/env python3
"""Which ranking position does the SEARCH actually commit to at a real tutor resolution?

Run wide (MTG_TUTOR_WIDTH) with MTG_TUTOR_CHOSEN_RANK=1 and read the committed choice. This is the
sound version of "is the width covering for a bad ranking":

  * a committed rank PAST the shipped width  -> a real ranking miss; the card name is trustworthy
  * a committed rank INSIDE the shipped width -> the extra width bought plan diversity, not a better
    fetch, and no reordering of the candidates will recover that game

It replaces the forced-rank table, which cannot tell those apart: forcing the axis to width 1 changes
the plan, so the same card can appear at two "ranks" in two different board states (s7007 gi371 --
forced rank 6 casts Matron on T4, forced rank 10 casts it on T3, both fetching Goblin Piledriver).

Ranks are over NAMES deduped in list order, matching how the fetch actually works.

Usage:
    python3 test/goblins_chosen_rank.py                 # the games W=12 changes vs shipped W=6
    python3 test/goblins_chosen_rank.py --sample 300    # base rate over arbitrary games
    python3 test/goblins_chosen_rank.py --width 12 --shipped-width 6
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build/Release/mtg")
DECK, PROF = "decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"

BETTER_AT_12 = [(3003, 101), (4004, 14), (4004, 124), (4004, 206), (4004, 828), (5005, 553),
                (6006, 496), (7007, 371), (7007, 783)]
WORSE_AT_12 = [(2002, 299), (4004, 483), (7007, 624)]

LINE = re.compile(r"\[tutor-chosen\] T(\d+) src=(.+?) chose=(.+?) rank=(-?\d+)/(\d+) :: (.*)")


def run(base, gi, width):
    tmp = tempfile.mkdtemp(prefix="chosen_")
    try:
        env = dict(os.environ, MTG_TUTOR_CHOSEN_RANK="1", MTG_TUTOR_WIDTH=str(width))
        p = subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", "3", "--budget-ms", "0",
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        turn = None
        for ln in p.stdout.splitlines():
            if ln.startswith("avg (turns)"):
                turn = int(float(ln.split(":")[1].split()[0]))
        picks = []
        for ln in p.stderr.splitlines():
            m = LINE.match(ln)
            if m:
                picks.append((int(m.group(1)), m.group(3), int(m.group(4)), int(m.group(5)),
                              m.group(6)))
        return turn, picks
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=12)
    ap.add_argument("--shipped-width", type=int, default=6)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2

    if a.sample:
        cases = [(4004, i, "sample") for i in range(1, a.sample + 1)]
    else:
        cases = [(b, g, "W12 better") for b, g in BETTER_AT_12] + \
                [(b, g, "W12 WORSE") for b, g in WORSE_AT_12]

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(lambda c: run(c[0], c[1], a.width), cases))

    miss_cards = collections.Counter()
    n_dec = n_miss = 0
    games_with_miss = []
    for (b, g, tag), (turn, picks) in zip(cases, res):
        if not picks:
            continue
        flags = []
        for t, card, rank, total, top in picks:
            n_dec += 1
            out = rank > a.shipped_width
            if out:
                n_miss += 1
                miss_cards[card] += 1
            flags.append(out)
        if not a.sample:
            print(f"\n=== s{b} gi{g}  (W={a.width} -> T{turn})  [{tag}] ===")
            for t, card, rank, total, top in picks:
                mark = "  <-- OUTSIDE the shipped W=%d" % a.shipped_width if rank > a.shipped_width else ""
                print(f"    T{t}  chose {card:<26} rank {rank}/{total}{mark}")
                print(f"         order: {top}")
        if any(flags):
            games_with_miss.append((b, g))

    print(f"\n\n================ COMMITTED TUTOR CHOICES (width {a.width}) ================")
    print(f"  real tutor decisions observed            : {n_dec}")
    print(f"  committed to a rank PAST W={a.shipped_width} (real ranking miss) : {n_miss}"
          f"  ({100*n_miss/max(n_dec,1):.0f}%)")
    print(f"  games containing at least one such miss  : {len(games_with_miss)}")
    if miss_cards:
        print("\n  cards the search reached past the window for:")
        for c, n in miss_cards.most_common():
            print(f"     {n:3d}x  {c}")
    if games_with_miss and not a.sample:
        print("\n  games: " + ", ".join(f"s{b} gi{g}" for b, g in games_with_miss))
    return 0


if __name__ == "__main__":
    sys.exit(main())
