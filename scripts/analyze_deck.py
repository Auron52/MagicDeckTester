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
    p.add_argument("--analyzer-seed", type=int, default=None,
                   help="Base seed passed to mtg-analyze (omit = analyzer randomizes per "
                        "invocation). Pin it to make a profile run reproducible -- e.g. to "
                        "re-examine a degenerate scoring game (the FiveColour 3.4h atom's "
                        "seed was unrecoverable because the run was unseeded).")
    p.add_argument("--discard-analysis", action="store_true",
                   help="Run ONLY the discard-analysis stage: search-labelled evidence run, "
                        "candidate-rule derivation, paired outcome A/B, recommendation report. "
                        "Report only — adoption means writing a PROVIDER override, user-approved.")
    p.add_argument("--discard-evidence-games", type=int, default=400,
                   help="Evidence-run games (searched trial tables, d3, single-thread; default 400)")
    p.add_argument("--discard-ab-games", type=int, default=4000,
                   help="Outcome-A/B games per arm at d0 (default 4000); play-config arm uses 1/5th")
    p.add_argument("--discard-ab-threads", type=int, default=0,
                   help="Threads for the outcome-A/B batch (0 = all cores). Lower this when "
                        "running several decks' analyses concurrently.")
    return p.parse_args()

# ---------------------------------------------------------------------------
# Decklist parsing — plain text and Cockatrice .cod XML
# ---------------------------------------------------------------------------

def LoadDeckCounts(path: Path, board: str = "main") -> dict[str, int]:
    """Return {card_name: count} for one board, preserving insertion order.

    `board` is "main" or "side". The zone names mirror DeckLoader.cpp exactly (Cockatrice
    `<zone name="side">`; text `Sideboard` / `SB:` section headers), so the script and the
    engine never disagree about which card sits on which board.
    """
    if path.suffix.lower() == ".cod":
        return _LoadCockatriceDeckCounts(path, board)
    return _LoadTextDeckCounts(path, board)

def _LoadTextDeckCounts(path: Path, board: str = "main") -> dict[str, int]:
    counts: dict[str, int] = {}
    in_sideboard = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or line.startswith("#"):
                continue
            low = line.lower()
            if low in ("sideboard", "sideboard:") or low.startswith("sb:"):
                in_sideboard = True
                if low.startswith("sb:"):
                    line = line[3:].strip()
                    if not line:
                        continue
                else:
                    continue
            match = re.match(r"^(\d+)x?\s+(.*)", line)
            if not match:
                continue
            if in_sideboard != (board == "side"):
                continue
            count = int(match.group(1))
            name  = match.group(2).strip()
            name  = re.sub(r"\s+\([A-Z0-9]+\)\s+\d+$", "", name)
            if name:
                counts[name] = counts.get(name, 0) + count
    return counts

def _LoadCockatriceDeckCounts(path: Path, board: str = "main") -> dict[str, int]:
    tree = ET.parse(path)
    root = tree.getroot()
    wanted = ("side", "sideboard") if board == "side" else ("main",)
    counts: dict[str, int] = {}
    for zone in root.findall("zone"):
        if zone.get("name") not in wanted:
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
# Sideboard reachability
# ---------------------------------------------------------------------------

# Cards that fetch from OUTSIDE THE GAME, i.e. from the sideboard. Needed as a NAME list, not
# just a cards.json lookup, because the whole point is to work BEFORE the card is implemented --
# on a wish deck the wish itself is typically one of the missing cards, so a param-based test
# would answer "no sideboard access" precisely when the answer matters most.
WISH_CARD_NAMES = frozenset({
    "Living Wish", "Burning Wish", "Cunning Wish", "Death Wish", "Golden Wish",
    "Glittering Wish", "Fae of Wishes // Awaken the Ancient", "Fae of Wishes",
    "Wish", "Mastermind's Acquisition", "Spawnsire of Ulamog", "Ring of Ma'ruf",
    "Ring of Ma'rûf", "Karn, the Great Creator", "Garth One-Eye",
})

