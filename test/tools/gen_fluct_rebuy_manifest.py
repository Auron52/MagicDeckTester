#!/usr/bin/env python3
"""THE FLUCTUATOR REFERENCE-CLOSING BATCH (2026-09-05), ONE POOLED QUEUE.

Three levers landed together to close the last two reference shortfalls (s2 T5->T4, s10 T4->T3;
ref_bench 2/10 short -> 0/10, search avg 3.700 -> 3.500 = the human):

  MTG_FLUCT_REBUY      the cycle-Stinger -> Unearth this-turn deployment: stop-on-threat no longer
                       reads an in-hand Stinger as "wait" while the rebuy is executable NOW, the
                       Stinger cycles FIRST, and the dig loop re-solves the moment it hits the yard.
  MTG_FLUCT_HOLD_FUEL  the USER's going-off rule (was default OFF / measuring; now ON).
  MTG_DIG_HOLD_FUEL    that rule's DIG-SITE half: the nested re-solve skips fuel casts while the
                       chain can close (reference s2's chain died of hand exhaustion 6 pings short
                       because the re-solve cast a free Hollow One mid-kill).
  MTG_GREEDY_HOLD_LAND that rule's LAND-DROP site (d0 greedy): skip the drop while going off so
                       the dig loop cycles the drawn land for a ping (smoke d0 gi803: three turns
                       each played a cycler for a 2-power attack -> loss vs GT's T7 win).

ARMS. `old` restores the pre-change engine (all levers off) so old-vs-new is the headline;
`rebuy` and `holdfuel` isolate the two rules (the hold-fuel sites travel together here --
each has its own gate for finer probes if a number demands one).

Every non-fluctuator deck runs old-vs-new only, as the negative control: all the levers sit
behind FluctuatorProvider hooks that return false everywhere else, so anything nonzero there
means a lever leaks outside its provider.

CELLS + BLOCKS: exactly gen_land_idle_manifest.py's (play settings, disjoint train/hold seeds,
deck map read from regression_cases.sh, never copied).
"""
import json
import pathlib
import re
import sys

_ROOT = pathlib.Path(__file__).resolve().parents[2]


def _decks():
    text = (_ROOT / "test" / "regression_cases.sh").read_text()
    out = {}
    for arr, slot in (("DECK_FILE", 0), ("DECK_PROF", 1)):
        block = re.search(r"declare -A %s=\((.*?)\n\)" % arr, text, re.S)
        if not block:
            raise SystemExit(f"could not find {arr} in test/regression_cases.sh")
        for name, val in re.findall(r"\[(\w+)\]=\"?([^\"\n]+)\"?", block.group(1)):
            out.setdefault(name, ["", ""])[slot] = val.strip()
    missing = [k for k, v in out.items() if not v[0] or not v[1]]
    if missing:
        raise SystemExit(f"deck rows missing a half: {missing}")
    return {k: tuple(v) for k, v in out.items()}


LEVERS = ("MTG_FLUCT_REBUY", "MTG_FLUCT_HOLD_FUEL", "MTG_DIG_HOLD_FUEL",
          "MTG_GREEDY_HOLD_LAND")
ARMS = {
    "old":      {lv: False for lv in LEVERS},                                  # pre-change engine
    "new":      {},                                                            # shipped defaults
    # hold-fuel family off (its dig-site + land-drop halves too) -> the rebuy line alone
    "rebuy":    {"MTG_FLUCT_HOLD_FUEL": False, "MTG_DIG_HOLD_FUEL": False,
                 "MTG_GREEDY_HOLD_LAND": False},
    "holdfuel": {"MTG_FLUCT_REBUY": False},                                    # going-off rule alone
}
SUITE_ARMS = ("old", "new")   # negative control needs only the headline pair

BLOCKS = {"train": 610001, "hold": 1220001}
FLUCT_CELLS = [(0, 0, 6000), (3, 10, 1000), (5, 20, 500)]
SUITE_CELLS = [(3, 10, 400)]


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    decks = _decks()
    jobs = []
    for deck, (path, prof) in sorted(decks.items()):
        fluct = deck == "fluctuator"
        cells = FLUCT_CELLS if fluct else SUITE_CELLS
        arms = ARMS if fluct else {a: ARMS[a] for a in SUITE_ARMS}
        for depth, budget, games in cells:
            for arm, flags in arms.items():
                for block, seed in BLOCKS.items():
                    job = {
                        "name": f"{arm}.{deck}.d{depth}.{block}",
                        "deck": path,
                        "profile": prof,
                        "games": max(1, int(games * scale)),
                        "seed": seed,
                        "depth": depth,
                        "budget_ms": budget,
                        "ignore_play_profile": True,
                        "weight": 0,
                    }
                    if flags:
                        job["flags"] = dict(flags)
                    jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
