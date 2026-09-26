#!/usr/bin/env python3
"""Collect the Stage-6a disclosure inputs for WhiteKnights straight out of the tree.

The skill is explicit that the disclosure must be READ FROM THE CODE, not written from memory,
so this prints the three machine-readable sources verbatim:

  1. every bracket note in the deck's own cards.json entries (card-modeling simplifications),
  2. every non-default CardParams field the deck's cards set (what actually shapes play),
  3. the deck's profile settings the numbers were produced at (depth / budget / scores).

Usage: python3 scripts/wk_stage6_disclosure.py [decks/WhiteKnights/WhiteKnights.cod]
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "decks/WhiteKnights/WhiteKnights.cod"


def decklist_names(path: Path):
    """Mainboard names + counts from a .cod (XML) or .txt decklist, in list order."""
    text = path.read_text()
    out = []
    if path.suffix == ".cod":
        # <card number="4" price="0" name="Plains"/> inside <zone name="main">
        main = text.split('<zone name="main"', 1)[-1].split("</zone>", 1)[0]
        for m in re.finditer(r'<card number="(\d+)"[^>]*name="([^"]+)"', main):
            out.append((int(m.group(1)), m.group(2)))
    else:
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("//") or line.lower().startswith("sideboard"):
                continue
            m = re.match(r"^(\d+)\s*x?\s+(.*)$", line)
            if m:
                out.append((int(m.group(1)), m.group(2).strip()))
    return out


def main():
    cards = json.load(open(ROOT / "src/cards/data/cards.json"))
    if isinstance(cards, dict):
        cards = cards.get("cards", [])
    by_name = {c["name"]: c for c in cards}

    entries = decklist_names(DECK)
    print(f"# Stage 6a disclosure inputs -- {DECK}")
    print(f"# {sum(n for n, _ in entries)} cards, {len(entries)} distinct\n")

    print("## 1. Bracket notes in this deck's cards.json entries (verbatim)\n")
    any_note = False
    for n, name in entries:
        c = by_name.get(name)
        if not c:
            print(f"  !! {name}: NOT IN cards.json")
            continue
        # The notes are tagged by CATEGORY, not with a literal "bracket note:" prefix --
        # [PARTIAL: ...], [SIMPLIFIED: ...], [DEFERRED: ...] and friends. Match the shape
        # (a leading ALL-CAPS tag then a colon) so a new tag is picked up rather than silently
        # dropped from a disclosure whose whole point is completeness.
        for note in re.findall(r"\[[A-Z][A-Z /-]*:.*?\]", c.get("oracle_text", ""), re.S):
            any_note = True
            print(f"* **{name}** ({n}x)")
            print(f"  {' '.join(note.split())}\n")
    if not any_note:
        print("  (none)\n")

    print("## 2. Every param the deck's cards set\n")
    # Print EVERY param present, including ones set to false/0. Filtering on falsiness was wrong:
    # a param deliberately written as `false` is the most disclosure-worthy kind there is -- Hero of
    # Bladehold's `attack_tokens_per_opponent: false` IS the 2HG flat-count fix, and a falsy filter
    # hid exactly it. Presence in cards.json is the signal that a human chose the value.
    for n, name in entries:
        c = by_name.get(name)
        if not c:
            continue
        params = c.get("parameters", {}) or {}
        if params:
            print(f"* **{name}** ({n}x): " + ", ".join(f"`{k}={v}`" for k, v in sorted(params.items())))
    print()

    print("## 3. Profile the numbers were produced at\n")
    prof_path = DECK.parent / (DECK.stem + ".profile.json")
    if prof_path.exists():
        prof = json.load(open(prof_path))
        for k in ("play_depth", "play_budget_ms", "depth", "budget_ms", "search_depth",
                  "second_main", "uses_second_main", "mulligan", "bottoming_enabled"):
            if k in prof:
                print(f"* `{k}`: {prof[k]}")
        scores = prof.get("card_scores") or {}
        if scores:
            # A score is a LIST -- one entry per additional copy (marginal value of the 1st copy,
            # the 2nd, ...), so a card can be worth keeping once and not twice. Rank on the FIRST
            # copy and print the whole vector, because the tail is where a redundancy story lives.
            def first(v):
                return float(v[0]) if isinstance(v, list) else float(v)
            print(f"\n### card_scores ({len(scores)} entries), by first-copy value\n")
            print("| card | per-copy marginal score |")
            print("|---|---|")
            for name, sc in sorted(scores.items(), key=lambda kv: -first(kv[1])):
                vec = sc if isinstance(sc, list) else [sc]
                print(f"| {name} | " + ", ".join(f"{float(x):+.4f}" for x in vec) + " |")
        # anything else top-level, so nothing is silently dropped from the disclosure
        rest = [k for k in prof if k not in ("card_scores",)]
        print(f"\n### all top-level profile keys\n\n{sorted(rest)}")
    else:
        print(f"  !! no profile at {prof_path}")


if __name__ == "__main__":
    main()
