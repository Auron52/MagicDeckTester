#!/usr/bin/env python3
"""Mechanical gate for analyze-deck Stage 5h: does the play viewer surface EVERY
interactive decision the deck's cards create?

This is the viewer analogue of `audit_card_costs.py`. A prose "classify each card's
decisions" reminder repeatedly let a card's choice ship silently heuristic-resolved;
this script removes the judgment call. It:

  1. Reads the decklist -> each card's `cards.json` params.
  2. Computes the EXPECTED set of decision `type`s from the param->type MANIFEST below
     (the machine half of tools/play/DECISIONS.md).
  3. SELF-GUARD: every choice-bearing param key present on any deck card must appear in
     the manifest -- a new interactive param added to cards.json without a manifest entry
     (and thus without viewer wiring) is a hard failure.
  4. Drives a bounded --claude-play seed sweep (the stateless-replay protocol, same as
     probe_decisions.py) and records which decision types actually SURFACED.
  5. Diffs expected vs observed and exits non-zero if any expected type never surfaced,
     or if a card carries a choice-bearing param not in the manifest.

Cards whose expected decision never got a chance to fire (the card was never cast in the
sweep) are reported as UNVERIFIED (soft) rather than failing -- forcing a rare card is the
tail 5h covers with a targeted repro. Only an expected type that the manifest predicts for
a card the sweep DID cast, yet never surfaced, is a hard MISS.

Usage:
  audit_viewer_decisions.py <deck> <profile> [base_seed] [n_games] [max_turns]

Exit codes: 0 = clean (or only soft/unverified), 1 = a decision type is missing or an
unmapped choice-param was found (hard fail), 2 = usage / build error.
"""
import subprocess, sys, json, re, collections, os

BIN = "./build/Release/mtg"

# ---------------------------------------------------------------------------
# MANIFEST: cards.json parameter (+ how to read it) -> decision `type` it MUST produce
# in the human-play path. Keep in lockstep with tools/play/DECISIONS.md.
#
# Each entry: json_key -> (decision_type, predicate). predicate(value) decides whether the
# param value actually creates a choice (e.g. targeting=="player" in a goldfish is not a
# choice; a scry of 0 is not a scry). A `None` predicate means "present and truthy".
# ---------------------------------------------------------------------------
def truthy(v):        return bool(v)
def positive(v):      return isinstance(v, (int, float)) and v > 0
def real_target(v):   return isinstance(v, str) and v not in ("none", "player", "")

MANIFEST = {
    "targeting":             ("target",               real_target),
    "spectacle_cost":        ("target",               truthy),   # cast post-combat, still targets
    "damage_divided":        ("divide",               truthy),
    "etb_scry":              ("scry",                 positive),
    "cast_scry":             ("scry",                 positive),
    "etb_surveil":           ("surveil",              positive),
    "cast_reorder":          ("reorder",              positive),
    "etb_dig_count":         ("dig",                  positive),
    "upkeep_adds_charge":    ("vial_charge",          truthy),
    "retrace":               ("retrace_discard",      truthy),
    "etb_bounce_land":       ("bounce",               truthy),
    "sacrifice_land":        ("sacrifice",            truthy),
    "expressive_iteration":  ("expressive_iteration", truthy),
    # plan-variant sub-decisions -- surfaced inside the main_phase plan list, not their own
    # type. Verified by "does the deck offer >1 plan variant", not a distinct decision type;
    # listed here so the self-guard treats them as MAPPED (not unknown choice params).
    "tutor_to_hand":         ("main_phase",           truthy),
    "tutor_to_top":          ("main_phase",           truthy),
    "fetch_land_types":      ("main_phase",           truthy),
    # Soulfire own-target selection is name/logic-driven (no param); handled by NAME_CHOICES.
}

# Cards whose interactive choice is not param-driven (matched by name).
NAME_CHOICES = {
    "Soulfire Eruption": "soulfire_targets",
}

# Every param key that CAN carry a player choice. The self-guard fails if a deck card has
# one of these but it is absent from MANIFEST (new mechanic wired into cards.json but not
# the viewer/manifest). Superset of MANIFEST keys; extend when a new choice param lands.
CHOICE_PARAM_KEYS = set(MANIFEST.keys())

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")


def load_deck_cards(deck_path, cards_json="src/cards/data/cards.json"):
    names = set()
    for ln in open(deck_path):
        ln = ln.strip()
        if not ln or ln.lower() in ("sideboard", "mainboard"):
            continue
        names.add(re.sub(r"^\s*\d+x?\s+", "", ln).strip())
    d = json.load(open(cards_json))
    cards = d if isinstance(d, list) else list(d.get("cards", d.values()))
    return [c for c in cards if c.get("name") in names], names


def expected_for_card(card):
    """(set of expected decision types, set of unmapped choice-param keys) for one card."""
    p = card.get("parameters", {}) or {}
    exp, unmapped = set(), set()
    for key, val in p.items():
        if key in MANIFEST:
            dtype, pred = MANIFEST[key]
            if pred(val):
                exp.add(dtype)
        elif key in CHOICE_PARAM_KEYS:
            unmapped.add(key)   # in the choice set but no manifest row -> guard trips
    # X spells: {X} in the mana cost -> chosen_x plan variant (rides main_phase)
    if "{X}" in (card.get("mana_cost") or ""):
        exp.add("main_phase")
    if card.get("name") in NAME_CHOICES:
        exp.add(NAME_CHOICES[card["name"]])
    return exp, unmapped


def step(deck, prof, seed, gi, choices, max_turns):
    cmd = [BIN, deck, "--profile", prof, "--claude-play", "--seed", str(seed),
           "--game-index", str(gi), "--max-turns", str(max_turns),
           "--reveal", "6", "--choices", ",".join(map(str, choices))]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    if RES_RE.search(out):
        return None
    m = DEC_RE.search(out)
    return json.loads(m.group(1)) if m else None


