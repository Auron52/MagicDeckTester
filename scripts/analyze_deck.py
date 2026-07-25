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
import os
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT    = Path(__file__).parent.parent.resolve()
CARDS_JSON   = REPO_ROOT / "src" / "cards" / "data" / "cards.json"
CUSTOM_DIR   = REPO_ROOT / "src" / "cards" / "custom"
BUILD_DIR    = REPO_ROOT / "build"
# Multi-config CMake generators (Visual Studio on Windows, Ninja Multi-Config in
# the Linux dev container) both place the binary under build/Release/; only the
# executable suffix differs by platform.
EXE_SUFFIX   = ".exe" if platform.system() == "Windows" else ""
ANALYZER_BIN = BUILD_DIR / "Release" / f"mtg-analyze{EXE_SUFFIX}"
MTG_BIN      = BUILD_DIR / "Release" / f"mtg{EXE_SUFFIX}"   # goldfish binary (cost-diagnostic A/B)

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
    p.add_argument("--cards-json",   default=None,
                   help="Override path to cards.json")
    p.add_argument("--no-cost-diagnostic", action="store_true",
                   help="Skip the same-turn cost-handling A/B diagnostic (reframe off vs on)")
    p.add_argument("--cost-diag-games", type=int, default=200,
                   help="Games per condition for the cost-diagnostic A/B (default 200)")
    return p.parse_args()

# ---------------------------------------------------------------------------
# Decklist parsing — plain text and Cockatrice .cod XML
# ---------------------------------------------------------------------------

def LoadDeckCounts(path: Path) -> dict[str, int]:
    """Return {card_name: count} for mainboard cards, preserving insertion order."""
    if path.suffix.lower() == ".cod":
        return _LoadCockatriceDeckCounts(path)
    return _LoadTextDeckCounts(path)

