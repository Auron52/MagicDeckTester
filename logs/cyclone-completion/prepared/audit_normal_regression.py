#!/usr/bin/env python3
"""Read-only normal-tier audit. Never runs an engine or changes ground truth.

Read the baseline from an explicit immutable git commit, not HEAD~1. Require a
completed harness summary and every manifest job's exact per-game index set.
Write a new exclusive evidence directory; retain both raw sides for every change.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(w, *args):
    return subprocess.check_output(
        ["git", "-C", str(w), *args], env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})


def wins(raw):
    rows = {}
    for line in raw.decode().splitlines():
        p = line.split()
        if not p:
            continue
        if len(p) != 3 or not re.fullmatch(r"[0-9a-f]{1,16}", p[2]):
            raise ValueError(f"malformed wins row: {line!r}")
        gi, turn = map(int, p[:2])
        if gi in rows:
            raise ValueError(f"duplicate GI {gi}")
        rows[gi] = (turn, p[2])
    return rows


def fingerprint(rows, horizon):
    h = 1469598103934665603
    for gi in sorted(rows):
        d = int(rows[gi][1], 16)
        for b in range(8):
            h = ((h ^ ((d >> (b * 8)) & 255)) * 1099511628211) & ((1 << 64) - 1)
    avg = sum(t if t > 0 else horizon + 1 for t, _ in rows.values()) / len(rows)
    return avg, f"{h:016x}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--worktree", required=True, type=Path)
    ap.add_argument("--mode", required=True, choices=["smoke", "regression"])
    ap.add_argument("--baseline-ref", required=True)
    ap.add_argument("--expected-source", required=True)
    ap.add_argument("--output", required=True, type=Path)
    a = ap.parse_args()
    w = a.worktree.resolve()
    source = git(w, "rev-parse", "HEAD:src").decode().strip()
    if source != a.expected_source or git(w, "status", "--porcelain", "--untracked-files=no", "--", "src").strip():
        raise ValueError("source pin mismatch or dirty src")
    baseline = git(w, "rev-parse", a.baseline_ref + "^{commit}").decode().strip()
    run = w / "test/logs" / a.mode
    manifest_raw = (run / "manifest.json").read_bytes()
    jobs = json.loads(manifest_raw)["jobs"]
    names = [j["name"] for j in jobs]
    if len(names) != len(set(names)):
        raise ValueError("duplicate manifest job names")
    summary = (w / f"test/regression_result_{a.mode}.txt").read_text()
    if not re.search(r"^Result: \d+ passed, \d+ failed, \d+ new", summary, re.M):
        raise ValueError("tier has no completed harness summary")
    baseline_gt = git(w, "show", baseline + ":test/regression_gt.txt")
    fingerprints = {}
    for line in baseline_gt.decode().splitlines():
        m = re.fullmatch(r"([A-Za-z0-9_]+)=([0-9.]+)/([0-9a-f]+)", line)
        if m:
            fingerprints[m[1]] = (float(m[2]), m[3])
    batch_raw = (run / "batch.log").read_bytes()
    batch_results = {}
    for line in batch_raw.decode().splitlines():
        m = re.match(r"^([A-Za-z0-9_]+): played=(\d+) avg=([0-9.]+) digest=([0-9a-f]+)(?: |$)", line)
        if m and m[1] in names:
            if m[1] in batch_results:
                raise ValueError("duplicate batch summary: " + m[1])
            batch_results[m[1]] = (int(m[2]), float(m[3]), m[4])
    if set(batch_results) != set(names):
        raise ValueError("missing/ambiguous batch job summaries")

    changed, cases, evidence, recovery = [], [], {}, []
    tallies = {"searched": collections.Counter(), "d0": collections.Counter()}
    for job in jobs:
        name = job["name"]
        if not re.fullmatch(r"[A-Za-z0-9_]+", name):
            raise ValueError("unsafe job name")
        old_raw = git(w, "show", baseline + f":test/gt_logs/{name}.wins")
        new_raw = (run / "wins" / (name + ".wins")).read_bytes()
        old, new = wins(old_raw), wins(new_raw)
        start = job.get("game_index", 0)
        expected = set(range(start, start + job["games"]))
        if set(new) != expected or set(old) != expected:
            raise ValueError("incomplete/mismatched GI coverage: " + name)
        horizon = job.get("max_turns", 8)
        old_fp, new_fp = fingerprint(old, horizon), fingerprint(new, horizon)
        accepted = fingerprints[name]
        observed = batch_results[name]
        if old_fp[1] != accepted[1] or abs(old_fp[0] - accepted[0]) > 5.01e-5:
            raise ValueError("baseline aggregate/per-game GT inconsistent: " + name)
        if observed[0] != job["games"] or new_fp[1] != observed[2] or abs(new_fp[0] - observed[1]) > 5.01e-5:
            raise ValueError("run aggregate/per-game result inconsistent: " + name)
        band = "d0" if "_d0_" in name else "searched"
        nchanged = 0
        for gi in sorted(expected):
            if old[gi] == new[gi]:
                tallies[band]["unchanged"] += 1
                continue
            nchanged += 1
            o, od = old[gi]
            n, nd = new[gi]
            delta = (n if n > 0 else horizon + 1) - (o if o > 0 else horizon + 1)
            kind = "slower" if delta > 0 else "faster" if delta < 0 else "same_score_play_change"
            tallies[band][kind] += 1
            row = dict(key=name, gi=gi, actual_seed=job["seed"] + gi - start,
                       band=band, kind=kind, old_turn=o, new_turn=n, old_digest=od,
                       new_digest=nd, score_delta=delta, original_job=job)
            changed.append(row)
            if band == "searched" and kind == "slower":
                r = {k: v for k, v in job.items() if k not in ("weight", "abandon_units")}
                r.update(name=f"d8_{name}_gi{gi}", games=1, seed=row["actual_seed"],
                         game_index=gi, depth=8, budget_ms=0, ignore_play_profile=True)
                recovery.append(r)
        cases.append(dict(key=name, games=len(new), old_avg=old_fp[0], new_avg=new_fp[0],
                          changed=nchanged, old_sha256=sha(old_raw), new_sha256=sha(new_raw)))
        if nchanged:
            evidence[f"baseline/{name}.wins"] = old_raw
            evidence[f"observed/{name}.wins"] = new_raw
    err_raw = (run / "batch.err").read_bytes()
    attention = [dict(line=i, text=l) for i, l in enumerate(err_raw.decode(errors="replace").splitlines(), 1)
                 if re.search(r"FORECAST.DISAGREEMENT|\[FD\]|NONCONV|\[counterlist\] OVERFLOW", l, re.I)]
    report = dict(status="COMPLETE_MECHANICAL_AUDIT_REQUIRES_CAUSAL_REVIEW_NOT_ADOPTION",
                  source_tree=source, baseline_commit=baseline, mode=a.mode,
                  jobs=len(jobs), games=sum(j["games"] for j in jobs), tallies=tallies,
                  changed=changed, cases=cases, diagnostic_attention=attention,
                  raw_sha256={"manifest.json":sha(manifest_raw), "batch.log":sha(batch_raw),
                              "batch.err":sha(err_raw), "mtg.run":sha((run / "mtg.run").read_bytes())},
                  scope="No engine runs. Raw digests remain exact; every searched slower game needs review.")
    a.output.mkdir(parents=True, exist_ok=False)
    evidence.update({"baseline-regression_gt.txt": baseline_gt, "manifest.json":manifest_raw,
                     "batch.log":batch_raw, "batch.err":err_raw,
                     "harness-summary.txt":summary.encode()})
    for rel, raw in evidence.items():
        p = a.output / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(raw)
    (a.output / "audit.json").write_text(json.dumps(report, indent=2) + "\n")
    (a.output / "searched-slower.json").write_text(json.dumps(
        [r for r in changed if r["band"] == "searched" and r["kind"] == "slower"], indent=2) + "\n")
    (a.output / "d8-recovery.UNSELECTED.json").write_text(json.dumps({"jobs":recovery}, indent=2) + "\n")
    print(json.dumps({"output":str(a.output), "jobs":len(jobs), "tallies":tallies,
                      "diagnostic_attention":len(attention), "d8_jobs_UNRUN_UNSELECTED":len(recovery)}))


if __name__ == "__main__":
    main()
