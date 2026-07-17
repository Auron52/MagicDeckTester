#!/usr/bin/env python3
"""AI-driven heuristic optimization loop (workstream 5): measure -> decide -> adopt -> report.

Runs a registered "heuristic experiment" -- a runtime selector (an MTG_* env var) with a
baseline value and one or more AI-authored variant values -- through the regression harness,
scores each variant AGAINST THE COMMITTED BASELINE, applies a strict noise-aware bar, and
emits a verdict. Adopt-then-review (the user vetoes AFTER, not before): a variant that passes
the bar on train AND held-out seeds is AUTONOMOUSLY ADOPTED -- its winning value is written into
the committed defaults file (src/ai/data/heuristic_defaults.env) the engine reads at startup, so
it becomes the LIVE default with no rebuild, and it is logged to a review ledger
(docs/design/heuristic-adoptions.md). Reversible: set the env var to baseline (an explicit env
var overrides the file), or delete the line. Nothing is gated on pre-approval. After an adoption
the loop prints a reminder to REBASELINE ground truth (the new default changes play).

Why one run per variant is enough: the committed gt_logs / test/results/<mode>.env ARE the
baseline, so `test/regression.sh <mode>` prints `exp=<baseline_avg>[/<baseline_digest>]` vs
`got=<variant_avg>[/<variant_digest>]` per case AND a `[searched]/[d0] win->loss=...` audit of
the run against gt_logs. Setting the experiment's env var makes that a variant-vs-baseline A/B.

Metric -- THE ONLY ONE: average win turn, with a loss counted as max_turns+1 (= 9 for the default
max_turns=8); lower is better. This is exactly the `avg` in the harness fingerprint (ComputeAvgTurns,
unwon=9). win%/win-count and "win->loss" game flips are NOISE and are deliberately NOT used: a game
that flips win->loss already shows up as its turn going from <=8 to 9, i.e. a worse average -- the
avg-9 metric subsumes it. Judge on the average alone.

Strict bar to ADOPT a variant (purely avg-9):
  * total avg improves beyond the noise floor (sum of per-case avg deltas <= -NOISE_EPS),
  * NO per-deck avg regression beyond the noise floor (a variant that worsens any deck's average
    isn't a clean global win -- that's archetype-specific, not a default),
  * the winner then VALIDATES on the held-out mode (disjoint seeds) by the same criteria.
A variant that is byte-identical to baseline (all digests match) is "no behavioral change" ->
keep the default (nothing to adopt). Anything failing the bar -> keep baseline, reported.

Diagnostics: SLOWER cases (positive delta) are the primary regression/issue cases. FASTER cases
(negative delta) are the win signal and are surfaced for a quick sanity check -- a faster win is
usually good, but rule out one thing first:
  * an ENGINE BUG (an illegal play / miscounted damage / a mis-modeled rule that happens to close
    sooner) -- especially likely EARLY, before the deck's cards + engine are fully validated. That
    is a correctness bug to fix, not a heuristic to keep.
  * otherwise it's a legitimate improvement -- adopt.
NOTE on clairvoyance: the search is CLAIRVOYANT BY DEFAULT (it sees the seeded future within its
depth), so a variant winning by "seeing the future" is NOT an artifact to hunt here -- both arms of
the A/B are equally clairvoyant at the same depth, so a delta is a real ordering difference, and
optimizing the clairvoyant goldfish is the actual goal. (The skill's clairvoyance caution is about
MTG_UNPRUNE gate-opening, a different sweep.) The held-out validation still guards against overfitting
the train SEEDS -- that is seed noise, not clairvoyance.

Usage:
  python scripts/auto_heuristics.py --list
  python scripts/auto_heuristics.py <experiment> [--mode smoke] [--holdout overnight] [--json]
  python scripts/auto_heuristics.py <experiment> --parse-file <captured_output>   # offline scoring
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADOPTION_LEDGER = ROOT / "docs/design/heuristic-adoptions.md"
# The committed defaults file the engine reads at startup (src/core/HeuristicDefaults.h). Writing a
# KEY=VALUE line here makes an adopted variant the LIVE default without a rebuild; an explicit env var
# still overrides it (= the disable / A-B lever). Empty => stock behavior (byte-identical).
DEFAULTS_FILE = ROOT / "src/ai/data/heuristic_defaults.env"

# avg-turn deltas smaller than this (summed / per-deck) are inside run-to-run noise, not a result.
NOISE_EPS = 0.02

# --- experiment registry: AI-authored variants behind an existing runtime selector -----------
# Each variant value is the string assigned to the env var ("" / None => unset => baseline).
EXPERIMENTS = {
    # Proof-of-concept on a committed, tuned toggle: scarcity-first mana tap order (default) vs
    # the legacy order. Expected: baseline (scarcity-first) confirmed >= legacy -- validates that
    # the loop correctly KEEPS a well-tuned default rather than adopting a regression.
    "tap_order": {
        "env": "MTG_TAP_LEGACY",
        "baseline": "",           # unset = scarcity-first (the adopted default)
        "variants": {"legacy": "1"},
        "hypothesis": {"legacy": "does the pre-scarcity tap order still lose, confirming scarcity?"},
    },
}

CASE_RE = re.compile(
    r"^\s*(PASS|FAIL|NEW)\s+(\S+)\s+exp=([0-9.]+|\S*?)(?:/(\w+))?\s+got=([0-9.]+|\(no output\)|\S*?)(?:/(\w+))?\s*$"
)
AUDIT_RE = re.compile(
    r"\[(searched|d0)\s*\]\s*win->loss=(\d+)\s+loss->win=(\d+)\s+later=(\d+)\s+earlier=(\d+)\s+play-changed=(\d+)"
)


def deck_of(case_key):
    """burn_smoke_d0_s1001 -> 'burn'; anti-lifegain style keeps the leading token."""
    return case_key.split("_")[0]


def parse_harness(text):
    """Parse harness stdout into {cases: {key: {...}}, audit: {searched:{}, d0:{}}}."""
    cases, audit = {}, {}
    for ln in text.splitlines():
        m = CASE_RE.match(ln)
        if m:
            status, key, exp_avg, exp_dig, got_avg, got_dig = m.groups()
            def num(x):
                try:
                    return float(x)
                except (TypeError, ValueError):
                    return None
            cases[key] = {"status": status, "deck": deck_of(key),
                          "exp_avg": num(exp_avg), "got_avg": num(got_avg),
                          "exp_dig": exp_dig, "got_dig": got_dig,
                          "digest_changed": bool(exp_dig and got_dig and exp_dig != got_dig)}
            continue
        a = AUDIT_RE.search(ln)
        if a:
            grp, wl, lw, later, earlier, pc = a.groups()
            audit[grp] = {"win_loss": int(wl), "loss_win": int(lw), "later": int(later),
                          "earlier": int(earlier), "play_changed": int(pc)}
    return {"cases": cases, "audit": audit}


def score(parsed):
    """Score a variant run vs the committed baseline (exp). Metric = avg win turn (loss=9).
    A POSITIVE per-case delta is a SLOWER game -- those are the issue cases to inspect."""
    cases = parsed["cases"]
    per_deck = {}
    total_delta, missing, slower, faster = 0.0, [], [], []
    for key, c in cases.items():
        if c["got_avg"] is None or c["exp_avg"] is None:
            missing.append(key)
            continue
        d = c["got_avg"] - c["exp_avg"]              # + = SLOWER (worse), - = faster (better)
        total_delta += d
        if d > 1e-9:
            slower.append((key, round(d, 3)))        # slower games = the issue (regression) cases
        elif d < -1e-9:
            faster.append((key, round(d, 3)))        # faster games = inspect for clairvoyance/robustness
        pd = per_deck.setdefault(c["deck"], {"delta": 0.0, "cases": 0, "digest_changed": 0})
        pd["delta"] += d
        pd["cases"] += 1
        pd["digest_changed"] += int(c["digest_changed"])
    play_changed = sum(parsed["audit"].get(g, {}).get("play_changed", 0) for g in ("searched", "d0"))
    digests_all_match = all(not c["digest_changed"] for c in cases.values()) and bool(cases)
    slower.sort(key=lambda x: -x[1])
    faster.sort(key=lambda x: x[1])
    return {"total_delta": round(total_delta, 4), "per_deck": per_deck,
            "slower": slower, "faster": faster, "play_changed": play_changed,
            "digests_all_match": digests_all_match, "missing": missing}


def verdict(sc):
    """Apply the strict bar on avg win turn (loss=9) ONLY. Returns (decision, reasons[]); decision in
    {byte_identical, reject_regression, reject_noise, adopt_candidate, reject_incomplete}."""
    reasons = []
    if sc["missing"]:
        return "reject_incomplete", [f"{len(sc['missing'])} case(s) produced no comparable avg "
                                     f"(e.g. '(no output)'): {', '.join(sc['missing'][:5])}"]
    if sc["digests_all_match"]:
        return "byte_identical", ["all case digests match baseline -> no behavioral change; keep default"]
    worst = max((v["delta"] for v in sc["per_deck"].values()), default=0.0)
    worst_deck = max(sc["per_deck"], key=lambda d: sc["per_deck"][d]["delta"], default=None)
    slow = ("; slower cases: " + ", ".join(f"{k} +{d}" for k, d in sc["slower"][:4])) if sc["slower"] else ""
    if worst > NOISE_EPS:
        return "reject_regression", [f"per-deck regression: {worst_deck} avg +{worst:.3f} SLOWER "
                                     f"> noise {NOISE_EPS}{slow}"]
    if sc["total_delta"] <= -NOISE_EPS:
        return "adopt_candidate", [f"total avg {sc['total_delta']:+.3f} FASTER beyond noise "
                                   f"{NOISE_EPS}, no per-deck regression"]
    return "reject_noise", [f"total avg {sc['total_delta']:+.3f} within noise {NOISE_EPS} "
                            f"(play-changed {sc['play_changed']}) -- not a result{slow}"]


def run_variant(env_var, env_val, mode):
    """Run test/regression.sh <mode> with the selector set; return (raw, parsed).

    A non-`--accept` run overwrites the scratch test/results/<mode>.env (what `--accept`
    promotes). We SAVE + RESTORE it so a variant sweep never leaves variant fingerprints
    as the acceptable "last run" (which a later `--accept` would wrongly promote as GT).
    The committed GT (test/regression_gt.txt) and gt_logs are only touched by --accept, so
    the exp=/audit baseline the variant is scored against stays intact."""
    import os, shutil
    e = dict(os.environ)
    if env_val:
        e[env_var] = env_val
    else:
        e.pop(env_var, None)
    scratch = ROOT / "test/results" / f"{mode}.env"
    backup = scratch.with_suffix(".env.autoh_bak") if scratch.exists() else None
    if backup:
        shutil.copy2(scratch, backup)
    try:
        p = subprocess.run(["bash", str(ROOT / "test/regression.sh"), f"--{mode}"],
                           capture_output=True, text=True, env=e, timeout=8 * 3600)
        return p.stdout + "\n" + p.stderr, parse_harness(p.stdout + "\n" + p.stderr)
    finally:
        if backup:
            shutil.move(str(backup), str(scratch))   # restore baseline scratch


def evaluate(exp, mode, holdout, run=True, parse_text=None):
    """Full loop for one experiment: sweep variants on `mode`, holdout-validate an adopt candidate."""
    env_var = exp["env"]
    results = {}
    for label, val in exp["variants"].items():
        if parse_text is not None:
            parsed = parse_harness(parse_text)
        else:
            _, parsed = run_variant(env_var, val, mode)
        sc = score(parsed)
        dec, why = verdict(sc)
        entry = {"label": label, "env_val": val, "train": {"score": sc, "decision": dec, "why": why}}
        # Held-out validation only for an adopt candidate (expensive; disjoint seeds).
        if dec == "adopt_candidate" and holdout and parse_text is None:
            _, hparsed = run_variant(env_var, val, holdout)
            hsc = score(hparsed)
            hdec, hwhy = verdict(hsc)
            entry["holdout"] = {"score": hsc, "decision": hdec, "why": hwhy}
            entry["adopted"] = (hdec == "adopt_candidate")
        else:
            entry["adopted"] = False
        results[label] = entry
    return results


def render_report(exp_name, exp, results):
    out = [f"# auto_heuristics: {exp_name}  (selector `{exp['env']}`)", ""]
    adopted = []
    for label, r in results.items():
        t = r["train"]["score"]
        out.append(f"## variant `{label}` (`{exp['env']}={r['env_val'] or '<unset>'}`)")
        out.append(f"- hypothesis: {exp.get('hypothesis', {}).get(label, '(none)')}")
        out.append(f"- train: decision **{r['train']['decision']}** — {'; '.join(r['train']['why'])}")
        out.append(f"  - total avg delta {t['total_delta']:+.3f} (− faster/better, + slower/worse), "
                   f"play-changed {t['play_changed']}")
        pd = ", ".join(f"{d} {v['delta']:+.3f}" for d, v in sorted(t["per_deck"].items()))
        out.append(f"  - per-deck avg delta: {pd}")
        if t["slower"]:
            out.append(f"  - slower cases (regressions): "
                       + ", ".join(f"{k} +{d}" for k, d in t["slower"][:6]))
        if t["faster"]:
            out.append(f"  - faster cases (CHECK FOR A BUG, esp. early — else a legitimate win): "
                       + ", ".join(f"{k} {d}" for k, d in t["faster"][:6]))
        if "holdout" in r:
            h = r["holdout"]
            out.append(f"- holdout: decision **{h['decision']}** — {'; '.join(h['why'])}")
        out.append(f"- **{'ADOPTED' if r['adopted'] else 'not adopted'}**")
        out.append("")
        if r["adopted"]:
            adopted.append(label)
    return "\n".join(out), adopted


def activate_adoption(env_var, env_val):
    """Make an adopted variant the LIVE default: write its KEY=VALUE line into the committed defaults
    file the engine reads at startup. Idempotent (replaces any prior line for this key). Reversible:
    remove the line, or set the env var to baseline (an explicit env var overrides the file). A
    baseline value ("") just removes the line (deactivate). Returns True if the file changed."""
    lines = DEFAULTS_FILE.read_text().splitlines() if DEFAULTS_FILE.exists() else []
    kept = [ln for ln in lines if not re.match(rf"\s*{re.escape(env_var)}\s*=", ln)]
    changed = (len(kept) != len(lines))
    if env_val:                                  # non-baseline -> set/replace; baseline -> just remove
        kept.append(f"{env_var}={env_val}")
        changed = True
    DEFAULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    DEFAULTS_FILE.write_text("\n".join(kept).rstrip("\n") + "\n")
    return changed


def record_adoption(exp_name, exp, label, r):
    """Append an adoption record to the review ledger (adopt-then-veto: the user reviews AFTER).
    Recording != activating: making the winner the live default (behind its toggle) is a separate
    step; this makes the autonomous decision auditable and easy to reverse (set the env var back)."""
    import datetime
    t = r["train"]["score"]
    ADOPTION_LEDGER.parent.mkdir(parents=True, exist_ok=True)
    if not ADOPTION_LEDGER.exists():
        ADOPTION_LEDGER.write_text(
            "# Heuristic adoptions (auto_heuristics.py)\n\n"
            "Autonomous adoptions from the measure->decide->report loop, for USER REVIEW (veto, not "
            "pre-approval). Each is behind its runtime toggle and reversible by setting the env var. "
            "An entry records the measured evidence; activating it as the live default is noted per row.\n\n")
    today = datetime.date.today().isoformat()
    with ADOPTION_LEDGER.open("a") as f:
        f.write(f"## {today} — {exp_name}: adopt `{label}` (`{exp['env']}={r['env_val'] or '<unset>'}`)\n")
        f.write(f"- hypothesis: {exp.get('hypothesis', {}).get(label, '(none)')}\n")
        f.write(f"- train: total avg {t['total_delta']:+.3f} (− faster/better), "
                f"play-changed {t['play_changed']}\n")
        if t["faster"]:
            f.write(f"- faster cases (CHECK FOR A BUG, esp. early — else a legitimate win): "
                    + ", ".join(f"{k} {d}" for k, d in t["faster"][:6]) + "\n")
        if "holdout" in r:
            f.write(f"- holdout: total avg {r['holdout']['score']['total_delta']:+.3f} — "
                    f"{r['holdout']['decision']}\n")
        f.write(f"- disable: `{exp['env']}={exp['baseline'] or '<unset>'}` restores the baseline\n\n")


def main():
    ap = argparse.ArgumentParser(description="AI-driven heuristic optimization loop (workstream 5).")
    ap.add_argument("experiment", nargs="?", help="experiment name (see --list)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--mode", default="smoke", help="train mode (disjoint seeds): smoke|regression|overnight")
    ap.add_argument("--holdout", default="overnight", help="held-out validation mode (disjoint seeds)")
    ap.add_argument("--parse-file", default=None, help="score an already-captured harness output (offline; no run)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if args.list or not args.experiment:
        print("Registered experiments:")
        for k, v in EXPERIMENTS.items():
            print(f"  {k:14s} selector={v['env']}  variants={list(v['variants'])}")
        return 0
    if args.experiment not in EXPERIMENTS:
        print(f"unknown experiment {args.experiment!r}; see --list", file=sys.stderr)
        return 2
    exp = EXPERIMENTS[args.experiment]
    parse_text = Path(args.parse_file).read_text() if args.parse_file else None
    results = evaluate(exp, args.mode, args.holdout, parse_text=parse_text)
    report, adopted = render_report(args.experiment, exp, results)
    for label in adopted:                       # autonomous adoption: activate (live default) + record
        if parse_text is None:                  # offline scoring never mutates the live defaults
            activate_adoption(exp["env"], results[label]["env_val"])
        record_adoption(args.experiment, exp, label, results[label])
    if adopted and not args.json:
        print(f"\n>> ADOPTED {adopted}: wrote the winning default into "
              f"{DEFAULTS_FILE.relative_to(ROOT)} (active now; disable by setting "
              f"`{exp['env']}={exp['baseline'] or '<unset>'}`). Recorded in "
              f"{ADOPTION_LEDGER.relative_to(ROOT)} for review. NOW REBASELINE GT: inspect, then "
              f"`bash test/regression.sh --{args.mode} --accept` so ground truth reflects the new default.")

    if args.json:
        print(json.dumps({"experiment": args.experiment, "adopted": adopted,
                          "results": {k: {"env_val": v["env_val"], "adopted": v["adopted"],
                                          "train": v["train"]} for k, v in results.items()}},
                         indent=2, default=str))
    else:
        print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