def _LoadTextDeckCounts(path: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or line.startswith("#"):
                continue
            match = re.match(r"^(\d+)x?\s+(.*)", line)
            if not match:
                continue
            count = int(match.group(1))
            name  = match.group(2).strip()
            name  = re.sub(r"\s+\([A-Z0-9]+\)\s+\d+$", "", name)
            if name:
                counts[name] = counts.get(name, 0) + count
    return counts

def _LoadCockatriceDeckCounts(path: Path) -> dict[str, int]:
    tree = ET.parse(path)
    root = tree.getroot()
    counts: dict[str, int] = {}
    for zone in root.findall("zone"):
        if zone.get("name") != "main":
            continue
        for card in zone.findall("card"):
            name  = card.get("name", "").strip()
            count = int(card.get("number", "1"))
            if name:
                counts[name] = counts.get(name, 0) + count
    return counts

def LoadDeckNames(path: Path) -> list[str]:
    """Return a deduplicated list of mainboard card names (order-preserving)."""
    return list(LoadDeckCounts(path).keys())

# ---------------------------------------------------------------------------
# Vial target computation
# ---------------------------------------------------------------------------

def _ManaValue(mana_cost: str) -> int:
    total = 0
    for symbol in re.findall(r'\{([^}]+)\}', mana_cost):
        if symbol.isdigit():
            total += int(symbol)
        elif symbol != 'X':
            total += 1  # one generic mana per colored pip
    return total

def ComputeVialTargetMv(deck_counts: dict[str, int], cards_json: Path) -> int:
    """
    Returns the most common creature MV (weighted by deck count) if the deck
    contains Aether Vial, otherwise returns 0. Tie-breaks toward higher MV.
    """
    if "Aether Vial" not in deck_counts:
        return 0
    if not cards_json.exists():
        return 0
    with open(cards_json, encoding="utf-8") as f:
        data = json.load(f)
    card_map = {c["name"]: c for c in data.get("cards", []) if "name" in c}
    mv_count: dict[int, int] = {}
    for name, count in deck_counts.items():
        entry = card_map.get(name)
        if not entry or "Creature" not in entry.get("types", []):
            continue
        mv = _ManaValue(entry.get("mana_cost", ""))
        if mv > 0:
            mv_count[mv] = mv_count.get(mv, 0) + count
    if not mv_count:
        return 0
    best_mv, best_cnt = 0, 0
    for mv, cnt in mv_count.items():
        if cnt > best_cnt or (cnt == best_cnt and mv > best_mv):
            best_cnt, best_mv = cnt, mv
    return best_mv

def UpdateDeckProfile(deck_path: Path, updates: dict) -> None:
    """Merge `updates` into the deck's .profile.json, creating it if needed."""
    profile_path = deck_path.with_name(deck_path.stem + ".profile.json")
    data: dict = {"version": 1, "mulligan": {}}
    if profile_path.exists():
        try:
            with open(profile_path, encoding="utf-8") as f:
                data = json.load(f)
        except Exception:
            pass
    data.update(updates)
    with open(profile_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print(f"  Profile updated: {profile_path.name}", file=sys.stderr)

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

def RunAnalyzer(deck_path: Path, cards_json: Path) -> dict:
    # The analyzer is a fixed-recipe profile generator: it takes no game-count,
    # depth, or budget knobs. Win-rate evaluation is the regression suite's job.
    if not ANALYZER_BIN.exists():
        raise RuntimeError(f"Analyzer binary not found: {ANALYZER_BIN}")
    cmd = [
        str(ANALYZER_BIN),
        str(deck_path),
        "--cards-json", str(cards_json),
    ]
    # Capture ONLY stdout (the AnalysisResultToJson we must parse); let the analyzer's
    # stderr INHERIT our stderr so its phase-by-phase progress ("Analysing required
    # pieces...", "Computing card scores...", "Grid-searching...", "Done.") streams LIVE.
    # std::cerr is unbuffered, so with a plain redirect (`2> run.log`) a long run's
    # progress is visible via `tail -f`. Previously capture_output=True buffered stderr
    # until the process exited, so a multi-hour run (the land grid) looked hung with no
    # signal of which phase it was in. stdout stays piped so the JSON is unpolluted.
    result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)
    if result.returncode != 0:
        # The analyzer's stderr already streamed live (above / to the redirect log),
        # so point there rather than re-printing a captured copy we no longer hold.
        raise RuntimeError(
            f"Analyzer exited {result.returncode} — see its stderr above "
            f"(or the run log) for the failing phase."
        )
    return json.loads(result.stdout)

# ---------------------------------------------------------------------------
# Cost-aggregate diagnostic (automated onboarding test)
# ---------------------------------------------------------------------------
#
# Answers automatically the question the cost-reframe experiment left for onboarding: "does this
# deck's same-turn COST handling need attention?" -- so a new deck's cost interactions are a
# measured verdict, not tribal knowledge. See docs/design/enumeration-feasibility-via-executor.md.
#
# Each param below is a same-turn cost interaction that ALREADY has a generic, param-driven credit
# in the engine (TurnSolver) -- handled per-MECHANIC, not per-deck. A deck using only these needs no
# per-deck code. A cost-shaped param NOT listed here is a NOVEL mechanic (add a generic credit + list
# it here); until then the reframe still OFFERS its lines, so the deck works, just sub-optimally.
KNOWN_COST_MECHANICS = {
    "reduces_spell_color":  "cost reducer (Medallion-style)",   # SameTurnReducerGenericCredit
    "affinity_for_subtype": "affinity",                          # SameTurnAffinityGenericCredit
    "ritual_floating_mana": "ritual float",                      # BuildPool float credit
    "mana_rock":            "mana-rock ramp",                    # rock-ramp credit
}

def _CardsIndex(cards_json: Path) -> dict:
    if not cards_json.exists():
        return {}
    with open(cards_json, encoding="utf-8") as f:
        data = json.load(f)
    return {c["name"]: c for c in data.get("cards", []) if "name" in c}

def ScanCostMechanics(card_names: list[str], cards_json: Path) -> list[dict]:
    """Same-turn cost-interaction mechanics this deck uses (pure param scan, no game run)."""
    index = _CardsIndex(cards_json)
    seen  = []
    for name in card_names:
        params = (index.get(name) or {}).get("parameters", {})
        for p, mech in KNOWN_COST_MECHANICS.items():
            v = params.get(p)
            present = (v is True) \
                or (isinstance(v, (int, float)) and not isinstance(v, bool) and v) \
                or (isinstance(v, str) and v)
            if present:
                seen.append({"card": name, "param": p, "mechanic": mech})
    return seen

def RunGoldfishAvg(deck_path: Path, games: int, seed: int, depth: int, extra_env: dict) -> float:
    """Run the goldfish binary and return the avg-turn-to-win metric (lower = better)."""
    if not MTG_BIN.exists():
        raise RuntimeError(f"goldfish binary not found: {MTG_BIN} (build first)")
    env = os.environ.copy()
    env.update(extra_env)
    # --ignore-play-profile so a fixed --depth drives BOTH conditions consistently (a deck whose
    # profile enables value_play depth otherwise rejects --depth). The A/B only needs a common,
    # cheap depth; the deck's shipped play policy is irrelevant to the cost-offer question.
    cmd = [str(MTG_BIN), str(deck_path), "--ignore-play-profile",
           "--games", str(games), "--seed", str(seed), "--depth", str(depth)]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(f"goldfish run exited {result.returncode}")
    m = re.search(r"avg \(turns\)\s*:\s*([0-9.]+)", result.stdout)
    if not m:
        raise RuntimeError("could not parse 'avg (turns)' from goldfish output")
    return float(m.group(1))

def CostAggregateDiagnostic(deck_path: Path, cards_json: Path, card_names: list[str],
                            games: int = 200, seed: int = 90001, depth: int = 3) -> dict:
    """
    Automated onboarding test for same-turn cost handling. Verdicts:
      NO_COST_INTERACTIONS -- no cost-mechanic cards; base aggregate suffices (no game run).
      COST_NEUTRAL         -- reframe changes nothing within noise; handled by generic credits.
      REFRAME_HELPS        -- base aggregate under-credits; reframe recovers (faster avg). Enable
                              MTG_COST_REFRAME for the deck, or add/verify the mechanic's credit.
      HIGH_DISCOUNT_COMBO  -- reframe floods the fixed budget and plays worse; wants the ACCURATE
                              aggregate (its generic credit), NOT the reframe.
    Metric = avg-turn-to-win (lower = better); delta = reframe_on - base. seed disjoint from the
    regression suite's (1001/2002/3003/700001) so the diagnostic never overlaps its ground truth.
    """
    mechanics = ScanCostMechanics(card_names, cards_json)
    out = {"cost_mechanics": mechanics}
    if not mechanics:
        out["verdict"] = "NO_COST_INTERACTIONS"
        out["detail"]  = "No same-turn cost-interaction cards; the base mana aggregate suffices."
        return out
    try:
        base    = RunGoldfishAvg(deck_path, games, seed, depth, {"MTG_COST_REFRAME": "0"})
        reframe = RunGoldfishAvg(deck_path, games, seed, depth, {"MTG_COST_REFRAME": "1"})
    except RuntimeError as e:
        out["verdict"] = "SKIPPED"
        out["detail"]  = f"cost A/B skipped: {e}"
        return out
    delta = round(reframe - base, 4)
    NOISE = 0.02
    out.update({"avg_base": base, "avg_reframe": reframe, "delta": delta,
                "games": games, "seed": seed, "depth": depth})
    if delta <= -NOISE:
        out["verdict"] = "REFRAME_HELPS"
        out["detail"]  = ("base aggregate UNDER-credits this deck's same-turn cost lines; the reframe "
                          "recovers them. Enable MTG_COST_REFRAME for this deck, or add/verify the "
                          "generic credit for its mechanic(s).")
    elif delta >= NOISE:
        out["verdict"] = "HIGH_DISCOUNT_COMBO"
        out["detail"]  = ("reframe floods the fixed-budget search and plays WORSE -- high-discount combo; "
                          "keep the ACCURATE aggregate (generic credit), do NOT enable the reframe.")
    else:
        out["verdict"] = "COST_NEUTRAL"
        out["detail"]  = ("reframe neither helps nor hurts within noise -- cost interactions are already "
                          "handled by the base aggregate's generic credits.")
    return out

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
    deck_counts = LoadDeckCounts(deck_path)
    card_names  = list(deck_counts.keys())

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
        # HARD-STOP on missing OR partial gaps (workstream 2 / verify_deck.py 3). A `partial`
        # is an implemented clause the coverage scan could not confirm and that carries NO
        # deferral bracket note -- exactly the "hand-waved the gist" gap the process must not let
        # exit 0. A genuine deferral is reclassified `deferred` (status stays `full`) by a bracket
        # note, so signing one off keeps this green. This aligns the tool with the spine, which
        # already treats `partial` as a blocking finding.
        partial = [c for c in coverage if c.get("status") == "partial"]
        sys.exit(1 if (missing or partial) else 0)

    # 3. Block on missing cards
    if missing:
        report["error"] = (
            f"{len(missing)} card(s) missing from the database. "
            "Implement them first, then re-run without --coverage-only."
        )
        print(json.dumps(report, indent=2))
        sys.exit(1)

    # 4. Update deck profile with computed AI parameters
    vial_target_mv = ComputeVialTargetMv(deck_counts, cards_json)
    if vial_target_mv > 0:
        UpdateDeckProfile(deck_path, {"vial_target_mv": vial_target_mv})

    # 5. Rebuild
    if not args.no_rebuild:
        if not RebuildProject():
            sys.exit(1)

    # 6. Analyze
    print("  Running analyzer (generating profile)...", file=sys.stderr)
    analysis = RunAnalyzer(deck_path, cards_json)

    # Write card scores and threshold into the profile if the analyzer produced them.
    profile_updates: dict = {}
    if "card_scores" in analysis:
        profile_updates["card_scores"] = analysis["card_scores"]
    if "hand_score_threshold" in analysis:
        profile_updates["hand_score_threshold"] = analysis["hand_score_threshold"]
    if profile_updates:
        UpdateDeckProfile(deck_path, profile_updates)

    report["analysis"] = analysis

    # 7. Cost-aggregate diagnostic — automated onboarding test for same-turn cost handling.
    if not args.no_cost_diagnostic:
        print("  Running cost-aggregate diagnostic (reframe A/B)...", file=sys.stderr)
        try:
            diag = CostAggregateDiagnostic(deck_path, cards_json, existing,
                                           games=args.cost_diag_games)
        except Exception as e:  # never let the diagnostic sink the analysis
            diag = {"verdict": "ERROR", "detail": str(e)}
        report["cost_diagnostic"] = diag
        print(f"  Cost diagnostic: {diag.get('verdict')} — {diag.get('detail', '')}", file=sys.stderr)

    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    Main()
