#!/usr/bin/env python3
"""scripts/verify_deck.py -- the deck-onboarding ENFORCEMENT SPINE (workstream 3).

ONE green gate. Runs the whole verification battery over a deck and exits non-zero
unless *every* blocking check is green OR its failure is a recorded user sign-off in the
per-deck ledger docs/design/analysis-<deck>.md. Emits the Stage-6a disclosure and (with
--write-ledger) records the run summary, the disclosure, and the items still pending the
user's sign-off.

The point (docs/design/deck-onboarding-hardening.md, workstream 3): the only thing that
reaches the user is an explicit approve-or-defer decision -- never a bug they had to find.
So a check that is NOT YET BUILT (workstream 2 field/clause audit, workstream 4 broadened
claude-play correctness sweep) is reported as a DISCLOSED skip, never silently omitted --
a silent skip reads as "covered" when it isn't.

Gates (blocking unless noted):
  coverage      -- scripts/analyze_deck.py --coverage-only, HARD on missing OR partial gaps
  card_costs    -- scripts/audit_card_costs.py (Scryfall); skipped with --no-network
  card_fields   -- workstream 2 (P/T, keywords vs Scryfall): NOT BUILT -> disclosed skip
  clause_ledger -- workstream 2 (every oracle clause accounted): NOT BUILT -> disclosed skip
  viewer        -- scripts/audit_viewer_decisions.py (self-guard + surface sweep)
  viewer_wiring -- every decision type the deck uses has an emitter (main.cpp) AND a GUI
                   branch (index.html), per the DECISIONS.md registry (sites 3 & 4, static)
  mismatch      -- engine MTG_FLAG_NONCONV + MTG_FD_ORACLE across seeds (no [nonconv]/[fd-diverge])
  claude_play   -- workstream 4 broadened correctness sweep: NOT BUILT -> disclosed skip

Sign-off: the ledger's "## Approved deferrals" section (user-owned) lists keys like
`coverage:Ignoble Hierarch` or `viewer_wiring:land_entry`. A blocking failure whose every
finding key is approved is downgraded to DEFERRED (disclosed, non-blocking). Un-approved
blocking findings fail the gate.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

BIN = str(ROOT / "build/Release/mtg")
CARDS_JSON = str(ROOT / "src/cards/data/cards.json")
DECISIONS_MD = ROOT / "tools/play/DECISIONS.md"
MAIN_CPP = ROOT / "src/main.cpp"
INDEX_HTML = ROOT / "tools/play/index.html"

PASS, FAIL, SKIP, DEFERRED, ERROR = "PASS", "FAIL", "SKIP", "DEFERRED", "ERROR"


class Gate:
    """One check's result. `findings` are (key, detail) pairs the ledger can sign off."""
    def __init__(self, name, status, blocking, summary, findings=None, disclose=None):
        self.name = name
        self.status = status          # PASS | FAIL | SKIP | DEFERRED | ERROR
        self.blocking = blocking
        self.summary = summary
        self.findings = findings or []   # list[(key, detail)] -- the sign-off-able items
        self.disclose = disclose or []   # list[str] -- disclosed (deferred/skip) notes for Stage 6a


# --------------------------------------------------------------------------- helpers
def run(cmd, timeout=600, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=e)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", f"timeout after {timeout}s"
    except FileNotFoundError as exc:
        return 127, "", str(exc)


def deck_stem(deck_path):
    return Path(deck_path).stem


