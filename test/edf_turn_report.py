#!/usr/bin/env python3
"""Why did a board that HAS the combo pieces not go off the next turn?

The user's challenge (2026-09-05): *"That makes no sense. If we have the pieces on board, we
should at least be able to go off next turn, not 2 turns out."* That claim cannot be settled from
a game log -- the log shows the OUTCOME, and what is missing is which TERM was short. So
`MTG_EDF_TURN_TRACE=1` prints the go-off recognizer's own inputs once per REAL pre-combat main
(`EdfTurnTrace`, hooked in `AIEngine::TakeTurn` -- above the search entry, because every other
instrument the recognizer has fires ~85,000 times a game inside rollouts).

This reads that stream and answers, per game and in aggregate:

    turn the PIECES landed (untapper + outlet both on board)
    -> turn the LOOP first read live (ok=1 and net>0)
    -> turn the game was actually won

and buckets every turn at-or-after the pieces landed into the reason it did not kill:

    no-loop        ok=0  -- the recognizer does not see a loop at all (missing/uncastable piece)
    dead-loop      ok=1, net<=0 -- the untap does not pay for the blink; iterating loses mana
    no-sink        net>0 but nothing to cash unbounded mana into (no Gorge/drain/exile reachable)
    SINK-DECLINED  net>0 AND a sink is reachable -- the loop was live, priced, and NOT taken

Only the last bucket is the user's bug. The first three are board facts and would justify the lag;
the fourth is the engine leaving a kill on the table, and it is the one worth chasing.

Usage:
    MTG_EDF_TURN_TRACE=1 MTG_DUMP_WINS=1 build/Release/mtg <deck> --games N --seed S \
        --threads 1 [...] 2> logs/edf_trace/run.err
    python3 test/edf_turn_report.py logs/edf_trace/run.err [--per-game]

`--threads 1` and MTG_DUMP_WINS are both REQUIRED: games are attributed to a `[win] gi=` marker by
position in the stream, which only holds when games are played in order on one thread.
"""
import argparse
import re
import sys
from collections import Counter, defaultdict

TRACE = re.compile(
    r"\[edf-turn\] t(?P<t>\d+) lands=(?P<lands>\d+) auras=(?P<auras>\d+) "
    r"untapper=(?P<untapper>\d+) outlet=(?P<outlet>\d+) drawland=(?P<drawland>\d+) "
    r"wish=(?P<wish>\d+) \| ok=(?P<ok>\d+) untaps=(?P<untaps>-?\d+) refund=(?P<refund>-?\d+) "
    r"cost=(?P<cost>-?\d+) net=(?P<net>-?\d+) pool=(?P<pool>\d+) "
    r"gorge=(?P<gorge>\d+)/(?P<gcost>\d+) "
    r"drain=(?P<dmg>\d+)/(?P<dcost>\d+) exile=(?P<ecost>\d+) setup=(?P<setup>-?\d+) "
    r"needgorge=(?P<needgorge>-?\d+) needdrain=(?P<needdrain>-?\d+)"
)
WIN = re.compile(r"\[win\] gi=(?P<gi>\d+) wt=(?P<wt>-?\d+)")


def parse(paths):
    """-> {key: {'turns': [dict...], 'wt': int}}. Trace lines accumulate until a [win] closes a game.

    Accepts MANY files so a 60-game trace can be run as N single-threaded processes over disjoint
    seed blocks instead of one serial process -- `--threads 1` is required WITHIN a file (games are
    attributed to a `[win] gi=` marker by position in the stream), but the files themselves are
    independent, so the box can be saturated without breaking attribution. Games are keyed by
    (file, gi) because separate blocks reuse gi numbering.
    """
    games = {}
    for path in paths:
        pending = []
        stem = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        for line in open(path, errors="replace"):
            m = TRACE.search(line)
            if m:
                pending.append({k: int(v) for k, v in m.groupdict().items()})
                continue
            m = WIN.search(line)
            if m:
                games[(stem, int(m.group("gi")))] = {"turns": pending, "wt": int(m.group("wt"))}
                pending = []
        if pending:
            print(f"warning: {path}: {len(pending)} trace lines after the last [win] -- dropped "
                  f"(unfinished game, or --threads > 1 interleaving)", file=sys.stderr)
    return games


def sink_live(r):
    """Is there anything to cash unbounded mana INTO on this turn's board?

    The printed fields are AMOUNTS and COSTS, not booleans -- `drain=<drain_amount>/<cost_mv>`,
    `exile=<cost_mv>`, `gorge=<gorge_dmg>`. A drain is reachable when its per-activation damage is
    nonzero; an exile/gorge when the recognizer managed to PRICE it. (ScanHandSinks folds in a
    drain held in hand and one a Living Wish can still fetch, which is why `setup` can be nonzero
    on a board that shows no sink permanent at all.)
    """
    return r["gorge"] > 0 or r["dmg"] > 0 or r["ecost"] > 0


def startable(r):
    """Could the loop afford ONE full iteration plus the hand setup, out of the mana available NOW?

    Mirrors EldraziFlickerProvider::ExtraLethalDamage's `startable(...)` on the board-sink path
    (the `prospective` path additionally nets out the casts, which the executor-side trace never
    sees because it is taken before any cast). A route counts only if its sink is reachable AND the
    Gorge's `refund > cost + gorge_cost` sustain test can hold.
    """
    if r["gorge"] > 0 and r["refund"] > r["needgorge"] and r["pool"] >= r["needgorge"]:
        return True
    if r["dmg"] > 0 and r["net"] > 0 and r["pool"] >= r["needdrain"]:
        return True
    return False


