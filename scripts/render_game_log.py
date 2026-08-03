#!/usr/bin/env python3
"""Render a game log JSON as a readable turn-by-turn transcript.

The engine's `--log-dir` output is a machine format (card numbers, board snapshots per phase).
This prints what actually happened in a form you can read: one line per action, with the
board state that resulted, so two runs of the same seed can be compared side by side.

Usage:  scripts/render_game_log.py <game_log.json> [--tag LABEL]
"""
import argparse
import glob
import json
import sys


def name_of(numbering, num):
    for card_name, nums in numbering.items():
        if num in nums:
            return card_name
    return f"#{num}"


def render(path, tag=None):
    d = json.load(open(path))
    numbering = d.get("cardNumbering", {})
    out = []
    res = d.get("result", {})
    head = f"seed {d.get('seed')}  ->  {res.get('winner','?')} on turn {res.get('turn','?')}"
    out.append(f"=== {tag+'  ' if tag else ''}{head} ===")

    hand = ", ".join(c["cardName"] for c in d.get("openingHand", []))
    out.append(f"  opening hand: {hand}")

    last_turn = None
    for seg in d.get("turns", []):
        turn = seg.get("turn")
        if turn != last_turn:
            out.append(f"  --- turn {turn} ---")
            last_turn = turn
        for a in seg.get("actions", []):
            t = a.get("type", "?")
            cn = a.get("cardName") or name_of(numbering, a.get("card", -1))
            extra = ""
            if a.get("manaPaid"):
                extra += f"  [{a['manaPaid']}]"
            # An aura attaching / a tutor fetch names its target or source where the log has one.
            for k in ("target", "targetName", "attachedTo", "source", "sourceName"):
                if a.get(k):
                    v = a[k]
                    v = v if isinstance(v, str) else name_of(numbering, v)
                    extra += f"  -> {v}"
                    break
            if t == "DRAW":
                continue  # draws are noise for a play comparison
            if t == "ATTACK":
                # ATTACK carries no card, so the generic name_of() lookup rendered it as "#-1".
                out.append(f"    {'ATTACK':<16} {a.get('damage', '?')} damage"
                           f"  -> opp life {a.get('oppLife', '?')}")
                continue
            if t == "REVEAL":
                # The log must read like the viewer history (user, 2026-08-03: "what we would show in
                # the viewer history should show in the log"). Same derivation as EmitReveal /
                # RevealDisposition: explicit `to` wins, else kept / bottomed decide.
                kept, bottomed = set(a.get("kept", [])), set(a.get("bottomed", []))
                shown = []
                for c in a.get("lookedAt", []):
                    lbl = c.get("to") or ("kept" if c.get("card") in kept else
                                          "to the bottom" if c.get("card") in bottomed else "")
                    shown.append(f"{c.get('cardName', '?')}" + (f" ({lbl})" if lbl else ""))
                out.append(f"    {'REVEAL':<16} {a.get('source', '?')}: {', '.join(shown)}")
                continue
            out.append(f"    {t:<16} {cn}{extra}")
        b = seg.get("boardAfter") or {}
        if seg.get("phase") in ("COMBAT", "END", "MAIN2") and b:
            bf = b.get("battlefield", [])
            creatures = [c for c in bf if not c.get("isLand")]
            names = ", ".join(c.get("cardName", "?") for c in creatures) or "(empty)"
            out.append(f"      board: {names}   |  opp life {b.get('opponentLife')}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--tag", default=None)
    args = ap.parse_args()
    p = args.path
    if "*" in p:
        matches = sorted(glob.glob(p))
        if not matches:
            print(f"no log matching {p}", file=sys.stderr)
            return 1
        p = matches[0]
    print(render(p, args.tag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
