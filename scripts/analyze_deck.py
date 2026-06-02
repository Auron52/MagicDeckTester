#!/usr/bin/env python3
"""
analyze_deck.py  —  Phase 1.2 deck analysis orchestrator

Workflow:
  1. Parse the decklist and look up each card on Scryfall.
  2. Check which cards are already in src/cards/data/cards.json or src/cards/custom/.
  3. For missing cards, ask Claude to generate JSON definitions (Tier 1/2) or
     C++ implementations (Tier 3). Write the generated files to the repo.
  4. If any C++ files were written, rebuild the project with cmake.
  5. Run mtg-analyze on the deck and capture the JSON analysis output.
  6. Ask Claude to interpret the analysis and produce a DeckProfile (heuristics).
  7. Print the rules for user review.
  8. Save the DeckProfile to <deckname>.profile.json.

Usage:
    python scripts/analyze_deck.py <decklist> [--games N] [--output path]

Environment:
    ANTHROPIC_API_KEY  — required for Claude API calls
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import anthropic
import requests

# ---------------------------------------------------------------------------
# Paths (relative to repo root, resolved at startup)
# ---------------------------------------------------------------------------

REPO_ROOT    = Path(__file__).parent.parent.resolve()
CARDS_JSON   = REPO_ROOT / "src" / "cards" / "data" / "cards.json"
CUSTOM_DIR   = REPO_ROOT / "src" / "cards" / "custom"
BUILD_DIR    = REPO_ROOT / "build"
ANALYZER_BIN = BUILD_DIR / "Release" / "mtg-analyze.exe"
CLAUDE_MODEL = "claude-sonnet-4-6"

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def ParseArgs():
    parser = argparse.ArgumentParser(description="Analyze a Magic deck and produce a DeckProfile.")
    parser.add_argument("decklist", help="Path to .txt or .cod decklist")
    parser.add_argument("--games",  type=int, default=500,
                        help="Number of analysis games (default: 500)")
    parser.add_argument("--output", default=None,
                        help="Output path for the DeckProfile JSON (default: <deckname>.profile.json)")
    return parser.parse_args()

# ---------------------------------------------------------------------------
# Decklist parsing
# ---------------------------------------------------------------------------

def LoadDeckNames(path: Path) -> list[str]:
    """Return a deduplicated list of card names from a plain-text decklist."""
    names = []
    seen  = set()
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or line.startswith("#"):
                continue
            # Strip count prefix: "4 Card Name" or "4x Card Name"
            match = re.match(r"^\d+x?\s+(.*)", line)
            name  = match.group(1).strip() if match else line.strip()
            # Strip Arena set suffix: "Card Name (SET) 123"
            name  = re.sub(r"\s+\([A-Z0-9]+\)\s+\d+$", "", name)
            if name and name not in seen:
                seen.add(name)
                names.append(name)
    return names

# ---------------------------------------------------------------------------
# Scryfall
# ---------------------------------------------------------------------------

SCRYFALL_EXACT = "https://api.scryfall.com/cards/named"

def FetchScryfallCard(name: str) -> dict | None:
    """Fetch a card's data from Scryfall by exact name. Returns None on 404."""
    try:
        resp = requests.get(SCRYFALL_EXACT, params={"exact": name}, timeout=10)
        if resp.status_code == 404:
            return None
        resp.raise_for_status()
        return resp.json()
    except requests.RequestException as e:
        print(f"  Warning: Scryfall request failed for '{name}': {e}", file=sys.stderr)
        return None

def FetchAllCards(names: list[str]) -> dict[str, dict]:
    """Fetch Scryfall data for each name, with polite rate limiting."""
    results = {}
    for i, name in enumerate(names):
        print(f"  Scryfall [{i+1}/{len(names)}]: {name}")
        data = FetchScryfallCard(name)
        if data:
            results[name] = data
        else:
            print(f"    Not found on Scryfall: {name}", file=sys.stderr)
        time.sleep(0.1)  # Scryfall rate limit: max 10 req/s
    return results

# ---------------------------------------------------------------------------
# Card coverage check
# ---------------------------------------------------------------------------

