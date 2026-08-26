#!/usr/bin/env python3
"""Emit ONE pooled manifest that re-plays every CHANGED game at escalated budget and depth.

This is gate 2 of the standing method (docs/design/searched-design-deck-rollout.md §5 trap 1):

  "Unrecoverable" requires escalating BOTH budget AND depth. A budget-only escalation proved
  nothing twice -- 10000x budget moved nothing while ONE extra depth ply fixed two of three games.

The point is to separate the two reasons a searched-instead-of-greedy arm can lose:

  * BUDGET DILUTION -- the arm's extra interior search spends from the SAME per-decision allowance,
    so the outer candidate loop gets fewer candidates. Recovers when the budget grows.
  * A WORSE DECISION -- the arm genuinely prefers a worse line. Survives any budget and any depth.

Both arms are escalated together at every cell: escalating only the arm compares two different
configurations and answers nothing. Games are named "<deck>_<block>_g<gi>" so the per-game .wins
rows line up across arms.

Usage: gen_escalate_manifest.py <paired-wins-dir> <baseline-arm> <test-arm> > manifest.json
"""
import json
import pathlib
import sys

MAX_TURNS = 8

DECKS = {
    "al":    ("decks/Anti-Lifegain/Anti-Lifegain.cod",
              "decks/Anti-Lifegain/Anti-Lifegain.profile.json", 5),
    "kitty": ("decks/KittyEquipment/KittyEquipment.cod",
              "decks/KittyEquipment/KittyEquipment.profile.json", 5),
    "5c":    ("decks/FiveColour/FiveColour.cod",
              "decks/FiveColour/FiveColour.profile.json", 6),
}
BLOCKS = {"train": 400001, "hold": 950001}

# A FiveColour game already runs to 100 s at the shipped b20, so a 100x cell would be hours per
# game. Cap its escalation at 10x and say so rather than silently dropping the cell.
CELLS = {
    "al":    [("b200", 200, 0), ("b2000", 2000, 0), ("b2000d6", 2000, 1)],
    "kitty": [("b200", 200, 0), ("b2000", 2000, 0), ("b2000d6", 2000, 1)],
    "5c":    [("b200", 200, 0), ("b200d7", 200, 1)],
}


def load(path):
    out = {}
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = MAX_TURNS + 1 if int(p[1]) < 0 else int(p[1])
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    base_arm, test_arm = sys.argv[2], sys.argv[3]
    jobs = []
    changed = []
    for deck, (path, prof, native_depth) in DECKS.items():
        for block, seed_base in BLOCKS.items():
            b = load(root / f"{base_arm}.{deck}_{block}.wins")
            t = load(root / f"{test_arm}.{deck}_{block}.wins")
            for gi in sorted(set(b) & set(t)):
                if b[gi] == t[gi]:
                    continue
                changed.append((deck, block, gi, b[gi], t[gi]))
                for cell, budget, ddelta in CELLS[deck]:
                    for arm, flags in ((base_arm, {}), (test_arm, {"MTG_M2_D0_SEARCHED": True})):
                        job = {
                            "name":       f"{arm}.{deck}_{block}_{cell}_g{gi}",
                            "deck":       path,
                            "profile":    prof,
                            "games":      1,
                            "seed":       seed_base + gi,
                            "game_index": gi,
                            "budget_ms":  budget,
                        }
                        # DEPTH is the play POLICY and an enabled value_play block OWNS it, so a
                        # manifest that pins `depth` on a profiled deck is a hard error. Budget is a
                        # RESOURCE knob and overrides freely -- so the budget cells simply omit
                        # depth (keeping the shipped policy), and only the depth cells opt out.
                        # ignore_play_profile touches depth/budget resolution ONLY; the value-leaf
                        # sidecar activates by PRESENCE and is unaffected, so the two arms still
                        # differ in exactly one thing.
                        if ddelta:
                            job["depth"] = native_depth + ddelta
                            job["ignore_play_profile"] = True
                        if flags:
                            job["flags"] = flags
                        jobs.append(job)
    print(f"# {len(changed)} changed games -> {len(jobs)} jobs", file=sys.stderr)
    for c in changed:
        print(f"#   {c[0]}.{c[1]} gi={c[2]}  base={c[3]} arm={c[4]}", file=sys.stderr)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