def classify(r):
    if not r["ok"]:
        return "no-loop"
    if r["net"] <= 0:
        return "dead-loop"
    if not sink_live(r):
        return "no-sink"
    # A live loop with a reachable sink is STILL a correct decline when the board cannot pay for the
    # first iteration -- that is arithmetic, not a missed kill. Only what survives this is a bug.
    if not startable(r):
        return "short-to-start"
    return "SINK-DECLINED"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="+", help="one or more stderr files (one per --threads 1 process)")
    ap.add_argument("--per-game", action="store_true", help="one line per game as well as the summary")
    args = ap.parse_args()

    games = parse(args.path)
    if not games:
        print("no [edf-turn]/[win] pairs found -- was MTG_EDF_TURN_TRACE=1 MTG_DUMP_WINS=1 set?")
        return 1

    buckets = Counter()
    lag_hist = Counter()
    piece_to_win, live_to_win = [], []
    declined_examples = []
    n_pieces = n_live = 0

    n_unwon = 0
    for key in sorted(games):
        gi = f"{key[0]}:{key[1]}" if isinstance(key, tuple) else key
        g = games[key]
        if g["wt"] <= 0:
            n_unwon += 1
        turns = g["turns"]
        wt = g["wt"]
        t_pieces = next((r["t"] for r in turns if r["untapper"] and r["outlet"]), None)
        t_live = next((r["t"] for r in turns if r["ok"] and r["net"] > 0), None)

        # ONE ROW PER TURN. `AIEngine::TakeTurn` re-solves within a turn (draw breakpoints), so a
        # single turn emits several trace lines whose pool/auras GROW as the turn is played out.
        # Counting each as its own "turn that did not kill" both inflates the denominator and, worse,
        # blames the same turn up to five times. The LAST line of a turn is the right representative:
        # it is the state with the most resources assembled, so it is the strongest case that a kill
        # was available and not taken.
        by_turn = {}
        for r in turns:
            by_turn[r["t"]] = r
        turns = [by_turn[t] for t in sorted(by_turn)]
        # Only turns at-or-after the pieces landed can be blamed: before that there is no combo.
        rel = [r for r in turns if t_pieces is not None and r["t"] >= t_pieces]
        # The winning turn itself is not a failure to go off, so it is excluded from the buckets.
        for r in rel:
            if wt > 0 and r["t"] >= wt:
                continue
            buckets[classify(r)] += 1
            if classify(r) == "SINK-DECLINED" and len(declined_examples) < 12:
                declined_examples.append((gi, r))

        if t_pieces is not None:
            n_pieces += 1
            if wt > 0:
                piece_to_win.append(wt - t_pieces)
                lag_hist[wt - t_pieces] += 1
        if t_live is not None:
            n_live += 1
            if wt > 0:
                live_to_win.append(wt - t_live)

        if args.per_game:
            print(f"  gi={gi:<4} pieces=t{t_pieces if t_pieces else '-':<3} "
                  f"live=t{t_live if t_live else '-':<3} win=t{wt if wt > 0 else '-':<3} "
                  + " ".join(f"t{r['t']}:{classify(r)[:4]}" for r in rel))

    def stat(v):
        return f"mean {sum(v)/len(v):+.2f} turns (n={len(v)})" if v else "n/a"

    print(f"\n=== EDF turn trace: {len(games)} games ===")
    print(f"games where the pieces ever landed : {n_pieces}/{len(games)}")
    print(f"games where the loop ever read live: {n_live}/{len(games)}")
    # The lag stats below can only be computed on games that WERE won, so an unwon game -- the worst
    # possible outcome -- silently drops out of them. Printing the count keeps that bias visible
    # instead of letting a flattering mean stand unqualified.
    print(f"unwon games (EXCLUDED from the lag means below): {n_unwon}/{len(games)}")
    print(f"\npieces-on-board -> win  : {stat(piece_to_win)}   <-- the user's '1, not 2+' claim")
    print(f"loop-reads-live -> win  : {stat(live_to_win)}")
    if lag_hist:
        print("  lag histogram (turns from pieces to win):")
        for k in sorted(lag_hist):
            print(f"    {k:+3d} : {'#' * lag_hist[k]} ({lag_hist[k]})")

    total = sum(buckets.values())
    print(f"\nwhy each post-pieces turn did NOT kill  (n={total} turns):")
    for k, v in buckets.most_common():
        mark = "  <-- ENGINE LEFT A KILL" if k == "SINK-DECLINED" else ""
        print(f"  {k:<14} {v:5d}  {100.0*v/total if total else 0:5.1f}%{mark}")

    if declined_examples:
        print("\nSINK-DECLINED examples (loop live AND a sink reachable, still no kill):")
        for gi, r in declined_examples:
            print(f"  gi={gi} t{r['t']}: lands={r['lands']} auras={r['auras']} net={r['net']} "
                  f"untaps={r['untaps']} cost={r['cost']} pool={r['pool']} "
                  f"gorge={r['gorge']}/{r['gcost']} (need {r['needgorge']}) "
                  f"drain={r['dmg']}/{r['dcost']} (need {r['needdrain']}) "
                  f"exile={r['ecost']} setup={r['setup']} wish={r['wish']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
