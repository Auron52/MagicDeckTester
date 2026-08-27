#!/usr/bin/env python3
"""Phase F -- finalise the deck's MULLIGAN-GENERATION contract in its play profile.

The last stage of a value-leaf run, and the answer to two things that were previously manual (user,
2026-08-15: "It probably would be good to include a step near the end of the value-leaf that fills in
the play profile with the right generation setting for mulligan profiles... At the same time as we are
figuring out the best mulligan settings we can also store K if it isn't already there").

It does exactly two things, both of which need the value model to already exist -- which is why they
belong here and not in `analyze-deck`:

  1. THE GENERATION SETTING (value_play.mull_gen_depth / mull_gen_budget_ms), by MEASUREMENT.
     Delegates to derive_mullgen_setting.py, which scores real openers under candidate (depth, budget)
     pairs against the deck's own shipped play policy and takes the cheapest that clears a rank-
     fidelity floor. It is not read off the depth table: that rule was tried and disproven
     (docs/design/mullgen-setting-is-a-trust-question.md) -- burn is trusted at its play depth yet its
     play settings cost 15x d1 b3 for +0.001 fidelity, so "trusted -> generate at play settings" is
     not a rule, and the table's H ladder measures how a depth PLAYS, not how it RANKS hands.

  2. THE BUCKET COUNT K (value_play.expected_buckets), recorded ONCE.
     ExhaustiveKeep already REFUSES to generate when the discovered K differs from this field -- but
     nothing ever wrote it, so that guard has been inert since it was added: a check whose input never
     arrives is not a check. Recording it here closes that loop, and the first value is the one a human
     confirms (user: "store it the first time we generate and have it checked by the user. After that,
     we always check against what was stored... Since we never want to be caught off-guard by a
     different K").

WHY K IS SAFE TO RECORD HERE. K comes from equivalence DISCOVERY, which reads the deck's PLAY settings
(not mull_gen_*) since `fix(keepgen): discovery runs under PLAY settings`. So it is stable against the
generation setting this same script picks -- writing one cannot move the other. Discovery is also the
cheap part of generation (minutes against the hours-to-days of the table itself), which is what makes
it affordable to run for a number rather than a table.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys


def discover_k(deck, cards_json, binary, timeout_note=True, play=None):
    """Run equivalence discovery only, and return the bucket count it finds.

    THE DISCOVERY DEPTH MUST BE THE DECK'S PLAY DEPTH, and it has to be passed explicitly.
    `--gen-mulligan` resolves it from value_play.target_depth ("discovery depth: N (source:
    value_play.target_depth (play))"), but the bare MTG_KEEP_DISCOVERY_ONLY route does not -- it
    takes the binary's built-in default of 5. So on any deck whose play depth is not 5, Phase F
    recorded K from a DIFFERENT discovery than the one generation runs, and the K guard then
    refuses every generation attempt.

    Measured on StompySurprise (play d6/b20): discovery-only gave K=16 at the default depth 5 and
    K=15 at depth 6, which is what --gen-mulligan finds; the cached fingerprints differ in exactly
    the `depth` field. The bug hid because the only two decks carrying expected_buckets --
    KittyEquipment (play d5) and Mirrorwing -- happen to match the default, so their recorded K was
    right by coincidence.
    """
    env = dict(os.environ, MTG_KEEP_EXHAUSTIVE="1", MTG_KEEP_DISCOVERY_ONLY="1")
    if play:
        if play.get("target_depth"):
            env["MTG_EQUIV_DEPTH"] = str(play["target_depth"])
        if play.get("budget_ms"):
            env["MTG_EQUIV_BUDGET"] = str(play["budget_ms"])
    p = subprocess.run([binary, str(deck), "--cards-json", cards_json],
                       capture_output=True, text=True, env=env)
    blob = p.stdout + p.stderr
    m = re.search(r"discovery-only:\s*K=(\d+)", blob)
    if m:
        return int(m.group(1)), blob
    m = re.search(r"deck=\d+ cards,\s*(\d+) buckets", blob)
    return (int(m.group(1)) if m else None), blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck", help="path to the decklist (.txt/.cod)")
    ap.add_argument("--cards-json", default="src/cards/data/cards.json")
    ap.add_argument("--binary", default="build/Release/mtg-analyze")
    ap.add_argument("--hands", type=int, default=48)
    ap.add_argument("-R", type=int, default=24)
    ap.add_argument("--floor-rho", type=float, default=0.99)
    ap.add_argument("--write", action="store_true", help="actually modify the .value.json")
    ap.add_argument("--skip-k", action="store_true",
                    help="do not run discovery for K (use when discovery is known to be expensive)")
    args = ap.parse_args()

    deck = pathlib.Path(args.deck)
    vpath = deck.parent / (deck.stem + ".value.json")
    if not vpath.exists():
        print("phase F: no %s -- the value leaf must exist first" % vpath.name)
        return 1

    rc = 0
    # ---------------------------------------------------------------- 1. the generation setting
    print("=" * 70)
    print("PHASE F (1/2): mulligan-generation setting, by measurement")
    print("=" * 70)
    # The child writes straight to the same fd, so our buffered header would otherwise land AFTER
    # its output and label the wrong block.
    sys.stdout.flush()
    cmd = [sys.executable, "scripts/derive_mullgen_setting.py", str(deck),
           "--cards-json", args.cards_json, "--hands", str(args.hands),
           "-R", str(args.R), "--floor-rho", str(args.floor_rho)]
    if args.write:
        cmd.append("--write")
    r = subprocess.run(cmd)
    rc |= (r.returncode != 0)

    # ---------------------------------------------------------------- 2. K, recorded once
    print("\n" + "=" * 70)
    print("PHASE F (2/2): bucket count K -> value_play.expected_buckets")
    print("=" * 70)
    v = json.load(open(vpath))
    vp = v.setdefault("value_play", {})
    have = vp.get("expected_buckets")
    if args.skip_k:
        print("  SKIPPED (--skip-k)")
        return rc
    if have:
        print("  already recorded: expected_buckets=%d" % have)
        print("  (ExhaustiveKeep REFUSES to generate if discovery disagrees with this -- that is the"
              "\n   point of recording it, so it is left alone here.)")
        return rc

    # `vp` is the deck's value_play; pass it so discovery runs at the deck's PLAY depth/budget --
    # the same settings --gen-mulligan will use. See discover_k's note.
    k, blob = discover_k(deck, args.cards_json, args.binary, play=vp)
    if k is None:
        print("  could NOT determine K from discovery output -- leaving expected_buckets unset.")
        print("  (A wrong K is worse than none: it would refuse every future generation.)")
        tail = "\n".join(blob.strip().splitlines()[-6:])
        print("  last output:\n%s" % tail)
        return rc
    print("  discovered K = %d" % k)
    if args.write:
        vp["expected_buckets"] = k
        json.dump(v, open(vpath, "w"), indent=1)
        print("  RECORDED in %s." % vpath.name)
        print("  CONFIRM THIS NUMBER. Every later generation must match it or it refuses to run;\n"
              "  that is deliberate -- a silently different K means a differently-bucketed deck and a\n"
              "  hand space that can grow as C(K+6,7).")
    else:
        print("  (dry run -- re-run with --write to record it)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
