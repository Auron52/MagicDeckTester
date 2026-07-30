#!/usr/bin/env python3
"""Wide multi-seed PAIRED A/B: crossover TABLE (default) vs UNIFORM committed-3 (MTG_VALUE_TRUST_OFFSET=3),
fixed binary (fresh-budget OFF). Same seed for both arms => paired: identical play until the take-decision
diverges, so the per-seed delta IS the crossover's real effect on that seed's games (not independent-sample
noise). Averaging per-seed deltas over many seeds tells us whether the crossover is genuinely worse/better/wash.

Usage: xover_wide_ab.py <deck[,deck...]|all> <nseeds> [games_override]
Logs incrementally to logs/eval/xover_wide_ab.log ; prints a per-deck summary (mean delta, sign counts)."""
import subprocess, re, sys, os
from concurrent.futures import ThreadPoolExecutor

BIN = "build_xover/Release/mtg"
DECKS = {
    "antilife": ("decks/Anti-Lifegain/Anti-Lifegain.cod", 5, 250),
    "hinata":   ("decks/Hinata2/Hinata2.cod",             5, 100),
    "burn":     ("decks/burn/burn.txt",                   5, 500),
    "knights":  ("decks/Knights/Knights.cod",             5, 250),
    "th":       ("decks/treasure_hunt/treasure_hunt.txt", 5, 300),
    "slivers":  ("decks/slivers_vial/slivers_vial.txt",   5, 300),
}
# held-out + train seeds (2002/3003 are the regression train seeds; rest are held-out)
SEEDS = [2002, 3003, 4004, 5005, 6006, 7007, 10010, 11011, 12012, 13013,
         14014, 15015, 16016, 17017, 18018, 19019, 20020, 21021, 22022, 23023]
AVG = re.compile(r"Avg win turn : ([0-9.]+)")
LOG = "logs/eval/xover_wide_ab.log"
_wlock = __import__("threading").Lock()

def say(s):
    with _wlock:
        print(s, flush=True)
        open(LOG, "a").write(s + "\n")

def run(deck, seed, uniform, games):
    f, d, _g = DECKS[deck]
    g = games or _g
    env = dict(os.environ)
    if uniform:
        env["MTG_VALUE_TRUST_OFFSET"] = "3"
    else:
        env.pop("MTG_VALUE_TRUST_OFFSET", None)
    out = subprocess.run([BIN, f, "--depth", str(d), "--seed", str(seed), "--games", str(g),
                          "--max-turns", "8", "--budget-ms", "20", "--threads", "1"],
                         capture_output=True, text=True, env=env)
    m = AVG.search(out.stdout)
    return float(m.group(1)) if m else None

def main():
    decks = list(DECKS) if sys.argv[1] == "all" else sys.argv[1].split(",")
    nseeds = int(sys.argv[2]); seeds = SEEDS[:nseeds]
    games = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    open(LOG, "w").write("")
    say(f"=== WIDE crossover A/B (default vs uniform, fresh OFF)  decks={decks} seeds={seeds} games={games or 'default'} ===")
    tasks = [(dk, s, u) for dk in decks for s in seeds for u in (False, True)]
    res = {}
    with ThreadPoolExecutor(max_workers=6) as ex:
        futs = {ex.submit(run, dk, s, u, games): (dk, s, u) for (dk, s, u) in tasks}
        done = 0
        for fut in futs:
            pass
        # collect as completed
        from concurrent.futures import as_completed
        for fut in as_completed(futs):
            dk, s, u = futs[fut]
            res[(dk, s, u)] = fut.result()
            done += 1
            if done % 10 == 0:
                say(f"  ... {done}/{len(tasks)} runs done")
    say("")
    for dk in decks:
        rows = []
        for s in seeds:
            de, un = res.get((dk, s, False)), res.get((dk, s, True))
            if de is None or un is None:
                continue
            rows.append((s, de, un, round(de - un, 5)))
        if not rows:
            say(f"{dk}: no results"); continue
        deltas = [r[3] for r in rows]
        mean_def = sum(r[1] for r in rows) / len(rows)
        mean_uni = sum(r[2] for r in rows) / len(rows)
        mean_d = sum(deltas) / len(deltas)
        worse = sum(1 for x in deltas if x > 1e-9)   # crossover higher avg = worse
        better = sum(1 for x in deltas if x < -1e-9)
        same = sum(1 for x in deltas if abs(x) <= 1e-9)
        say(f"### {dk}: mean default(xover)={mean_def:.5f}  uniform={mean_uni:.5f}  mean delta={mean_d:+.5f}  "
            f"(worse:{worse} better:{better} same:{same} of {len(rows)} seeds)")
        for s, de, un, dl in rows:
            flag = "" if abs(dl) <= 1e-9 else ("  <<< WORSE" if dl > 0 else "  <<< better")
            say(f"    s{s}: xover={de:.5f}  uniform={un:.5f}  delta={dl:+.5f}{flag}")
    say("=== DONE ===")

if __name__ == "__main__":
    main()
