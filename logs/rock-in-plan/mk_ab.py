"""usage: mk_ab.py <out manifest> <base seed> [deck=pairs ...]
Held-out paired A/B: every rock deck (rock_decks.json) + controls, one pooled batch, both arms per job
({"MTG_LINE_KEEP_ROCK": true|false}), seed = BASE + off, gi = off, 25 games per job."""
import json, sys
S = '/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/'
out, base = sys.argv[1], int(sys.argv[2])
rock = json.load(open(S + 'rock_decks.json'))
suite = json.load(open(S + 'suite_decks.json'))
sizes = {k: 1000 for k in rock if k != 'snow'}
sizes.update({'stuffy': 5000, 'dragons': 5000, 'sea': 500})
sizes.update({'burn': 500, 'goblins': 500})   # controls: no rock in the list -> byte-identical by construction
for a in sys.argv[3:]:
    k, v = a.split('='); sizes[k] = int(v)
jobs = []
CH = 25
for deck, n in sorted(sizes.items()):
    f, p = suite[deck]
    for off in range(0, n, CH):
        g = min(CH, n - off)
        for arm, on in (('on', True), ('off', False)):
            j = {"name": f"{arm}__{deck}__o{off}", "deck": f, "games": g, "seed": base + off, "game_index": off,
                 "flags": {"MTG_LINE_KEEP_ROCK": on}}
            if p: j["profile"] = p
            jobs.append(j)
# interleave the costliest decks first so the tail is short
json.dump({"jobs": jobs}, open(S + out, 'w'), indent=1)
print(len(jobs), 'jobs', sum(sizes.values()), 'pairs', sizes)
