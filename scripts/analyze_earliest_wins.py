#!/usr/bin/env python3
"""
analyze_earliest_wins.py  --  pattern-miner for the enumerate-all-earliest-wins dump.

Reads the JSON lines emitted by the runner under MTG_DUMP_EWINS=1 (one
`{"ewins":{...}}` object per real pre-combat decision; each lists every candidate
top-level play with the EARLIEST full-game win turn it leads to) and mines the COMMON
structure of optimal lines, to GROUND ordering/targeting/inclusion heuristics for the
DecisionProvider (analyze-deck Stage 5g). It does NOT invent rules from card text -- it
reports what the full search's own earliest-win lines agree on, with support counts.

Three reports:
  1. ORDER rules     -- ordered card pairs (A before B) that are consistently faster.
  2. INCLUSION rules -- casting card C this turn helps / is neutral / hurts the win turn.
  3. LAND / FETCH    -- which land (and fetch target) the earliest-win lines pick.

Usage:
  # generate the dump (single decision turn, a chunk of games; orderings need SEARCH_ORDER)
  MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1 MTG_DUMP_EWINS_TURN=1 \
      ./build/Release/mtg <deck> --profile <prof> --seed 2002 --games 200 \
      --depth 5 --budget-ms 3000 2>dump.jsonl >/dev/null
  python3 scripts/analyze_earliest_wins.py dump.jsonl
"""
import sys, json, argparse
from collections import defaultdict
from itertools import combinations


def load(path):
    decisions = []
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln.startswith('{"ewins"'):
                continue
            try:
                decisions.append(json.loads(ln)["ewins"])
            except Exception:
                pass
    return decisions


def order_rules(decisions, min_support):
    """For each unordered card pair, count decisions whose FASTEST line containing both
    casts has A before B vs B before A. A lopsided, low-conflict pair is a precedence rule."""
    # pair -> {"A<B": n, "B<A": n} where A<B is lexical-canonical key direction
    pair = defaultdict(lambda: defaultdict(int))
    for d in decisions:
        # group candidates by cast-set; within each set find the min win turn and which
        # orderings achieve it; only ordered (searched) candidates carry order information.
        best_for_pair = {}  # frozenset(a,b) -> (min_win, set_of_(first,second)_at_min)
        for c in d["candidates"]:
            casts = c["casts"]
            if len(casts) < 2 or not c.get("searched"):
                continue
            w = c["win"]
            for i in range(len(casts)):
                for j in range(i + 1, len(casts)):
                    a, b = casts[i], casts[j]          # a precedes b in this ordering
                    if a == b:
                        continue
                    key = frozenset((a, b))
                    cur = best_for_pair.get(key)
                    if cur is None or w < cur[0]:
                        best_for_pair[key] = (w, {(a, b)})
                    elif w == cur[0]:
                        cur[1].add((a, b))
        # a pair is decisive in this decision only if the min-win orderings agree on direction
        for key, (w, dirs) in best_for_pair.items():
            firsts = {a for (a, b) in dirs}
            if len(dirs) and len({frozenset((a, b)) for (a, b) in dirs}) == 1:
                # all min-win orderings of this pair share one direction?
                only = {(a, b) for (a, b) in dirs}
                if len(only) == 1:
                    a, b = next(iter(only))
                    canon = tuple(sorted((a, b)))
                    pair[canon]["fwd" if (a, b) == canon else "rev"] += 1
                else:
                    a, b = sorted(key)
                    pair[tuple(sorted(key))]["tie"] += 1
    rows = []
    for (a, b), cnt in pair.items():
        fwd, rev, tie = cnt.get("fwd", 0), cnt.get("rev", 0), cnt.get("tie", 0)
        if fwd + rev < min_support:
            continue
        if fwd >= rev:
            lead, conflict, first, second = fwd, rev, a, b
        else:
            lead, conflict, first, second = rev, fwd, b, a
        rows.append((lead, conflict, tie, first, second))
    rows.sort(key=lambda r: (-(r[0] - r[1]), -r[0]))
    print("\n== ORDER rules (cast FIRST before SECOND is faster) ==")
    print(f"   {'support':>7} {'conflict':>8} {'tie':>4}  rule")
    if not rows:
        print("   (none with enough support -- run with MTG_SEARCH_ORDER=1 and more games)")
    for lead, conflict, tie, first, second in rows:
        flag = "  <-- consistent" if conflict == 0 else ""
        print(f"   {lead:7} {conflict:8} {tie:4}  cast '{first}' before '{second}'{flag}")


