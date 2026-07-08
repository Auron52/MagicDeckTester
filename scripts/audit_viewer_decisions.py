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

ORACLE-TEXT CROSS-CHECK (advisory, always run -- static & instant). The param manifest can
only see choices that were IMPLEMENTED as params; if the card modeling dropped a Tier 1-3
clause, a param-only audit is blind to it. So this also reads each card's real oracle text
for choice phrases ("any target", "sacrifice a creature", "search your library", "choose
one", "divided as you choose", ...) and reports any the params do NOT model -- a prompt to
read the card and wire/model or disclose it. Advisory only (regex on prose is fuzzy); it
never changes the exit code.

The <deck> may be a plain-text decklist (.txt) or a Cockatrice deck (.cod).

Usage:
  audit_viewer_decisions.py <deck> [profile] [base_seed] [n_games] [max_turns] [--no-sweep]
  audit_viewer_decisions.py <deck> <profile> [base_seed] [budget] [max_turns] --verify-card "<name>"

  --no-sweep      : static analysis only (param expectations + oracle cross-check, no binary).
                    Fast pre-check usable at implementation time, before a profile exists.
  --verify-card N : seed-search up to `budget` deterministic games biased toward casting card
                    N, then confirm its expected decision type surfaces (VERIFIED), fires-not
                    (HARD_MISS), or the card could not be forced into play (NOT_FORCED). This
                    is the automated form of 5h's targeted repro -- it closes a normal run's
                    UNVERIFIED tail for any card whose cast is forward-reachable. A decision
                    that needs a manufactured state the forward driver can't reach (e.g.
                    retrace, which needs the card already in the graveyard) reports NOT_FORCED.

Exit codes: 0 = clean (or only soft/unverified/advisory), 1 = a decision type is missing or
an unmapped choice-param was found (hard fail), 2 = usage / build error.
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
# NB Soulfire Eruption's board-click targeting REUSES the generic `target` decision at runtime
# (main.cpp soulfire_chooser -> EnumerateTargetSets, source="Soulfire Eruption"); the distinct
# `soulfire_targets` type / WriteSoulfireDecisionJson is dead code, never emitted. So expect
# `target`, not `soulfire_targets`. (Crackle with Power is the same pattern.) Verified by live
# trace: Soulfire cast -> `target` decision, source="Soulfire Eruption", 256 (=2^8) subset opts.
NAME_CHOICES = {
    "Soulfire Eruption": "target",
}

# Every param key that CAN carry a player choice. The self-guard fails if a deck card has
# one of these but it is absent from MANIFEST (new mechanic wired into cards.json but not
# the viewer/manifest). Superset of MANIFEST keys; extend when a new choice param lands.
CHOICE_PARAM_KEYS = set(MANIFEST.keys())

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")


def load_deck_cards(deck_path, cards_json="src/cards/data/cards.json"):
    names = set()
    if deck_path.lower().endswith(".cod"):
        # Cockatrice XML: <card number="4" name="..."/> under the "main" zone.
        import xml.etree.ElementTree as ET
        root = ET.parse(deck_path).getroot()
        for zone in root.iter("zone"):
            if zone.get("name") != "main":
                continue
            for card in zone.iter("card"):
                nm = card.get("name")
                if nm:
                    names.add(nm.strip())
    else:
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


# ---------------------------------------------------------------------------
# ORACLE-TEXT CROSS-CHECK (advisory). The param manifest above can only see choices that
# were IMPLEMENTED as params -- if the card modeling dropped a Tier 1-3 clause (the exact
# "hand-waved the gist" failure analyze-deck warns about), a param-only audit is blind to
# it. So also read the real oracle text for choice-creating phrases and diff what the TEXT
# implies against what the PARAMS model. A gap is advisory (regex on prose is fuzzy), and
# means one of: a genuinely dropped/unmodeled choice (-> Stage 2), a goldfish-inert clause
# (-> disclose; usually carries a bracket note), or a regex false match (-> ignore).
# ---------------------------------------------------------------------------
ORACLE_PATTERNS = [
    ("target",    re.compile(r"\bany target\b|\btarget (creature|permanent|artifact|"
                             r"enchantment|nonland permanent|creature or planeswalker)\b", re.I)),
    ("sacrifice", re.compile(r"\bsacrifice (a|an|another) (creature|land|artifact|"
                             r"permanent|enchantment|nonland permanent)\b", re.I)),
    ("search",    re.compile(r"\bsearch your library\b", re.I)),
    ("scry",      re.compile(r"\bscry \d", re.I)),
    ("surveil",   re.compile(r"\bsurveil \d", re.I)),
    ("modal",     re.compile(r"\bchoose (one|two|three|one or more|up to)\b", re.I)),
    ("divide",    re.compile(r"\bdivided (as you choose|among|evenly)\b", re.I)),
    ("bounce",    re.compile(r"\breturn .{0,40}\bland\b.{0,25}to (its|their) owner'?s? hand", re.I)),
    ("discard",   re.compile(r"\bdiscard (a|one|two|three|\d+) .{0,20}?card(?!.{0,12}at random)", re.I)),
]
INERT_NOTE = re.compile(r"\[[^\]]*(inert|deferred|not modelled|not modeled|resolved by|"
                        r"goldfish|simplified)[^\]]*\]", re.I)


