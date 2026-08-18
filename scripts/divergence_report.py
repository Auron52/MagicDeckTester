#!/usr/bin/env python3
"""Tier-1 divergence analysis: WHY did two paired arms end on different win turns?

Selection rule (user, 2026-08-18): a game is in scope iff the two arms END ON DIFFERENT
WIN TURNS. Everything else here is a derived diagnostic computed INSIDE that set.

The point of the separate `same_cards` class is the user's sharpened invariant: when both
arms held the same indexed cards, the win turn should be IDENTICAL. The shuffle is seeded
on card numbers and `deck_compare`'s replace map keeps shared cards at the same numbers, so
a game that never touches a swapped number plays a literally identical card sequence in
both arms. A win-turn difference there cannot be the cards -- it is apparatus or a bug.

Usage:  python3 scripts/divergence_report.py <tracedir> <armA> <armB> [--swapped N,N,...]
"""
import json, os, sys, glob, collections, argparse


def load(tracedir, arm):
    out = {}
    for p in glob.glob(os.path.join(tracedir, f"{arm}_gi*.json")):
        gi = int(os.path.basename(p).rsplit("_gi", 1)[1][:-5])
        out[gi] = json.load(open(p))
    return out


def seen_numbers(g):
    """Every card NUMBER this game actually put in hand: opening hand + every DRAW."""
    s = {c["card"] for c in g.get("openingHand", [])}
    for t in g["turns"]:
        for a in t.get("actions", []):
            if a.get("type") == "DRAW":
                s.add(a["card"])
    return s


def casts(g):
    """Ordered [(turn, cardName, manaPaid)] of CAST_SPELL."""
    out = []
    for t in g["turns"]:
        for a in t.get("actions", []):
            if a.get("type") == "CAST_SPELL":
                out.append((t["turn"], a["cardName"], a.get("manaPaid", "")))
    return out


def action_sig(g):
    """Per-turn action signature, for locating the first turn the lines diverge."""
    return {t["turn"]: [(a.get("type"), a.get("cardName"), a.get("manaPaid", ""))
                        for a in t.get("actions", [])] for t in g["turns"]}


def first_divergence(a, b):
    sa, sb = action_sig(a), action_sig(b)
    for t in sorted(set(sa) | set(sb)):
        if sa.get(t) != sb.get(t):
            return t
    return None


def mull_count(g):
    return max(0, len(g.get("mulliganSequence", [])) - 1)


