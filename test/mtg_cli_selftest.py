#!/usr/bin/env python3
"""CLI self-test for two silent-failure traps in `mtg`'s argument handling (2026-10-18).

1. A MISSING CARD DATABASE MUST FAIL. The default `--cards-json` is CWD-relative; launched from any
   other directory, `mtg` used to skip the load silently, play a deck of empty card definitions, and
   print "all games unwon, avg 9.0" as a result. Both the single-run and the --batch paths must now
   exit non-zero naming the path.
2. AN UNCONSUMED ARGUMENT IS REPORTED. The single-run parser ignores flags it does not know
   (`--game-log-dir` is batch-only); it must say so on stderr rather than look accepted -- and the
   run itself must be unchanged.

    python3 test/mtg_cli_selftest.py [path/to/mtg]     # default build/Release/mtg; exit 0 = pass
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/Release/mtg"))
DECK = os.path.join(ROOT, "decks/AccomplishedAlchemist/AccomplishedAlchemist.cod")
fails = []


def run(args, cwd):
    return subprocess.run([BIN] + args, cwd=cwd, capture_output=True, text=True)


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  -- " + detail))
    if not ok:
        fails.append(name)


with tempfile.TemporaryDirectory() as elsewhere:
    # 1a. single run from a directory with no src/cards/data/cards.json
    r = run([DECK, "--games", "1", "--seed", "1", "--depth", "0"], elsewhere)
    check("single run without cards.json exits non-zero", r.returncode != 0, f"rc={r.returncode}")
    check("single run names the missing database", "card database not found" in r.stderr, r.stderr[-300:])
    check("single run plays no games", "Games played" not in r.stdout, r.stdout[-300:])

    # 1b. the pooled batch path, same directory
    man = os.path.join(elsewhere, "m.json")
    json.dump({"jobs": [{"name": "x", "deck": DECK, "games": 1, "seed": 1, "depth": 0}]}, open(man, "w"))
    r = run(["--batch", man, "--threads", "1"], elsewhere)
    check("batch without cards.json exits non-zero", r.returncode != 0, f"rc={r.returncode}")
    check("batch names the missing database", "card database not found" in r.stderr, r.stderr[-300:])

# 1c. an explicit --cards-json makes the other-directory run work again
with tempfile.TemporaryDirectory() as elsewhere:
    r = run([DECK, "--games", "1", "--seed", "1", "--depth", "0", "--cards-json",
             os.path.join(ROOT, "src/cards/data/cards.json")], elsewhere)
    check("explicit --cards-json from another directory runs", r.returncode == 0 and "Games played" in r.stdout,
          f"rc={r.returncode} {r.stderr[-300:]}")

# 2. an unknown flag is reported, and the result is unchanged
base = run([DECK, "--games", "2", "--seed", "9100", "--depth", "0", "--threads", "1"], ROOT)
with tempfile.TemporaryDirectory() as d:
    flag = run([DECK, "--games", "2", "--seed", "9100", "--depth", "0", "--threads", "1",
                "--game-log-dir", d], ROOT)
avg = lambda out: [l for l in out.splitlines() if l.startswith("avg")]
check("baseline run succeeds", base.returncode == 0, base.stderr[-300:])
check("unknown flag is warned about", "'--game-log-dir' is not recognised" in flag.stderr, flag.stderr[-300:])
check("unknown flag does not change the result", avg(base.stdout) == avg(flag.stdout) != [],
      f"{avg(base.stdout)} vs {avg(flag.stdout)}")
check("no warning without an unknown flag", "[cli] WARNING" not in base.stderr, base.stderr[-300:])

print(f"\n{len(fails)} failure(s)")
sys.exit(1 if fails else 0)
