#!/usr/bin/env python3
"""Sample size-7 bucket COMPOSITIONS from a deck's committed exhaustive profile.

Emits `MTG_SCORE_FILE` lines ("7:c0,c1,...,cK-1") for the targeted comp-scorer, sampled from the
REALISTIC opening-hand distribution (draw 7 from the 60-card list, map cards -> buckets) rather than
uniformly over compositions -- a uniform sample is dominated by comps the deck essentially never
draws, which would weight the depth comparison toward hands that never arise in play.

Deliberately reads the COMMITTED bucket map so every arm of a depth comparison scores the same
hands under the same bucketing (the whole point of the discovery/label split).
"""
import argparse
import collections
import gzip
import json
import pathlib
import random
import re
import sys


def load_buckets(deck_path: pathlib.Path):
    """Return the committed profile's bucket list (list of list-of-card-names)."""
    stem = deck_path.parent / (deck_path.stem + ".keepmodel.exhaustive.profile.json")
    cand = [pathlib.Path(str(stem) + ".gz"), stem]
    for p in cand:
        if p.exists():
            opener = gzip.open if p.suffix == ".gz" else open
            with opener(p, "rt") as fh:
                blob = fh.read()
            prof = json.loads(blob)
            ek = prof.get("exhaustive_keep") or {}
            return ek.get("buckets") or []
    raise SystemExit("no exhaustive sidecar beside %s (looked for %s)" % (deck_path, cand[0]))


def load_deck(deck_path: pathlib.Path):
    """Parse a .txt/.cod decklist into a flat list of card names (maindeck only)."""
    text = deck_path.read_text(encoding="utf-8", errors="replace")
    cards = []
    if deck_path.suffix == ".cod":
        # ONLY the main zone. Matching every <card> element sweeps the sideboard in too, which shows
        # up downstream as "deck cards absent from the bucket map" (the bucket map is maindeck-only)
        # and, worse, silently yields short comps -- a "7:" line summing to 5.
        main = re.search(r'<zone name="main".*?</zone>', text, re.S)
        body = main.group(0) if main else ""
        for m in re.finditer(r'<card number="(\d+)"[^>]*name="([^"]+)"', body):
            cards += [m.group(2)] * int(m.group(1))
        return cards
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        if line.lower().startswith("sideboard"):
            break
        m = re.match(r"^(\d+)x?\s+(.+?)\s*$", line)
        if m:
            cards += [m.group(2)] * int(m.group(1))
    return cards


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck")
    ap.add_argument("-n", "--num", type=int, default=150, help="distinct comps to emit")
    ap.add_argument("--size", type=int, default=7)
    ap.add_argument("--seed", type=int, default=20260815)
    ap.add_argument("-o", "--out", default="-")
    ap.add_argument("--allow-unmapped", action="store_true",
                    help="sample over the mapped subset even if the decklist has cards the bucket "
                         "map lacks (produces SHORT comps -- see the error text)")
    args = ap.parse_args()

    deck_path = pathlib.Path(args.deck)
    buckets = load_buckets(deck_path)
    K = len(buckets)
    if K == 0:
        raise SystemExit("empty bucket map")
    bof = {}
    for b, names in enumerate(buckets):
        for n in names:
            bof[n] = b

    cards = load_deck(deck_path)
    if not cards:
        raise SystemExit("could not parse a decklist from %s" % deck_path)
    unmapped = sorted({c for c in cards if c not in bof})
    if unmapped:
        # REFUSE, do not warn. An unmapped maindeck card means the committed sidecar's bucket map and
        # the decklist disagree, and the failure is SILENT downstream: the card is dropped from every
        # sampled hand, so a line written "7:..." actually describes a 5- or 6-card hand and every arm
        # scores a hand the deck cannot draw. That reads as data, not as a failure.
        print("ERROR: %d maindeck card(s) absent from the bucket map: %s\n"
              "       The committed exhaustive sidecar does not describe this decklist. Re-generate\n"
              "       it, or pass --allow-unmapped to sample only over the mapped subset (which\n"
              "       produces SHORT comps -- valid only if you know why)."
              % (len(unmapped), ", ".join(unmapped)), file=sys.stderr)
        if not args.allow_unmapped:
            raise SystemExit(2)

    rng = random.Random(args.seed)
    seen = collections.Counter()
    order = []
    # Sample generously; dedupe to distinct comps, keeping draw frequency as the tiebreak for which
    # comps make the cut (most-drawn first).
    for _ in range(args.num * 400):
        hand = rng.sample(cards, args.size)
        comp = [0] * K
        for c in hand:
            b = bof.get(c)
            if b is not None:
                comp[b] += 1
        key = tuple(comp)
        if key not in seen:
            order.append(key)
        seen[key] += 1

    order.sort(key=lambda k: -seen[k])
    picked = order[: args.num]
    lines = ["%d:%s" % (args.size, ",".join(str(x) for x in comp)) for comp in picked]

    fh = sys.stdout if args.out == "-" else open(args.out, "w")
    fh.write("\n".join(lines) + "\n")
    if fh is not sys.stdout:
        fh.close()
    print("K=%d  distinct_sampled=%d  emitted=%d  coverage=%.1f%% of draws"
          % (K, len(order), len(picked),
             100.0 * sum(seen[k] for k in picked) / max(1, sum(seen.values()))),
          file=sys.stderr)


if __name__ == "__main__":
    main()