def modeled_tokens(card):
    """The choice tokens the card's PARAMS actually surface a decision for."""
    p = card.get("parameters", {}) or {}
    t = set()
    if real_target(p.get("targeting", "none")) or p.get("spectacle_cost"): t.add("target")
    if p.get("sacrifice_land"):                                            t.add("sacrifice")
    if p.get("etb_scry", 0) > 0 or p.get("cast_scry", 0) > 0:              t.add("scry")
    if p.get("etb_surveil", 0) > 0:                                        t.add("surveil")
    if p.get("damage_divided"):                                           t.add("divide")
    if p.get("etb_bounce_land"):                                          t.add("bounce")
    if p.get("retrace") or p.get("discard_land_damage"):                  t.add("discard")
    if p.get("tutor_to_hand") or p.get("tutor_to_top") or p.get("fetch_land_types"):
        t.add("search")
    return t


def oracle_advisories(card):
    """[(token, snippet, has_inert_note)] for choice phrases in the text NOT modeled by params."""
    text = (card.get("oracle_text") or "")
    if not text:
        return []
    modeled = modeled_tokens(card)
    has_note = bool(INERT_NOTE.search(text))
    # Scan the ORACLE text with implementer bracket-notes stripped -- those are comments, not
    # card text, and their prose causes false matches (e.g. a note mentioning "Scry 3").
    scan = re.sub(r"\[[^\]]*\]", "", text)
    out = []
    for token, rx in ORACLE_PATTERNS:
        m = rx.search(scan)
        if m and token not in modeled:
            s = m.start()
            snippet = scan[max(0, s - 10):s + 40].replace("\n", " ").strip()
            out.append((token, snippet, has_note))
    return out


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


def pick_toward(d, target_lc):
    """Like pick(), but on a main_phase choice PREFER a plan that casts the target card, so a
    seed-search can force a specific card into play to verify its decision surfaces."""
    if d.get("type") == "main_phase":
        plans = d.get("plans", [])
        if not plans:
            return -1
        want = [p for p in plans if target_lc in p.get("summary", "").lower()]
        pool = want or plans
        return max(pool, key=lambda p: len(p.get("summary", "")))["index"]
    return pick(d)


def verify_card(deck, prof, card_name, expected_types, base_seed, budget, max_turns):
    """Seed-search for a game that casts `card_name`, then confirm its expected decision
    type(s) surface. Returns (status, detail): VERIFIED / HARD_MISS / NOT_FORCED.

    Closes the auditor's UNVERIFIED tail without an engine change: instead of hoping a card
    is drawn in the fixed sweep, drive many deterministic games biased toward casting it.
    Type-level attribution (a decision of the expected type appeared in a game where the card
    was cast) -- unambiguous unless the deck has two cards producing the SAME type.
    """
    target_lc = card_name.lower()
    cast_seen_anywhere = False
    for gi in range(budget):
        choices, guard = [], 0
        observed = set()            # (type, source_lc)
        cast_here = False
        while guard < 220:
            guard += 1
            d = step(deck, prof, base_seed, gi, choices, max_turns)
            if d is None:
                break
            t = d.get("type")
            if t not in ("main_phase", "mulligan", "bottom", "?"):
                observed.add((t, (d.get("source") or "").lower()))
            choice = pick_toward(d, target_lc)
            if t == "main_phase" and choice is not None and choice >= 0:
                for pl in d.get("plans", []):
                    if pl.get("index") == choice and target_lc in pl.get("summary", "").lower():
                        cast_here = True
                        break
            choices.append(choice)
        if cast_here:
            cast_seen_anywhere = True
            hit = {t for (t, _) in observed if t in expected_types}
            if hit:
                src_confirmed = any(t in expected_types and target_lc in s for (t, s) in observed)
                return ("VERIFIED", {"seed": base_seed, "game_index": gi,
                                     "types": sorted(hit), "source_confirmed": src_confirmed})
    if cast_seen_anywhere:
        return ("HARD_MISS", {"note": "card was cast but no expected decision surfaced"})
    return ("NOT_FORCED", {"note": f"card not cast in {budget} games from seed {base_seed}"})


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