# --------------------------------------------------------------------------- gates
def gate_coverage(deck_path):
    """analyze_deck.py --coverage-only, HARD on missing OR partial (the plan's gap: partials exit 0)."""
    rc, out, err = run([sys.executable, str(ROOT / "scripts/analyze_deck.py"),
                        deck_path, "--coverage-only"], timeout=180)
    # The tool prints JSON to stdout; a fully-absent card makes it exit 1. Parse regardless.
    try:
        data = json.loads(out[out.index("{"):out.rindex("}") + 1])
    except (ValueError, json.JSONDecodeError):
        return Gate("coverage", ERROR, True,
                    f"could not parse coverage JSON (rc={rc}); tool may be dormant. stderr: {err.strip()[:200]}")
    findings, disclose = [], []
    for c in data.get("missing", []):
        name = c if isinstance(c, str) else c.get("card", str(c))
        findings.append((f"coverage:{name}", f"MISSING card not implemented: {name}"))
    for c in data.get("coverage", []):
        name, status = c.get("card"), c.get("status")
        if status == "partial":
            for g in c.get("gaps", []):
                findings.append((f"coverage:{name}", f"PARTIAL {name}: {g}"))
        for d in c.get("deferred", []):
            disclose.append(f"coverage deferral -- {name}: {d}")
    if findings:
        return Gate("coverage", FAIL, True,
                    f"{len(findings)} missing/partial gap(s)", findings, disclose)
    return Gate("coverage", PASS, True,
                f"all {len(data.get('coverage', []))} cards full (missing=0, partial=0)",
                disclose=disclose)


def gate_card_costs(no_network):
    if no_network:
        return Gate("card_costs", SKIP, True, "skipped (--no-network)",
                    disclose=["card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run"])
    rc, out, err = run([sys.executable, str(ROOT / "scripts/audit_card_costs.py")], timeout=300)
    if rc == 127 or "Traceback" in err:
        return Gate("card_costs", ERROR, True, f"tool error (rc={rc}): {err.strip()[:200]}")
    findings = []
    if rc != 0 or "MISMATCHES" in out:
        for ln in out.splitlines():
            m = re.match(r"\s{2,}(\S.*?)\s{2,}local=", ln)
            if m:
                findings.append((f"card_costs:{m.group(1).strip()}", ln.strip()))
        if not findings:
            findings.append(("card_costs:*", f"cost audit non-zero (rc={rc})"))
        return Gate("card_costs", FAIL, True, f"{len(findings)} Scryfall cost mismatch(es)", findings)
    return Gate("card_costs", PASS, True, "all mana costs match Scryfall (cost/cmc only)")


def gate_card_fields():
    return Gate("card_fields", SKIP, True, "NOT BUILT (workstream 2)",
                disclose=["card_fields SKIPPED -- P/T, keywords, trigger thresholds vs Scryfall "
                          "(audit_card_fields.py) is workstream 2, not yet built. Cost/cmc IS checked "
                          "(card_costs); other Scryfall-checkable fields are NOT reconciled yet."])


def gate_clause_ledger():
    return Gate("clause_ledger", SKIP, True, "NOT BUILT (workstream 2)",
                disclose=["clause_ledger SKIPPED -- per-card 'every oracle clause modeled/inert/deferred' "
                          "accounting is workstream 2, not yet built. The viewer auditor's advisory "
                          "oracle-text cross-check partially covers dropped CHOICE clauses only."])


def gate_viewer(deck_path, profile, no_sweep):
    args = [sys.executable, str(ROOT / "scripts/audit_viewer_decisions.py"), deck_path]
    if profile and not no_sweep:
        args.append(str(profile))
    if no_sweep:
        args.append("--no-sweep")
    rc, out, err = run(args, timeout=900)
    tail = (out or err).strip().splitlines()
    tail = " | ".join(tail[-3:])[:300]
    if rc == 0:
        note = "self-guard + surface" + (" (static, --no-sweep)" if no_sweep else " sweep")
        disc = ["viewer SWEEP SKIPPED (--no-sweep) -- decision surfacing not runtime-verified"] if no_sweep else []
        return Gate("viewer", PASS, True, f"{note} clean", disclose=disc)
    return Gate("viewer", FAIL, True, f"auditor rc={rc}: {tail}",
                [("viewer:auditor", f"audit_viewer_decisions rc={rc}: {tail}")])