def LoadImplementedNames() -> set[str]:
    """Return names of all cards currently in cards.json or src/cards/custom/."""
    implemented = set()

    if CARDS_JSON.exists():
        with open(CARDS_JSON) as f:
            data = json.load(f)
        for card in data.get("cards", []):
            if "name" in card:
                implemented.add(card["name"])

    for cpp_file in CUSTOM_DIR.glob("*.cpp"):
        # Convention: file name matches card name with spaces replaced by underscores
        implemented.add(cpp_file.stem.replace("_", " "))

    return implemented

# ---------------------------------------------------------------------------
# Claude: card implementation generation
# ---------------------------------------------------------------------------

CARD_IMPL_SYSTEM = """You are a C++ game engine developer implementing Magic: The Gathering cards
for a simulator. You generate card definitions in one of two forms:

1. JSON entry for src/cards/data/cards.json (Tier 1: pure data, Tier 2: named template).
   Use this for: basic lands, vanilla creatures, simple damage spells, simple draw spells,
   mana dorks, counterspells, removal, pump spells, lord effects.

2. A note that the card requires Tier 3 custom C++ (complex unique mechanics).

For each card, output a JSON object with the following schema:
{
  "tier": 1 or 2 or 3,
  "entry": {                         // present for tiers 1 and 2
    "name": "...",
    "mana_cost": "{1}{R}",           // use {W}{U}{B}{R}{G}{C}{X}{N} symbols
    "mana_value": <int>,
    "types": ["Creature"],           // Land, Creature, Instant, Sorcery, Enchantment, Artifact, Planeswalker
    "subtypes": ["Elf", "Druid"],
    "keywords": ["Haste"],           // exact keyword names from the oracle text
    "power": <int or null>,
    "toughness": <int or null>,
    "oracle_text": "...",
    "template": "<template_name>",   // see templates list below
    "parameters": { ... }            // template-specific parameters
  },
  "reason": "..."                    // brief justification for tier/template choice
}

Available templates and their parameters:
- basic_land:        { "produces": ["G"] }
- vanilla_creature:  {}
- mana_dork:         { "produces": ["G"] }
- direct_damage:     { "damage": 3, "targeting": "any" }
- counter_spell:     { "conditional": false }
- removal:           { "exile": false, "targeting": "creature" }
- draw_spell:        { "draw": 2 }
- draw_x:            {}
- pump_spell:        { "power_bonus": 2, "tough_bonus": 2, "targeting": "creature" }
- lord_effect:       { "subtypes_affected": ["Goblin"], "power_bonus": 1, "tough_bonus": 1 }

Targeting values for the "targeting" parameter:
- "any"      — any target: player, planeswalker, or creature (e.g. Lightning Bolt, Shock)
- "player"   — players or planeswalkers only (no creatures)
- "creature" — creatures only (e.g. Searing Blood, Giant Growth)
- "multi"    — requires one player target AND one creature that player controls (e.g. Searing Blaze)
- omit field — no target required (draw spells, creatures, lands, etc.)

IMPORTANT: choose "targeting" based on the card's oracle text, not its feel.
A spell that says "any target" is "any". A spell that says "target creature" is "creature".
A spell that says "target player and target creature that player controls" is "multi".

For Tier 3, set "tier": 3, omit "entry", and explain in "reason" what mechanism requires custom code.
Output a JSON array — one object per card."""

def GenerateCardImplementations(missing_cards: list[str],
                                 scryfall_data: dict[str, dict],
                                 client: anthropic.Anthropic) -> list[dict]:
    """Ask Claude to classify and generate definitions for missing cards."""
    card_descriptions = []
    for name in missing_cards:
        sf = scryfall_data.get(name, {})
        desc = {
            "name":        name,
            "oracle_text": sf.get("oracle_text", ""),
            "type_line":   sf.get("type_line", ""),
            "mana_cost":   sf.get("mana_cost", ""),
            "power":       sf.get("power"),
            "toughness":   sf.get("toughness"),
            "keywords":    sf.get("keywords", []),
        }
        card_descriptions.append(desc)

    prompt = (
        "Generate card definitions for the following Magic: The Gathering cards.\n\n"
        + json.dumps(card_descriptions, indent=2)
    )

    message = client.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=8096,
        system=CARD_IMPL_SYSTEM,
        messages=[{"role": "user", "content": prompt}],
    )

    text = message.content[0].text.strip()
    # Extract JSON array from response (may be wrapped in markdown code block)
    match = re.search(r"\[.*\]", text, re.DOTALL)
    if not match:
        raise RuntimeError("Claude did not return a JSON array in its response")
    return json.loads(match.group(0))

