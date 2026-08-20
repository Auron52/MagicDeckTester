#!/usr/bin/env python3
"""Census: is a card DRAWN MID-MAIN-PHASE ever actually spent in that same main phase?

This is the measurement that identified the equipment-ETB breakpoint bug and it is the one that
confirms the fix, so it is a script rather than an ad-hoc pipe.

A draw engine's whole point is that the revealed card is castable with the mana still open. The
engine models that with a BREAKPOINT: the phase re-solves post-draw. If no breakpoint arms, the card
is simply stranded until next turn -- and the failure is SILENT, because the game still plays a
perfectly sensible turn without it. Nothing in the win-turn average says "we drew a Sol Ring with
three mana open and did not cast it".

The direct test: inside one MAIN_1/MAIN_2 phase entry, is there a DRAW action followed by a
CAST_SPELL / PLAY_LAND of that same card number? Draws in the DRAW phase (the natural draw) are not
counted -- they precede the main phase and any plan can already use them.

    python3 drawn_card_used.py <log-dir> [<log-dir> ...]

Prints, per directory: games, mid-main draws, how many were spent in the SAME phase, and the games
in which that happened. Before the fix the "spent" column is 0 by construction.

NOTE: mid-main draws are only visible here if the log's own draw reporter classifies them --
GameEngine::ResolveStack had the identical param-keyed blind spot and never logged a Puresteel draw
at all. If the draws column reads 0 on a deck you know draws, suspect the reporter, not the deck.
"""
import json
import pathlib
import sys

MAIN = ("MAIN_1", "MAIN_2")


def census(root):
    games = draws = used = 0
    games_with_use = set()
    per_phase = {}
    for path in sorted(pathlib.Path(root).glob("*.json")):
        try:
            doc = json.loads(path.read_text())
        except (ValueError, OSError):
            continue
        if "turns" not in doc:
            continue
        games += 1
        for entry in doc["turns"]:
            if entry.get("phase") not in MAIN:
                continue
            drawn = set()
            for act in entry.get("actions", []):
                kind = act.get("type")
                if kind == "DRAW":
                    drawn.add(act.get("card"))
                    draws += 1
                elif kind in ("CAST_SPELL", "PLAY_LAND") and act.get("card") in drawn:
                    used += 1
                    games_with_use.add(path.name)
                    per_phase[entry["phase"]] = per_phase.get(entry["phase"], 0) + 1
    return games, draws, used, len(games_with_use), per_phase


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print(f"{'log dir':<34} {'games':>6} {'mid-main draws':>15} {'spent same phase':>17} "
          f"{'games':>6}  by phase")
    for root in sys.argv[1:]:
        g, d, u, gu, pp = census(root)
        detail = ", ".join(f"{k}={v}" for k, v in sorted(pp.items())) or "-"
        print(f"{root:<34} {g:>6} {d:>15} {u:>17} {gu:>6}  {detail}")


if __name__ == "__main__":
    main()
