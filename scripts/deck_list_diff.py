#!/usr/bin/env python3
"""Diff two decklists' MAIN zones: the shared core, the cards added, the cards removed (stdlib only).

    python3 scripts/deck_list_diff.py <old.cod> <new.cod> [--md]

The shared core counts each card at min(old, new) copies, so core + added == new and core + removed ==
old (asserted). --md prints the three Markdown tables the deck-update summary pages use (core three
columns wide). Read the lists from the .cod files, never from a document's text: this is the check that
a hand-written added/removed table is complete (.claude/skills/deck-update-summary.md).
"""
import sys
import xml.etree.ElementTree as ET
from collections import Counter


def main_zone(path):
    c = Counter()
    for z in ET.parse(path).getroot().iter("zone"):
        if z.get("name") == "main":
            for card in z.iter("card"):
                c[card.get("name")] += int(card.get("number"))
    return c


def order(d):
    return sorted(d.items(), key=lambda kv: (-kv[1], kv[0]))


def core_table(core, cols=3):
    items = [f"| {n} | {k} " for k, n in order(core)]
    rows = (len(items) + cols - 1) // cols
    out = "| n | card " * cols + "|\n" + "|---|---" * cols + "|\n"
    for r in range(rows):
        out += "".join(items[c * rows + r] if c * rows + r < len(items) else "| | " for c in range(cols)) + "|\n"
    return out


def change_table(changes, old, new, sign):
    out = "| | card | old → new |\n|---|---|---|\n"
    for k, n in order(changes):
        out += f"| **{sign}{n}** | {k} | {old.get(k, 0)} → {new.get(k, 0)} |\n"
    return out


def run(old_path, new_path, md):
    old, new = main_zone(old_path), main_zone(new_path)
    core = {k: min(old[k], new[k]) for k in old if new.get(k)}
    added = {k: new[k] - old.get(k, 0) for k in new if new[k] > old.get(k, 0)}
    removed = {k: old[k] - new.get(k, 0) for k in old if old[k] > new.get(k, 0)}
    assert sum(core.values()) + sum(added.values()) == sum(new.values())
    assert sum(core.values()) + sum(removed.values()) == sum(old.values())
    if md:
        print(f"## Shared core — {sum(core.values())} of {sum(new.values())} cards in both lists\n")
        print(core_table(core))
        print("## Cards added\n")
        print(change_table(added, old, new, "+"))
        print("## Cards removed\n")
        print(change_table(removed, old, new, "−"))
        return
    print(f"old={sum(old.values())} new={sum(new.values())} core={sum(core.values())} "
          f"(+{sum(added.values())} / -{sum(removed.values())})")
    print("CORE:   ", "; ".join(f"{n} {k}" for k, n in order(core)))
    print("ADDED:  ", "; ".join(f"+{n} {k}" for k, n in order(added)))
    print("REMOVED:", "; ".join(f"-{n} {k}" for k, n in order(removed)))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--md"]
    if len(args) != 2:
        sys.exit(__doc__)
    run(args[0], args[1], "--md" in sys.argv)
