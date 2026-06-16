#!/usr/bin/env python3
"""Audit cards.json mana costs (and cascade thresholds) against Scryfall.

cards.json is hand-authored, so a mana cost can be transcribed from memory
incorrectly (e.g. Land's Edge entered as {1}{R} instead of {1}{R}{R}). Scryfall
is the authoritative source for printed costs; this script fetches each card's
mana_cost / cmc and reports any divergence, so miscosts are caught mechanically
instead of by chance during play analysis.

Usage:
    python scripts/audit_card_costs.py [--cards src/cards/data/cards.json]

Exit code 1 if any mismatch is found (so it can gate CI / the analyze-deck flow).
Network: hits api.scryfall.com once per distinct card (throttled ~100ms, per
Scryfall's request guidelines). Basic lands and tokens (empty cost) are skipped.
"""
import argparse
import json
import sys
import time
import urllib.parse
import urllib.request

SCRYFALL = "https://api.scryfall.com/cards/named?exact="
HEADERS = {"User-Agent": "MagicDeckTester-cost-audit/1.0", "Accept": "application/json"}


def fetch(name, retries=4):
    """Fetch a card, retrying with exponential backoff on HTTP 429 (Scryfall
    rate-limits bursts even under a steady throttle), so a transient limit never
    masquerades as a missing card."""
    url = SCRYFALL + urllib.parse.quote(name)
    req = urllib.request.Request(url, headers=HEADERS)
    backoff = 1.0
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                return json.load(resp)
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < retries - 1:
                time.sleep(backoff)
                backoff *= 2
                continue
            return {"_error": f"HTTP {exc.code}"}
        except Exception as exc:  # noqa: BLE001 - report and continue
            return {"_error": str(exc)}
    return {"_error": "HTTP 429 (exhausted retries)"}


def norm_cost(cost):
    """Normalise a mana-cost string for comparison (strip spaces, upper-case)."""
    return (cost or "").replace(" ", "").upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cards", default="src/cards/data/cards.json")
    ap.add_argument("--throttle", type=float, default=0.1, help="seconds between API calls")
    args = ap.parse_args()

    with open(args.cards, encoding="utf-8") as fh:
        data = json.load(fh)
    cards = data["cards"] if isinstance(data, dict) else data

    mismatches, not_found, checked = [], [], 0
    for c in cards:
        name = c.get("name", "")
        local_cost = c.get("mana_cost", "")
        # Skip basic lands / tokens / anything with no printed cost locally.
        if not local_cost:
            continue
        checked += 1
        sf = fetch(name)
        time.sleep(args.throttle)
        if "_error" in sf:
            not_found.append((name, sf["_error"]))
            continue
        sf_cost = sf.get("mana_cost", "")
        # Double-faced / split cards: mana_cost lives on card_faces[0].
        if not sf_cost and "card_faces" in sf:
            sf_cost = sf["card_faces"][0].get("mana_cost", "")
        if norm_cost(sf_cost) != norm_cost(local_cost):
            mismatches.append((name, local_cost, sf_cost, sf.get("cmc")))
            continue
        # Cross-check cascade threshold (cascade_max_mv must equal the card's CMC).
        params = c.get("parameters", {})
        if "cascade_max_mv" in params:
            cmc = int(sf.get("cmc") or 0)
            if params["cascade_max_mv"] != cmc:
                mismatches.append(
                    (name, f"cascade_max_mv={params['cascade_max_mv']}",
                     f"cmc={cmc}", sf.get("cmc")))

    print(f"Checked {checked} costed cards against Scryfall.\n")
    if mismatches:
        print(f"MISMATCHES ({len(mismatches)}):")
        for name, local, sf, cmc in mismatches:
            print(f"  {name:<28} local={local:<12} scryfall={sf}  (cmc={cmc})")
    else:
        print("All mana costs match Scryfall.")
    if not_found:
        print(f"\nNOT RESOLVED ({len(not_found)}) -- likely custom/token cards:")
        for name, err in not_found:
            print(f"  {name}: {err}")

    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
