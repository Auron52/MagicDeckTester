#!/usr/bin/env python3
"""Require real exercise and zero mismatches from the24-game VERIFY batch."""
import argparse
import json
import re

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("stdout")
ap.add_argument("stderr")
a = ap.parse_args()
stdout, stderr = open(a.stdout).read(), open(a.stderr).read()
errors, counters = [], {}
if not re.search(r"BATCH done \(\d+ jobs, 24 games\)", stdout):
    errors.append("24-game batch did not finish")
for label, pattern in (
    ("junk_verified_hits", r"\[cyc-junk-memo\] verified_hits=(\d+)"),
    ("rank_cache_hits", r"\[cantrip-rank-cache\] lookups=\d+ hits=(\d+)"),
    ("kill_only_verified", r"\[leaf-kill-only\][^\n]*\bverified=(\d+)"),
    ("kill_only_armed", r"\[leaf-kill-only\][^\n]*\barmed_turns=(\d+)"),
    ("kill_only_skipped", r"\[leaf-kill-only\][^\n]*\bcandidate_rollouts_skipped=(\d+)"),
):
    m = re.search(pattern, stderr)
    counters[label] = int(m.group(1)) if m else None
    if not m or int(m.group(1)) == 0:
        errors.append(label + " has no positive exercise evidence")
if re.search(r"\bVERIFY MISMATCH\b|\[leaf-kill-only\] MISMATCH\b", stderr):
    errors.append("verification mismatch emitted")
ko = re.search(r"\[leaf-kill-only\][^\n]*\bMISMATCHES=(\d+)", stderr)
if ko is None or int(ko.group(1)) != 0:
    errors.append("kill-only zero-mismatch summary missing or nonzero")
print(json.dumps(dict(pass_=not errors, counters=counters, errors=errors,
                     note="Diagnostic exercise only; do not interpret this batch's timings or quality as an A/B."), indent=2))
raise SystemExit(1 if errors else 0)
