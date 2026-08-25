#!/usr/bin/env python3
"""Classify StompySurprise cast-order counterexamples against the USER's written tier list.

WHY. The reviewed order (MTG_STOMPY_ORDER) wins on aggregate but deletes reachable wins (gate 2).
Two of those deletions came from clauses the engine ADDED beyond the written list, and restoring the
list recovered them (MTG_STOMPY_WT_LITERAL / MTG_STOMPY_TT_LITERAL). This tool answers the next
question the USER asked: for the games the literal order still loses, WHAT in the order does not
work?

METHOD. Diff the committed pre-combat cast sequence (MTG_ORDER_TRACE's `[ord]` lines) between the
arm that wins EARLIER (order off -> the generic order) and the literal-order arm, find the FIRST
turn they disagree, and classify it:

  * SAME SET, different sequence -> a pure ORDERING loss: the tier list ranked two cards the wrong
    way round on this board. Reported as "[a] before [b]" vs the winning "[b] before [a]", in the
    USER's own tier numbers, which is the form a ruling can be made in.
  * DIFFERENT SET -> not an ordering loss at that turn: the order changed which PLAN was picked
    (usually via an earlier turn), so the counterexample is about plan selection, not sequence.

Reading the output: group by the tier PAIR. A pair that recurs is a rule to revise; a pair that
appears once is a board-specific special case.

    python3 scripts/stompy_order_counterexamples.py logs/ord26
"""
import collections
import os
import re
import sys

# The USER's tier numbers, from docs/design/cast-order-rankings.md ("The USER's proposal, verbatim").
# Keyed by card name because the tier list is the thing under review -- reading params here would
# report what the ENGINE thinks the tier is, which is the very thing being audited.
TIER = {
    "Arbor Elf": (1, "1-mana elf"), "Elvish Mystic": (1, "1-mana elf"),
    "Llanowar Elves": (1, "1-mana elf"),
    "Sol Ring": (2, "Sol Ring"),
    "Priest of Titania": (3, "scaling elf (cheap)"),
    "Elvish Archdruid": (4, "scaling elf"),
    "Call of the Wild": (5, "Call of the Wild"),
    "Turntimber Symbiosis": (7, "Turntimber"),
    "Natural Order": (8, "Natural Order"),
    "Worldly Tutor": (9, "Worldly Tutor"),
    "Vaultborn Tyrant": (10, "Vaultborn"),
    "Elderscale Wurm": (11, "fatty"), "Hornet Queen": (11, "fatty"),
    "Terastodon": (11, "fatty"), "Worldspine Wurm": (11, "fatty"),
    "Craterhoof Behemoth": (14, "Craterhoof"),
    "Mirri's Guile": (15, "Mirri's Guile"),
}


def tier(name):
    return TIER.get(name, (99, name))


def read_trace(path):
    """-> {turn: [card names in committed cast order]}"""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        m = re.match(r"\[ord\] turn=(\d+) searched=\d+ opaque=\d+ casts: (.*)$", line)
        if not m:
            continue
        casts = [] if m.group(2).strip() == "(none)" else \
                [c.strip().replace("(alt)", "") for c in m.group(2).split(",")]
        out[int(m.group(1))] = casts
    return out


def read_wt(path):
    if not os.path.exists(path):
        return None
    for line in open(path):
        m = re.search(r"avg \(turns\)\s*:\s*([\d.]+)", line)
        if m:
            return float(m.group(1))
    return None


def first_divergence(a, b):
    """First turn present in both whose cast list differs. -> (turn, a_casts, b_casts) | None"""
    for t in sorted(set(a) & set(b)):
        if a[t] != b[t]:
            return t, a[t], b[t]
    return None


def inversions(win, lose):
    """Tier pairs the LOSING order got backwards relative to the WINNING order (same multiset)."""
    pos_w = {}
    for i, c in enumerate(win):
        pos_w.setdefault(c, []).append(i)
    pairs = set()
    for i in range(len(lose)):
        for j in range(i + 1, len(lose)):
            ci, cj = lose[i], lose[j]
            if ci == cj or tier(ci)[0] == tier(cj)[0]:
                continue
            # ci is before cj in the losing order; inverted if every ci sits after every cj in win.
            if pos_w.get(ci) and pos_w.get(cj) and min(pos_w[ci]) > max(pos_w[cj]):
                pairs.add((tier(cj), tier(ci)))   # (should-be-first, should-be-second)
    return pairs


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "logs/ord26"
    games = sorted({f.split(".")[0] for f in os.listdir(d) if f.endswith(".err")})
    rows, pair_counts, kinds = [], collections.Counter(), collections.Counter()

    for g in games:
        base, lit = read_trace(f"{d}/{g}.base.err"), read_trace(f"{d}/{g}.lit.err")
        wt_b, wt_l, wt_s = (read_wt(f"{d}/{g}.{a}.out") for a in ("base", "lit", "so"))
        div = first_divergence(base, lit)
        if div is None:
            kinds["no divergence in traced turns"] += 1
            rows.append((g, wt_b, wt_l, wt_s, "-", "no [ord] divergence", ""))
            continue
        t, cb, cl = div
        if sorted(cb) == sorted(cl):
            kind = "ORDERING"
            pairs = inversions(cb, cl)
            for p in pairs:
                pair_counts[p] += 1
            detail = "; ".join(f"[{a[0]}] {a[1]} BEFORE [{b[0]}] {b[1]}"
                               for a, b in sorted(pairs)) or "(same set, no tier inversion)"
        else:
            kind = "PLAN"
            only_b = sorted(set(cb) - set(cl))
            only_l = sorted(set(cl) - set(cb))
            detail = f"winning line casts {only_b or '-'}; order's line casts {only_l or '-'}"
        kinds[kind] += 1
        rows.append((g, wt_b, wt_l, wt_s, t, kind, detail))

    w = max(len(r[0]) for r in rows) if rows else 10
    print(f"{'game':{w}s} {'base':>5s} {'lit':>5s} {'so':>5s} {'T':>3s}  kind      what the order did")
    print("-" * 120)
    for g, wb, wl, ws, t, kind, detail in rows:
        f = lambda v: "-" if v is None else f"{v:.0f}"
        print(f"{g:{w}s} {f(wb):>5s} {f(wl):>5s} {f(ws):>5s} {str(t):>3s}  {kind:9s} {detail}")

    print("\n=== FIRST-DIVERGENCE KIND ===")
    for k, n in kinds.most_common():
        print(f"  {n:3d}  {k}")

    if pair_counts:
        print("\n=== TIER INVERSIONS, most recurrent first ===")
        print("    (the winning line puts the FIRST tier before the SECOND; the order does the reverse)")
        for (a, b), n in pair_counts.most_common():
            print(f"  {n:3d}x  [{a[0]:2d}] {a[1]:22s} BEFORE  [{b[0]:2d}] {b[1]}")


if __name__ == "__main__":
    main()
