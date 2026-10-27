#!/usr/bin/env python3
"""WARN when a suite PLAY row's budget is not the deck's shipped play budget, or it has fewer games than Hinata's.

A searched depth-5 row in test/regression_cases.sh is the deck's PLAY row: regression.sh drops its depth so the
deck's enabled value_play block picks it, but the row's budget_ms still overrides the block's. The rows are
meant to test what ships (USER 2026-10-27), so a play setting re-tuned in a sidecar must re-tune its rows too;
this prints the rows that drifted. Reads specs as "<key> <depth> <seed> <games> <budget>" lines on stdin and
the key -> profile map as "<key>=<profile path>" arguments. Exit status is always 0 (a warning, not a gate).
"""
import json
import os
import sys

DEFAULT_BUDGET = 20   # MulliganProfile::BuiltinDefaultPlay(): d5/20 when no value_play block is enabled


def shipped_budget(profile):
    stem = profile[:-len(".profile.json")] if profile.endswith(".profile.json") else os.path.splitext(profile)[0]
    for path in (stem + ".value.json", profile):
        try:
            vp = json.load(open(path)).get("value_play") or {}
        except (OSError, ValueError):
            continue
        if vp.get("enabled") and vp.get("target_depth"):
            return int(vp.get("budget_ms") or 0)
    return DEFAULT_BUDGET


def has_keep_table(profile):
    d = os.path.dirname(profile)
    return any(f.endswith(".keepmodel.exhaustive.profile.json.gz") or f.endswith(".keepmodel.exhaustive.profile.json")
               for f in os.listdir(d)) if os.path.isdir(d) else False


def main():
    profs = dict(a.split("=", 1) for a in sys.argv[1:] if "=" in a)
    seen = {}
    bad = []
    specs = [l.split() for l in sys.stdin]
    specs = [f for f in specs if len(f) == 5]
    # FLOOR (USER 2026-10-27): no play row of a deck WITH A KEEP TABLE has fewer games than Hinata's play rows in
    # this tier. A deck still in progress (no keep table) keeps its small ENTRY set until the adoption expands it.
    floor = {}
    for f in specs:
        if f[0] in ("hinata", "hinata2hg") and f[1] == "5" and f[4] != "0":
            floor[f[0]] = min(floor.get(f[0], 1 << 30), int(f[3]))
    for f in specs:
        key, depth, seed, games, budget = f[0], int(f[1]), f[2], f[3], int(f[4])
        if depth == 5 and budget != 0:
            ref = floor.get("hinata2hg" if key.endswith("2hg") else "hinata")
            base = key[:-3] if key.endswith("2hg") else key
            if ref is not None and int(games) < ref and base in profs and has_keep_table(profs[base]):
                bad.append(f"{key} s{seed}: {games} games, below Hinata's {ref} per row")
        if depth != 5 or budget == 0:
            continue
        base = key[:-3] if key.endswith("2hg") else key
        if base not in profs:
            continue
        if base not in seen:
            seen[base] = shipped_budget(profs[base])
        if budget != seen[base]:
            bad.append(f"{key} s{seed}: row b{budget}, ships b{seen[base]}")
    if bad:
        print(f"WARNING: {len(bad)} play row(s) off the PLAY-SETTING SHAPE (shipped budget, Hinata game floor; "
              f"test/regression_cases.sh):", file=sys.stderr)
        for b in bad:
            print(f"  {b}", file=sys.stderr)


if __name__ == "__main__":
    main()