def _parse_decisions_registry():
    """type -> (emitter_symbol|None, gui_symbol|None) from the DECISIONS.md markdown table."""
    reg = {}
    if not DECISIONS_MD.exists():
        return reg
    for ln in DECISIONS_MD.read_text().splitlines():
        if not ln.strip().startswith("|"):
            continue
        cells = [c.strip() for c in ln.strip().strip("|").split("|")]
        if len(cells) < 5 or cells[0] in ("`type`", "type", "---") or set(cells[0]) <= {"-", " "}:
            continue
        tkey = cells[0].strip("` ")
        if not re.fullmatch(r"[a-z_]+", tkey):
            continue

        def sym(cell):
            m = re.search(r"`([A-Za-z_][A-Za-z0-9_]*)`", cell)
            return m.group(1) if m else None
        reg[tkey] = (sym(cells[3]), sym(cells[4]))   # emitter (main.cpp), GUI (index.html)
    return reg


def gate_viewer_wiring(deck_path):
    """Static sites 3 & 4: every decision type the deck uses has an emitter in main.cpp AND a
    GUI branch in index.html, per the DECISIONS.md registry (closes 'type surfaces, GUI missing')."""
    try:
        import audit_viewer_decisions as ava
        cards, _ = ava.load_deck_cards(deck_path, CARDS_JSON)
        expected = set()
        for c in cards:
            exp, _ = ava.expected_for_card(c)
            expected |= exp
    except Exception as exc:   # noqa: BLE001
        return Gate("viewer_wiring", ERROR, True, f"could not compute expected types: {exc}")
    # main_phase / mulligan / bottom are engine-level, not per-deck-wired card decisions.
    expected -= {"main_phase", "mulligan", "bottom"}
    if not expected:
        return Gate("viewer_wiring", PASS, True, "no card-driven decision types in this deck")
    reg = _parse_decisions_registry()
    main_src = MAIN_CPP.read_text() if MAIN_CPP.exists() else ""
    html_src = INDEX_HTML.read_text() if INDEX_HTML.exists() else ""
    findings = []
    for t in sorted(expected):
        if t not in reg:
            findings.append((f"viewer_wiring:{t}", f"type '{t}' not in DECISIONS.md registry (unmapped)"))
            continue
        emitter, gui = reg[t]
        if emitter and emitter not in main_src:
            findings.append((f"viewer_wiring:{t}", f"type '{t}': emitter {emitter} not found in main.cpp"))
        if gui and gui not in html_src:
            findings.append((f"viewer_wiring:{t}", f"type '{t}': GUI branch {gui} not found in index.html"))
    if findings:
        return Gate("viewer_wiring", FAIL, True, f"{len(findings)} wiring gap(s)", findings)
    return Gate("viewer_wiring", PASS, True,
                f"{len(expected)} type(s) wired (emitter + GUI): {', '.join(sorted(expected))}")


def gate_mismatch(deck_path, profile, seeds, games, no_sweep):
    """Engine mismatch harnesses: no [nonconv] (search inconsistency) or [fd-diverge]
    (rollout-vs-real) across seeds. A single flagged line is a real defect (analyze-deck 5a)."""
    if no_sweep:
        return Gate("mismatch", SKIP, True, "skipped (--no-sweep)",
                    disclose=["mismatch SKIPPED (--no-sweep) -- nonconv/fd-diverge not exercised"])
    if not Path(BIN).exists():
        return Gate("mismatch", ERROR, True, f"engine binary not built at {BIN}")
    prof_args = ["--profile", str(profile)] if profile and Path(profile).exists() else []
    flagged = []
    for s in seeds:
        _, _, e1 = run([BIN, deck_path, *prof_args, "--games", str(games), "--seed", str(s),
                        "--depth", "3", "--budget-ms", "20", "--lookahead-bottoming", "--threads", "1"],
                       timeout=600, env={"MTG_FLAG_NONCONV": "1"})
        _, _, e2 = run([BIN, deck_path, *prof_args, "--games", str(games), "--seed", str(s),
                        "--depth", "5", "--budget-ms", "20", "--lookahead-bottoming", "--threads", "1"],
                       timeout=900, env={"MTG_FULL_DEPTH": "1", "MTG_FD_ORACLE": "1"})
        for line in (e1 + "\n" + e2).splitlines():
            if "[nonconv]" in line or "[fd-diverge]" in line:
                flagged.append((f"mismatch:seed{s}", line.strip()[:200]))
    if flagged:
        return Gate("mismatch", FAIL, True, f"{len(flagged)} nonconv/fd-diverge line(s)", flagged)
    return Gate("mismatch", PASS, True, f"no nonconv/fd-diverge across seeds {seeds} x {games} games")


