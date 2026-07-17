#!/usr/bin/env python3
"""Audit cards.json FIELDS against an authoritative Scryfall snapshot (workstream 2).

Extends the cost-only reality-diff (audit_card_costs.py) to every Scryfall-checkable field:
mana_cost, power/toughness, type line (types/subtypes/supertypes), keywords, AND the
verbatim oracle_text -- catching P/T fabrication, wrong types/keywords, and the
oracle-text drift/fabrication class (Irencrag Feat's invented "Add six {R}").

Design: the authoritative Scryfall data is a COMMITTED snapshot
(src/cards/data/scryfall_reference.json), fetched deliberately with --update where the
network is available. The default (offline) mode diffs cards.json against that snapshot --
fast, deterministic, network-free, CI-friendly. This beats live-fetching on every gate run
(slow and flaky). The engine never reads oracle_text (CardDatabase strips it), so this is
the ONLY thing reconciling the hand-authored text/fields against reality.

Usage:
    python scripts/audit_card_fields.py                 # offline diff vs the snapshot
    python scripts/audit_card_fields.py --update        # (network) refresh the snapshot
    python scripts/audit_card_fields.py --json           # machine-readable

Exit 1 if any HARD-field mismatch OR the snapshot is missing/incomplete (fails CLOSED --
an unfetched card is a pending item, never a silent pass). oracle_text divergence is
ADVISORY (prose normalisation is fuzzy) but always reported. Gate it via verify_deck.py.
"""
import argparse
import difflib
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARDS = ROOT / "src/cards/data/cards.json"
REFERENCE = ROOT / "src/cards/data/scryfall_reference.json"

SUPERTYPES = {"Legendary", "Basic", "Snow", "World", "Ongoing", "Host", "Elite"}
ORACLE_SIMILARITY_MIN = 0.80   # below this, the oracle text is flagged (advisory)


# ------------------------------------------------------------------ normalisation
def norm_cost(c):
    return (c or "").replace(" ", "").upper()


def norm_oracle(text, name):
    """Lower-case, collapse whitespace, and replace the card's own name (Scryfall uses the
    full name; cards.json may use 'this creature'/'~') so the diff is about SUBSTANCE."""
    t = (text or "").lower()
    if name:
        t = t.replace(name.lower(), "~")
    t = re.sub(r"\bthis (creature|permanent|card|land|spell|artifact|enchantment)\b", "~", t)
    t = re.sub(r"\s+", " ", t)
    return t.strip()


def parse_type_line(type_line):
    """'Legendary Creature — Human Monk' -> (supertypes, types, subtypes) as sorted lists."""
    left, _, right = (type_line or "").partition("—")
    if "—" not in (type_line or "") and "-" in (type_line or "") and " - " in type_line:
        left, _, right = type_line.partition(" - ")
    lwords = left.split()
    supers = [w for w in lwords if w in SUPERTYPES]
    types = [w for w in lwords if w not in SUPERTYPES]
    subs = right.split()
    return sorted(supers), sorted(types), sorted(subs)


# ------------------------------------------------------------------ snapshot (network)
def do_update(cards, throttle):
    import audit_card_costs as acc   # reuse the throttled, 429-retrying fetch
    ref = {}
    if REFERENCE.exists():
        ref = json.loads(REFERENCE.read_text())
    errors = []
    for i, c in enumerate(cards):
        name = c.get("name")
        if not name:
            continue
        sf = acc.fetch(name)
        if "_error" in sf:
            errors.append((name, sf["_error"]))
            continue
        ref[name] = {
            "mana_cost": sf.get("mana_cost", ""),
            "cmc": sf.get("cmc"),
            "power": sf.get("power"),
            "toughness": sf.get("toughness"),
            "type_line": sf.get("type_line", ""),
            "keywords": sorted(sf.get("keywords", [])),
            "oracle_text": sf.get("oracle_text", ""),
        }
        print(f"  [{i + 1}/{len(cards)}] {name}", file=sys.stderr)
        time.sleep(throttle)
    REFERENCE.write_text(json.dumps(ref, indent=2, sort_keys=True, ensure_ascii=False) + "\n")
    print(f"\nSnapshot written: {REFERENCE.relative_to(ROOT)}  ({len(ref)} cards)")
    if errors:
        print(f"NOT FETCHED ({len(errors)}):")
        for n, e in errors:
            print(f"  {n}: {e}")
        return 1
    return 0


# ------------------------------------------------------------------ offline diff
def as_int_str(v):
    return None if v is None else str(v).strip()


