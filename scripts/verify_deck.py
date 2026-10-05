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
  card_fields   -- workstream 2: offline diff of cards.json vs committed Scryfall snapshot
                   (mana_cost/P-T/types/keywords HARD; oracle_text advisory). Systematic
                   divergences stripped in-code; intentional per-card ones allowlisted
                   (scryfall_divergences.json). Blocking FAIL on any un-allowlisted mismatch.
  clause_ledger -- workstream 2 (every oracle clause accounted): NOT BUILT -> disclosed skip
  viewer        -- scripts/audit_viewer_decisions.py (self-guard + surface sweep)
  viewer_wiring -- every decision type the deck uses has an emitter (main.cpp) AND a GUI
                   branch (index.html), per the DECISIONS.md registry (sites 3 & 4, static)
  mismatch      -- engine MTG_FLAG_NONCONV + MTG_FD_ORACLE across seeds (no [nonconv]/[fd-diverge])
  play_invariants -- workstream 4a: drive the claude-play protocol auto-following engine
                   defaults; assert determinism + integrity + progress (runtime; --no-sweep skips)
  claude_sweep  -- workstream 4b: the Claude-DRIVEN judgment sweep, recorded in the per-deck
                   ledger ('## Claude-play sweep'); absent->SKIP, unresolved flags->FAIL, clean->PASS
  discard_policy -- the deck's provider must override CleanupDiscardCandidates with an AUTHORED
                   BUCKET policy (analyze-deck 5i). Missing -> blocking FAIL; inherited from a
                   base provider (another deck's buckets) -> FAIL; an override with no
                   MTG_*_BUCKET_DISCARD gate (a patch over the max-MV fallback) -> FAIL. Added
                   2026-09-23 because this mandated step had NO gate, so decks shipped on the
                   generic fallback silently and only the user asking ever surfaced it.

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
def run(cmd, env=None):
    """Run a child to COMPLETION. There is deliberately NO timeout parameter.

    CLAUDE.md: never wrap a command in a timeout -- a truncated run reads as a *result*.
    That is not theoretical here: `gate_mismatch` grepped the child's stderr for flag lines
    and ignored its return code, so a run killed at the cap produced empty stderr, zero
    flagged lines, and a confident **PASS** on a harness that never finished. The whole
    point of this spine is that nothing reaches the user except an explicit approve-or-defer,
    so a did-not-run must never be able to wear a green gate.

    The parameter is REMOVED rather than defaulted to None so that reintroducing a cap is a
    visible edit at every call site, not a one-word default nobody reads.
    """
    e = dict(os.environ)
    if env:
        e.update(env)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, env=e)
        return p.returncode, p.stdout, p.stderr
    except FileNotFoundError as exc:
        return 127, "", str(exc)


def deck_stem(deck_path):
    return Path(deck_path).stem


