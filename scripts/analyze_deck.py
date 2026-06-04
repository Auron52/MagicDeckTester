#!/usr/bin/env python3
"""
analyze_deck.py  —  Deterministic deck analysis tool

Handles the mechanical steps that don't require Claude:
  1. Parse the decklist (.txt plain text or .cod Cockatrice XML).
  2. Check card coverage: which cards are missing from the database,
     and which existing cards have known implementation gaps.
  3. (Optional) Rebuild the project.
  4. Run mtg-analyze and capture the JSON output.

The generate / review steps that require intelligence are handled by Claude
in the conversation, guided by .claude/skills/analyze-deck.md.

Usage:
    python scripts/analyze_deck.py <decklist> [options]

Options:
    --coverage-only    Report coverage and exit; do not build or analyze.
    --no-rebuild       Skip the cmake rebuild step.
    --games N          Number of simulation games (default: 500).
    --cards-json PATH  Path to card definitions (default: src/cards/data/cards.json).
"""

import argparse
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT    = Path(__file__).parent.parent.resolve()
CARDS_JSON   = REPO_ROOT / "src" / "cards" / "data" / "cards.json"
CUSTOM_DIR   = REPO_ROOT / "src" / "cards" / "custom"
BUILD_DIR    = REPO_ROOT / "build"
ANALYZER_BIN = BUILD_DIR / "Release" / "mtg-analyze.exe"

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def ParseArgs():
    p = argparse.ArgumentParser(description="Deterministic deck coverage check and analysis.")
    p.add_argument("decklist",       help="Path to .txt or .cod decklist")
    p.add_argument("--coverage-only", action="store_true",
                   help="Report coverage and exit without building or analyzing")
    p.add_argument("--no-rebuild",   action="store_true",
                   help="Skip cmake rebuild")
    p.add_argument("--games",        type=int, default=500)
    p.add_argument("--cards-json",   default=None,
                   help="Override path to cards.json")
    return p.parse_args()

# ---------------------------------------------------------------------------
# Decklist parsing — plain text and Cockatrice .cod XML
# ---------------------------------------------------------------------------

def LoadDeckNames(path: Path) -> list[str]:
    """
    Return a deduplicated list of mainboard card names from a decklist.
    Supports plain text (4 Card Name / 4x Card Name) and Cockatrice .cod XML.
    """
    if path.suffix.lower() == ".cod":
        return _LoadCockatriceDeck(path)
    return _LoadTextDeck(path)

def _LoadCockatriceDeck(path: Path) -> list[str]:
    tree = ET.parse(path)
    root = tree.getroot()
    names = []
    seen  = set()
    for zone in root.findall("zone"):
        if zone.get("name") != "main":
            continue
        for card in zone.findall("card"):
            name = card.get("name", "").strip()
            if name and name not in seen:
                seen.add(name)
                names.append(name)
    return names

def _LoadTextDeck(path: Path) -> list[str]:
    names = []
    seen  = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or line.startswith("#"):
                continue
            match = re.match(r"^\d+x?\s+(.*)", line)
            name  = match.group(1).strip() if match else line.strip()
            name  = re.sub(r"\s+\([A-Z0-9]+\)\s+\d+$", "", name)
            if name and name not in seen:
                seen.add(name)
                names.append(name)
    return names

# ---------------------------------------------------------------------------
# Card coverage
# ---------------------------------------------------------------------------

def LoadImplementedNames(cards_json: Path) -> set[str]:
    implemented = set()
    if cards_json.exists():
        with open(cards_json, encoding="utf-8") as f:
            data = json.load(f)
        for card in data.get("cards", []):
            if "name" in card:
                implemented.add(card["name"])
    if CUSTOM_DIR.exists():
        for cpp_file in CUSTOM_DIR.glob("*.cpp"):
            implemented.add(cpp_file.stem.replace("_", " "))
    return implemented

def LoadCardEntry(name: str, cards_json: Path) -> dict | None:
    if not cards_json.exists():
        return None
    with open(cards_json, encoding="utf-8") as f:
        data = json.load(f)
    for card in data.get("cards", []):
        if card.get("name") == name:
            return card
    return None

