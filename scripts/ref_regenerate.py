#!/usr/bin/env python3
"""Regenerate REPAIRED references as fresh engine traces, so they are literal again.

The intent-replay check (test/viewer_protocol_check.py) classifies a reference as `repaired`
when its recorded line reproduces the recorded outcome but only via on-the-fly repairs: stale
indices re-anchored by plan content, and decision points the reference predates answered with
the engine's defaults. Those repairs are deterministic, so the reference can be re-written
as a fresh trace of the SAME game against the current engine -- the same mechanism as the
07-22 antilife regeneration (antilife-reference-shuffle-alignment.md): replay the resolved
choice stream with --log-dir and let the engine write a schema-faithful reference file.

Safety: a candidate NEVER overwrites the original unless it passes ALL of:
  1. seed / game_index / won / win_turn equal the ORIGINAL recording,
  2. the semantic line (turn, land, cast multiset of every chosen main-phase plan) is
     identical to the original's,
  3. the candidate round-trips through check_reference as plain `ok` (a literal reference).
Anything else is reported and left untouched. `ok` references are never rewritten (no churn).

Usage:  python3 scripts/ref_regenerate.py            # dry-run: verify all repaired refs, report
        python3 scripts/ref_regenerate.py --apply    # overwrite the verified ones
        python3 scripts/ref_regenerate.py refs...    # restrict to specific reference paths
"""
import glob, importlib.util, json, os, shutil, subprocess, sys, tempfile

_spec = importlib.util.spec_from_file_location(
    "vpc", os.path.join(os.path.dirname(__file__), "..", "test", "viewer_protocol_check.py"))
vpc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vpc)


def semantic_line(ref):
    """(turn, land, sorted casts) of every chosen main-phase plan -- the human's line."""
    out = []
    for d in ref.get("decisions", []):
        dec = d.get("decision", {})
        if dec.get("type") != "main_phase":
            continue
        c = d.get("chosen")
        plans = dec.get("plans") or []
        if isinstance(c, int) and 0 <= c < len(plans):
            p = plans[c]
            out.append((dec.get("turn"), p.get("land"), tuple(sorted(p.get("casts") or []))))
    return out


def regenerate(path, tmproot):
    """Returns (verdict, detail, candidate_path|None). Verdicts:
    SKIP-OK / REGEN-VERIFIED / REJECT-<reason> / SKIP-<kind>."""
    orig = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))

    collect = {}
    c_ok, kind, detail = vpc.check_reference(path, collect=collect)
    if not c_ok:
        return "REJECT-CONTRACT", detail, None
    if kind == "ok":
        return "SKIP-OK", "already literal", None
    if kind != "repaired":
        return f"SKIP-{kind.upper()}", detail, None

    # Re-run the exact resolved stream once more, with --log-dir, to write the fresh trace.
    outdir = os.path.join(tmproot, "out", deck_dir)
    os.makedirs(outdir, exist_ok=True)
    args = [vpc.MTG, collect["deck"], "--claude-play",
            "--seed", str(collect["seed"]), "--game-index", str(collect["gi"]),
            "--max-turns", str(collect["mt"]), "--depth", "0", "--profile", collect["prof"],
            "--choices", ",".join(str(c) for c in collect["resolved"]),
            "--log-dir", outdir]
    if collect["force"] is not None:
        args += ["--force-mulligan", collect["force"]]
    args += collect["side"]
    p = subprocess.run(args, capture_output=True, text=True)
    cand_path = os.path.join(outdir, os.path.basename(path))
    if p.returncode != 0 or not os.path.isfile(cand_path):
        return "REJECT-NO-TRACE", f"rc={p.returncode}, no {cand_path}", None
    cand = json.load(open(cand_path))

    # Gate 1: identity + outcome vs the ORIGINAL recording.
    for k in ("seed", "game_index", "won", "win_turn"):
        if cand.get(k) != orig.get(k):
            return "REJECT-OUTCOME", f"{k}: orig={orig.get(k)} cand={cand.get(k)}", None
    if orig.get("mulligan") is not None and cand.get("mulligan") != orig.get("mulligan"):
        # The raw bottom lists may differ while the GAME is identical: --force-mulligan
        # substitutes an equivalent copy when a listed card number is not literally bottomable
        # (burn s11_gi10: "bottom 2" with card 2 in the kept hand -> engine bottoms copy 57).
        # Equivalent iff both force specs reconstruct the SAME opening hand.
        def forced_hand(mul):
            spec = f"{mul.get('count', 0)}:" + ",".join(str(n) for n in mul.get("bottom", []))
            rc, out = vpc.replay(collect["deck"], collect["prof"], collect["seed"],
                                 collect["gi"], [], spec, collect["side"], collect["mt"])
            m = vpc.DEC_RE.search(out)
            if rc != 70 or not m:
                return None
            h = json.loads(m.group(1)).get("me", {}).get("hand", [])
            return sorted((c.get("num"), c.get("name")) for c in h)
        ho, hc = forced_hand(orig["mulligan"]), forced_hand(cand["mulligan"])
        if ho is None or ho != hc:
            return "REJECT-MULLIGAN", f"orig={orig.get('mulligan')} cand={cand.get('mulligan')}", None

    # Gate 2: identical semantic line.
    ol, cl = semantic_line(orig), semantic_line(cand)
    if ol != cl:
        return "REJECT-LINE", f"orig {ol} != cand {cl}", None

    # Gate 3: the candidate must round-trip as a LITERAL reference (plain ok). Place it under a
    # <deck>/ dir so check_reference resolves the deck from the path as usual.
    rt_dir = os.path.join(tmproot, "roundtrip", deck_dir)
    os.makedirs(rt_dir, exist_ok=True)
    rt_path = os.path.join(rt_dir, os.path.basename(path))
    shutil.copyfile(cand_path, rt_path)
    r_ok, r_kind, r_detail = vpc.check_reference(rt_path)
    if not r_ok or r_kind != "ok":
        return "REJECT-ROUNDTRIP", f"{r_kind}: {r_detail}", None
    return "REGEN-VERIFIED", f"line intact, outcome {cand.get('win_turn')}, round-trips ok", cand_path


def main():
    apply_mode = "--apply" in sys.argv[1:]
    targets = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not targets:
        targets = sorted(p for p in glob.glob("references/*/claude_s*_gi*.json")
                         if not p.startswith(("references/suboptimal/", "references/optimal/")))
    tmproot = tempfile.mkdtemp(prefix="refregen_")
    verified, rejected, skipped = [], [], 0
    for path in targets:
        verdict, detail, cand = regenerate(path, tmproot)
        rel = path[len("references/"):] if path.startswith("references/") else path
        if verdict == "REGEN-VERIFIED":
            verified.append((path, cand))
            print(f"  {verdict:<16} {rel}: {detail}")
        elif verdict.startswith("REJECT"):
            rejected.append((path, verdict, detail))
            print(f"  {verdict:<16} {rel}: {detail}")
        else:
            skipped += 1
    print(f"\n{len(verified)} verified, {len(rejected)} rejected, {skipped} skipped "
          f"(candidates under {tmproot})")
    if rejected:
        print("REJECTED refs were left untouched -- review before any manual action.")
    if apply_mode:
        for path, cand in verified:
            shutil.copyfile(cand, path)
        print(f"APPLIED: {len(verified)} reference(s) overwritten with verified regenerated traces.")
    elif verified:
        print("Dry-run: re-run with --apply to overwrite the verified ones.")
    return 1 if rejected else 0


if __name__ == "__main__":
    sys.exit(main())
