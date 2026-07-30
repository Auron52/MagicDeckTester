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
DIVERGENCES = ROOT / "src/cards/data/scryfall_divergences.json"

SUPERTYPES = {"Legendary", "Basic", "Snow", "World", "Ongoing", "Host", "Elite"}
ORACLE_SIMILARITY_MIN = 0.80   # below this, the oracle text is flagged (advisory)

# --- Systematic (non-per-card) divergences: stripped in-code, NOT allowlisted ---
# Scryfall reports these in `keywords`, but the engine intentionally does NOT tag
# them in cards.json's `keywords` field -- they are either ability words (italic,
# no inherent rules meaning) or keyword abilities modeled STRUCTURALLY via params /
# oracle logic (confirmed: these cards work in their adopted decks). Stripping them
# from the Scryfall side avoids false HARD mismatches while STILL catching a
# genuinely-missing combat/evasion keyword (flying, trample, deathtouch, lifelink,
# first strike, double strike, vigilance, reach, menace, hexproof, indestructible...).
MODELED_ELSEWHERE_KEYWORDS = {
    # Ability words -- never real keywords:
    "landfall", "ferocious", "spectacle", "rot fly", "constellation", "addendum",
    "revolt", "raid", "delirium", "threshold", "hellbent", "morbid",
    # Keyword abilities this engine models via params / oracle logic, not a tag:
    "cycling", "scry", "surveil", "cascade", "retrace", "replicate",
    "affinity", "treasure",
    # Echo (echo_cost param, upkeep pay-or-sac decision) and Channel (channel_cost/
    # channel_damage, a from-hand discard-activated ability) are modeled STRUCTURALLY
    # via params, not as a keyword tag -- neither is a combat/evasion keyword, so
    # stripping them is safe (mirrors cycling/cascade). Goblins: Mogg War Marshal &
    # Stingscourger (echo), Twinshot Sniper (channel).
    "echo", "channel",
}
# Supertypes the engine does NOT model because they are inert in goldfishing:
# Basic-ness is derived from the card name; the World rule never fires (one
# permanent). Legendary (legend rule) and Snow (snow mana) ARE modeled, so they
# stay HARD-checked and are deliberately absent here.
IGNORED_SUPERTYPES = {"Basic", "World"}


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
    """'Legendary Creature — Human Monk' -> (supertypes, types, subtypes) as sorted lists.

    For modal double-faced cards Scryfall reports a combined 'Front // Back' type line;
    cards.json models the FRONT face (the DB synthesizes the back via mdfc_back_name), so
    compare only the front face (e.g. Branchloft Pathway 'Land // Land' -> 'Land')."""
    type_line = (type_line or "").split("//")[0].strip()
    left, _, right = type_line.partition("—")
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


def pt_component_mismatch(a, b):
    """True if two P/T components differ AND neither is a CDA placeholder ('*').
    Adeline's power is '*' (a characteristic-defining ability computed at runtime),
    so cards.json's numeric base is a modeling placeholder, not a real mismatch --
    but we still catch a genuinely-wrong toughness on such a card (component-wise)."""
    if a is None and b is None:
        return False
    if (a and "*" in a) or (b and "*" in b):
        return False
    return a != b


