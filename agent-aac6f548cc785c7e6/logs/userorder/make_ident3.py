"""ident3: the 27-job ident set + d0 jobs for every deck with a pump card (MTG_FB_HOLD_BANKED) + a Dragons d3 job.
Written to the build volume (9p rule)."""
import json, os
U = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder'
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
S = '/workspaces/MagicDeckTester/build/scratch/agent-aac6f548cc785c7e6/ident3'
m = json.load(open(U + '/ident_base/manifest.json'))
jobs = m['jobs'] if isinstance(m, dict) else m
extra = [("Dragons", 0), ("Dragons", 3), ("Giants", 0), ("Knights", 0), ("Minotaur", 0), ("Angels", 0),
         ("Prevent Damage", 0), ("Unpredictable Cyclone", 0), ("Mirrorwing Dragon", 0)]
for i, (n, depth) in enumerate(extra):
    j = {"name": "id3_%s_d%d" % (n.replace(' ', ''), depth), "deck": WT + '/decks/%s/%s.cod' % (n, n),
         "profile": WT + '/decks/%s/%s.profile.json' % (n, n), "games": 20, "seed": 9100001 + 1000 * i,
         "game_index": 0, "depth": depth, "budget_ms": 10 if depth else 0, "ignore_play_profile": True}
    if not os.path.exists(j["profile"]):
        del j["profile"]
    jobs.append(j)
for arm in ("base", "off"):
    os.makedirs(S + '/' + arm, exist_ok=True)
    json.dump(jobs if not isinstance(m, dict) else dict(m, jobs=jobs), open(S + '/' + arm + '/manifest.json', 'w'), indent=1)
print(len(jobs), 'jobs')
