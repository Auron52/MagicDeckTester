#!/usr/bin/env python3
"""Fail if PlanSignatureFast and EnumeratePlans' legacy plan_signature disagree on their markers.

The fast builder (MTG_PLAN_SIG_FAST, default ON) must emit every "#XX" marker and handle every
Action::Kind the legacy builder does, or it silently folds plans the legacy key keeps apart -- a greedy
pick inside the search window. It drifted twice in two days when other branches added markers to the
legacy builder only (#MT/#PX/#FE on 2026-10-17, #RM on 2026-10-19). Run after any rebase that touches
src/ai/TurnSolver.cpp:

    python3 test/check_plan_sig_lockstep.py        # exit 1 on any difference
"""
import re
import sys
from pathlib import Path

src = (Path(__file__).resolve().parent.parent / "src/ai/TurnSolver.cpp").read_text(encoding="utf-8")


def body(start_pat, end_pat):
    i = src.index(start_pat)
    j = src.index(end_pat, i)
    return src[i:j]


fast = body("static std::string PlanSignatureFast", "static std::vector<TurnSolver::Plan> EnumeratePlans(")
legacy = body("auto plan_signature_legacy", "const bool sig_fast")


def strip_comments(t):
    return "\n".join(l.split("//", 1)[0] for l in t.split("\n"))


def markers(t):
    return set(re.findall(r'"(#[A-Z][A-Z0-9]*)', strip_comments(t)))


def kinds(t):
    return set(re.findall(r"case Action::Kind::(\w+)", t))


bad = False
for name, a, b in (("markers", markers(fast), markers(legacy)), ("Action::Kind cases", kinds(fast), kinds(legacy))):
    if a != b:
        bad = True
        print(f"{name}: legacy-only {sorted(b - a)}  fast-only {sorted(a - b)}")
if bad:
    sys.exit(1)
print(f"plan-signature lock-step OK: {len(markers(fast))} markers, {len(kinds(fast))} action kinds")