# ---------------------------------------------------------------------------
# Apply generated card definitions
# ---------------------------------------------------------------------------

def ApplyCardDefinitions(definitions: list[dict]) -> tuple[list[str], list[str]]:
    """
    Write Tier 1/2 entries to cards.json, note Tier 3 cards for manual implementation.
    Returns (json_names, tier3_names).
    """
    with open(CARDS_JSON) as f:
        cards_data = json.load(f)

    json_names  = []
    tier3_names = []

    for defn in definitions:
        tier = defn.get("tier", 3)
        if tier in (1, 2):
            entry = defn["entry"]
            cards_data["cards"].append(entry)
            json_names.append(entry["name"])
            print(f"  Added to cards.json (Tier {tier}): {entry['name']}")
        else:
            name = defn.get("entry", {}).get("name") or defn.get("reason", "unknown")
            tier3_names.append(name)
            reason = defn.get("reason", "")
            print(f"  Tier 3 (custom C++ needed): {name} — {reason}")

    with open(CARDS_JSON, "w") as f:
        json.dump(cards_data, f, indent=4)

    return json_names, tier3_names

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def RebuildProject() -> bool:
    """Run cmake --build and return True on success."""
    if not BUILD_DIR.exists():
        print("  Build directory not found — run cmake configure first.", file=sys.stderr)
        return False

    print("  Rebuilding project...")
    result = subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--config", "Release"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("  Build failed:", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False

    print("  Build succeeded.")
    return True

# ---------------------------------------------------------------------------
# Run analyzer
# ---------------------------------------------------------------------------

def RunAnalyzer(deck_path: Path, num_games: int, seed: int | None = None) -> dict:
    """Run mtg-analyze and return its JSON output as a dict."""
    if not ANALYZER_BIN.exists():
        raise RuntimeError(f"Analyzer binary not found: {ANALYZER_BIN}\nRun cmake build first.")

    cmd = [
        str(ANALYZER_BIN),
        str(deck_path),
        "--games", str(num_games),
        "--cards-json", str(CARDS_JSON),
    ]
    if seed is not None:
        cmd += ["--seed", str(seed)]

    print(f"  Running analyzer ({num_games} games)...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Analyzer failed:\n{result.stderr}")

    return json.loads(result.stdout)

# ---------------------------------------------------------------------------
# Claude: heuristic generation
# ---------------------------------------------------------------------------

HEURISTIC_SYSTEM = """You are an expert Magic: The Gathering AI developer. Given analysis
statistics from a goldfishing simulation of a deck, produce a DeckProfile — a set of
AI decision heuristics that will help the test runner play the deck optimally.

Output a JSON object with this structure:
{
  "mulligan": {
    "min_lands": <int>,
    "max_lands": <int>,
    "required_pieces": ["Card Name"],  // card names that must be in opening hand, OR logic
    "skip_curve_check": <bool>,
    "stop_at": <int>
  },
  "play_priorities": [
    // Ordered list of card names or patterns, highest priority first.
    // These guide which spells to cast when mana is limited.
    { "card": "Card Name", "priority": <int>, "notes": "..." }
  ],
  "land_preferences": [
    // Which lands to play first when multiple are available.
    { "card": "Card Name", "priority": <int> }
  ],
  "custom_rules": [
    // Declarative conditional rules for edge cases.
    { "condition": "...", "action": "...", "notes": "..." }
  ],
  "notes": "Overall deck strategy and any caveats about the heuristics."
}

Base your output on the analysis statistics and any knowledge of the deck archetype.
Err on the side of explainable, conservative rules that are easy for a human to verify."""

