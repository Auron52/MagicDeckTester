#!/usr/bin/env python3
"""A/B the replicate PLAN DIMENSION against the old greedy-sink-plus-dialog behaviour.

Arm A (MTG_REPLICATE_DIM=1, the new default): the count is a plan variant, priced into the cast.
Arm B (MTG_REPLICATE_DIM=0, the revert hatch): the old path -- cast and replicate priced
separately, count decided greedily at resolution with a dialog offering 0..max.

Both arms are driven by the SAME rule (take the plan with the longest summary; take the dialog's
own heuristic default) so the comparison is like-for-like. Reported per game:
  * max replicate the human could actually GET  (arm A: the largest k offered at queue time;
    arm B: the largest max_count the resolution dialog offered)
  * dropped_casts -- the authoritative "a declared cast could not be paid" list. Report #3 is
    exactly this: a maximal greedy replicate eats the mana the line reserved for Thrumming
    Hivepool, and the Hivepool is silently dropped.
"""
import subprocess, sys, json, re, os, collections

BIN  = os.environ.get("MTG_BIN", "./build/Release/mtg")
DECK = "decks/slivers_vial/slivers_vial.txt"
PROF = "decks/slivers_vial/slivers_vial.profile.json"
DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")


def step(seed, gi, choices, dim, max_turns):
    cmd = [BIN, DECK, "--profile", PROF, "--claude-play", "--seed", str(seed + gi),
           "--game-index", str(gi), "--max-turns", str(max_turns),
           "--reveal", "6", "--choices", ",".join(map(str, choices))]
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_REPLICATE_DIM=("1" if dim else "0"))
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if RES_RE.search(r.stdout):
        return None
    m = DEC_RE.search(r.stdout)
    return json.loads(m.group(1)) if m else None


def pick(d):
    t = d.get("type")
    if t == "mulligan":
        ac = d.get("ai_choice");  return ac if isinstance(ac, int) else 1
    if t == "bottom":
        ac = d.get("ai_choice")
        if isinstance(ac, dict): return ac.get("index", 0)
        return ac if isinstance(ac, int) else 0
    if t == "main_phase":
        plans = d.get("plans", [])
        if not plans: return -1
        return max(plans, key=lambda p: len(p.get("summary", "")))["index"]
    if "heuristic_default" in d:
        return d["heuristic_default"]
    return 0


def run(seed, gi, dim, max_turns=12):
    choices = []
    best_k, dropped, dialogs = 0, [], 0
    for _ in range(400):
        d = step(seed, gi, choices, dim, max_turns)
        if d is None:
            break
        for nm in (d.get("dropped_casts") or []):
            dropped.append(nm)
        if d.get("type") == "main_phase":
            for p in d.get("plans", []):
                for a in p.get("actions", []):
                    if "replicate_count" in a:
                        best_k = max(best_k, a["replicate_count"])
        if d.get("type") == "replicate":
            dialogs += 1
            best_k = max(best_k, d.get("max_count") or 0)
        c = pick(d)
        if isinstance(c, list): choices.extend(c)
        else:                   choices.append(c)
    return {"max_k": best_k, "dropped": dropped, "dialogs": dialogs}


def main():
    base = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    n    = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    agg = collections.Counter()
    rows = []
    for gi in range(n):
        a = run(base, gi, True)
        b = run(base, gi, False)
        agg["k_up"]   += 1 if a["max_k"] > b["max_k"] else 0
        agg["k_down"] += 1 if a["max_k"] < b["max_k"] else 0
        agg["drop_a"] += len(a["dropped"])
        agg["drop_b"] += len(b["dropped"])
        agg["dialogs_a"] += a["dialogs"]
        agg["dialogs_b"] += b["dialogs"]
        if a["max_k"] != b["max_k"] or a["dropped"] != b["dropped"]:
            rows.append({"gi": gi, "new": a, "old": b})
    print(json.dumps({"games": n, **agg}, indent=1))
    for r in rows:
        print(" ", json.dumps(r))


main()