# --------------------------------------------------------------------------- gates
def gate_coverage(deck_path):
    """analyze_deck.py --coverage-only, HARD on missing OR partial (the plan's gap: partials exit 0)."""
    rc, out, err = run([sys.executable, str(ROOT / "scripts/analyze_deck.py"),
                        deck_path, "--coverage-only"])
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
    # A full cold sweep is ~230 cards x (round trip + 0.1s throttle), and every card Scryfall
    # 429s costs a further 1+2+4s of backoff, so a rate-limited run legitimately needs ~10 min.
    # A 300s cap once timed out on a HEALTHY audit; the cap is now gone entirely (see run()),
    # and each HTTP request carries its own socket timeout, which is what actually bounds this.
    rc, out, err = run([sys.executable, str(ROOT / "scripts/audit_card_costs.py")])
    if rc == 127 or "Traceback" in err:
        return Gate("card_costs", ERROR, True, f"tool error (rc={rc}): {err.strip()[:200]}")
    # DID-NOT-RUN IS NOT A FINDING. audit_card_costs exits 1 ONLY on a real mismatch (an
    # unresolved card does not move its rc), so a 124 means the audit was killed and nothing was
    # compared. Reporting that as "1 Scryfall cost mismatch(es)" invented a defect that did not
    # exist and hid the real cause; fail closed, but say which one it is. We no longer generate
    # 124 ourselves -- this now catches a child that was killed from outside.
    if rc == 124:
        return Gate("card_costs", FAIL, True, "cost audit DID NOT COMPLETE (killed) -- no costs compared",
                    [("card_costs:*", f"cost audit did not complete: {err.strip()[:120]}; "
                                      f"no cost was compared -- this is a did-not-run, not a mismatch")])
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
    """audit_card_fields.py: offline diff of cards.json against the committed Scryfall snapshot
    (mana_cost, P/T, types, keywords HARD; oracle_text advisory). Fails closed if the snapshot
    is missing/incomplete -- that is a DISCLOSED pending (run --update on a networked machine),
    never a silent pass. A hard field mismatch is a blocking FAIL."""
    rc, out, err = run([sys.executable, str(ROOT / "scripts/audit_card_fields.py"), "--json"])
    try:
        data = json.loads(out[out.index("{"):out.rindex("}") + 1])
    except (ValueError, json.JSONDecodeError):
        return Gate("card_fields", ERROR, True, f"tool error (rc={rc}): {(err or out).strip()[:200]}")
    if data.get("reason") == "snapshot_missing":
        return Gate("card_fields", SKIP, True, "Scryfall snapshot not populated",
                    disclose=["card_fields SKIPPED -- src/cards/data/scryfall_reference.json is not "
                              "populated. Run `python scripts/audit_card_fields.py --update` on a "
                              "networked machine, commit the snapshot, then this becomes a live gate "
                              "(P/T, types, keywords, oracle-text reality-diff)."])
    # Intentional per-card divergences are ALLOWLISTED (scryfall_divergences.json), not
    # silently dropped -- surface each so the ledger stays honest. A stale allowlist entry
    # (no longer mismatches) is disclosed for cleanup. Neither blocks.
    disclose = [f"allowlisted divergence -- {a['name']} [{a['field']}]: {a['reason'][:150]}"
                for a in data.get("allowlisted", [])]
    disclose += [f"STALE allowlist entry -- {s['name']} [{s['field']}] no longer mismatches; "
                 f"remove from scryfall_divergences.json" for s in data.get("stale_allowlist", [])]
    disclose += [f"oracle_text advisory -- {a['name']}: {a['detail'][:160]}"
                 for a in data.get("oracle_advisories", [])]
    findings = [(f"card_fields:{m['name']}", f"{m['name']}: {'; '.join(m['issues'])}")
                for m in data.get("mismatches", [])]
    if findings:
        return Gate("card_fields", FAIL, True, f"{len(findings)} Scryfall field mismatch(es)",
                    findings, disclose)
    unfetched = data.get("unfetched", [])
    if unfetched:
        return Gate("card_fields", SKIP, True, f"snapshot incomplete ({len(unfetched)} unfetched)",
                    disclose=disclose + [f"card_fields snapshot INCOMPLETE -- {len(unfetched)} card(s) not "
                                         f"in the snapshot; run --update to reality-check them: "
                                         f"{', '.join(unfetched[:8])}{'...' if len(unfetched) > 8 else ''}"])
    n_allow = len(data.get("allowlisted", []))
    return Gate("card_fields", PASS, True,
                f"{data.get('checked', 0)} cards match snapshot (cost/PT/types/keywords)"
                + (f"; {n_allow} allowlisted divergence(s)" if n_allow else ""), disclose=disclose)


def gate_clause_ledger():
    # The "every oracle clause accounted for" intent is now covered MECHANICALLY by the
    # combination of: coverage (hard-fails on a `partial` -- an implementable clause with no
    # deferral bracket note), bracket-note deferrals (accounted-as-deferred), the viewer auditor's
    # oracle-text cross-check (dropped CHOICE clauses), and audit_card_fields' oracle-text diff
    # (fabrication/drift vs the Scryfall snapshot). A dedicated hand-populated per-clause ledger
    # would add marginal rigor at a high per-card cost, so it is deferred (disclosed) rather than
    # a silent gap.
    return Gate("clause_ledger", SKIP, False, "covered by coverage+bracket-notes+oracle-diff",
                disclose=["clause_ledger: no dedicated per-clause artifact. Its function -- every oracle "
                          "clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + "
                          "bracket-note deferrals + viewer oracle cross-check + audit_card_fields "
                          "oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal "
                          "added rigor)."])