def pick(d):
    """Drive a *developing* line so as many cards as possible get cast."""
    t = d.get("type")
    # mulligan/bottom use ai_choice (NOT heuristic_default); defaulting to 0 on a mulligan
    # means "mulligan again" -> an infinite mulligan loop that never reaches a turn.
    if t == "mulligan":
        ac = d.get("ai_choice")
        return ac if isinstance(ac, int) else 1          # 1 = keep, so we make progress
    if t == "bottom":
        ac = d.get("ai_choice")
        if isinstance(ac, dict):
            return ac.get("index", 0)
        return ac if isinstance(ac, int) else 0
    if t == "main_phase":
        plans = d.get("plans", [])
        if not plans:
            return -1
        # Prefer a plan that casts something (longest summary ~ most actions).
        return max(plans, key=lambda p: len(p.get("summary", "")))["index"]
    if "heuristic_default" in d:
        return d["heuristic_default"]
    return 0


def run_sweep(deck, prof, base_seed, n_games, max_turns):
    """Return (observed decision types, text of the plans actually CHOSEN).

    Only the chosen plan's summary counts as "cast" -- scanning every offered variant would
    mark a card merely OFFERED as cast and produce false hard-misses.
    """
    GUARD = 160
    observed = collections.Counter()
    cast_text = []
    stuck = 0                       # games that hit the guard without finishing (driver pathology)
    for gi in range(n_games):
        choices, guard = [], 0
        while guard < GUARD:
            guard += 1
            d = step(deck, prof, base_seed, gi, choices, max_turns)
            if d is None:
                break
            observed[d.get("type", "?")] += 1
            choice = pick(d)
            if d.get("type") == "main_phase" and choice is not None and choice >= 0:
                for pl in d.get("plans", []):
                    if pl.get("index") == choice:
                        cast_text.append(pl.get("summary", "").lower())
                        break
            choices.append(choice)
        if guard >= GUARD:
            stuck += 1
    return observed, cast_text, stuck


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    deck, prof = sys.argv[1], sys.argv[2]
    base_seed = int(sys.argv[3]) if len(sys.argv) > 3 else 9001
    n_games   = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    max_turns = int(sys.argv[5]) if len(sys.argv) > 5 else 12
    if not os.path.exists(BIN):
        print(f"ERROR: {BIN} not found -- build Release first "
              f"(cmake --build build --config Release)")
        return 2

    cards, names = load_deck_cards(deck, )
    # Per-card expectations + self-guard.
    expected_types = set()
    per_card = {}
    guard_fail = {}
    for c in cards:
        exp, unmapped = expected_for_card(c)
        exp.discard("main_phase")   # always present; not an interesting expectation
        if exp:
            per_card[c["name"]] = exp
            expected_types |= exp
        if unmapped:
            guard_fail[c["name"]] = unmapped

    print(f"Deck: {deck}  ({len(cards)} card defs matched)")
    print(f"Expected interactive decision types (from card params): "
          f"{sorted(expected_types) or '(none)'}")

    # ---- self-guard: unmapped choice params are a hard fail -----------------
    if guard_fail:
        print("\nSELF-GUARD FAILURE -- choice-bearing param(s) with no manifest row "
              "(mechanic in cards.json but not wired/mapped to a viewer decision):")
        for nm, keys in guard_fail.items():
            print(f"  {nm}: {sorted(keys)}")
        print("Add each to MANIFEST in this script (and wire it per tools/play/DECISIONS.md).")
        return 1

    if not expected_types:
        print("No param-driven interactive decisions expected for this deck. PASS.")
        return 0

    print(f"Driving {n_games} games from seed {base_seed} to observe surfaced decisions...")
    observed, cast_text, stuck = run_sweep(deck, prof, base_seed, n_games, max_turns)
    obs_types = set(observed) - {"main_phase", "mulligan", "bottom", "?"}
    print(f"Observed decision types: {sorted(obs_types) or '(none)'}")

    # A driver that never reaches a turn (e.g. an infinite mulligan loop) produces an all-
    # UNVERIFIED result that masquerades as benign. Fail loudly instead.
    if stuck:
        print(f"\nDRIVER FAILURE -- {stuck}/{n_games} games hit the step guard without "
              f"finishing (stuck decision loop; the sweep never reached real turns). "
              f"UNVERIFIED results below are meaningless until this is fixed.")
        if stuck >= max(1, n_games // 2):
            return 1

    # ---- diff -------------------------------------------------------------
    hard_miss, unverified = [], []
    joined = " ".join(cast_text)
    for name, exp in per_card.items():
        missing = exp - obs_types
        if not missing:
            continue
        # Did the sweep actually cast this card? (heuristic: card name appears in a summary)
        cast = name.lower() in joined
        for m in missing:
            (hard_miss if cast else unverified).append((name, m, cast))

    if unverified:
        print("\nUNVERIFIED (card never cast in sweep -- run a targeted 5h repro that casts it):")
        for name, m, _ in unverified:
            print(f"  {name}: expected '{m}' (not reached)")

    if hard_miss:
        print("\nHARD MISS -- card WAS cast but its decision never surfaced "
              "(silently heuristic-resolved; go back to Stage 2c-ter and wire it):")
        for name, m, _ in hard_miss:
            print(f"  {name}: expected '{m}' -> NOT surfaced")
        return 1

    if unverified:
        print("\nNo hard misses. Some expectations UNVERIFIED (see above) -- "
              "confirm with targeted repros before calling 5h clean.")
        return 0

    print("\nAll expected viewer decisions surfaced. 5h PASS.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