def win_turn(g, max_turns):
    r = g.get("result", {}) or {}
    t = r.get("turn", -1)
    return max_turns + 1 if (t is None or t < 0 or t > max_turns) else t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracedir")
    ap.add_argument("armA")
    ap.add_argument("armB")
    ap.add_argument("--swapped", default="")
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--show", type=int, default=12)
    a = ap.parse_args()
    SW = {int(x) for x in a.swapped.split(",") if x.strip()}

    A, B = load(a.tracedir, a.armA), load(a.tracedir, a.armB)
    gis = sorted(set(A) & set(B))
    if not gis:
        sys.exit("no paired games found")

    div, same_cards_div, rows = [], [], []
    same_cards_n = 0
    for gi in gis:
        ga, gb = A[gi], B[gi]
        wa, wb = win_turn(ga, a.max_turns), win_turn(gb, a.max_turns)
        na, nb = seen_numbers(ga), seen_numbers(gb)
        # "Same indexed cards": neither arm ever drew a swapped number, AND both saw the
        # same numbers. Then the two arms held identical cards all game.
        touched = bool((na | nb) & SW)
        same_cards = (not touched) and na == nb
        if same_cards:
            same_cards_n += 1
        if wa == wb:
            continue
        d = wb - wa
        ca, cb = casts(ga), casts(gb)
        swa = sorted({n for _, n, _ in ca} & SWAP_NAMES) if SWAP_NAMES else []
        swb = sorted({n for _, n, _ in cb} & SWAP_NAMES) if SWAP_NAMES else []
        row = dict(gi=gi, wa=wa, wb=wb, d=d, fdt=first_divergence(ga, gb),
                   mulla=mull_count(ga), mullb=mull_count(gb),
                   # Compare by card NUMBER, never by name: the replace map makes number 41
                   # "Ancestral Anger" in one arm and "Impolite Entrance" in the other, so a
                   # name comparison reports an IDENTICAL keep as a different hand and blames
                   # the apparatus for what is actually the edit.
                   same_hand=(sorted(c["card"] for c in ga.get("openingHand", []))
                              == sorted(c["card"] for c in gb.get("openingHand", []))),
                   same_cards=same_cards, touched=touched,
                   casts_a=swa, casts_b=swb)
        div.append(row)
        if same_cards:
            same_cards_div.append(row)
        rows.append(row)

    n = len(gis)
    print(f"paired games            : {n:,}")
    print(f"diverged on win turn    : {len(div):,}  ({100*len(div)/n:.1f}%)")
    print()
    print("=== the user's invariant: same indexed cards => same win turn ===")
    print(f"  games where BOTH arms held the same cards all game : {same_cards_n:,}"
          f"  ({100*same_cards_n/n:.1f}% of games)")
    if same_cards_n:
        print(f"  ...of those, DIVERGED on win turn                  : {len(same_cards_div):,}"
              f"  ({100*len(same_cards_div)/same_cards_n:.2f}%)   <- should be ~0")
    print()

    mull_diff = [r for r in div if r["mulla"] != r["mullb"] or not r["same_hand"]]
    print("=== contamination: divergences that start before a card is played ===")
    print(f"  different mulligan count or different opening hand : {len(mull_diff):,}"
          f"  ({100*len(mull_diff)/max(1,len(div)):.1f}% of divergences)")
    untouched = [r for r in div if not r["touched"]]
    print(f"  no swapped card ever drawn by EITHER arm           : {len(untouched):,}"
          f"  ({100*len(untouched)/max(1,len(div)):.1f}% of divergences)")
    print()

    print("=== first-divergence turn (inside diverged set) ===")
    h = collections.Counter(r["fdt"] for r in div)
    for k in sorted(h, key=lambda x: (x is None, x)):
        print(f"  turn {k}: {h[k]:5,}")
    print()

    print("=== per-card attribution (which swapped cards the arms actually CAST) ===")
    att = collections.defaultdict(lambda: [0, 0, 0])   # card -> [games, sum_delta, faster]
    for r in div:
        for c in set(r["casts_a"]) | set(r["casts_b"]):
            att[c][0] += 1
            att[c][1] += r["d"]
            att[c][2] += 1 if r["d"] < 0 else 0
    print(f"  {'card':26s} {'games':>7s} {'mean delta':>11s} {'B faster%':>10s}")
    for c in sorted(att, key=lambda k: att[k][1] / max(1, att[k][0])):
        g, s, f = att[c]
        print(f"  {c:26s} {g:7,} {s/g:+11.3f} {100*f/g:9.1f}%")
    print("  (delta = armB - armA in turns; negative = B wins sooner)")
    print()

    print(f"=== largest |delta| games (repro: --seed <seed> --game-index <gi> --games 1) ===")
    for r in sorted(div, key=lambda r: -abs(r["d"]))[:a.show]:
        print(f"  gi={r['gi']:<6d} {a.armA}=T{r['wa']} {a.armB}=T{r['wb']} "
              f"(d={r['d']:+d}) firstdiff=T{r['fdt']} mull={r['mulla']}/{r['mullb']} "
              f"sameHand={'Y' if r['same_hand'] else 'N'} "
              f"A_cast={','.join(r['casts_a']) or '-'} B_cast={','.join(r['casts_b']) or '-'}")

    json.dump(rows, open(os.path.join(a.tracedir, "divergence_rows.json"), "w"), indent=1)
    print(f"\nrows -> {os.path.join(a.tracedir, 'divergence_rows.json')}")


SWAP_NAMES = {"Ancestral Anger", "Expedite", "Scale the Heights", "Twinflame",
              "Impolite Entrance", "Fortifying Draught", "Luxurious Libation"}

if __name__ == "__main__":
    main()