def gate_viewer(deck_path, profile, no_sweep):
    args = [sys.executable, str(ROOT / "scripts/audit_viewer_decisions.py"), deck_path]
    if profile and not no_sweep:
        args.append(str(profile))
    if no_sweep:
        args.append("--no-sweep")
    rc, out, err = run(args)
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
        # A row may cover SEVERAL types sharing one wiring ("`scry` / `surveil` / `reorder`"):
        # register each token separately (they share emitter + GUI symbols).
        tkeys = [t.strip("` ") for t in cells[0].split("/")]
        tkeys = [t for t in tkeys if re.fullmatch(r"[a-z_]+", t)]
        if not tkeys:
            continue

        def sym(cell):
            m = re.search(r"`([A-Za-z_][A-Za-z0-9_]*)`", cell)
            return m.group(1) if m else None
        for tkey in tkeys:
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


def _write_mismatch_manifest(path, deck_path, profile, seeds, games, depth):
    """One pooled `mtg --batch` manifest covering EVERY seed of one arm.

    CLAUDE.md forbids a loop of small invocations: each one strands cores on its own
    load-imbalance tail. Lookahead bottoming is not a manifest field -- the engine derives
    it from depth (on iff depth > 0), which is what the old `--lookahead-bottoming` flag
    did at these depths, so the two routes exercise the same play.
    """
    jobs = []
    for s in seeds:
        j = {"name": f"s{s}d{depth}", "deck": str(deck_path), "games": games,
             "seed": s, "depth": depth, "budget_ms": 20}
        if profile and Path(profile).exists():
            j["profile"] = str(profile)
        jobs.append(j)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"jobs": jobs}, indent=2))
    return path


def gate_mismatch(deck_path, profile, seeds, games, no_sweep):
    """Engine mismatch harnesses: no [nonconv] (search inconsistency) or [fd-diverge]
    (rollout-vs-real) across seeds. A single flagged line is a real defect (analyze-deck 5a).

    TWO arms, because both are selected by PROCESS-WIDE env vars (MTG_FLAG_NONCONV vs
    MTG_FULL_DEPTH+MTG_FD_ORACLE) and so cannot share one process. That is the floor, not a
    wave: within each arm every seed is pooled into ONE `mtg --batch` queue at full threads.
    The old shape was the opposite on both counts -- one invocation per (seed, arm), each
    pinned to `--threads 1`, which is why a 2-seed run took long enough to look hung.

    A non-zero return code is an ERROR, never a pass. This gate reads the engine's STDERR for
    flag lines, so a child that died produced no lines and therefore looked clean; combined
    with the old wrapper timeout, a harness that never finished reported PASS.
    """
    if no_sweep:
        return Gate("mismatch", SKIP, True, "skipped (--no-sweep)",
                    disclose=["mismatch SKIPPED (--no-sweep) -- nonconv/fd-diverge not exercised"])
    if not Path(BIN).exists():
        return Gate("mismatch", ERROR, True, f"engine binary not built at {BIN}")
    scratch = ROOT / "logs/verify" / deck_stem(deck_path)
    arms = [("nonconv", 3, {"MTG_FLAG_NONCONV": "1"}),
            ("fd_oracle", 5, {"MTG_FULL_DEPTH": "1", "MTG_FD_ORACLE": "1"})]
    flagged = []
    for arm, depth, env in arms:
        man = _write_mismatch_manifest(scratch / f"mismatch_{arm}.json", deck_path, profile,
                                       seeds, games, depth)
        rc, out, err = run([BIN, "--batch", str(man)], env=env)
        if rc != 0:
            tail = " | ".join((err or out).strip().splitlines()[-3:])[:300]
            return Gate("mismatch", ERROR, True,
                        f"{arm} arm did NOT complete (rc={rc}) -- nothing was compared: {tail}")
        for line in err.splitlines():
            if "[nonconv]" in line or "[fd-diverge]" in line:
                flagged.append((f"mismatch:{arm}", line.strip()[:200]))
    if flagged:
        return Gate("mismatch", FAIL, True, f"{len(flagged)} nonconv/fd-diverge line(s)", flagged)
    return Gate("mismatch", PASS, True,
                f"no nonconv/fd-diverge across seeds {seeds} x {games} games (both arms completed)")


