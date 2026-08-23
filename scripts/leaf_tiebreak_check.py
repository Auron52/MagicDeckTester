#!/usr/bin/env python3
"""Per-deck check for the horizon-honest no-win tie-break (DecisionProvider::GradesNoWinLeaf).

WHY THIS EXISTS. The tie-break ships DEFAULT ON, because a per-deck opt-in means every new deck
silently keeps a blindness we know how to fix until somebody remembers to look. But it is a
judgement about how to VALUE a position, and no single valuation suits every archetype, so each deck
needs a measurement. This automates it.

HOW TO READ THE RESULT -- and the mistake this script used to encode. THE METRIC IS THE BAR: this
lever exists for one reason, to lower avg turn-to-win, so a deck whose average it RAISES should opt
out. Do not go looking for a mechanism story first. In particular, do NOT try to rescue a regression
by escalating depth/budget: the tie-break only fires when a rollout reaches the horizon WITHOUT a
win, so raising depth/budget lets the search find real wins inside the horizon and the leaf stops
being consulted at all. A regression that "closes at 20x budget" has told you the lever stopped
FIRING, not that its valuation was sound -- the test is near-tautological for a leaf evaluator and
must not be used as grounds for keeping the default. (Escalation is still useful for EXPLAINING a
particular game once you have already decided from the metric.)

THE SAMPLE IS THE HARD PART. Binding rates are low and vary hugely by deck -- a deck that usually
wins well inside the horizon may change fewer than 1 game in 1,000, so a small run reports "0
changed" and that is indistinguishable from "the mechanism never fired". This script therefore
reports the CHANGED-GAME COUNT and refuses to call a sign it cannot support, printing the scale a
decisive run would need.

    python3 scripts/leaf_tiebreak_check.py decks/<Name>/<Name>.cod [--blocks 12] [--games 1000]

Several decks may be passed at once and they POOL INTO ONE batch (repo rule: one work queue, one
load-imbalance tail -- never a loop of per-deck invocations).

It runs BOTH arms in ONE pooled batch (per-job heurarm flags) at PRODUCTION search settings, splits
the seed blocks into two independent halves as a consistency check, and prints a verdict per deck.
It never edits code: adopting an opt-out is a one-line provider override you make deliberately.

See docs/design/horizon-honest-leaf.md and .claude/skills/analyze-deck.md step 5c2.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEED_SPACING = 100_000   # >> any sane games-per-cell, so seed ranges cannot overlap and replay games
SEED_BASE = 1_000_000
CELLS = ((3, 10), (5, 20))   # production search settings; d0 has no search, so no tie-break to move
MIN_CHANGED = 20         # below this the net's sign is not worth reporting as a direction


def build_manifest(decks, games, path, seeds, force=False):
    """decks: list of (stem, deck_path, profile_path). All arms/decks pool into ONE batch."""
    jobs = []
    for stem, deck, profile in decks:
        for arm, on in (("base", False), ("leaf", True)):
            for depth, budget in CELLS:
                for seed in seeds:
                    # "~"-separated: deck stems contain both "-" and "_" (creature_giving), so
                    # splitting a job name on "_" silently drops decks from the table.
                    jobs.append({"name": f"{stem}~{arm}~d{depth}~s{seed}", "deck": deck,
                                 "profile": profile, "games": games, "seed": seed, "depth": depth,
                                 "budget_ms": budget,
                                 # the deck profile may LOCK target_depth (value_play); this is the
                                 # same override the suite passes to pin an explicit depth.
                                 "ignore_play_profile": True, "weight": 0,
                                 "flags": {"MTG_LEAF_GRADE_NOWIN": on,
                                           # --force: a deck that ALREADY opts out reads identical
                                           # in both arms (the provider gate is compiled in), so the
                                           # A/B would silently measure nothing. Off by default so
                                           # ordinary onboarding respects a deliberate opt-out.
                                           **({"MTG_LEAF_NOWIN_FORCE": on} if force else {})}})
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    return sum(j["games"] for j in jobs)


def parse(out_path):
    pat = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")
    res = collections.defaultdict(dict)
    for line in open(out_path, errors="replace"):
        m = pat.search(line)
        if m:
            stem, arm, depth, seed = m.group(1).split("~")
            res[(stem, arm, depth, seed)][int(m.group(2))] = int(m.group(3))
    return res


def compare(res, stem, seeds):
    """-> (net turns, worse, better, games). Positive net = the tie-break is WORSE."""
    net = worse = better = games = 0
    for depth, _budget in CELLS:
        for seed in seeds:
            a = res.get((stem, "base", f"d{depth}", f"s{seed}"))
            b = res.get((stem, "leaf", f"d{depth}", f"s{seed}"))
            if not a or not b:
                continue
            games += len(a)
            for gi in a:
                # THE metric: loss-penalised win turn, unwon = max_turns+1 = 9.
                d = (9 if b[gi] < 0 else b[gi]) - (9 if a[gi] < 0 else a[gi])
                if d:
                    net += d
                    worse += d > 0
                    better += d < 0
    return net, worse, better, games


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck", nargs="+", help="one or more decks; they POOL into a single batch")
    ap.add_argument("--tag", default="check", help="output name under logs/leaf_tiebreak/")
    ap.add_argument("--blocks", type=int, default=12, help="seed blocks per cell (more = tighter sign)")
    ap.add_argument("--games", type=int, default=1000, help="games per cell")
    ap.add_argument("--quick", action="store_true", help="smoke the plumbing only (2 blocks x 200)")
    ap.add_argument("--force", action="store_true",
                    help="re-validate a deck that already opts out (forces the provider gate open)")
    args = ap.parse_args()

    decks = []
    for d in args.deck:
        profile = os.path.splitext(d)[0] + ".profile.json"
        for p in (d, profile):
            if not os.path.exists(os.path.join(ROOT, p)):
                sys.exit(f"not found: {p}")
        decks.append((os.path.splitext(os.path.basename(d))[0], d, profile))

    blocks, games = (2, 200) if args.quick else (args.blocks, args.games)
    if games > SEED_SPACING:      # SEED-OVERLAP TRAP: jobs would replay each other's games
        sys.exit(f"--games must be <= {SEED_SPACING} (seed spacing) or seed ranges overlap and replay")
    seeds = [SEED_BASE + i * SEED_SPACING for i in range(blocks)]

    binary = os.path.join(ROOT, "build/Release/mtg")
    if not os.path.exists(binary):
        sys.exit("build/Release/mtg missing -- run ./build.sh first")
    outdir = os.path.join(ROOT, "logs/leaf_tiebreak")
    os.makedirs(outdir, exist_ok=True)
    manifest, out = f"{outdir}/{args.tag}.json", f"{outdir}/{args.tag}.out"

    total = build_manifest(decks, games, manifest, seeds, args.force)
    print(f"{total} games ({total // 2} paired) over {len(decks)} deck(s), "
          "both arms in ONE pooled batch ...")
    env = dict(os.environ, MTG_DUMP_WINS="1")
    with open(out, "w") as fh:
        rc = subprocess.call([binary, "--batch", manifest], stdout=fh, stderr=subprocess.STDOUT,
                             cwd=ROOT, env=env)
    if rc != 0:
        sys.exit(f"batch failed (exit {rc}); see {out}")

    res = parse(out)
    for stem, _deck, _prof in decks:
        report(res, stem, seeds, games, blocks)


def report(res, stem, seeds, games, blocks):
    half = len(seeds) // 2 or 1
    splits = [("half A", seeds[:half]), ("half B", seeds[half:])]
    print(f"\n=== {stem} ===")
    print(f"{'split':10s} {'games':>8s} {'net turns':>10s} {'worse':>6s} {'better':>7s}"
          "   (negative = the tie-break HELPS)")
    signs = {}
    for label, split_seeds in splits:      # NOT `seeds`: shadowing it silently made ALL == one half
        net, w, b, n = compare(res, stem, split_seeds)
        signs[label] = net
        print(f"{label:10s} {n:8d} {net:+10d} {w:6d} {b:7d}")
    net_all, w_all, b_all, n_all = compare(res, stem, seeds)
    print(f"{'ALL':10s} {n_all:8d} {net_all:+10d} {w_all:6d} {b_all:7d}")

    changed = w_all + b_all
    rate = changed / n_all if n_all else 0.0
    print(f"\nbinding: {changed} changed games of {n_all} paired ({100 * rate:.3f}%)")

    if changed < MIN_CHANGED:
        # "0 changed" and "changed but neutral" are DIFFERENT results; never report them as one.
        print(f"\nVERDICT: NO SIGN AT THIS SAMPLE -- only {changed} game(s) changed, below the {MIN_CHANGED}"
              " needed\n  to trust a direction. This is NOT evidence the tie-break is harmless here;"
              " it may simply\n  not have fired enough to measure.")
        if changed:
            need = int(MIN_CHANGED / rate / (2 * len(CELLS) * games)) + 1
            print(f"  Re-run at roughly --blocks {max(need, blocks * 2)} to reach a decisive sample.")
        else:
            print(f"  Re-run at --blocks {blocks * 4} before concluding anything.")
        print("  If it stays unbindable at a large sample, the default (ON) is fine by default:"
              " a lever\n  that never fires costs the deck nothing.")
    elif net_all < 0:
        print("\nVERDICT: KEEP THE DEFAULT (ON). The tie-break lowers this deck's average.")
    elif net_all == 0:
        print("\nVERDICT: NEUTRAL. It fires but does not move the average; keep the default (ON).")
    else:
        consistent = all(v > 0 for v in signs.values())
        print("\nVERDICT: OPT OUT. The tie-break RAISES this deck's average"
              + (" on both halves." if consistent else " overall, though the halves disagree --"
                 " re-run at 2-4x --blocks to confirm before acting."))
        print("  THE METRIC IS THE BAR, and this lever's whole purpose is to lower it. Add")
        print(f"    bool GradesNoWinLeaf() const override {{ return false; }}")
        print("  to this deck's DecisionProvider, and record the measurement in")
        print("  docs/design/horizon-honest-leaf.md.")
        print("  Do NOT try to rescue it by escalating depth/budget -- escalation removes the very")
        print("  regime the leaf operates in, so it can only ever report that the lever stopped")
        print("  firing. Escalation is for EXPLAINING a game, not for keeping the default.")
        print("  The known cause, worth naming if it fits: opponent life at the horizon is a")
        print("  DAMAGE-RACE proxy, so a deck whose value is STORED rather than expressed as damage")
        print("  by the horizon (combo, storm, ramp) has its build-up priced at zero.")


if __name__ == "__main__":
    main()