def print_oracle_crosscheck(cards):
    """Advisory: what the oracle TEXT implies vs what the params model. Never fails the
    build (fuzzy), but every line is a card whose text mentions a choice the modeling does
    not surface -- triage each: real drop -> Stage 2; inert -> disclose; false match -> ignore."""
    findings = []
    for c in cards:
        for token, snippet, note in oracle_advisories(c):
            findings.append((c["name"], token, snippet, note))
    print("\n--- ORACLE-TEXT CROSS-CHECK (advisory) ---")
    if not findings:
        print("  No oracle-text choice phrase is left unmodeled by params. Clean.")
        return
    print("  Card text mentions a choice the params do NOT model a decision for. Triage each\n"
          "  (real dropped choice -> Stage 2/2c-ter; goldfish-inert -> disclose; regex false\n"
          "  match -> ignore). '[note]' = the card carries a disclosed inert/deferred note.")
    for name, token, snippet, note in findings:
        tag = "  [has inert/deferred note]" if note else ""
        print(f"  {name}: text implies '{token}'  (...{snippet}...){tag}")


def main():
    raw = sys.argv[1:]
    no_sweep = "--no-sweep" in raw
    verify_name = None
    if "--verify-card" in raw:
        i = raw.index("--verify-card")
        verify_name = raw[i + 1] if i + 1 < len(raw) else None
        raw = raw[:i] + raw[i + 2:]
    argv = [a for a in raw if not a.startswith("--")]
    if len(argv) < 1:
        print(__doc__)
        return 2
    deck = argv[0]
    prof = argv[1] if len(argv) > 1 else None
    base_seed = int(argv[2]) if len(argv) > 2 else 9001
    n_games   = int(argv[3]) if len(argv) > 3 else 40
    max_turns = int(argv[4]) if len(argv) > 4 else 12

    # --verify-card mode: seed-search to confirm one card's decision surfaces (targeted repro).
    if verify_name:
        if prof is None:
            print("ERROR: --verify-card needs a profile path.")
            return 2
        cards, _ = load_deck_cards(deck)
        match = next((c for c in cards if c["name"].lower() == verify_name.lower()), None)
        if match is None:
            print(f"ERROR: '{verify_name}' not found in {deck}.")
            return 2
        exp, _ = expected_for_card(match)
        exp.discard("main_phase")
        if not exp:
            print(f"{verify_name}: no param-driven interactive decision expected. Nothing to verify.")
            return 0
        print(f"Verifying '{verify_name}' surfaces {sorted(exp)} "
              f"(seed-searching {n_games} games from {base_seed})...")
        status, detail = verify_card(deck, prof, match["name"], exp, base_seed, n_games, max_turns)
        print(f"  {status}: {detail}")
        return 1 if status == "HARD_MISS" else 0
    if not no_sweep and not os.path.exists(BIN):
        print(f"ERROR: {BIN} not found -- build Release first "
              f"(cmake --build build --config Release), or pass --no-sweep for static-only.")
        return 2

    cards, names = load_deck_cards(deck)
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

    # ---- oracle-text cross-check (advisory, always run -- it is static & instant) ----
    print_oracle_crosscheck(cards)

    # ---- self-guard: unmapped choice params are a hard fail -----------------
    if guard_fail:
        print("\nSELF-GUARD FAILURE -- choice-bearing param(s) with no manifest row "
              "(mechanic in cards.json but not wired/mapped to a viewer decision):")
        for nm, keys in guard_fail.items():
            print(f"  {nm}: {sorted(keys)}")
        print("Add each to MANIFEST in this script (and wire it per tools/play/DECISIONS.md).")
        return 1

    if no_sweep:
        print("\n--no-sweep: static analysis only (param expectations + oracle cross-check). "
              "Run without --no-sweep to verify decisions actually surface.")
        return 0

    if not expected_types:
        print("No param-driven interactive decisions expected for this deck. PASS.")
        return 0
    if prof is None:
        print("ERROR: a profile path is required for the dynamic sweep "
              "(or pass --no-sweep for static-only).")
        return 2

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