def SideboardReachability(main_names: list[str], side_names: list[str],
                          cards_json: Path) -> dict:
    """Can anything in the mainboard fetch a sideboard card during a game?

    In this simulator there is no game 2 and no sideboarding, so a sideboard is reachable
    ONLY through a wish effect. That makes reachability a real gate rather than a formality:
    scanning every sideboard unconditionally would hard-fail `--coverage-only` on the five
    decks whose sideboards are vestigial import residue (Knights, Minotaur, Hinata2, Goblins,
    Unpredictable Cyclone), while NOT scanning a wish deck's sideboard hides its win condition.

    Three detectors, most authoritative first. All three are name/board level -- a wish's type
    restriction ("creature or land card" for Living Wish) is deliberately NOT applied, because
    an unimplemented card has no known types, so filtering by type would skip exactly the cards
    that still need implementing.
    """
    entries = {}
    if cards_json.exists():
        with open(cards_json, encoding="utf-8") as f:
            data = json.load(f)
        for card in data.get("cards", []):
            if card.get("name") in main_names:
                entries[card["name"]] = card

    via: list[dict] = []
    for name in main_names:
        entry = entries.get(name)
        if entry and (entry.get("parameters", {}) or {}).get("wish_from_sideboard"):
            via.append({"card": name, "detected_by": "wish_from_sideboard parameter"})
        elif entry and "outside the game" in entry.get("oracle_text", "").lower():
            via.append({"card": name, "detected_by": "oracle text 'outside the game'"})
        elif name in WISH_CARD_NAMES:
            via.append({"card": name, "detected_by": "known wish card (not yet implemented)"})

    if not side_names:
        reason = "deck has no sideboard"
    elif via:
        reason = (f"{len(side_names)} sideboard card(s) reachable via "
                  + ", ".join(v["card"] for v in via))
    else:
        reason = ("no mainboard card fetches from outside the game -- sideboard is unreachable "
                  "in this simulator (no game 2, no sideboarding) and is NOT scanned")

    return {"reachable": bool(via and side_names), "via": via, "reason": reason}

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