def CheckExistingCoverage(card_names: list[str], cards_json: Path) -> list[dict]:
    """
    Deterministic oracle-text pattern check for cards already in the database.
    Checks: spectacle_cost, sacrifice_land, landfall_damage, stages_cards,
            death_trigger_damage, on_cast_trigger, attack_trigger_damage,
            upkeep_adds_charge, static effects, unbracketed triggers.
    """
    coverage = []
    for name in card_names:
        entry = LoadCardEntry(name, cards_json)
        if not entry:
            continue

        oracle = entry.get("oracle_text", "")
        params = entry.get("parameters", {})
        gaps   = []
        deferred = re.findall(r'\[([^\]]+)\]', oracle)

        def is_d(kw: str) -> bool:
            return any(kw.lower() in d.lower() for d in deferred)

        # Spectacle
        if "Spectacle" in oracle and not params.get("spectacle_cost") and not is_d("spectacle"):
            gaps.append("Spectacle in oracle text but spectacle_cost not set")

        # Sacrifice land
        if "sacrifice a land" in oracle.lower() and not params.get("sacrifice_land") \
                and not is_d("sacrifice"):
            gaps.append("'Sacrifice a land' cost in oracle text but sacrifice_land not set")

        # Landfall
        if "Landfall" in oracle and not params.get("landfall_damage") and not is_d("landfall"):
            gaps.append("Landfall in oracle text but landfall_damage not set")

        # Staged exile
        if ("end of your next turn" in oracle or "your next turn" in oracle) \
                and "exile" in oracle.lower() \
                and not params.get("stages_cards") \
                and not is_d("next turn") and not is_d("staged"):
            gaps.append("Staged exile in oracle text but stages_cards not set")

        # Death trigger
        if ("creature dies" in oracle or "that creature dies" in oracle) \
                and not params.get("death_trigger_damage") \
                and not is_d("dies") and not is_d("death"):
            gaps.append("Death trigger in oracle text but death_trigger_damage not set")

        # On-cast trigger
        if ("casts a spell with mana value" in oracle
                or "casts a spell with converted mana cost" in oracle) \
                and not params.get("on_cast_trigger_max_mv") and not is_d("cast"):
            gaps.append("On-cast trigger in oracle text but on_cast_trigger params not set")

        # Static effects needing engine support
        if "can't gain life" in oracle and not is_d("life") and not is_d("gain"):
            gaps.append("Static 'can't gain life' not implemented — needs GameState flag")
        if "can't be prevented" in oracle and not is_d("prevent"):
            gaps.append("Static 'damage can't be prevented' not implemented — needs engine support")

        # Unbracketed triggered abilities
        # Skip the check if a known trigger parameter already implements the trigger.
        trigger_implemented = (
            params.get("on_cast_trigger_max_mv")
            or params.get("death_trigger_damage")
            or params.get("attack_trigger_damage")
            or params.get("upkeep_adds_charge")
        )
        if not trigger_implemented:
            for trigger in ("Whenever ", "At the beginning of "):
                if trigger in oracle and not deferred:
                    gaps.append(f"Triggered ability ('{trigger.strip()}') with no deferral bracket note — "
                                 "verify it is implemented or add a bracket note")
                    break

        record: dict = {"card": name, "status": "partial" if gaps else "full"}
        if deferred:
            record["deferred"] = deferred
        if gaps:
            record["gaps"] = gaps
        coverage.append(record)

    return coverage

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def RebuildProject() -> bool:
    if not BUILD_DIR.exists():
        print("  Build directory not found — run cmake configure first.", file=sys.stderr)
        return False
    print("  Rebuilding...", file=sys.stderr)
    result = subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--config", "Release"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("  Build failed:\n" + result.stderr, file=sys.stderr)
        return False
    print("  Build OK.", file=sys.stderr)
    return True

# ---------------------------------------------------------------------------
# Run C++ analyzer
# ---------------------------------------------------------------------------

def RunAnalyzer(deck_path: Path, num_games: int, cards_json: Path) -> dict:
    if not ANALYZER_BIN.exists():
        raise RuntimeError(f"Analyzer binary not found: {ANALYZER_BIN}")
    cmd = [
        str(ANALYZER_BIN),
        str(deck_path),
        "--games", str(num_games),
        "--cards-json", str(cards_json),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Analyzer failed:\n{result.stderr}")
    return json.loads(result.stdout)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def Main():
    args      = ParseArgs()
    deck_path = Path(args.decklist).resolve()
    cards_json = Path(args.cards_json).resolve() if args.cards_json else CARDS_JSON

    if not deck_path.exists():
        print(f"Error: decklist not found: {deck_path}", file=sys.stderr)
        sys.exit(1)

    # 1. Parse
    card_names = LoadDeckNames(deck_path)

    # 2. Coverage
    implemented = LoadImplementedNames(cards_json)
    missing     = [n for n in card_names if n not in implemented]
    existing    = [n for n in card_names if n in implemented]
    coverage    = CheckExistingCoverage(existing, cards_json)

    report = {
        "deck":     deck_path.stem,
        "cards":    card_names,
        "missing":  missing,
        "coverage": coverage,
    }

    if args.coverage_only:
        print(json.dumps(report, indent=2))
        sys.exit(0)

    # 3. Block on missing cards
    if missing:
        report["error"] = (
            f"{len(missing)} card(s) missing from the database. "
            "Implement them first, then re-run without --coverage-only."
        )
        print(json.dumps(report, indent=2))
        sys.exit(1)

    # 4. Rebuild
    if not args.no_rebuild:
        if not RebuildProject():
            sys.exit(1)

    # 5. Analyze
    print(f"  Running analyzer ({args.games} games)...", file=sys.stderr)
    analysis = RunAnalyzer(deck_path, args.games, cards_json)

    report["analysis"] = analysis
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    Main()
