#!/usr/bin/env python3
"""What cast ORDER do the winning lines actually use, and where does the DECLARED order disagree?

USER, 2026-08-27: *"I don't want to exempt things from the prune. Instead, I want to figure out an
order that works reliably with occasional cases where a specific card is given a range."*

Condemnation enforces the declared cast order, so every line it deletes is a case where the declared
order disagrees with the line that WINS. That makes the deleted-line census a test suite for the
order: replay the winning (condemnation-off) games, read each turn's cast sequence, and report every
INVERSION -- a cast whose declared rank is lower than one already made this turn, i.e. a card the
order says should have come first and the winning line deliberately played later.

An inversion is not automatically an order bug; a pair that inverts in BOTH directions across games
is a genuine RANGE (no fixed slot is right), while one that inverts consistently in ONE direction is
a straightforward mis-rank. The output separates the two.
"""
import collections
import json
import pathlib
import sys

RANKS = {}


def load_ranks():
    raw = json.load(open("src/cards/data/cards.json"))
    cards = raw["cards"] if isinstance(raw, dict) and "cards" in raw else raw
    for c in cards:
        p = c.get("parameters", {}) or {}
        # MirrorwingProvider::CastOrderRank, in ITS test order (payoff precedes draw: Fists of
        # Flame carries cast_draw AND pump_per_cards_drawn, and ranks as the payoff).
        if p.get("copies_solo_targeted_spells"):        r = 5
        elif p.get("trick_token_power", 0) > 0:         r = 11
        elif p.get("token_copy_of_target"):             r = 12
        elif p.get("pump_per_cards_drawn_power", 0) > 0: r = 16
        elif p.get("cast_draw", 0) > 0:                 r = 14
        elif p.get("creates_treasures", 0) > 0:         r = 15
        elif p.get("pump_per_life_gained_power", 0) > 0: r = 18
        elif "Creature" in (c.get("types") or []):      r = 10
        else:                                           r = 20
        RANKS[c["name"]] = r


def main():
    load_ranks()
    root = pathlib.Path(sys.argv[1])
    # OBSERVED orderings, counted in BOTH directions for every co-occurring pair. Counting only
    # inversions cannot answer the range question: the reverse of an inversion is by construction
    # never itself an inversion, so a "reverse count" taken over inversions alone is identically
    # zero and reads as "always one-way" no matter what the data says.
    pairs = collections.Counter()      # (first_cast, second_cast) -> times seen in that order
    seqs = collections.Counter()
    for d in sorted(root.iterdir()):
        if not d.is_dir():
            continue
        f = sorted((d / "base").glob("*.json"))
        if not f:
            continue
        g = json.load(open(f[0]))
        for t in g.get("turns", []):
            casts = [a["cardName"] for a in t.get("actions", [])
                     if a.get("type") == "CAST_SPELL"]
            if len(casts) < 2:
                continue
            seqs["/".join(f"{c}({RANKS.get(c,20)})" for c in casts)] += 1
            for i, later in enumerate(casts):
                for earlier in casts[:i]:
                    if earlier != later:
                        pairs[(earlier, later)] += 1

    print("=== DECLARED order vs the order WINNING lines actually use ===")
    print("    fwd = times A cast before B; rev = times B cast before A\n")
    print(f"    {'A':26} {'rk':>3}  {'B':26} {'rk':>3} {'fwd':>4} {'rev':>4}  verdict")
    seen = set()
    rows = []
    for (a, b), n in pairs.items():
        if (b, a) in seen or (a, b) in seen:
            continue
        seen.add((a, b))
        rev = pairs.get((b, a), 0)
        ra, rb = RANKS.get(a, 20), RANKS.get(b, 20)
        # Which way does the DECLARED order say? Lower rank first.
        declared_fwd = ra < rb
        agree = n if declared_fwd else rev
        against = rev if declared_fwd else n
        rows.append((against, agree, a, ra, b, rb, n, rev))
    for against, agree, a, ra, b, rb, n, rev in sorted(rows, reverse=True)[:16]:
        if against == 0:
            verdict = "order OK"
        elif agree == 0:
            verdict = "MIS-RANKED (always the other way)"
        else:
            verdict = f"RANGE ({agree} with / {against} against)"
        print(f"    {a:26} {ra:>3}  {b:26} {rb:>3} {n:>4} {rev:>4}  {verdict}")

    print("\n=== most common multi-cast sequences in winning turns ===")
    for s, n in seqs.most_common(12):
        print(f"    {n:>3}  {s}")


if __name__ == "__main__":
    main()
