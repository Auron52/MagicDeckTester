"""usage: mk_repro.py <out manifest> arm=FLAG1,FLAG2 [arm=...]   (arm with no flags: `off=`)
Every flagged game (stuffy_flags_base.txt, dragons_flags_base.txt), one job per game per arm."""
import json, sys
D = '/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/'
decks = {'stuffy': ('Stuffy Doll', 'stuffy_flags_base.txt'), 'dragons': ('Dragons', 'dragons_flags_base.txt')}
arms = [a.split('=') for a in sys.argv[2:]]
jobs = []
for key, (name, fl_file) in decks.items():
    seeds = sorted({int(l.split()[2].split('=')[1]) for l in open(D + fl_file)})
    for arm, flag in arms:
        fl = {f: True for f in flag.split(',') if f}
        for s in seeds:
            jobs.append({"name": f"{arm}__{key}__{s-30000000}", "deck": f"decks/{name}/{name}.cod",
                         "profile": f"decks/{name}/{name}.profile.json", "games": 1, "seed": s,
                         "game_index": s - 30000000, "flags": fl})
json.dump({"jobs": jobs}, open(D + sys.argv[1], 'w'), indent=1)
print(len(jobs))
