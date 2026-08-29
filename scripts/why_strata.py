#!/usr/bin/env python3
"""WHERE does each card do well, and where does it fall short -- from replayed game logs.

Takes the index.json why_card.py writes (a set of game pairs replayed under two decklists, each
verified against its recorded win turn) and slices the A-B delta by things that might explain it:

  * game LENGTH -- does the card earn its slot in fast kills or in grindy ones?
  * whether the distinctive card was actually CAST (a card that never appears cannot be the cause)
  * the TURN it was cast
  * how much of the game it was on the battlefield for

The conditional slices are only meaningful on an UNBIASED sample (why_card --sample random).
A divergence-selected pool is balanced across both tails by construction, so its conditional means
are selected rather than estimated -- on the Libation question that difference was +0.214 vs +0.000.

Usage: why_strata.py <index.json> [--card-a NAME] [--card-b NAME]
"""
import argparse, collections, json, statistics as st
from pathlib import Path


def load(p):
    return json.load(open(p)) if p and Path(p).exists() else None


def casts(trace):
    """[(turn, cardName)] for every spell actually cast."""
    out = []
    for e in trace.get("turns", []):
        for a in e.get("actions", []):
            if a.get("type") == "CAST_SPELL":
                out.append((e.get("turn"), a.get("cardName")))
    return out


def row(label, pairs):
    if not pairs:
        return f"  {label:<34} {0:>6}" + " " * 28
    d = [x - y for x, y in pairs]
    se = (st.pstdev(d) / len(d) ** 0.5) if len(d) > 1 else 0.0
    a = st.mean(x for x, _ in pairs); b = st.mean(y for _, y in pairs)
    t = (st.mean(d) / se) if se else 0.0
    return (f"  {label:<34} {len(pairs):>6} {a:>8.3f} {b:>8.3f} {st.mean(d):>+8.3f}"
            f" {se:>7.3f} {t:>+6.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("index")
    ap.add_argument("--card-a", default=None, help="card that only arm A holds")
    ap.add_argument("--card-b", default=None, help="card that only arm B holds")
    args = ap.parse_args()

    idx = json.load(open(args.index))
    a, b, MT = idx["a"], idx["b"], idx["max_turns"]
    rows = [r for r in idx["games"] if r["reproduced"]]
    G = []
    for r in rows:
        ta, tb = load(r["logs"][a]["path"]), load(r["logs"][b]["path"])
        if not ta or not tb:
            continue
        G.append({"wa": r["recorded"][a], "wb": r["recorded"][b],
                  "ca": casts(ta), "cb": casts(tb)})

    print(f"{a}  (A)   vs   {b}  (B)      {len(G)} verified pairs, max_turns {MT}")
    print(f"  {'stratum':<34} {'pairs':>6} {'A':>8} {'B':>8} {'A-B':>8} {'se':>7} {'t':>6}")
    print(f"  {'-'*34} {'-'*6} {'-'*8} {'-'*8} {'-'*8} {'-'*7} {'-'*6}")
    P = lambda g: (g["wa"], g["wb"])
    print(row("ALL", [P(g) for g in G]))

    print(f"\n  -- by GAME LENGTH (bucketed on the FASTER of the two arms, so the bucket is a")
    print(f"     property of the GAME, not of either list) --")
    def faster(g): return min(g["wa"], g["wb"])
    for lo, hi, lab in ((0, 4, "fast   (<=T4)"), (5, 5, "T5"), (6, 6, "T6"),
                        (7, 8, "T7-T8"), (9, 99, "grindy (T9+ / unwon)")):
        print(row(lab, [P(g) for g in G if lo <= faster(g) <= hi]))

    for who, card, key in ((a, args.card_a, "ca"), (b, args.card_b, "cb")):
        if not card:
            continue
        print(f"\n  -- conditioned on {who} casting {card} --")
        hit = [g for g in G if any(n == card for _, n in g[key])]
        print(row(f"cast {card}", [P(g) for g in hit]))
        print(row(f"never cast {card}", [P(g) for g in G if g not in hit]))
        if hit:
            turns = collections.Counter(min(t for t, n in g[key] if n == card) for g in hit)
            print(f"\n     first cast on turn:  " +
                  "  ".join(f"T{k}:{turns[k]}" for k in sorted(turns)))
            for k in sorted(turns):
                sub = [g for g in hit if min(t for t, n in g[key] if n == card) == k]
                print(row(f"  first cast T{k}", [P(g) for g in sub]))


if __name__ == "__main__":
    main()
