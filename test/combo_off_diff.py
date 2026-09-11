#!/usr/bin/env python3
"""Diff two COMBO OFF result sets (sweep or replay hunt) on their STABLE state ids.

Both `test/combo_off_sweep.py` and `test/combo_off_replay_hunt.py` write
`{"results": [{"id": ..., "class": ..., "offer": {...}}, ...]}` with content-addressed ids, which
is precisely so a before/after pair can be compared state by state rather than in aggregate.  An
aggregate class table hides the two things a fixer needs: how many states MOVED, and in which
direction (a net +0 can be 12 gained and 12 lost).

    python3 test/combo_off_diff.py BEFORE.json AFTER.json [--show N]

Reports, on the intersection of the two id sets:
  * the class transition matrix,
  * the offered/verified/wins agreement rates on each side,
  * per-rule offers and win rates on each side,
  * and up to `--show` example ids per non-trivial transition.
"""
import json
import sys
import argparse


def load(path):
    """Accepts BOTH result shapes.

    `combo_off_sweep.py` writes {"results": [...]} with the offer under `offer`; the replay hunt
    writes {"states": [...]} with `offered` / `rule` / `verified` at the top level and the click's
    outcome under `click`.  Normalised here so one tool can diff either, which matters because the
    same fix has to be reported against both populations.
    """
    with open(path) as f:
        d = json.load(f)
    rows = d.get("results")
    if rows is None:
        rows = d.get("states") or []
    out = {}
    for r in rows:
        rid = r.get("id")
        if rid:
            out[rid] = r
    return out


def offer_of(r):
    o = r.get("offer")
    if isinstance(o, dict):
        return o
    click = r.get("click") or {}
    return {"offered": r.get("offered"), "rule": r.get("rule"), "verified": r.get("verified"),
            "apply_win": click.get("won_this_turn")}


def won(r):
    """Did the offered line win THIS turn?"""
    return bool(offer_of(r).get("apply_win"))


def agreement(rows):
    """offered-vs-wins and verified-vs-wins, the two numbers the user asks for."""
    off = [r for r in rows if offer_of(r).get("offered")]
    off_win = [r for r in off if won(r)]
    ver = [r for r in off if offer_of(r).get("verified")]
    ver_win = [r for r in ver if won(r)]
    # a MISSED offer is a state that was not offered and whose line nevertheless won that turn
    missed = [r for r in rows if r.get("class") == "c_missed_offer"]
    return dict(states=len(rows), offered=len(off), offered_wins=len(off_win),
                verified=len(ver), verified_wins=len(ver_win), missed=len(missed))


def pct(a, b):
    return (100.0 * a / b) if b else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--show", type=int, default=4)
    args = ap.parse_args()

    b, a = load(args.before), load(args.after)
    ids = sorted(set(b) & set(a))
    print("before %d states, after %d states, %d shared"
          % (len(b), len(a), len(ids)))
    only_b, only_a = sorted(set(b) - set(a)), sorted(set(a) - set(b))
    if only_b or only_a:
        print("  (only-before %d, only-after %d -- NOT compared)" % (len(only_b), len(only_a)))

    trans = {}
    for i in ids:
        k = (b[i].get("class"), a[i].get("class"))
        trans.setdefault(k, []).append(i)
    print("\n-- class transitions (shared states) --")
    for k in sorted(trans, key=lambda x: -len(trans[x])):
        mark = "   " if k[0] == k[1] else "***"
        print("  %s %-26s -> %-26s %5d" % (mark, k[0], k[1], len(trans[k])))
        if k[0] != k[1] and args.show:
            for i in trans[k][:args.show]:
                o = offer_of(a[i])
                print("        %s  rule=%-12s verified=%s win=%s"
                      % (i, o.get("rule") or "-", o.get("verified"), o.get("apply_win")))

    print("\n-- agreement --")
    for name, src in (("before", b), ("after", a)):
        g = agreement([src[i] for i in ids])
        print("  %-7s offered %4d  of those WIN %4d (%.1f%%)   verified %4d  of those WIN %4d "
              "(%.1f%%)   missed-offer %d"
              % (name, g["offered"], g["offered_wins"], pct(g["offered_wins"], g["offered"]),
                 g["verified"], g["verified_wins"], pct(g["verified_wins"], g["verified"]),
                 g["missed"]))

    print("\n-- offers per rule (shared states) --")
    rules = set()
    per = {}
    for name, src in (("before", b), ("after", a)):
        d = {}
        for i in ids:
            o = offer_of(src[i])
            if not o.get("offered"):
                continue
            rl = o.get("rule") or "(EMPTY)"
            e = d.setdefault(rl, [0, 0])
            e[0] += 1
            e[1] += 1 if won(src[i]) else 0
        per[name] = d
        rules |= set(d)
    print("  %-14s %-18s %-18s" % ("rule", "before off/win", "after off/win"))
    for rl in sorted(rules):
        bb = per["before"].get(rl, [0, 0])
        aa = per["after"].get(rl, [0, 0])
        print("  %-14s %-18s %-18s" % (rl, "%d/%d" % (bb[0], bb[1]), "%d/%d" % (aa[0], aa[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
