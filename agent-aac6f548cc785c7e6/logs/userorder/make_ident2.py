"""ident2: the 27-job ident manifest + the rock / ritual / Treasure decks P2/P3 reach (d0 20 + a d3 20 each)."""
import json, os
U = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder'
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
m = json.load(open(U + '/ident_base/manifest.json'))
jobs = m['jobs'] if isinstance(m, dict) else m
for i, n in enumerate(("Dragons", "Giants", "Snow", "WhiteKnights", "Unpredictable Cyclone", "Angels", "Bruna",
                       "KittyEquipment", "StompySurprise", "FiveColour")):
    deck = WT + '/decks/%s/%s.cod' % (n, n)
    prof = WT + '/decks/%s/%s.profile.json' % (n, n)
    for depth, off in ((0, 0), (3, 500)):
        if depth == 3 and n in ("Snow", "FiveColour"):
            continue
        j = {"name": "id2_%s_d%d" % (n.replace(' ', ''), depth), "deck": deck, "profile": prof, "games": 20,
             "seed": 7900001 + 1000 * i + off, "game_index": 0, "depth": depth,
             "budget_ms": 10 if depth else 0, "ignore_play_profile": True}
        jobs.append(j)
os.makedirs(U + '/ident2', exist_ok=True)
json.dump(jobs if not isinstance(m, dict) else dict(m, jobs=jobs), open(U + '/ident2/manifest.json', 'w'), indent=1)
print(len(jobs), 'jobs')