def CheckExistingCoverage(card_names: list[str], cards_json: Path,
                          boards: dict[str, str] | None = None) -> list[dict]:
    """
    Deterministic oracle-text pattern check for cards already in the database.
    Checks: spectacle_cost, sacrifice_land, landfall_damage, stages_cards,
            death_trigger_damage, on_cast_trigger, attack_trigger_damage,
            upkeep_adds_charge, static effects, unbracketed triggers.

    `boards` optionally maps card name -> "main"/"side" so the report says which board a
    finding came from. A reachable sideboard card is held to the SAME standard as a mainboard
    one -- in a wish deck the sideboard is part of the deck.
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

        # Death trigger. Also satisfied by the structural death models: persist ("when this
        # creature dies ... return it"), the dies_* watcher family (Voice of Resurgence's
        # dies-token), and Melira Pod's other dies-adjacent params.
        if ("creature dies" in oracle or "that creature dies" in oracle) \
                and not params.get("death_trigger_damage") \
                and not params.get("persist") \
                and not params.get("dies_trigger_creates_tokens") \
                and not params.get("dies_trigger_damage") \
                and not params.get("dies_watch_includes_self") \
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
        if boards and boards.get(name, "main") != "main":
            record["board"] = boards[name]
        if deferred:
            record["deferred"] = deferred
        if gaps:
            record["gaps"] = gaps
        coverage.append(record)

    return coverage

# ---------------------------------------------------------------------------
# Card dependency map (docs/design/card-dependency-map.md)
# ---------------------------------------------------------------------------

def DeriveDependencyMap(card_names: list[str], cards_json: Path) -> dict:
    """
    Mirror of the engine's param-derived dependency edges (GoldFishRunner::DeriveDependencyPulls
    + the generic CastOrderRank tiers), printed so a human reviews what the engine inferred --
    a missing edge is an implementation-review item, same as a coverage gap. Edge types:
      ENABLES      lifegain_to_loss enabler -> each opponent-lifegain payload
      CAST-PAYOFF  verse_damage card <- each instant/sorcery cast (benefits when cast after it)
      DESTROYS     destroy_all_enchantments card -> each enchantment (ordered last, needs a
                   surviving enabler or subset-level lethality)
    """
    entries = {}
    if cards_json.exists():
        with open(cards_json, encoding="utf-8") as f:
            data = json.load(f)
        for card in data.get("cards", []):
            if card.get("name") in card_names:
                entries[card["name"]] = card

    def params(n): return entries[n].get("parameters", {}) or {}
    def types(n):  return entries[n].get("types", []) or []

    enablers = [n for n in entries if params(n).get("lifegain_to_loss")]
    payloads = [n for n in entries if params(n).get("alt_lifegain_cost", 0) > 0
                or params(n).get("opponent_lifegain", 0) > 0
                or params(n).get("etb_opponent_lifegain", 0) > 0
                or params(n).get("tap_opponent_lifegain", 0) > 0
                or params(n).get("controller_lifegain_equals_power")]
    verses   = [n for n in entries if params(n).get("verse_damage")]
    casts    = [n for n in entries if "Instant" in types(n) or "Sorcery" in types(n)]
    destroys = [n for n in entries if params(n).get("destroy_all_enchantments")]
    enchants = [n for n in entries if "Enchantment" in types(n)]

    edges = []
    for a in enablers:
        for b in payloads:
            edges.append({"type": "ENABLES", "from": a, "to": b})
    for a in verses:
        for b in casts:
            if b != a:
                edges.append({"type": "CAST-PAYOFF", "payoff": a, "feeder": b})
    for a in destroys:
        for b in enchants:
            if b != a:
                edges.append({"type": "DESTROYS", "from": a, "to": b})
    return {"edges": edges} if edges else {"edges": []}

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

def RunAnalyzer(deck_path: Path, cards_json: Path, seed: int | None = None) -> dict:
    # The analyzer is a fixed-recipe profile generator: it takes no game-count,
    # depth, or budget knobs. Win-rate evaluation is the regression suite's job.
    if not ANALYZER_BIN.exists():
        raise RuntimeError(f"Analyzer binary not found: {ANALYZER_BIN}")
    cmd = [
        str(ANALYZER_BIN),
        str(deck_path),
        "--cards-json", str(cards_json),
    ]
    if seed is not None:
        cmd += ["--seed", str(seed)]
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
# Discard analysis — per-deck cleanup-discard policy, search-informed
# ---------------------------------------------------------------------------
# Process (user design 2026-08-07, docs/design/per-deck-discard-analysis-phase.md):
#   1. EVIDENCE: replay the deck with the retired probe re-enabled offline
#      (MTG_DISCARD_NODE=0 + MTG_DISCARD_TRACE=1) so every real cleanup discard emits a
#      searched trial table (per-candidate rollout win turn). The search LABELS the decision;
#      no rule is invented from scratch.
#   2. RULES: evaluate candidate visible-info rules against those labels (status quo = the
#      deck's current ranking; spare-copy band; a derived shed-order of names the search
#      likes shedding). Only rules that beat the status quo ON LABELS go to step 3.
#   3. OUTCOME A/B: paired-seed pooled batch, one arm per surviving rule (profile variants in
#      scratch dirs with symlinked sibling models, so arms differ ONLY in discard policy).
#   4. REPORT ONLY: prints the evidence, the label table, the A/B, and a recommendation.
#      Adoption (writing mulligan.spare_copy_band / mulligan.discard_order) is a separate,
#      USER-APPROVED step — this stage never touches the profile.
# A deck whose provider owns its ranking (TreasureHunt) reports DISCARD_INERT here: the probe
# fans the provider's candidate return, which is a single card.

DISCARD_TRACE_HEADER = re.compile(
    r"^\[discard_trace turn=(\d+) depth=(\d+) seed=(\d+) handsize=(\d+) heur=(.+)\]$")
DISCARD_TRACE_CAND = re.compile(
    r"^  discard (.+?) mv=(-?\d+) copies=(\d+) land=([01]) prot=([01]) -> win=(\d+)( \*)?$")

LOSS_TURN = 9   # THE metric: unwon = max_turns + 1


def _RunBatch(manifest: dict, log_dir: Path, threads: int, extra_env: dict) -> str:
    """Run one pooled mtg --batch over `manifest`, return its stderr text."""
    log_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = log_dir / "manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    env = os.environ.copy()
    env.update(extra_env)
    cmd = [str(MTG_BIN), "--batch", str(manifest_path), "--threads", str(threads),
           "--game-log-dir", str(log_dir / "wins")]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=REPO_ROOT)
    if result.returncode != 0:
        raise RuntimeError(f"batch exited {result.returncode}: {result.stderr[-500:]}")
    return result.stderr


_SHED_STATS_RE = re.compile(
    r"=== SHED STATS: real=(\d+)\s+rollout=(\d+) \(low-land=(\d+)\) ===")


def _ShedCensus(deck_path: Path, profile_path: Path, scratch: Path,
                games: int = 40) -> tuple[int, int, int]:
    """(real, rollout, low_land) cleanup sheds -- the denominator the label trace cannot see.

    Short by design: this only has to separate "never reached" from "reached constantly, just not
    where the labeller looks", and the two are orders of magnitude apart.
    """
    manifest = {"jobs": [{
        "name": f"{deck_path.stem}_shedcensus", "deck": str(deck_path),
        "profile": str(profile_path), "games": games, "seed": 930000, "depth": 3,
        "ignore_play_profile": True, "weight": 0}]}
    text = _RunBatch(manifest, scratch / "shed_census", threads=0,
                     extra_env={"MTG_SHED_STATS": "1"})
    m = _SHED_STATS_RE.search(text)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (0, 0, 0)


def _ParseWins(path: Path) -> dict[int, int]:
    """Parse a harness-format .wins file (gi wt digest); losses (wt=-1) -> LOSS_TURN."""
    out: dict[int, int] = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            gi, wt = int(parts[0]), int(parts[1])
            out[gi] = LOSS_TURN if wt < 0 else wt
    return out


def _ParseTraceDecisions(stderr_text: str) -> list[dict]:
    """Parse [discard_trace] blocks into decision dicts."""
    decisions: list[dict] = []
    cur = None
    for line in stderr_text.splitlines():
        h = DISCARD_TRACE_HEADER.match(line)
        if h:
            cur = {"turn": int(h.group(1)), "depth": int(h.group(2)), "seed": int(h.group(3)),
                   "handsize": int(h.group(4)), "heur": h.group(5), "cands": []}
            decisions.append(cur)
            continue
        c = DISCARD_TRACE_CAND.match(line)
        if c and cur is not None:
            cur["cands"].append({"name": c.group(1), "mv": int(c.group(2)),
                                 "copies": int(c.group(3)), "land": c.group(4) == "1",
                                 "prot": c.group(5) == "1", "win": int(c.group(6)),
                                 "opt": c.group(7) is not None})
    return [d for d in decisions if d["cands"]]


def _RulePick(decision: dict, rule: str, order: list[str]) -> dict | None:
    """The candidate a rule sheds at this decision (None -> falls back to status quo)."""
    cands = decision["cands"]
    if rule == "status_quo":
        for c in cands:
            if c["name"] == decision["heur"]:
                return c
        return cands[0]
    if rule == "band":
        band = [c for c in cands if c["copies"] >= 2 and not c["land"] and not c["prot"]]
        if band:
            return max(band, key=lambda c: c["mv"])
        return None
    if rule == "order":
        for name in order:
            for c in cands:
                if c["name"] == name and not c["prot"]:
                    return c
        return None
    raise ValueError(rule)


def _LabelStats(decisions: list[dict], rule: str, order: list[str]) -> dict:
    """Mean regret (rule pick win - best win) and optimal rate of `rule` over the labels."""
    regret_sum, optimal, n = 0.0, 0, 0
    for d in decisions:
        pick = _RulePick(d, rule, order) or _RulePick(d, "status_quo", order)
        best = min(c["win"] for c in d["cands"])
        regret_sum += pick["win"] - best
        optimal += 1 if pick["win"] == best else 0
        n += 1
    return {"mean_regret": round(regret_sum / n, 4) if n else 0.0,
            "optimal_rate": round(optimal / n, 3) if n else 1.0, "decisions": n}


def _PairedDelta(base: dict[int, int], arm: dict[int, int]) -> dict:
    """Paired per-game stats: mean delta (negative = arm better), t-stat, games changed."""
    gis = sorted(set(base) & set(arm))
    diffs = [arm[g] - base[g] for g in gis]
    n = len(diffs)
    if n == 0:
        return {"n": 0, "mean_delta": 0.0, "t": 0.0, "changed": 0}
    mean = sum(diffs) / n
    var = sum((d - mean) ** 2 for d in diffs) / (n - 1) if n > 1 else 0.0
    t = mean / ((var / n) ** 0.5) if var > 0 else 0.0
    return {"n": n, "mean_delta": round(mean, 5), "t": round(t, 2),
            "changed": sum(1 for d in diffs if d != 0)}


def DiscardAnalysis(deck_path: Path, evidence_games: int, ab_games: int,
                    ab_threads: int = 0) -> dict:
    deck_name = deck_path.stem
    profile_path = deck_path.parent / (deck_name + ".profile.json")
    scratch = REPO_ROOT / "logs" / "discard_analysis" / deck_name
    out: dict = {"deck": deck_name}

    # -- 1. EVIDENCE: searched trial tables from the offline probe -----------------------
    print(f"  [discard] evidence run: {evidence_games} games d3, single-thread, probe as "
          f"offline labeller...", file=sys.stderr)
    ev_manifest = {"jobs": [{
        "name": f"{deck_name}_evidence", "deck": str(deck_path), "profile": str(profile_path),
        "games": evidence_games, "seed": 930000, "depth": 3, "budget_ms": 10,
        "ignore_play_profile": True, "weight": 0}]}
    stderr_text = _RunBatch(ev_manifest, scratch / "evidence", threads=1,
                            extra_env={"MTG_DISCARD_NODE": "0", "MTG_DISCARD_TRACE": "1"})
    # Persist the trial tables: the residual-case audit (which label-suboptimal decisions are
    # clairvoyance vs a visible-info miss) needs them after the fact.
    with open(scratch / "evidence" / "trace.log", "w", encoding="utf-8") as f:
        f.write(stderr_text)
    decisions = _ParseTraceDecisions(stderr_text)
    out["evidence"] = {"games": evidence_games, "decisions": len(decisions)}
    if not decisions:
        # ZERO LABELS IS NOT THE SAME AS ZERO DECISIONS. The labeller above parses
        # [discard_trace], emitted inside AIEngine::ChooseDiscard -- the REAL cleanup only. The
        # search's own cleanup (TurnSolver::SimulateEndAndStartNextTurn) sheds too, takes index 0
        # of the same ranking with NO search above it, and is invisible here. A deck can therefore
        # shed zero times in play and hundreds of thousands of times inside the rollouts that pick
        # its plans: KittyEquipment is real=0 / rollout=145,888 per 50 games, and reporting that as
        # DISCARD_INERT once cost a session the wrong conclusion (see
        # docs/design/kitty-tutor-and-discard-heuristics.md §1). Count both callers before deciding
        # there is nothing here.
        real, rollout, lowland = _ShedCensus(deck_path, profile_path, scratch)
        out["evidence"]["shed_census"] = {"real": real, "rollout": rollout, "low_land": lowland}
        if rollout == 0:
            out["verdict"] = "DISCARD_INERT"
            out["detail"] = ("no cleanup shed is reached by EITHER caller (real or rollout); "
                             "no policy to derive.")
            return out
        out["verdict"] = "DISCARD_UNLABELLED"
        out["detail"] = (
            f"no LABELS (the trace covers real sheds only, and this deck sheds {real} times in "
            f"play) -- but the rollout sheds {rollout} times, {lowland} of them with under 4 lands "
            f"out, each taking index 0 of the ranking with no search above it. The stage cannot "
            f"author a rule without labels; BOUND the axis instead before assuming it is inert: "
            f"pair `MTG_SHED_WORST=1` (rollout sheds the LAST-ranked candidate) against the "
            f"default over >=150 games/cell -- see test/tools/kitty_ab/gen_shed_suite_manifest.py. "
            f"A zero delta there means no ranking can pay; a non-zero one means this deck needs a "
            f"policy the labeller structurally cannot see.")
        return out

    # Hand-size buckets: =8 is the easy shed-1-of-8 cleanup; bigger hands (and any future
    # pitch-site labels) are the harder small-margin cases the rule must also survive.
    by_bucket: dict[str, int] = {}
    for d in decisions:
        b = "hand8" if d["handsize"] == 8 else "hand9plus"
        by_bucket[b] = by_bucket.get(b, 0) + 1
    out["evidence"]["by_handsize"] = by_bucket

    # Ambiguity: how often the searched labels themselves near-tie (multiple optimal sheds).
    multi_opt = sum(1 for d in decisions if sum(1 for c in d["cands"] if c["opt"]) > 1)
    out["evidence"]["multi_optimal_rate"] = round(multi_opt / len(decisions), 3)

    # -- 2. RULES vs labels --------------------------------------------------------------
    # Per-name shed stats -> derived shed order (names the search prefers shedding).
    name_stats: dict[str, dict] = {}
    for d in decisions:
        best = min(c["win"] for c in d["cands"])
        seen = set()
        for c in d["cands"]:
            if c["name"] in seen:
                continue
            seen.add(c["name"])
            s = name_stats.setdefault(c["name"], {"offered": 0, "optimal": 0, "regret": 0.0})
            s["offered"] += 1
            s["optimal"] += 1 if c["win"] == best else 0
            s["regret"] += c["win"] - best
    sq = _LabelStats(decisions, "status_quo", [])
    eligible = {n: s for n, s in name_stats.items() if s["offered"] >= 5}
    order = sorted((n for n, s in eligible.items()
                    if s["regret"] / s["offered"] < sq["mean_regret"]),
                   key=lambda n: eligible[n]["regret"] / eligible[n]["offered"])[:8]
    label_table = {"status_quo": sq,
                   "band": _LabelStats(decisions, "band", []),
                   "order": _LabelStats(decisions, "order", order) if order else None}
    out["label_table"] = label_table
    out["derived_order"] = order
    out["name_stats"] = {n: {"offered": s["offered"], "optimal": s["optimal"],
                             "mean_regret": round(s["regret"] / s["offered"], 3)}
                         for n, s in sorted(eligible.items(),
                                            key=lambda kv: kv[1]["regret"] / kv[1]["offered"])}

    # -- 3. OUTCOME A/B: the derived order, trialled via the MTG_DISCARD_ORDER test lever ---
    # (Shipped rules are PROVIDER-owned -- user ruling 2026-08-07 -- so the arm uses the
    # testing-only env lever, not a shipped config. The band is a LABEL-ONLY hypothesis: it
    # lost to authored orders on every deck where duplicates mattered, so it has no engine
    # support; if a deck's labels ever demand it, implement it in that deck's provider.)
    out["ab"] = {}
    if order and label_table["order"]["mean_regret"] < sq["mean_regret"]:
        def _arm_jobs(tag: str) -> list[dict]:
            return [{"name": f"{deck_name}_{tag}_d0", "deck": str(deck_path),
                     "profile": str(profile_path), "games": ab_games, "seed": 910000,
                     "depth": 0, "budget_ms": 0, "ignore_play_profile": True, "weight": 0},
                    {"name": f"{deck_name}_{tag}_d3", "deck": str(deck_path),
                     "profile": str(profile_path), "games": ab_games // 5, "seed": 920000,
                     "depth": 3, "budget_ms": 10, "ignore_play_profile": True, "weight": 0}]
        print("  [discard] outcome A/B: order arm (MTG_DISCARD_ORDER lever) vs control, "
              "paired seeds...", file=sys.stderr)
        threads = ab_threads or os.cpu_count() or 8
        _RunBatch({"jobs": _arm_jobs("control")}, scratch / "ab", threads=threads, extra_env={})
        _RunBatch({"jobs": _arm_jobs("order")}, scratch / "ab_order", threads=threads,
                  extra_env={"MTG_DISCARD_ORDER": ";".join(order)})
        res = {}
        for cfg in ("d0", "d3"):
            base = _ParseWins(scratch / "ab" / "wins" / f"{deck_name}_control_{cfg}.wins")
            arm = _ParseWins(scratch / "ab_order" / "wins" / f"{deck_name}_order_{cfg}.wins")
            res[cfg] = _PairedDelta(base, arm)
        out["ab"]["order"] = res
        arms = {"order": {"discard_order": order}}
    else:
        arms = {}

    # -- 4. RECOMMENDATION (report only; adoption is user-approved) ----------------------
    best_tag, best_delta = None, 0.0
    for tag, res in out["ab"].items():
        # Adopt-worthy: SIGNIFICANTLY better (|t| >= 2) in at least one config and
        # significantly worse in none -- an insignificant positive delta is noise, not a veto.
        deltas = [res[c]["mean_delta"] for c in res]
        ts = [res[c]["t"] for c in res]
        if (any(d < 0 and abs(t) >= 2 for d, t in zip(deltas, ts))
                and not any(d > 0 and abs(t) >= 2 for d, t in zip(deltas, ts))):
            total = sum(deltas)
            if total < best_delta:
                best_tag, best_delta = tag, total
    if best_tag:
        out["verdict"] = "RULE_FOUND"
        out["recommendation"] = {"policy": arms[best_tag], "arm": best_tag,
                                 "summed_delta": round(best_delta, 5)}
        out["detail"] = (f"'{best_tag}' beats the current ranking on search labels AND on the "
                         f"paired outcome A/B; on user approval, implement it as the deck "
                         f"provider's CleanupDiscardCandidates override (rules are "
                         f"provider-owned; see HinataProvider for the simple-order shape).")
    elif sq["mean_regret"] > 0.1 and sq["optimal_rate"] < 0.9:
        out["verdict"] = "NO_RULE_CONSIDER_SEARCH"
        out["detail"] = (f"status quo leaves {sq['mean_regret']:.2f} mean label regret "
                         f"({(1-sq['optimal_rate'])*100:.0f}% of decisions suboptimal) but no "
                         f"candidate rule captured it -- a candidate for the searched-width "
                         f"escalation (a few options searched via Plan::discard_choice).")
    else:
        out["verdict"] = "STATUS_QUO_OK"
        out["detail"] = "the current ranking is at or near the searched optimum on this deck."
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

    # Standalone discard-analysis stage (also runs as stage 8 of the full flow). Report only;
    # adoption of the recommended policy is a separate, user-approved provider override.
    if args.discard_analysis:
        result = DiscardAnalysis(deck_path, args.discard_evidence_games, args.discard_ab_games,
                                 args.discard_ab_threads)
        print(json.dumps(result, indent=2))
        return

    # 1. Parse
    deck_counts = LoadDeckCounts(deck_path)
    card_names  = list(deck_counts.keys())
    side_counts = LoadDeckCounts(deck_path, "side")
    side_names  = list(side_counts.keys())

    # 2. Coverage -- over the mainboard PLUS any sideboard the deck can actually reach.
    # A wish deck's sideboard is not optional colour: on EldraziDisplacerFlicker both win
    # conditions (Essence Depleter, Dimensional Infiltrator) live there, and scanning the
    # mainboard alone reported a clean two-card gap while staying silent on them.
    reach       = SideboardReachability(card_names, side_names, cards_json)
    scanned     = card_names + ([n for n in side_names if n not in card_names]
                                if reach["reachable"] else [])
    boards      = {n: "main" for n in card_names}
    for n in side_names:
        boards.setdefault(n, "side")

    implemented = LoadImplementedNames(cards_json)
    missing     = [n for n in scanned if n not in implemented]
    existing    = [n for n in scanned if n in implemented]
    coverage    = CheckExistingCoverage(existing, cards_json, boards)

    report = {
        "deck":     deck_path.stem,
        "cards":    card_names,
        "sideboard": {
            "cards":     side_names,
            "reachable": reach["reachable"],
            "via":       reach["via"],
            "reason":    reach["reason"],
        },
        "missing":  missing,
        "coverage": coverage,
        # Engine-inferred card dependency edges (review item: a missing edge here means the
        # implementation lacks the param that would derive it -- same class as a coverage gap).
        "dependency_map": DeriveDependencyMap(existing, cards_json),
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
    analysis = RunAnalyzer(deck_path, cards_json, seed=args.analyzer_seed)

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

    # 8. Discard analysis — search-informed per-deck discard policy (report only; adoption of
    # the recommendation is a separate, user-approved profile edit).
    print("  Running discard analysis (search-labelled evidence + rule A/B)...", file=sys.stderr)
    try:
        da = DiscardAnalysis(deck_path, args.discard_evidence_games, args.discard_ab_games,
                             args.discard_ab_threads)
    except Exception as e:  # never let the diagnostic sink the analysis
        da = {"verdict": "ERROR", "detail": str(e)}
    report["discard_analysis"] = da
    print(f"  Discard analysis: {da.get('verdict')} — {da.get('detail', '')}", file=sys.stderr)

    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    Main()
