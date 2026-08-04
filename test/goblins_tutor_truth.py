#!/usr/bin/env python3
"""GROUND TRUTH for the Goblins tutor ranking: the real win turn of fetching each ranked candidate.

The question (user, 2026-08-04): "determine how real game win-turns match against our ranking. Then
see if we can move our ranking close enough that W=4 captures everything ... W=4 being insufficient
means we are quite far off that ranking."

Every previous measurement here was a PROXY -- aggregate turn-units, or "what width does the search
need". Neither says where the ranking put the card that actually wins soonest. This forces the tutor
to each ranked position in turn (MTG_TUTOR_FORCE_RANK) and records the resulting win turn, so the
ranking can be scored against the outcome directly:

  * best_rank  -- the position of the fastest-winning candidate. If the ranking were sound this is 1,
                  and W=4 is enough whenever it is <= 4.
  * regret@W   -- turns lost by only ever searching the top W, i.e. best(1..W) - best(all).

Run at unlimited budget so the numbers are the CARD's merit, not the search's luck at a tight budget.

Usage:
    python3 test/goblins_tutor_truth.py                    # the games W=6 cannot recover
    python3 test/goblins_tutor_truth.py --sample 40        # a random-ish spread of overnight games
    python3 test/goblins_tutor_truth.py --ranks 16         # how many ranked positions to probe
"""
import argparse
import concurrent.futures
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build/Release/mtg")
DECK, PROF = "decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"

# The searched games that persist against pre-W ground truth at the shipped width.
PERSIST = [(4004, 14), (4004, 124), (4004, 206), (4004, 727), (6006, 32),
           (7007, 371), (7007, 783), (3003, 101), (3003, 194), (3003, 290), (3003, 4)]


def win_turn(base, gi, rank):
    """Win turn with the tutor forced to the `rank`-th candidate (0 = the ranking as it ships)."""
    tmp = tempfile.mkdtemp(prefix="tutortruth_")
    try:
        env = dict(os.environ)
        if rank:
            env["MTG_TUTOR_FORCE_RANK"] = str(rank)
        p = subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", "3", "--budget-ms", "0",
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        for ln in p.stdout.splitlines():
            if ln.startswith("avg (turns)"):
                return int(float(ln.split(":")[1].split()[0]))
        return None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def fetched_name(base, gi, rank):
    """Which card rank `rank` actually is, in this game's first searched tutor."""
    tmp = tempfile.mkdtemp(prefix="tutorname_")
    try:
        env = dict(os.environ, MTG_TUTOR_FORCE_RANK=str(rank))
        subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", "3", "--budget-ms", "0",
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        f = glob.glob(os.path.join(tmp, "*.json"))
        if not f:
            return "?"
        for seg in json.load(open(f[0])).get("turns", []):
            for a in seg.get("actions", []):
                if a.get("type") == "REVEAL" and "searched" in a.get("source", ""):
                    return ", ".join(c["cardName"] for c in a.get("lookedAt", []))
        return "(no tutor)"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ranks", type=int, default=16)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2

    cases = list(PERSIST)
    if a.sample:
        cases = [(4004, i) for i in range(1, a.sample + 1)]

    # Drop games with no searched tutor at all: their win turn is constant in `rank` for a reason
    # that has nothing to do with the ranking, and counting them would inflate every statistic below.
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        has = list(ex.map(lambda c: fetched_name(c[0], c[1], 1), cases))
    skipped = sum(1 for h in has if h in ("(no tutor)", "?"))
    cases = [c for c, h in zip(cases, has) if h not in ("(no tutor)", "?")]
    if skipped:
        print(f"({skipped} of {skipped + len(cases)} sampled games have no searched tutor -- excluded)\n")

    work = [(b, g, r) for b, g in cases for r in range(0, a.ranks + 1)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = dict(zip(work, ex.map(lambda w: win_turn(*w), work)))

    print(f"{'case':<16} {'ship':>5} {'best':>5} {'@rank':>6} {'regret@4':>9} {'regret@6':>9}   "
          f"win turn by forced rank 1..{a.ranks}")
    tot = {"n": 0, "top1": 0, "top4": 0, "top6": 0, "r4": 0, "r6": 0}
    rows, rows_full = [], []
    for b, g in cases:
        turns = [res[(b, g, r)] for r in range(1, a.ranks + 1)]
        ship = res[(b, g, 0)]
        real = [t for t in turns if t is not None]
        if not real or ship is None:
            continue
        best = min(real)
        best_rank = turns.index(best) + 1
        top = lambda w: min([t for t in turns[:w] if t is not None] or [99])
        r4, r6 = top(4) - best, top(6) - best
        tot["n"] += 1
        tot["top1"] += best_rank == 1
        tot["top4"] += best_rank <= 4
        tot["top6"] += best_rank <= 6
        tot["r4"] += r4
        tot["r6"] += r6
        rows.append((b, g, best_rank, r4))
        rows_full.append((b, g, best_rank, r4, max(real) - best))
        seq = " ".join(str(t) if t is not None else "-" for t in turns)
        print(f"s{b} gi{g:<9} {ship:>5} {best:>5} {best_rank:>6} {r4:>9} {r6:>9}   {seq}")

    n = tot["n"] or 1
    print(f"\n=== ranking quality over {tot['n']} decisions ===")
    print(f"  best candidate ranked #1      : {tot['top1']:3d} / {tot['n']}  ({100*tot['top1']/n:.0f}%)")
    print(f"  best candidate inside top 4   : {tot['top4']:3d} / {tot['n']}  ({100*tot['top4']/n:.0f}%)")
    print(f"  best candidate inside top 6   : {tot['top6']:3d} / {tot['n']}  ({100*tot['top6']/n:.0f}%)")
    print(f"  total regret @W=4             : {tot['r4']:3d} turns")
    print(f"  total regret @W=6             : {tot['r6']:3d} turns")

    # THE statistic. Most Goblins tutor decisions are ties -- every candidate wins on the same turn,
    # so the ranking cannot be wrong and a flat row counts as a "hit" purely by argmin convention.
    # Averaging those in flatters the ranking badly. Score it only where the fetch CHANGES the win
    # turn, which is the only place width can ever be needed.
    live = [(b, g, r, reg, spread) for b, g, r, reg, spread in rows_full if spread]
    ln = len(live) or 1
    print(f"\n=== restricted to decisions where the fetch CHANGES the win turn ===")
    print(f"  such decisions                : {len(live):3d} / {tot['n']}  "
          f"({100*len(live)/n:.0f}% of tutor decisions)")
    print(f"  best candidate ranked #1      : {sum(1 for x in live if x[2] == 1):3d} / {len(live)}")
    print(f"  best candidate inside top 4   : {sum(1 for x in live if x[2] <= 4):3d} / {len(live)}")
    print(f"  best candidate inside top 6   : {sum(1 for x in live if x[2] <= 6):3d} / {len(live)}")
    if live:
        rk = sorted(x[2] for x in live)
        print(f"  rank of the best card         : median {rk[len(rk)//2]}, "
              f"range {rk[0]}..{rk[-1]}")

    misses = [(b, g, r) for b, g, r, reg in rows if reg > 0]
    if misses:
        print("\n=== what the ranking should have picked (W=4 misses) ===")
        for b, g, r in misses:
            print(f"  s{b} gi{g:<6} rank {r}: {fetched_name(b, g, r)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