def diff_card(local, ref, allow):
    """Compare one card to its snapshot. Returns (hard, allowlisted, oracle_advisory):
       hard        = ["<field> <detail>", ...]   -- real, un-allowlisted mismatches
       allowlisted = [(field, detail, reason), ...] -- intentional per-card divergences
       oracle_advisory = str|None."""
    name = local.get("name")
    findings = []  # (field, detail)

    lc, rc = norm_cost(local.get("mana_cost")), norm_cost(ref.get("mana_cost"))
    if lc != rc:
        findings.append(("mana_cost", f"local={local.get('mana_cost')!r} scryfall={ref.get('mana_cost')!r}"))

    lp, rp = as_int_str(local.get("power")), as_int_str(ref.get("power"))
    lt, rt = as_int_str(local.get("toughness")), as_int_str(ref.get("toughness"))
    if pt_component_mismatch(lp, rp) or pt_component_mismatch(lt, rt):
        findings.append(("P/T", f"local={lp}/{lt} scryfall={rp}/{rt}"))

    r_supers, r_types, r_subs = parse_type_line(ref.get("type_line", ""))
    r_supers = [s for s in r_supers if s not in IGNORED_SUPERTYPES]        # Basic/World inert
    l_types = sorted(local.get("types", []))
    l_subs = sorted(local.get("subtypes", []))
    l_supers = sorted(s for s in local.get("supertypes", []) if s not in IGNORED_SUPERTYPES)
    # The engine leaves a land's printed subtypes empty and derives them at runtime
    # (SpellEffects.h: "the land's printed m_subtypes is empty"), so land subtype
    # diffs are systematic, not per-card. Creature/other subtypes stay checked.
    is_land = "Land" in l_types
    if l_types != r_types:
        findings.append(("types", f"local={l_types} scryfall={r_types}"))
    if not is_land and l_subs != r_subs:
        findings.append(("subtypes", f"local={l_subs} scryfall={r_subs}"))
    if l_supers != r_supers:
        findings.append(("supertypes", f"local={l_supers} scryfall={r_supers}"))

    l_kw = sorted(k.lower() for k in local.get("keywords", []))
    r_kw = sorted(k for k in (kw.lower() for kw in ref.get("keywords", []))
                  if k not in MODELED_ELSEWHERE_KEYWORDS)
    if l_kw != r_kw:
        findings.append(("keywords", f"local={l_kw} scryfall={r_kw}"))

    # Split findings by the per-card allowlist.
    card_allow = allow.get(name, {})
    hard, allowlisted = [], []
    for field, detail in findings:
        reason = card_allow.get(field)
        if reason:
            allowlisted.append((field, detail, reason))
        else:
            hard.append(f"{field} {detail}")

    advisory = None
    lo, ro = norm_oracle(local.get("oracle_text"), name), norm_oracle(ref.get("oracle_text"), name)
    if lo != ro:
        sim = difflib.SequenceMatcher(None, lo, ro).ratio()
        if sim < ORACLE_SIMILARITY_MIN:
            advisory = f"oracle_text diverges (similarity {sim:.2f}); scryfall={ref.get('oracle_text')!r}"
    return hard, allowlisted, advisory


def load_allowlist():
    """Per-card intentional divergences (card -> field -> reason). Missing file = {}."""
    if not DIVERGENCES.exists():
        return {}
    raw = json.loads(DIVERGENCES.read_text())
    return {k: v for k, v in raw.items() if not k.startswith("_")}  # drop _README


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
    allow = load_allowlist()
    mismatches, unfetched, advisories, allowed_all, checked = [], [], [], [], 0
    used_allow = set()  # (card, field) allowlist entries that actually fired
    for c in cards:
        name = c.get("name")
        if not name or not norm_cost(c.get("mana_cost")) and not c.get("types"):
            continue
        if name not in ref:
            unfetched.append(name)
            continue
        checked += 1
        hard, allowlisted, adv = diff_card(c, ref[name], allow)
        if hard:
            mismatches.append((name, hard))
        for field, detail, reason in allowlisted:
            allowed_all.append((name, field, detail, reason))
            used_allow.add((name, field))
        if adv:
            advisories.append((name, adv))

    # Stale allowlist entries: a card+field is allowlisted but no longer mismatches
    # (e.g. cards.json was fixed). Report so the ledger stays honest -- non-blocking.
    stale = [(n, f) for n, fields in allow.items() for f in fields if (n, f) not in used_allow]

    if as_json:
        print(json.dumps({
            "ok": not mismatches and not unfetched,
            "checked": checked,
            "mismatches": [{"name": n, "issues": h} for n, h in mismatches],
            "unfetched": unfetched,
            "allowlisted": [{"name": n, "field": f, "detail": d, "reason": r}
                            for n, f, d, r in allowed_all],
            "stale_allowlist": [{"name": n, "field": f} for n, f in stale],
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
        print("All hard fields (cost, P/T, types, keywords) match the snapshot "
              "(after systematic + allowlisted divergences).")
    if allowed_all:
        print(f"\nALLOWLISTED DIVERGENCES ({len(allowed_all)}) -- intentional, "
              f"see {DIVERGENCES.relative_to(ROOT)}:")
        for n, f, d, r in allowed_all:
            print(f"  {n} [{f}]: {d}\n      reason: {r}")
    if stale:
        print(f"\nSTALE ALLOWLIST ({len(stale)}) -- entry no longer mismatches; "
              f"remove it from {DIVERGENCES.relative_to(ROOT)}:")
        for n, f in stale:
            print(f"  {n} [{f}]")
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
    ap.add_argument("--throttle", type=float, default=0.25,
                    help="seconds between API calls (--update). Default 0.25s: Scryfall 429-throttles "
                         "a sustained 100ms burst partway through ~100 cards, so stay well under its limit.")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    d = json.loads(Path(args.cards).read_text())
    cards = d if isinstance(d, list) else list(d.get("cards", d.values()))

    if args.update:
        return do_update(cards, args.throttle)
    return do_audit(cards, args.json)


if __name__ == "__main__":
    sys.exit(main())