def inclusion_rules(decisions, min_support):
    """For each card C, across decisions where C is castable this turn, compare the best
    (min) win turn among candidates that CAST C vs those that do NOT. Negative delta =
    casting C this turn tends to win SOONER; positive = it tends to slow the line."""
    agg = defaultdict(lambda: {"n": 0, "with": 0, "wo": 0, "help": 0, "hurt": 0, "neutral": 0})
    for d in decisions:
        cards = set()
        for c in d["candidates"]:
            cards.update(c["casts"]); cards.update(c["sac"])
        for card in cards:
            best_with = min((c["win"] for c in d["candidates"]
                             if card in c["casts"] or card in c["sac"]), default=None)
            best_wo = min((c["win"] for c in d["candidates"]
                           if card not in c["casts"] and card not in c["sac"]), default=None)
            if best_with is None or best_wo is None:
                continue
            a = agg[card]; a["n"] += 1; a["with"] += best_with; a["wo"] += best_wo
            if best_with < best_wo:   a["help"] += 1
            elif best_with > best_wo: a["hurt"] += 1
            else:                     a["neutral"] += 1
    rows = [(card, v) for card, v in agg.items() if v["n"] >= min_support]
    rows.sort(key=lambda kv: (kv[1]["with"] - kv[1]["wo"]) / kv[1]["n"])
    print("\n== INCLUSION rules (casting this card THIS turn: avg win-turn delta) ==")
    print("   delta = best-win(cast it) - best-win(don't), averaged over decisions. "
          "READ THE SPLIT, not just the mean:")
    print("     -delta, help>>hurt  -> payoff/enabler: cast it (usually already cast; no code needed).")
    print("     +delta, help == 0   -> consistently slower here: an inert-in-goldfish ability "
          "(e.g. shroud) or just outclassed; the search already deprioritises it -> gate ONLY if a")
    print("                            per-game misplay is confirmed. (Small samples can hide a rare combo.)")
    print("     +delta BUT help > 0 -> SITUATIONAL / setup-timing (good only WITH a follow-up, e.g. a")
    print("                            token-maker before more creatures) -> do NOT gate; LEAVE TO THE SEARCH.")
    print(f"   {'avgdelta':>8} {'help':>5} {'hurt':>5} {'neut':>5} {'n':>5}  card  | reading")
    for card, v in rows:
        delta = (v["with"] - v["wo"]) / v["n"]
        if delta <= -0.25:
            tag = "cast it (payoff/enabler)"
        elif delta >= 0.25 and v["help"] == 0:
            tag = "slower early (inert ability? / outclassed -- leave to search unless misplay confirmed)"
        elif delta >= 0.25:
            tag = "SITUATIONAL/setup -- leave to search (helps in %d/%d)" % (v["help"], v["n"])
        else:
            tag = ""
        sep = "  | " if tag else ""
        print(f"   {delta:+8.2f} {v['help']:5} {v['hurt']:5} {v['neutral']:5} {v['n']:5}  {card}{sep}{tag}")


def land_rules(decisions, min_support):
    land = defaultdict(int); fetch = defaultdict(int); n = 0
    for d in decisions:
        e = d["earliest"]
        best = [c for c in d["candidates"] if c["win"] == e]
        if not best:
            continue
        n += 1
        for c in best:
            if c["land"]: land[c["land"]] += 1
            if c["fetch"]: fetch[c["fetch"]] += 1
    if n < min_support:
        return
    print("\n== LAND / FETCH in earliest-win lines ==")
    for name, cnt in sorted(land.items(), key=lambda kv: -kv[1]):
        print(f"   land  {cnt:5}  {name}")
    for name, cnt in sorted(fetch.items(), key=lambda kv: -kv[1]):
        print(f"   fetch {cnt:5}  {name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="JSONL file of {\"ewins\":...} lines (MTG_DUMP_EWINS stderr)")
    ap.add_argument("--min-support", type=int, default=3,
                    help="minimum decisions backing a reported rule (default 3)")
    args = ap.parse_args()

    decisions = load(args.dump)
    if not decisions:
        print("No {\"ewins\":...} lines found. Did you redirect the runner's STDERR to the file?")
        sys.exit(1)

    turns = sorted({d["turn"] for d in decisions})
    ties = [sum(1 for c in d["candidates"] if c["win"] == d["earliest"]) for d in decisions]
    print(f"Loaded {len(decisions)} decisions (turns {turns}).")
    print(f"Earliest-win ties: median {sorted(ties)[len(ties)//2]} candidate(s) per decision "
          f"(min {min(ties)}, max {max(ties)}).")

    order_rules(decisions, args.min_support)
    inclusion_rules(decisions, args.min_support)
    land_rules(decisions, args.min_support)


if __name__ == "__main__":
    main()