def gate_play_invariants(deck_path, profile, seeds, games, no_sweep):
    """workstream 4a (mechanical half): drive the claude-play stateless-replay protocol
    auto-following the engine's own defaults and assert protocol/engine INVARIANTS the
    autonomous smoke can't -- determinism (same CSV -> byte-identical decision block),
    integrity (valid JSON, known decision type, contiguous plan indices, clean 70/0 exit
    codes), progress (reaches a result, no runaway). A HARD violation is a real oracle/
    engine regression; cast-availability notes are advisory (cascade/vial/staged-from-exile
    legitimately cast a name not in hand)."""
    if no_sweep:
        return Gate("play_invariants", SKIP, True, "skipped (--no-sweep)",
                    disclose=["play_invariants SKIPPED (--no-sweep) -- claude-play protocol "
                              "determinism/integrity/progress not exercised"])
    if not Path(BIN).exists():
        return Gate("play_invariants", ERROR, True, f"claude-play binary not built at {BIN}")
    prof = ["--profile", str(profile)] if profile and Path(profile).exists() else []
    n_games = min(games, 4)   # a few game-indices/seed is plenty; keep the gate fast (~1s/game)
    rc, out, err = run([sys.executable, str(ROOT / "scripts/play_invariants.py"), deck_path, *prof,
                        "--seeds", ",".join(str(s) for s in seeds), "--games", str(n_games), "--json"])
    try:
        data = json.loads(out[out.index("{"):out.rindex("}") + 1])
    except (ValueError, json.JSONDecodeError):
        return Gate("play_invariants", ERROR, True, f"tool error (rc={rc}): {(err or out).strip()[:200]}")
    disclose = [f"play advisory -- seed {s} gi {gi}: {a[:150]}" for s, gi, a in data.get("advisories", [])]
    hard = data.get("hard", [])
    if hard:
        findings = [(f"play_invariants:s{s}g{gi}", f"seed {s} gi {gi}: {f}") for s, gi, f in hard]
        return Gate("play_invariants", FAIL, True, f"{len(hard)} invariant violation(s)", findings, disclose)
    return Gate("play_invariants", PASS, True,
                f"{data.get('games', 0)} game(s)/{data.get('decisions', 0)} decisions: "
                f"determinism+integrity+progress hold", disclose=disclose)


def _ledger_section(deck_path, heading):
    """Lines of a user-owned '## <heading>' section (outside the generated block), or None."""
    p = ledger_path(deck_path)
    if not p.exists():
        return None
    text = re.sub(re.escape(LEDGER_BEGIN) + r".*?" + re.escape(LEDGER_END), "", p.read_text(), flags=re.S)
    out, in_sec = [], False
    for ln in text.splitlines():
        if re.match(r"^##\s+" + re.escape(heading), ln, re.I):
            in_sec = True
            continue
        if in_sec and ln.startswith("## "):
            break
        if in_sec:
            out.append(ln)
    return out if in_sec else None