def GenerateDeckProfile(deck_name: str, card_names: list[str],
                         analysis: dict, client: anthropic.Anthropic) -> dict:
    """Ask Claude to produce a DeckProfile from the analysis results."""
    prompt = (
        f"Deck: {deck_name}\n"
        f"Cards: {', '.join(card_names)}\n\n"
        f"Analysis results:\n{json.dumps(analysis, indent=2)}\n\n"
        "Generate a DeckProfile with AI heuristics for playing this deck optimally."
    )

    message = client.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=4096,
        system=HEURISTIC_SYSTEM,
        messages=[{"role": "user", "content": prompt}],
    )

    text = message.content[0].text.strip()
    match = re.search(r"\{.*\}", text, re.DOTALL)
    if not match:
        raise RuntimeError("Claude did not return a JSON object for the DeckProfile")
    return json.loads(match.group(0))

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def Main():
    args = ParseArgs()
    deck_path = Path(args.decklist).resolve()

    if not deck_path.exists():
        print(f"Error: decklist not found: {deck_path}", file=sys.stderr)
        sys.exit(1)

    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("Error: ANTHROPIC_API_KEY environment variable not set.", file=sys.stderr)
        sys.exit(1)

    client    = anthropic.Anthropic(api_key=api_key)
    deck_name = deck_path.stem

    output_path = Path(args.output) if args.output else deck_path.parent / f"{deck_name}.profile.json"

    print(f"\n=== Analyzing: {deck_name} ===\n")

    # 1. Parse decklist
    print("Step 1: Parsing decklist...")
    card_names = LoadDeckNames(deck_path)
    print(f"  {len(card_names)} unique card(s): {', '.join(card_names[:5])}{'...' if len(card_names) > 5 else ''}")

    # 2. Scryfall lookup
    print("\nStep 2: Fetching card data from Scryfall...")
    scryfall_data = FetchAllCards(card_names)

    # 3. Check coverage
    print("\nStep 3: Checking card coverage...")
    implemented  = LoadImplementedNames()
    missing      = [n for n in card_names if n not in implemented]
    print(f"  Implemented: {len(card_names) - len(missing)}/{len(card_names)}")
    if missing:
        print(f"  Missing: {', '.join(missing)}")

    # 4. Generate missing card implementations
    tier3_names = []
    if missing:
        print(f"\nStep 4: Generating {len(missing)} card definition(s) via Claude...")
        definitions = GenerateCardImplementations(missing, scryfall_data, client)
        _, tier3_names = ApplyCardDefinitions(definitions)
    else:
        print("\nStep 4: All cards implemented — skipping generation.")

    if tier3_names:
        print(f"\n  WARNING: {len(tier3_names)} card(s) require Tier 3 custom C++:")
        for name in tier3_names:
            print(f"    - {name}")
        print("  These must be implemented manually in src/cards/custom/ before analysis.")
        print("  Re-run this script after implementing them.")
        sys.exit(1)

    # 5. Rebuild if any JSON was updated (always safe to rebuild; skips if unchanged)
    print("\nStep 5: Rebuilding project...")
    if not RebuildProject():
        sys.exit(1)

    # 6. Run the C++ analyzer
    print("\nStep 6: Running analysis...")
    analysis = RunAnalyzer(deck_path, args.games)

    print(f"  Average win turn : {analysis.get('average_win_turn', 'N/A')}")
    print(f"  Win rate         : {analysis.get('win_rate', 0) * 100:.1f}%")

    # 7. Generate DeckProfile heuristics
    print("\nStep 7: Generating DeckProfile via Claude...")
    profile = GenerateDeckProfile(deck_name, card_names, analysis, client)
    profile["deck_name"]      = deck_name
    profile["analysis_seed"]  = analysis.get("seed")
    profile["games_analyzed"] = analysis.get("games_played")

    # 8. Present for review
    print("\n=== Generated DeckProfile (review before use) ===\n")
    print(json.dumps(profile, indent=2))

    # 9. Save
    with open(output_path, "w") as f:
        json.dump(profile, f, indent=2)
    print(f"\nDeckProfile saved to: {output_path}")

if __name__ == "__main__":
    Main()
