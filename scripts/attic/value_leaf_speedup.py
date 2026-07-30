#!/usr/bin/env python3
"""Matched-quality speedup: value-leaf d5 vs heuristic d5 (same depth) — quality parity + wall-clock.
This is the adoption payoff: identical search, O(1) leaf vs greedy-playout leaf."""
import sys, time, subprocess, os
sys.path.insert(0, 'scripts')
import eval_ab as e

DECKS = [
    ("burn",     "decks/burn.txt",          "decks/burn.value.json"),
    ("knights",  "decks/Knights.cod",       "decks/Knights.value.json"),
    ("slivers",  "decks/slivers_vial.txt",  "decks/slivers_vial.value.json"),
    ("TH",       "decks/treasure_hunt.txt", "decks/treasure_hunt.value.json"),
    ("antilife", "decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json"),
]
SEED, G, D = 2002, 100, 5

def timed(deck, vs=None):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE"):
        env.pop(k, None)
    if vs:
        env["MTG_VALUE_MODEL"] = "1"; env["MTG_VALUE_PROFILE"] = vs
    t0 = time.time()
    subprocess.run(["build/Release/mtg", deck, "--games", str(G), "--seed", str(SEED),
                    "--depth", str(D), "--max-turns", "8", "--threads", "12"],
                   capture_output=True, env=env)
    return time.time() - t0

print("# deck | heur d5 LP / value-leaf d5 LP (150g) | wall heur / value (100g) | speedup")
for name, f, vs in DECKS:
    qh = e.run(f, D, 150, SEED, 8, 12, None)[3]
    qv = e.run(f, D, 150, SEED, 8, 12, None, vs)[3]
    th = timed(f); tv = timed(f, vs)
    print("%-9s q h=%.4f v=%.4f (d=%+.4f) | wall %.2fs / %.2fs | %.1fx"
          % (name, qh, qv, qv-qh, th, tv, th/tv if tv else 0), flush=True)
print("done", flush=True)
