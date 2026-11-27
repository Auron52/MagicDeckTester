#!/usr/bin/env python3
"""Probe for docs/design/control-scoring-mode.md section 8: control-effect injection into suite goldfishers.

  make  <out_manifest.json> [--games N] [--seed S]   write ONE pooled --batch manifest (every deck x arm)
  report <game_log_dir> <batch_stderr>                per (deck, arm) paired delay vs the base arm

Every arm of a deck plays the SAME seeds (paired), at the cheap suite shape d3 b10 with max_turns 12 so a
delayed kill is not truncated at the goldfish horizon (unwon = 13). The `ctrl_inject` job field and its
grammar are specified in docs/design/control-inject-harness.md (src/core/ControlInject.h; inert without it).
The counter arms use the stage-1 grammar: `counter:T:mv>=M` (the probe's "first spell of mana value >= M").
"""
import json
import math
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DECKS = {
    "burn": ("decks/Burn/Burn.txt", "decks/Burn/Burn.profile.json"),
    "whiteknights": ("decks/White Knights/White Knights.cod", "decks/White Knights/White Knights.profile.json"),
    "stompy": ("decks/Stompy Surprise/Stompy Surprise.cod", "decks/Stompy Surprise/Stompy Surprise.profile.json"),
    "dragons": ("decks/Dragons/Dragons.cod", "decks/Dragons/Dragons.profile.json"),
    "breaching": ("decks/Breaching Dragonstorm/Breaching Dragonstorm.cod",
                  "decks/Breaching Dragonstorm/Breaching Dragonstorm.profile.json"),
}

ARMS = {
    "base": "",
    "skip3": "skip:3",
    "land2": "land:2:0", "land3": "land:3:0", "land4": "land:4:0",
    "land3x2": "land:3:0;land:4:0", "land3x3": "land:3:0;land:4:0;land:5:0",
    "crt2": "bigcrt:2", "crt3": "bigcrt:3", "crt4": "bigcrt:4",
    "crt3x2": "bigcrt:3;bigcrt:4", "crt3x3": "bigcrt:3;bigcrt:4;bigcrt:5",
    "ctr2": "counter:2:mv>=1", "ctr3": "counter:3:mv>=1", "ctr4": "counter:4:mv>=1",
    "ctr3x2": "counter:3:mv>=1;counter:4:mv>=1", "ctr3x3": "counter:3:mv>=1;counter:4:mv>=1;counter:5:mv>=1",
    "ctr3same2": "counter:3:mv>=1;counter:3:mv>=1",
    "ctr3mv3": "counter:3:mv>=3",
    "disc2": "bigdisc:2", "disc3": "bigdisc:3", "disc4": "bigdisc:4",
    "disc2x2": "bigdisc:2;bigdisc:3", "disc2x3": "bigdisc:2;bigdisc:3;bigdisc:4",
    "sweep3": "sweep:3", "sweep4": "sweep:4",
}

MAX_TURNS = 12


def make(out, games, seed):
    jobs = []
    for deck, (dfile, prof) in DECKS.items():
        for arm, spec in ARMS.items():
            j = {"name": f"{deck}__{arm}", "deck": dfile, "profile": prof, "games": games, "seed": seed,
                 "game_index": 0, "depth": 3, "budget_ms": 10, "ignore_play_profile": True,
                 "max_turns": MAX_TURNS}
            if spec:
                j["ctrl_inject"] = spec
            jobs.append(j)
    with open(out, "w") as f:
        json.dump({"jobs": jobs}, f, indent=1)
    print(f"{len(jobs)} jobs x {games} games -> {out}")


def read_wins(path):
    out = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                t = int(p[1])
                out[int(p[0])] = MAX_TURNS + 1 if t < 0 else t
    return out


def report(logdir, stderr_path):
    applied = defaultdict(lambda: [0, 0])   # (job) -> [effects applied, effects attempted]
    if stderr_path and os.path.exists(stderr_path):
        with open(stderr_path, errors="replace") as f:
            for line in f:
                if not line.startswith("[ctrl-inject] "):
                    continue
                rec = json.loads(line[len("[ctrl-inject] "):])
                if rec.get("phase") in ("end", "return"):
                    continue
                a = applied[rec["job"]]
                a[1] += 1
                a[0] += 1 if rec.get("applied", 0) > 0 else 0
    rows = []
    for deck in DECKS:
        bp = os.path.join(logdir, f"{deck}__base.wins")
        if not os.path.exists(bp):
            continue
        base = read_wins(bp)
        bmean = sum(base.values()) / len(base)
        rows.append((deck, "base", len(base), bmean, 0.0, 0.0, ""))
        for arm in ARMS:
            if arm == "base":
                continue
            p = os.path.join(logdir, f"{deck}__{arm}.wins")
            if not os.path.exists(p):
                continue
            w = read_wins(p)
            ds = [w[g] - base[g] for g in w if g in base]
            n = len(ds)
            m = sum(ds) / n
            sd = math.sqrt(sum((d - m) ** 2 for d in ds) / max(1, n - 1))
            a = applied.get(f"{deck}__{arm}", [0, 0])
            rows.append((deck, arm, n, sum(w.values()) / len(w), m, sd / math.sqrt(n),
                         f"{a[0]}/{a[1]}"))
    print(f"{'deck':13s} {'arm':10s} {'n':>4s} {'avg':>6s} {'delay':>7s} {'se':>5s} applied/tried")
    for r in rows:
        print(f"{r[0]:13s} {r[1]:10s} {r[2]:4d} {r[3]:6.3f} {r[4]:+7.3f} {r[5]:5.3f} {r[6]}")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "make":
        a = sys.argv[3:]
        g = int(a[a.index("--games") + 1]) if "--games" in a else 200
        s = int(a[a.index("--seed") + 1]) if "--seed" in a else 7900001
        make(sys.argv[2], g, s)
    elif len(sys.argv) >= 3 and sys.argv[1] == "report":
        report(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "")
    else:
        print(__doc__)
        sys.exit(2)