def gate_claude_play():
    return Gate("claude_play", SKIP, True, "NOT ENFORCED (workstream 4)",
                disclose=["claude_play SKIPPED -- the broadened Claude-driven correctness sweep "
                          "(.claude/skills/claude-play.md, analyze-deck 5d) is workstream 4, not yet a "
                          "gated step. The viewer gate above verifies surfacing; PLAY correctness "
                          "beyond nonconv/fd-diverge is still a manual sweep."])


# --------------------------------------------------------------------------- ledger
LEDGER_BEGIN = "<!-- verify_deck:begin (generated -- do not edit inside) -->"
LEDGER_END = "<!-- verify_deck:end -->"


def ledger_path(deck_path):
    return ROOT / "docs/design" / f"analysis-{deck_stem(deck_path)}.md"


def read_approved(deck_path):
    """Keys under a user-owned '## Approved deferrals' section (outside the generated block)."""
    p = ledger_path(deck_path)
    if not p.exists():
        return set()
    text = p.read_text()
    # strip the generated block so an approval can never live inside it
    text = re.sub(re.escape(LEDGER_BEGIN) + r".*?" + re.escape(LEDGER_END), "", text, flags=re.S)
    approved = set()
    in_sec = False
    for ln in text.splitlines():
        if re.match(r"^##\s+Approved deferrals", ln, re.I):
            in_sec = True
            continue
        if in_sec and ln.startswith("## "):
            break
        if in_sec:
            m = re.match(r"^\s*[-*]\s*`?([a-z_]+:[^`\s].*?)`?\s*(?:--|—|:|$)", ln)
            if m:
                approved.add(m.group(1).strip().rstrip("` "))
    return approved


def apply_signoff(gates, approved):
    """Downgrade a blocking FAIL to DEFERRED iff every finding key is approved."""
    for g in gates:
        if g.status == FAIL and g.findings:
            if all(k in approved for k, _ in g.findings):
                g.status = DEFERRED
                g.disclose = list(g.disclose) + [f"{g.name} DEFERRED (user sign-off): {d}" for _, d in g.findings]


def write_ledger(deck_path, gates, approved, blocking_fail, cmdline):
    p = ledger_path(deck_path)
    today = datetime.date.today().isoformat()
    lines = [LEDGER_BEGIN,
             f"## Last verification ({today})",
             "",
             f"`{cmdline}` -> **{'FAIL' if blocking_fail else 'PASS'}**",
             "",
             "| Gate | Status | Blocking | Summary |",
             "|---|---|---|---|"]
    for g in gates:
        lines.append(f"| {g.name} | {g.status} | {'yes' if g.blocking else 'no'} | {g.summary} |")
    pending = [(k, d) for g in gates if g.status in (FAIL, ERROR) for (k, d) in (g.findings or [(f'{g.name}:*', g.summary)])
               if k not in approved]
    lines += ["", "### Pending user sign-off (block the gate until fixed OR approved below)"]
    if pending:
        lines.append("Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, "
                     "understood deferral -- not a bug):")
        for k, d in pending:
            lines.append(f"- `{k}` -- {d}")
    else:
        lines.append("_none_ -- every blocking gate is green or already signed off.")
    disclosures = [d for g in gates for d in g.disclose]
    lines += ["", "### Stage 6a disclosure (deferrals + not-yet-built checks)"]
    lines += ([f"- {d}" for d in disclosures] or ["_none_"])
    lines += ["", LEDGER_END, ""]
    block = "\n".join(lines)

    if p.exists():
        text = p.read_text()
        if LEDGER_BEGIN in text and LEDGER_END in text:
            text = re.sub(re.escape(LEDGER_BEGIN) + r".*?" + re.escape(LEDGER_END), block.strip(), text, flags=re.S)
        else:
            text = text.rstrip() + "\n\n" + block
    else:
        text = (f"# Analysis ledger -- {deck_stem(deck_path)}\n\n"
                f"Per-deck onboarding verification record (workstream 3 spine). The generated block "
                f"below is overwritten by `verify_deck.py`; the **Approved deferrals** section is "
                f"yours to edit and is never touched by the tool.\n\n"
                f"## Approved deferrals\n\n"
                f"_Add `- \\`gate:key\\` -- why this is a genuine, understood deferral (not a bug)` "
                f"lines here to sign off a pending item. Requires explicit user judgement._\n\n"
                + block)
    p.write_text(text)
    return p


# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Deck-onboarding enforcement spine (workstream 3).")
    ap.add_argument("deck", help="Path to the .cod/.txt decklist")
    ap.add_argument("--profile", default=None, help="profile.json (default: auto-detect sibling)")
    ap.add_argument("--seeds", default="7001 7002", help="mismatch-harness seeds (space-separated)")
    ap.add_argument("--games", type=int, default=60, help="games per mismatch-harness seed")
    ap.add_argument("--no-network", action="store_true", help="skip the Scryfall cost audit")
    ap.add_argument("--no-sweep", action="store_true", help="skip runtime gates (viewer sweep, mismatch)")
    ap.add_argument("--write-ledger", action="store_true", help="write docs/design/analysis-<deck>.md")
    ap.add_argument("--json", action="store_true", help="machine-readable summary to stdout")
    args = ap.parse_args()

    deck = args.deck
    if not Path(deck).exists():
        print(f"ERROR: deck not found: {deck}", file=sys.stderr)
        return 2
    profile = args.profile
    if profile is None:
        cand = Path(deck).with_suffix("").parent / (Path(deck).stem + ".profile.json")
        profile = str(cand) if cand.exists() else None
    seeds = [int(s) for s in args.seeds.split()]

    gates = [
        gate_coverage(deck),
        gate_card_costs(args.no_network),
        gate_card_fields(),
        gate_clause_ledger(),
        gate_viewer(deck, profile, args.no_sweep),
        gate_viewer_wiring(deck),
        gate_mismatch(deck, profile, seeds, args.games, args.no_sweep),
        gate_claude_play(),
    ]

    approved = read_approved(deck)
    apply_signoff(gates, approved)
    blocking_fail = any(g.blocking and g.status in (FAIL, ERROR) for g in gates)

    icon = {PASS: "PASS ", FAIL: "FAIL ", SKIP: "SKIP ", DEFERRED: "DEFER", ERROR: "ERROR"}
    print(f"\n=== verify_deck: {deck_stem(deck)} ===")
    for g in gates:
        print(f"  [{icon[g.status]}] {g.name:<14} {g.summary}")
        for k, d in g.findings:
            mark = "signed-off" if k in approved else "PENDING"
            print(f"            - ({mark}) {d}")

    disclosures = [d for g in gates for d in g.disclose]
    if disclosures:
        print("\n--- Stage 6a disclosure (deferrals + not-yet-built checks) ---")
        for d in disclosures:
            print(f"  * {d}")

    if args.write_ledger:
        cmdline = "verify_deck.py " + " ".join([deck] + [a for a in sys.argv[1:] if a != deck])
        p = write_ledger(deck, gates, approved, blocking_fail, cmdline)
        print(f"\nledger written: {p.relative_to(ROOT)}")

    print("\n" + ("GATE FAIL: fix the pending items above, or sign each off in the ledger's "
                  "'## Approved deferrals'." if blocking_fail
                  else "GATE PASS: every blocking check is green or signed off."))

    if args.json:
        print(json.dumps({"deck": deck_stem(deck), "pass": not blocking_fail,
                          "gates": [{"name": g.name, "status": g.status, "blocking": g.blocking,
                                     "summary": g.summary,
                                     "findings": [{"key": k, "detail": d} for k, d in g.findings]}
                                    for g in gates]}, indent=2))
    return 1 if blocking_fail else 0


if __name__ == "__main__":
    sys.exit(main())
