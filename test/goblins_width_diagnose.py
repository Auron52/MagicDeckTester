#!/usr/bin/env python3
"""For every game the tutor WIDTH decides: our ranked ordering, and the real win turn of each option.

The question (user, 2026-08-04): "that seems very surprising to me that we have so many beyond our
W=6. We should really analyze all of the games, output our ordering for the decision and present the
win turn for each option so we can figure out why this goes south so badly."

For each game this forces the tutor to rank k (MTG_TUTOR_FORCE_RANK) for k = 1..N and records BOTH
what card that rank is and the win turn that results. The table therefore shows the ranking's own
ordering next to the outcome of each choice -- no dump-state matching required, which is what made
earlier attempts unreliable (the ranking dump is dominated by search-simulated states, and the arms
diverge on WHICH turn the Matron is even cast).

It also separates the two reasons width helps, which turn-units cannot:

  * RANKING MISS    -- some single forced rank reaches the wide arm's win turn, and that rank is > 6.
                       The right card exists in the list, the window just cannot see it.
  * SEARCH BRANCHING -- NO single forced rank reaches it. The wide axis is buying the search more
                       plans to arbitrate, not a better fetch. gi14 is the known example: T6 at every
                       rank 1..16, yet T5 at W=12.

Usage:
    python3 test/goblins_width_diagnose.py               # the games W=12 changes vs shipped W=6
    python3 test/goblins_width_diagnose.py --ranks 16
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

# (seed_base, game_index) -- measured by test/goblins_width_games.py against the shipped W=6.
BETTER_AT_12 = [(1001, 113), (3003, 101), (3003, 194), (3003, 290), (4004, 14), (4004, 124),
                (4004, 206), (4004, 727), (4004, 768), (4004, 828), (5005, 553), (5005, 920),
                (6006, 32), (7007, 371), (7007, 783), (6006, 496)]
WORSE_AT_12 = [(2002, 299), (4004, 483), (7007, 624)]


def run(base, gi, env_extra):
    """(win_turn, first searched-tutor fetch) for one configuration."""
    tmp = tempfile.mkdtemp(prefix="wdiag_")
    try:
        env = dict(os.environ, **env_extra)
        p = subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", "3", "--budget-ms", "0",
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        turn = None
        for ln in p.stdout.splitlines():
            if ln.startswith("avg (turns)"):
                turn = int(float(ln.split(":")[1].split()[0]))
        name = "(no tutor)"
        f = glob.glob(os.path.join(tmp, "*.json"))
        if f:
            for seg in json.load(open(f[0])).get("turns", []):
                done = False
                for a in seg.get("actions", []):
                    if a.get("type") == "REVEAL" and "searched" in a.get("source", ""):
                        name = ", ".join(c["cardName"] for c in a.get("lookedAt", []))
                        done = True
                        break
                if done:
                    break
        return turn, name
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ranks", type=int, default=16)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2

    cases = [(b, g, "W12 better") for b, g in BETTER_AT_12] + \
            [(b, g, "W12 WORSE") for b, g in WORSE_AT_12]

    jobs = []
    for b, g, _ in cases:
        jobs.append((b, g, {}))                              # shipped W=6
        jobs.append((b, g, {"MTG_TUTOR_WIDTH": "12"}))       # wide
        for r in range(1, a.ranks + 1):
            jobs.append((b, g, {"MTG_TUTOR_FORCE_RANK": str(r)}))
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = dict(zip(map(lambda j: (j[0], j[1], tuple(sorted(j[2].items()))), jobs),
                       ex.map(lambda j: run(*j), jobs)))

    key = lambda b, g, e: (b, g, tuple(sorted(e.items())))
    verdicts = {"ranking miss": [], "search branching": [], "other": []}
    for b, g, tag in cases:
        ship_t, ship_n = res[key(b, g, {})]
        wide_t, wide_n = res[key(b, g, {"MTG_TUTOR_WIDTH": "12"})]
        rows = [res[key(b, g, {"MTG_TUTOR_FORCE_RANK": str(r)})] for r in range(1, a.ranks + 1)]
        turns = [t for t, _ in rows]
        real = [t for t in turns if t is not None]
        best = min(real) if real else None
        print(f"\n=== s{b} gi{g}   shipped W=6 -> T{ship_t} ({ship_n})   |   W=12 -> T{wide_t} "
              f"({wide_n})   [{tag}] ===")
        print(f"    {'rank':>4}  {'card at that rank':<28} {'win turn':>8}")
        for i, (t, n) in enumerate(rows, start=1):
            mark = " <<" if t is not None and best is not None and t == best else ""
            win = "  W" if i <= 6 else "  ." # inside the shipped window?
            print(f"   {win}{i:>4}  {n:<28} {('T'+str(t)) if t is not None else '-':>8}{mark}")
        # Which mechanism?
        if best is not None and wide_t is not None and best <= wide_t:
            rank_of_best = turns.index(best) + 1
            if rank_of_best > 6:
                verdicts["ranking miss"].append((b, g, rank_of_best, rows[rank_of_best - 1][1]))
                print(f"    -> RANKING MISS: rank {rank_of_best} ({rows[rank_of_best-1][1]}) "
                      f"reaches T{best}, outside the W=6 window")
            else:
                verdicts["other"].append((b, g, rank_of_best, rows[rank_of_best - 1][1]))
                print(f"    -> best single card is rank {rank_of_best}, INSIDE the window "
                      f"-- width is not the fetch here")
        else:
            verdicts["search branching"].append((b, g, best, wide_t))
            print(f"    -> SEARCH BRANCHING: no single forced rank reaches T{wide_t} "
                  f"(best single card = T{best})")

    print("\n\n================ WHY WIDTH HELPS ================")
    for k in ("ranking miss", "search branching", "other"):
        print(f"\n{k.upper()}  ({len(verdicts[k])} games)")
        for v in verdicts[k]:
            print("   ", v)
    return 0


if __name__ == "__main__":
    sys.exit(main())
