#!/usr/bin/env python3
"""Audit cards.json mana costs (and cascade thresholds) against Scryfall.

cards.json is hand-authored, so a mana cost can be transcribed from memory
incorrectly (e.g. Land's Edge entered as {1}{R} instead of {1}{R}{R}). Scryfall
is the authoritative source for printed costs; this script fetches each card's
mana_cost / cmc and reports any divergence, so miscosts are caught mechanically
instead of by chance during play analysis.

Usage:
    python scripts/audit_card_costs.py [--cards src/cards/data/cards.json]

Exit codes (so it can gate CI / the analyze-deck flow):
    0  every card that resolved matched, and every card resolved
    1  at least one real mismatch
    2  DID-NOT-RUN: one or more costs went unverified because Scryfall was
       unreachable or rate-limited. Nothing is known to be wrong, but nothing is
       known to be right either -- re-run when the limit clears.
A card Scryfall 404s (a genuinely custom card) is reported and stays non-fatal.
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

    # Two DIFFERENT kinds of "no answer", and conflating them is what let a rate-limited run
    # report a clean bill of health over zero comparisons (see below):
    #   absent      -- Scryfall says the card does not exist (404). Expected for custom/token
    #                  entries, non-fatal, nothing more we can learn.
    #   unreachable -- we never got an answer (429 rate limit, timeout, DNS, 5xx). The cost is
    #                  UNVERIFIED, which is a did-not-run, not a pass.
    # A third, milder category: the COST verified fine but a secondary cross-check could not be
    # made from this source. Disclosed, but it does NOT move the exit code -- the alternative is a
    # permanently-red gate nobody can clear, which is the false-red mirror of the false green.
    mismatches, absent, unreachable, uncrosschecked, checked, compared = [], [], [], [], 0, 0
    for c in cards:
        name = c.get("name", "")
        local_cost = c.get("mana_cost", "")
        # Skip basic lands / tokens / anything with no printed cost locally.
        if not local_cost:
            continue
        checked += 1
        # An alternate PRINTED name (Secret Lair "Rick, Steadfast Leader" = the oracle card Greymond,
        # Avacyn's Stalwart): the deck lists the printed name, which /cards/named?exact= 404s on, so
        # the entry carries `scryfall_name` and the audit queries THAT. Without it the miss read as a
        # benign "custom card" 404 and the cost went unverified without moving the exit code.
        sf = fetch(c.get("scryfall_name") or name)
        time.sleep(args.throttle)
        if "_error" in sf:
            err = sf["_error"]
            if "HTTP 404" in err:
                absent.append((name, err))
            else:
                unreachable.append((name, err))
            continue
        compared += 1
        sf_cost = sf.get("mana_cost", "")
        # Double-faced / split / ADVENTURE cards: the per-half cost lives on the faces. Select the
        # face whose NAME matches the cards.json entry -- a deck may model the BACK face (Kaldring,
        # the Rimestaff is the back of Jorn, God of Winter), and hardcoding card_faces[0] diffed
        # Kaldring against Jorn's cost.
        #
        # A NAME MATCH WINS EVEN WHEN THE TOP-LEVEL COST IS FILLED. This used to be gated on
        # `not sf_cost`, which is true for an MDFC but FALSE for an adventure: Scryfall returns
        # layout=adventure with a JOINED top-level mana_cost ("{3}{G} // {2}{G}" for Brightcap
        # Badger // Fungus Frolic) plus correct per-face costs. Both halves therefore diffed
        # against the joined pair and the audit reported two mismatches on card data that is
        # right -- a blocking FAIL on the Fungus deck with nothing to fix.
        # Fall back to the joined top-level string for a split card whose cards.json entry is
        # named by the full "A // B" (no face name matches), and to face 0 when the top-level
        # cost is empty and no name matches.
        if "card_faces" in sf:
            face = next((f for f in sf["card_faces"] if f.get("name") == name), None)
            if face is not None:
                sf_cost = face.get("mana_cost", "")
            elif not sf_cost:
                sf_cost = sf["card_faces"][0].get("mana_cost", "")
        if norm_cost(sf_cost) != norm_cost(local_cost):
            mismatches.append((name, local_cost, sf_cost, sf.get("cmc")))
            continue
        # Cross-check cascade threshold (cascade_max_mv must equal the card's CMC).
        # NOTE: Scryfall puts `cmc` on the WHOLE card and leaves it null on every face, so once we
        # have narrowed to a face the top-level cmc belongs to the other half (an adventure's
        # top-level cmc is the creature's). No current cascade card is double-faced, so rather
        # than diff against the wrong number, say plainly that it was not cross-checked.
        params = c.get("parameters", {})
        if "cascade_max_mv" in params:
            if "card_faces" in sf:
                uncrosschecked.append(
                    (name, "double-faced card: Scryfall reports cmc only for the whole card, so "
                           "the per-face mv is not available to diff against"))
                continue
            cmc = int(sf.get("cmc") or 0)
            if params["cascade_max_mv"] != cmc:
                mismatches.append(
                    (name, f"cascade_max_mv={params['cascade_max_mv']}",
                     f"cmc={cmc}", sf.get("cmc")))

    print(f"Checked {checked} costed cards against Scryfall; {compared} actually compared.\n")
    if mismatches:
        print(f"MISMATCHES ({len(mismatches)}):")
        for name, local, sf, cmc in mismatches:
            print(f"  {name:<28} local={local:<12} scryfall={sf}  (cmc={cmc})")
    elif compared:
        print(f"All {compared} compared mana costs match Scryfall.")
    else:
        print("NOTHING WAS COMPARED -- not one card resolved.")
    if absent:
        print(f"\nNOT RESOLVED ({len(absent)}) -- not on Scryfall (custom/token cards):")
        for name, err in absent:
            print(f"  {name}: {err}")
    if uncrosschecked:
        print(f"\nCOST OK, CASCADE NOT CROSS-CHECKED ({len(uncrosschecked)}) -- the mana cost above "
              f"matched; only the cascade_max_mv == cmc check was skipped:")
        for name, err in uncrosschecked:
            print(f"  {name}: {err}")
    # A CLEAN BILL OF HEALTH OVER ZERO COMPARISONS IS THE BUG THIS BLOCK EXISTS TO PREVENT.
    # Scryfall 429s hard when several agents query it at once. Every card then landed in the
    # unresolved bucket, which did not move the exit code, so the audit printed "All mana costs
    # match Scryfall" and exited 0 having compared NOTHING -- and verify_deck reported card_costs
    # PASS. That masked a real (if benign) adventure-card failure and would mask a genuine miscost
    # just as well. Unverified is not verified: exit non-zero and name the cards.
    if unreachable:
        print(f"\nNOT COMPARED ({len(unreachable)}) -- Scryfall unreachable/rate-limited, so these "
              f"costs are UNVERIFIED (a did-not-run, NOT a pass). Re-run when the limit clears:")
        for name, err in unreachable:
            print(f"  {name}: {err}")

    # A real mismatch outranks incompleteness: it is actionable now, and the NOT COMPARED block
    # above still discloses what went unchecked in the same run.
    if mismatches:
        return 1
    # `not compared` closes the same hole one size smaller: a run in which NOTHING was compared is
    # a did-not-run whatever the reason, so it must not report success even if every card was a
    # benign 404. (A card file of nothing but custom cards legitimately verifies nothing, and
    # saying so is accurate, not a false alarm.)
    return 2 if (unreachable or not compared) else 0


if __name__ == "__main__":
    sys.exit(main())