def _git_head():
    rc, out, _ = run(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    return out.strip() if rc == 0 else ""


def gate_claude_sweep(deck_path):
    """workstream 4b (judgment half): the Claude-DRIVEN play sweep (analyze-deck 5d /
    Workflow, one agent per game) is an expensive, user-initiated step whose result is
    RECORDED in the per-deck ledger under '## Claude-play sweep' (commit / seeds / flags).
    This gate enforces the record: absent -> disclosed SKIP (run it); >=1 unresolved flag
    -> blocking FAIL; clean -> PASS (staleness vs HEAD disclosed -- the live play_invariants
    gate + smoke digests track whether play changed, so the user re-runs when it does)."""
    sec = _ledger_section(deck_path, "Claude-play sweep")
    if sec is None:
        return Gate("claude_sweep", SKIP, True, "no Claude-play sweep recorded",
                    disclose=["claude_sweep SKIPPED -- no '## Claude-play sweep' section in "
                              f"{ledger_path(deck_path).relative_to(ROOT)}. Run the Claude-driven sweep "
                              "(.claude/skills/claude-play.md, analyze-deck 5d; fan game-indices out with "
                              "the Workflow engine), verify any flags against cards.json + the rules skill, "
                              "then record `commit:` / `seeds:` / `games:` / `flags: N unresolved` under "
                              "that heading. play_invariants (above) already guards the protocol mechanically."])
    body = "\n".join(sec)
    mflags = re.search(r"flags:\s*(\d+)\s*unresolved", body, re.I)
    mcommit = re.search(r"commit:\s*`?([0-9a-f]{7,40})`?", body, re.I)
    commit = mcommit.group(1) if mcommit else ""
    head = _git_head()
    disclose = []
    if commit and head and not head.startswith(commit):
        # The staleness rationale used to read "play_invariants + smoke digests track play live",
        # which is only true for a deck the suite actually RUNS. For a deck that is not a
        # regression case, no digest watches it at all, and the sweep's age is therefore
        # unbounded -- saying otherwise overstates the coverage of a gate that just went green.
        stem = deck_stem(deck_path)
        cases = ROOT / "test/regression_cases.sh"
        in_suite = cases.exists() and stem.lower() in cases.read_text().lower()
        tracked = ("play_invariants + smoke digests track play live"
                   if in_suite else
                   f"NOTE: {stem} is NOT a regression case, so NO digest tracks its play -- nothing "
                   f"will tell you when this record goes stale. Re-run the sweep on judgement, or "
                   f"add the deck to the suite")
        disclose.append(f"claude_sweep recorded at commit {commit} (HEAD {head[:12]}); re-run if play "
                        f"changed since ({tracked}).")
    if mflags is None:
        return Gate("claude_sweep", SKIP, True, "sweep record present but flag count unparseable",
                    disclose=disclose + ["claude_sweep record found but no 'flags: N unresolved' line -- "
                                         "add one so the gate can enforce cleanliness."])
    unresolved_n = int(mflags.group(1))
    if unresolved_n > 0:
        return Gate("claude_sweep", FAIL, True, f"{unresolved_n} unresolved play flag(s)",
                    [("claude_sweep:unresolved", f"{unresolved_n} unresolved flag(s) in the recorded sweep")],
                    disclose)
    return Gate("claude_sweep", PASS, True, "Claude-play sweep recorded, 0 unresolved flags", disclose=disclose)


# --------------------------------------------------------------------------- discard policy
# A policy that is AUTHORED but predates the flag convention. Keyed by class, with the reason, so
# the allowlist cannot quietly grow: an entry here asserts "this is a real bucket policy", which is
# a claim a reader can check against the code.
DISCARD_AUTHORED_NO_FLAG = {
    "TreasureHuntProvider":
        "the keep-set rule with the broken-up mana bucket -- analyze-deck 5i names it a reference "
        "implementation; it predates the MTG_*_BUCKET_DISCARD convention",
}

# The signals that an override IS an authored bucket policy rather than a patch over the shared
# max-MV fallback: the default-on A/B hatch the convention requires (see analyze-deck 5i).
DISCARD_FLAG_RE = re.compile(r"_BUCKET_DISCARD|_DISCARD_ORDER|BucketDiscardEnabled\s*\(\s*\)")


def _provider_classes():
    """-> {class: (base, declares_CleanupDiscardCandidates_in_its_OWN_body)}.

    Reading the class's OWN body is the point, not an implementation detail. The compiler cannot
    catch a provider that INHERITS another deck's discard buckets -- KnightsProvider derives from
    VialProvider -- and inheriting another archetype's narrowing is the recorded misroute class this
    repo keeps paying for (Goblin Matron, Stoneforge, the FiveColour fetchlands...). provider_audit.py
    closes the same loophole for Certificate(); this closes it for the discard policy.
    """
    hdr = (ROOT / "src/ai/DecisionProviders.h").read_text()
    out = {}
    for m in re.finditer(r"^class (\w+) : public (\w+)\b", hdr, re.M):
        cls, base = m.group(1), m.group(2)
        try:
            body = hdr[m.end(): hdr.index("\n};", m.end())]
        except ValueError:
            continue
        out[cls] = (base, "CleanupDiscardCandidates" in body)
    return out


def _discard_impl_body(cls):
    """The CODE of `<cls>::CleanupDiscardCandidates` -- comments stripped -- or '' if it has none.

    Brace-matched, not delimited by "the next function definition", and comment-stripped rather
    than taken raw. Both were found necessary by testing this gate rather than reasoning about it:
    a next-definition scan keyed on `^std::vector<int> \\w+::` does not match the `int ...::` and
    `static bool ...` definitions that actually follow CreatureGivingProvider's override, so the
    body ran 24,000 characters downstream into FiveColourProvider's header comment and matched ITS
    `MTG_5C_BUCKET_DISCARD` -- passing the one deck in the repo whose override is a known patch.
    Comments are stripped for the same reason at a smaller scale: a body that merely NAMES another
    deck's flag must not read as carrying one.

    Braces inside string and character literals are skipped, so a mana-symbol string ("{W}{W}")
    cannot unbalance the scan.
    """
    src = (ROOT / "src/ai/DecisionProviders.cpp").read_text()
    m = re.search(r"^std::vector<int> " + re.escape(cls) + r"::CleanupDiscardCandidates\b",
                  src, re.M)
    if not m:
        return ""
    open_at = src.find("{", m.end())
    if open_at < 0:
        return ""
    out, depth, i, n = [], 0, open_at, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "//":
            j = src.find("\n", i)
            i = n if j < 0 else j            # drop to end of line, keep the newline out
            continue
        if two == "/*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in "\"'":
            j, q = i + 1, c
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == q:
                    break
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                out.append(c)
                break
        out.append(c)
        i += 1
    return "".join(out)


def _resolve_provider(deck_path):
    """The provider the ENGINE routes this deck to, or None.

    Runs provider_audit.py over the deck's folder rather than re-deriving detection in Python --
    that script's own rationale, and the right one: `SelectDecisionProvider` keys on card params, so
    a second implementation would just be a thing to drift.
    """
    rc, out, err = run([sys.executable, str(ROOT / "scripts/provider_audit.py"),
                        str(Path(deck_path).parent)])
    stem = deck_stem(deck_path)
    for ln in out.splitlines():
        m = re.match(r"\s+(\S.*?)\s\s+(\w+)\s+cert=", ln)
        if m and m.group(1).strip() == stem:
            return m.group(2)
    return None


def gate_discard_policy(deck_path):
    """Every deck must carry an AUTHORED, user-reviewed cleanup-discard BUCKET policy.

    WHY THIS GATE EXISTS (user, 2026-09-23): "I keep finding problems only when I ask explicitly.
    That's the worst case scenario." The discard policy is a mandated analysis step -- analyze-deck
    5i, on the 2026-08-07 ruling that there is NO general discard heuristic and the 2026-08-21
    ruling that the shape is BUCKETS -- and it had no gate. So every deck onboarded after the
    doctrine was written shipped on the shared fallback silently, and the omission was only ever
    found by the user asking. That is precisely the failure mode this spine exists to prevent: the
    only thing that reaches the user should be an explicit approve-or-defer decision.

    The fallback is not neutral, which is why a missing policy is BLOCKING rather than advisory. Its
    tier B is descending mana value -- "most expensive = most expendable" -- which is backwards for
    every payoff/ramp/combo deck here: it ranked Creature Giving's Defense of the Heart FIRST to
    pitch (measured a full turn worse, gi564/gi798) and shed FiveColour's Progenitus for a measured
    1-turn cost. And `real == 0` does not excuse it: the ROLLOUT takes index 0 of this ranking with
    no search above it (Minotaur: 99 real sheds against 250,265 inside the search), so a deck that
    never sheds in play still has every searched line biased by the rule.
    """
    classes = _provider_classes()
    prov = _resolve_provider(deck_path)
    if prov is None:
        return Gate("discard_policy", SKIP, True, "could not resolve the deck's provider",
                    disclose=["discard_policy SKIPPED -- scripts/provider_audit.py did not report a "
                              f"provider for {deck_stem(deck_path)} (needs build/Release/mtg and a "
                              "sibling .profile.json). Build with ./build.sh and re-run; a deck with "
                              "no profile has never been measured at shipped play, so its routing -- "
                              "and therefore its discard policy -- is undecided."])
    cls = prov + "Provider"
    if cls not in classes:
        return Gate("discard_policy", ERROR, True,
                    f"provider {prov} reported by the engine has no class {cls} in DecisionProviders.h")

    ledger = _ledger_section(deck_path, "Discard policy")

    # Walk the chain so an INHERITED policy is reported as its own defect, never as coverage.
    own = classes[cls][1]
    inherited_from = None
    if not own:
        cur = classes[cls][0]
        while cur in classes:
            if classes[cur][1]:
                inherited_from = cur
                break
            cur = classes[cur][0]

    if not own and inherited_from:
        return Gate("discard_policy", FAIL, True,
                    f"{cls} INHERITS its discard buckets from {inherited_from}",
                    [(f"discard_policy:inherited",
                      f"{cls} declares no CleanupDiscardCandidates of its own and inherits "
                      f"{inherited_from}'s -- another deck's buckets, keyed on another deck's cards. "
                      f"Author this deck's own policy (analyze-deck 5i) and declare the override in "
                      f"{cls}'s own body.")])

    if not own:
        return Gate("discard_policy", FAIL, True,
                    f"{cls} has NO cleanup-discard policy -- running the generic max-MV fallback",
                    [(f"discard_policy:missing",
                      f"{cls} does not override CleanupDiscardCandidates, so this deck sheds by the "
                      f"shared fallback's descending-mana-value tier. Author a BUCKET policy per "
                      f"analyze-deck 5i + docs/design/discard-bucket-authoring-brief.md, present it "
                      f"to the user for confirmation, and gate it default-on behind "
                      f"MTG_<DECK>_BUCKET_DISCARD.")])

    body = _discard_impl_body(cls)
    if not body:
        return Gate("discard_policy", ERROR, True,
                    f"{cls} declares CleanupDiscardCandidates but no definition was found in "
                    f"DecisionProviders.cpp")

    authored = bool(DISCARD_FLAG_RE.search(body)) or cls in DISCARD_AUTHORED_NO_FLAG
    if not authored:
        return Gate("discard_policy", FAIL, True,
                    f"{cls}'s override is a PATCH over the generic ranking, not a bucket policy",
                    [(f"discard_policy:patch",
                      f"{cls}::CleanupDiscardCandidates carries no MTG_*_BUCKET_DISCARD gate, which "
                      f"is the convention's marker for an authored policy. Inspect it: an override "
                      f"that defers to GenericProvider and then reorders one or two named cards is a "
                      f"special case bolted onto the max-MV rule the user has called \"too "
                      f"arbitrary\", not the bucket structure 5i requires.")])

    disclose = []
    if cls in DISCARD_AUTHORED_NO_FLAG:
        disclose.append(f"discard_policy: {cls} is allowlisted as authored-without-a-flag -- "
                        f"{DISCARD_AUTHORED_NO_FLAG[cls]}. It has no =0 A/B hatch, so the policy "
                        f"cannot be measured against the generic baseline without a code edit.")
    if ledger is None:
        disclose.append("discard_policy: an authored policy is in place, but there is no "
                        f"'## Discard policy' section in {ledger_path(deck_path).relative_to(ROOT)}. "
                        "Adoption is a USER REVIEW (same gate as cast order), so record the bucket "
                        "list, the quotas and the user's confirmation there -- otherwise the review "
                        "is unevidenced and the next agent cannot tell an approved policy from an "
                        "assumed one.")
    return Gate("discard_policy", PASS, True,
                f"{cls} carries an authored bucket policy"
                + ("" if ledger is None else " (user review recorded)"), disclose=disclose)


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
        gate_play_invariants(deck, profile, seeds, args.games, args.no_sweep),
        gate_claude_sweep(deck),
        gate_discard_policy(deck),
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
