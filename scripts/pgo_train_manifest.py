#!/usr/bin/env python3
"""Emit the PGO training manifest for scripts/build_pgo.sh -- ONE pooled --batch, never a per-deck loop.

    python3 scripts/pgo_train_manifest.py <out.json> [<deck-dir> ...]

With no deck named (the default, the binary every long run uses): every SMOKE case of
test/regression_cases.sh at PGO_FRACTION (default 0.1) of its games, in the suite's own job shape --
d5 driven by the deck's value_play block, d0/d3 pinned with ignore_play_profile, 2HG variants at 30 life
and two heads -- plus one small job per deck at its MULLIGAN-GEN settings (value_play.mull_gen_depth /
mull_gen_budget_ms), the search the keep-gen rollouts run. So the profile sees every deck at every
depth the suite, the screens and the generators play.

With decks named: PGO_GAMES (default 48) games of each at its gen settings (the original per-deck build).

Training only steers code layout, inlining and branch weights; it cannot change what the binary computes
(no -ffast-math, no -march), and code the training never ran is still optimised normally
(-fprofile-partial-training). Seeds are 7001+, away from every tier's seeds, though nothing depends on it.
"""
import importlib.util
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location("suite_gate", os.path.join(ROOT, "scripts/suite_gate.py"))
sg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sg)


def gen_settings(deck_path):
    d = os.path.dirname(deck_path)
    stem = os.path.splitext(os.path.basename(deck_path))[0]
    try:
        vp = json.load(open(os.path.join(d, stem + ".value.json"))).get("value_play", {})
    except Exception:
        vp = {}
    return (vp.get("mull_gen_depth") or vp.get("target_depth") or 5,
            vp.get("mull_gen_budget_ms") or vp.get("budget_ms") or 20)


def gen_job(name, deck, prof, games):
    depth, budget = gen_settings(deck)
    j = {"name": name, "deck": deck, "games": games, "seed": 7001, "depth": depth,
         "budget_ms": budget, "ignore_play_profile": True}
    if prof and os.path.exists(prof):
        j["profile"] = prof
    return j


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out, decks = sys.argv[1], sys.argv[2:]
    os.chdir(ROOT)
    jobs = []
    if decks:
        games = int(os.environ.get("PGO_GAMES", "48"))
        for d in decks:
            deck = os.path.relpath(sg.resolve_decklist(d))
            stem = os.path.splitext(os.path.basename(deck))[0]
            jobs.append(gen_job("gen_" + stem, deck, os.path.join(os.path.dirname(deck), stem + ".profile.json"),
                                games))
    else:
        frac = float(os.environ.get("PGO_FRACTION", "0.1"))
        files, profs = sg._read_map("DECK_FILE"), sg._read_map("DECK_PROF")
        for key, depth, _seed, n, budget in sg._cases("smoke"):
            base = key[:-3] if key.endswith("2hg") else key
            if base not in files or not os.path.exists(files[base]):
                continue
            j = {"name": "%s_d%d" % (key, depth), "deck": files[base], "games": max(2, math.ceil(n * frac)),
                 "seed": 7001}
            if profs.get(base):
                j["profile"] = profs[base]
            if key.endswith("2hg"):
                j.update(starting_life=30, opponent_heads=2)
            if depth == 5:
                j["budget_ms"] = budget                   # the deck's value_play block drives the depth
            else:
                j.update(depth=depth, budget_ms=budget if depth > 0 else 0, ignore_play_profile=True)
            jobs.append(j)
        for key in sorted(files):
            if os.path.exists(files[key]):
                jobs.append(gen_job("gen_" + key, files[key], profs.get(key, ""), 8))
    if not jobs:
        sys.exit("pgo_train_manifest: no training jobs")
    with open(out, "w") as fh:
        json.dump({"jobs": jobs}, fh, indent=1)
    print("    %d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


if __name__ == "__main__":
    main()
