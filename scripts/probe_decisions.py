#!/usr/bin/env python3
"""Auto-drive --claude-play games and report which decision `type`s surface.

A lightweight 5h surfacing probe: for each game it plays the stateless-replay
protocol to completion, picking a *developing* main_phase plan (prefer one that
retraces / casts the most) and the heuristic default for every sub-decision, then
records the set of decision types seen. Use it to confirm a new deck's cards emit
their interactive decisions (retrace_discard, soulfire_targets, target, bounce, ...).

Usage: probe_decisions.py <deck> <profile> <base_seed> <n_games> [max_turns]
"""
import subprocess, sys, json, re, collections

BIN = "./build/Release/mtg"
deck, prof, base_seed, n_games = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
max_turns = int(sys.argv[5]) if len(sys.argv) > 5 else 10

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")

def step(seed, gi, choices):
    cmd = [BIN, deck, "--profile", prof, "--claude-play", "--seed", str(seed),
           "--game-index", str(gi), "--max-turns", str(max_turns),
           "--choices", ",".join(map(str, choices))]
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = p.stdout
    if RES_RE.search(out):
        return None
    m = DEC_RE.search(out)
    return json.loads(m.group(1)) if m else None

def pick(d):
    t = d.get("type")
    # mulligan/bottom are driven by ai_choice, not heuristic_default; defaulting to 0 on a
    # mulligan means "mulligan again" -> an infinite loop that never reaches a turn.
    if t == "mulligan":
        ac = d.get("ai_choice")
        return ac if isinstance(ac, int) else 1
    if t == "bottom":
        ac = d.get("ai_choice")
        if isinstance(ac, dict):
            return ac.get("index", 0)
        return ac if isinstance(ac, int) else 0
    if t == "main_phase":
        plans = d.get("plans", [])
        if not plans:
            return -1
        # prefer a plan that retraces (to exercise retrace_discard), else the most
        # developed line (longest summary ~ most actions), else pass occasionally.
        rt = [p for p in plans if "retrace" in (p.get("summary", "").lower())]
        if rt:
            return rt[0]["index"]
        best = max(plans, key=lambda p: len(p.get("summary", "")))
        return best["index"]
    # sub-decisions: take the heuristic default (or first option / -1 for dig)
    if "heuristic_default" in d:
        return d["heuristic_default"]
    return 0

seen = collections.Counter()
for gi in range(n_games):
    seed = base_seed
    choices, guard = [], 0
    while guard < 120:
        guard += 1
        d = step(seed, gi, choices)
        if d is None:
            break
        seen[d.get("type", "?")] += 1
        choices.append(pick(d))
print("decision types seen across %d games:" % n_games)
for t, c in seen.most_common():
    print("  %-20s %d" % (t, c))