def diff_card(local, ref):
    """Return (hard_findings, oracle_advisory|None) comparing one card to its snapshot."""
    name = local.get("name")
    hard = []

    lc, rc = norm_cost(local.get("mana_cost")), norm_cost(ref.get("mana_cost"))
    if lc != rc:
        hard.append(f"mana_cost local={local.get('mana_cost')!r} scryfall={ref.get('mana_cost')!r}")

    lp, rp = as_int_str(local.get("power")), as_int_str(ref.get("power"))
    lt, rt = as_int_str(local.get("toughness")), as_int_str(ref.get("toughness"))
    if lp != rp or lt != rt:
        hard.append(f"P/T local={lp}/{lt} scryfall={rp}/{rt}")

    r_supers, r_types, r_subs = parse_type_line(ref.get("type_line", ""))
    l_types = sorted(local.get("types", []))
    l_subs = sorted(local.get("subtypes", []))
    l_supers = sorted(local.get("supertypes", []))
    if l_types != r_types:
        hard.append(f"types local={l_types} scryfall={r_types}")
    if l_subs != r_subs:
        hard.append(f"subtypes local={l_subs} scryfall={r_subs}")
    if l_supers != r_supers:
        hard.append(f"supertypes local={l_supers} scryfall={r_supers}")

    l_kw = sorted(k.lower() for k in local.get("keywords", []))
    r_kw = sorted(k.lower() for k in ref.get("keywords", []))
    if l_kw != r_kw:
        hard.append(f"keywords local={l_kw} scryfall={r_kw}")

    advisory = None
    lo, ro = norm_oracle(local.get("oracle_text"), name), norm_oracle(ref.get("oracle_text"), name)
    if lo != ro:
        sim = difflib.SequenceMatcher(None, lo, ro).ratio()
        if sim < ORACLE_SIMILARITY_MIN:
            advisory = f"oracle_text diverges (similarity {sim:.2f}); scryfall={ref.get('oracle_text')!r}"
    return hard, advisory


def do_audit(cards, as_json):
    if not REFERENCE.exists():
        msg = (f"Scryfall snapshot missing: {REFERENCE.relative_to(ROOT)}. "
               f"Run `python scripts/audit_card_fields.py --update` where the network is available.")
        if as_json:
            print(json.dumps({"ok": False, "reason": "snapshot_missing"}))
        else:
            print("FAIL (fails closed): " + msg)
        return 1
    ref = json.loads(REFERENCE.read_text())
    mismatches, unfetched, advisories, checked = [], [], [], 0
    for c in cards:
        name = c.get("name")
        if not name or not norm_cost(c.get("mana_cost")) and not c.get("types"):
            continue
        if name not in ref:
            unfetched.append(name)
            continue
        checked += 1
        hard, adv = diff_card(c, ref[name])
        if hard:
            mismatches.append((name, hard))
        if adv:
            advisories.append((name, adv))

    if as_json:
        print(json.dumps({
            "ok": not mismatches and not unfetched,
            "checked": checked,
            "mismatches": [{"name": n, "issues": h} for n, h in mismatches],
            "unfetched": unfetched,
            "oracle_advisories": [{"name": n, "detail": a} for n, a in advisories],
        }, indent=2))
        return 1 if (mismatches or unfetched) else 0

    print(f"Checked {checked} cards against the Scryfall snapshot.\n")
    if mismatches:
        print(f"HARD MISMATCHES ({len(mismatches)}):")
        for n, issues in mismatches:
            print(f"  {n}:")
            for i in issues:
                print(f"      {i}")
    else:
        print("All hard fields (cost, P/T, types, keywords) match the snapshot.")
    if unfetched:
        print(f"\nUNFETCHED ({len(unfetched)}) -- not in the snapshot; run --update (fails closed):")
        for n in unfetched:
            print(f"  {n}")
    if advisories:
        print(f"\nORACLE-TEXT ADVISORIES ({len(advisories)}) -- verbatim divergence (fuzzy; verify):")
        for n, a in advisories:
            print(f"  {n}: {a}")
    return 1 if (mismatches or unfetched) else 0


def main():
    ap = argparse.ArgumentParser(description="Audit cards.json fields vs an authoritative Scryfall snapshot.")
    ap.add_argument("--cards", default=str(CARDS))
    ap.add_argument("--update", action="store_true", help="(network) refresh the Scryfall snapshot")
    ap.add_argument("--throttle", type=float, default=0.1, help="seconds between API calls (--update)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    d = json.loads(Path(args.cards).read_text())
    cards = d if isinstance(d, list) else list(d.get("cards", d.values()))

    if args.update:
        return do_update(cards, args.throttle)
    return do_audit(cards, args.json)


if __name__ == "__main__":
    sys.exit(main())
