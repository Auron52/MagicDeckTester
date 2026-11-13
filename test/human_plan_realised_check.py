#!/usr/bin/env python3
"""HUMAN PLAN REALISED: every main-phase plan the viewer OFFERS is realised as offered when committed.

Spirits integrator E3 (2026-11-13). Integrator B found, in `--claude-play` Spirits seed 8 T5, a plan the menu
offered -- `land=Godless Shrine; cast: Lingering Souls, Lingering Souls (flashback)` -- whose human commit paid
the hand Souls' generic with the Shrine and stranded the flashback's {B}. The autonomous executor paid the same
plan in full (MTG_LINE_SPARE_GENERIC_FIRST was autonomous-only). A menu entry the commit cannot deliver is a
lie to the human, and nothing else in the suite sees it: viewer_protocol_check replays recorded indices, the
digests are autonomous.

WHAT IT CHECKS. For each seed it walks a Spirits game through the real `--claude-play` protocol (the same
external-chooser commit the viewer uses). At every main-phase decision it commits EVERY offered plan, one at a
time, and reads the next decision's `dropped_casts` -- what the commit actually failed to cast. The invariant:

    dropped_casts (as a multiset) == the plan's own `drops` label

i.e. a plan with no `drops` label casts everything it lists, and a plan that cannot (an ordering whose payment
genuinely strands a later cast) SAYS so on the menu. A plan reading `not_verified` that then drops is a FAIL.

The walk itself follows, at each main phase, the offered plan with the most casts whose commit dropped nothing
(ties: lowest index) so it reaches multi-cast boards; every other decision takes the engine's `ai_choice` (else
the frame's `heuristic_default`, else 0) -- a shock land's land_entry thus pays as the committed line needs.

Usage: python3 test/human_plan_realised_check.py [--seeds 1-12] [--turns 6] [--bin build/Release/mtg]
Exit 0 = every offered plan realised as offered; 1 = a violation (each printed); 2 = harness error.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECK = ROOT / "decks/Spirits/Spirits.cod"


def parse_seeds(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-", 1)
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def run(binary, deck, seed, turns, choices):
    cmd = [str(binary), str(deck), "--claude-play", "--seed", str(seed), "--max-turns", str(turns),
           "--reveal", "0", "--choices", ",".join(map(str, choices))]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT,
                       env=dict(os.environ, MTG_RSS_CAP_GB=os.environ.get("MTG_RSS_CAP_GB", "20")))
    if p.returncode == 0:
        return None, p.stdout          # game over: the result JSON carries the last line's dropped_casts
    if p.returncode != 70 or "<<<CLAUDE_DECISION>>>" not in p.stdout:
        raise RuntimeError(f"seed {seed} choices {choices}: rc={p.returncode} {p.stderr[-400:]}")
    raw = p.stdout.split("<<<CLAUDE_DECISION>>>", 1)[1].split("<<<END_DECISION>>>", 1)[0]
    return json.loads(raw), p.stdout


def decide(binary, deck, seed, turns, choices):
    return run(binary, deck, seed, turns, choices)[0]


def engine_pick(d):
    ac = d.get("ai_choice")
    if ac is None:                # e.g. a shock land's land_entry: the AI's pick for the committed line
        ac = d.get("heuristic_default")
    if isinstance(ac, dict):      # a card-pick frame names its pick as an option object
        ac = ac.get("index", 0)
    return 0 if ac is None else ac


def realised_drops(binary, deck, seed, turns, choices):
    """The casts the commit at the END of `choices` dropped. Only a main_phase decision (and the game's
    result) carries `dropped_casts`, so step the engine's own picks through any combat / trigger / discard
    decision in between -- none of them commits a main-phase line, so the field still holds only ours."""
    choices = list(choices)
    for _ in range(60):
        d, out = run(binary, deck, seed, turns, choices)
        if d is None:
            m = re.search(r'"dropped_casts": (\[[^\]]*\])', out)
            return json.loads(m.group(1)) if m else [], None
        if d.get("type") == "main_phase":
            return d.get("dropped_casts") or [], d.get("dropped_reasons")
        choices.append(engine_pick(d))
    raise RuntimeError(f"seed {seed}: no main phase within 60 decisions of {choices}")


def walk_pick(d, realised_clean):
    """The plan the walk commits: the most casts among the plans whose commit dropped nothing (ties: lowest
    index), so the walk reaches multi-cast boards and never re-commits a dropping line in a loop."""
    plans = [p for p in (d.get("plans") or []) if p["index"] in realised_clean]
    if not plans:
        return -1                     # pass the frame
    return max(plans, key=lambda p: (len(p.get("casts") or []), -p["index"]))["index"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="1-12")
    ap.add_argument("--turns", type=int, default=6)
    ap.add_argument("--bin", default=os.environ.get("MTG_BIN") or str(ROOT / "build/Release/mtg"))
    ap.add_argument("--deck", default=str(DECK))
    args = ap.parse_args()
    binary = Path(args.bin).resolve()
    if not binary.exists():
        print(f"no binary at {binary}", file=sys.stderr)
        return 2
    bad, frames, plans_checked, labelled = [], 0, 0, 0
    for seed in parse_seeds(args.seeds):
        choices = []
        for _ in range(200):
            d = decide(binary, args.deck, seed, args.turns, choices)
            if d is None:
                break
            if d.get("type") == "main_phase" and d.get("plans"):
                frames += 1
                clean = set()
                for pl in d["plans"]:
                    got_l, why = realised_drops(binary, args.deck, seed, args.turns, choices + [pl["index"]])
                    plans_checked += 1
                    want = collections.Counter(pl.get("drops") or [])
                    if want:
                        labelled += 1
                    got = collections.Counter(got_l)
                    if not got:
                        clean.add(pl["index"])
                    if got != want:
                        bad.append((seed, list(choices), d.get("turn"), pl["index"], pl["summary"],
                                    pl.get("declared_order_payability"), dict(want), dict(got), why))
                choices.append(walk_pick(d, clean))
            else:
                choices.append(engine_pick(d))
    for seed, pre, turn, idx, summ, pay, want, got, why in bad:
        print(f"FAIL seed {seed} T{turn} plan {idx} [{pay}] {summ}\n     labelled drops {want}, commit dropped "
              f"{got} {why or ''}\n     --choices {','.join(map(str, pre + [idx]))}")
    print(f"human_plan_realised_check: {frames} main-phase frames, {plans_checked} offered plans committed, "
          f"{labelled} carry a drops label, {len(bad)} realised differently from their label")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
