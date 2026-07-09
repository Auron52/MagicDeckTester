#!/usr/bin/env python3
"""Ground the value-leaf deliverable across all value-model decks x held-out seeds.
For each deck: heuristic d1 (the bar), value-leaf d1/d3/d5 (min depth to beat d1-heur),
and heuristic d5 vs value-leaf d5 at matched quality + timing (the speedup)."""
import sys, time
sys.path.insert(0, 'scripts')
import eval_ab as e

DECKS = [
    ("burn",     "decks/burn.txt",          "decks/burn.value.json"),
    ("knights",  "decks/Knights.cod",       "decks/Knights.value.json"),
    ("slivers",  "decks/slivers_vial.txt",  "decks/slivers_vial.value.json"),
    ("TH",       "decks/treasure_hunt.txt", "decks/treasure_hunt.value.json"),
    ("antilife", "decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.value.json"),
]
SEEDS = [2002, 3003, 7007]
G = 150

def avg(deck, depth, vs=None):
    xs = [e.run(deck, depth, G, s, 8, 12, None, vs)[3] for s in SEEDS]
    return sum(xs) / len(xs), xs

print("# deck  heur_d1 | value-leaf d1 / d3 / d5 | min-depth-beats-d1-heur")
for name, f, vs in DECKS:
    h1, h1s = avg(f, 1)
    v = {}
    for d in (1, 3, 5):
        v[d], _ = avg(f, d, vs)
    beat = next((d for d in (1, 3, 5) if v[d] <= h1 + 0.005), None)
    print("%-9s h1=%.4f | vl d1=%.4f d3=%.4f d5=%.4f | beats@d%s"
          % (name, h1, v[1], v[3], v[5], beat if beat else ">5"), flush=True)
print("done", flush=True)
