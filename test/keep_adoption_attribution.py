#!/usr/bin/env python3
"""Per-game verdicts for a MULLIGAN PROFILE adoption: is every changed game explained by the keep table?

A keep/bottom table acts only at the mulligan, so adopting one should change a game ONLY by changing
what the game starts from. This replays one deck's cases from a tier run, with the exact binary that
run used, twice -- table OFF (or the OLD table, for a regeneration) and table ON -- with per-game traces,
and then for every game whose result or play digest moved:

  1. proves the OFF replay reproduces the committed GT (test/gt_logs) and the ON replay reproduces the
     run under audit, game for game -- so the table is the ONLY difference between the two sides;
  2. classifies each changed game from the two traces:
       kept hand differs, different mulligan count       -> explained (a different keep decision)
       kept hand differs, same count, different bottom   -> explained (a different bottom decision)
       same decisions, same bottomed SET, turns identical,
         only the ORDER of the bottomed cards differs    -> explained (digest-only; play identical)
       anything else                                     -> UNEXPLAINED: the table leaked into play
Exit 0 when every changed game is explained and both replays reproduce their baselines, else 1.

  python3 test/keep_adoption_attribution.py <mode> <deck-key> [--run-dir DIR] [--old-profile none|PATH]
        [--out DIR]
  e.g. python3 test/keep_adoption_attribution.py regression selesnya

--run-dir defaults to test/logs/<mode> (needs manifest.json, mtg.run, wins/). Run it BEFORE --accept:
the committed gt_logs are the OLD side. Built 2026-10-08 for the SelesnyaLifegain adoption.
"""
import argparse
import collections
import glob
import json
import os
import subprocess
import sys

ap = argparse.ArgumentParser()
ap.add_argument("mode")
ap.add_argument("deck_key")
ap.add_argument("--run-dir")
ap.add_argument("--old-profile", default="none")
ap.add_argument("--out")
a = ap.parse_args()
# Paths are relative to the repo root, like the other test/ tools: run it from there.
if not os.path.isdir("test/gt_logs"):
    sys.exit("run from the repo root (test/gt_logs not found)")
run_dir = a.run_dir or f"test/logs/{a.mode}"
out = a.out or f"logs/keep_attr/{a.mode}_{a.deck_key}"
os.makedirs(out, exist_ok=True)

man = json.load(open(f"{run_dir}/manifest.json"))
jobs = [j for j in man["jobs"] if j["name"].startswith(a.deck_key + "_")]
if not jobs:
    sys.exit(f"no jobs named {a.deck_key}_* in {run_dir}/manifest.json")
json.dump({"jobs": jobs}, open(f"{out}/manifest.json", "w"), indent=1)
binary = os.path.abspath(f"{run_dir}/mtg.run")


def replay(arm, env_extra):
    env = dict(os.environ)
    env.pop("MTG_EXHAUSTIVE_PROFILE", None)
    env.update(env_extra)
    if os.path.isdir(f"{out}/{arm}/wins") and len(glob.glob(f"{out}/{arm}/trace/*.json")) == sum(
            j["games"] for j in jobs):
        return   # already replayed (rerunning the report is free)
    with open(f"{out}/{arm}.log", "w") as lo, open(f"{out}/{arm}.err", "w") as le:
        subprocess.run([binary, "--batch", f"{out}/manifest.json", "--threads", "0",
                        "--game-log-dir", f"{out}/{arm}/wins", "--game-trace-dir", f"{out}/{arm}/trace"],
                       env=env, stdout=lo, stderr=le, check=True)


replay("off", {"MTG_EXHAUSTIVE_PROFILE": a.old_profile})
replay("on", {})


def wins(p):
    d = {}
    for ln in open(p):
        f = ln.split()
        if len(f) >= 2:
            d[int(f[0])] = (int(f[1]), f[2] if len(f) > 2 else "")
    return d


def trace(arm, key, gi):
    d = json.load(open(f"{out}/{arm}/trace/{key}_gi{gi}.json"))
    seq = d["mulliganSequence"]
    kept = next(m for m in seq if m.get("kept"))
    return dict(mulls=kept["attempt"],
                kept=sorted(c["card"] for c in d["openingHand"]),
                decisions=[(m["attempt"], m["kept"], [c["card"] for c in m.get("bottomed", [])])
                           for m in seq],
                hands=[[c["card"] for c in m["hand"]] for m in seq],
                turns=json.dumps(d["turns"], sort_keys=True))


ok = True
grand = collections.Counter()
unexplained = []
print(f"=== keep-table adoption attribution: {a.mode} / {a.deck_key} (old side: table {a.old_profile}) ===")
for j in jobs:
    key = j["name"]
    gt, run = wins(f"test/gt_logs/{key}.wins"), wins(f"{run_dir}/wins/{key}.wins")
    off, on = wins(f"{out}/off/wins/{key}.wins"), wins(f"{out}/on/wins/{key}.wins")
    r_off, r_on = off == gt, on == run
    ok &= r_off and r_on
    c = collections.Counter()
    for gi in sorted(on):
        if off.get(gi) == on[gi]:
            c["unchanged"] += 1
            continue
        to, tn = trace("off", key, gi), trace("on", key, gi)
        if to["kept"] != tn["kept"]:
            c["changed: kept hand differs, " + ("different mulligan count" if to["mulls"] != tn["mulls"]
                                                else "same count, different bottom")] += 1
        elif (to["hands"] == tn["hands"] and to["turns"] == tn["turns"]
              and to["decisions"] != tn["decisions"]
              and [(x, y, sorted(z)) for x, y, z in to["decisions"]]
              == [(x, y, sorted(z)) for x, y, z in tn["decisions"]]):
            # Same keeps, same bottomed SET, every turn identical: only the order the bottomed cards
            # went under the library differs, which the play digest records.
            c["changed: digest only -- bottomed-card ORDER, play identical"] += 1
        else:
            c["changed: UNEXPLAINED (same kept hand, play differs)"] += 1
            unexplained.append((key, gi, off.get(gi), on[gi]))
    print(f"\n{key}: off == committed GT: {r_off}   on == run under audit: {r_on}")
    for k, v in sorted(c.items()):
        print(f"    {k:62s} {v:5d}")
        grand[k] += v
print("\n=== TOTAL ===")
for k, v in sorted(grand.items()):
    print(f"    {k:62s} {v:5d}")
print(f"\nbaselines reproduced on both sides: {ok}")
print(f"unexplained: {len(unexplained)}")
for u in unexplained[:50]:
    print("   ", u)
sys.exit(0 if ok and not unexplained else 1)
