"""usage: mk_d8.py <compare output> <out manifest> <base seed>  -- the slower games of an A/B at --depth 8 --budget-ms 0,
both arms, one job each (ignore_play_profile, as the line-keep-all replays)."""
import json, sys
S = '/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/'
suite = json.load(open(S + 'suite_decks.json'))
base = int(sys.argv[3])
jobs = []
for ln in open(sys.argv[1]):
    p = ln.split()
    if not p or p[0] != 'slower': continue
    deck, gi = p[1], int(p[2])
    f, prof = suite[deck]
    for arm, on in (('on', True), ('off', False)):
        j = {"name": f"{arm}__{deck}__g{gi}", "deck": f, "games": 1, "seed": base + gi, "game_index": gi,
             "depth": 8, "budget_ms": 0, "flags": {"MTG_LINE_KEEP_ROCK": on}, "ignore_play_profile": True}
        if prof: j["profile"] = prof
        jobs.append(j)
json.dump({"jobs": jobs}, open(S + sys.argv[2], 'w'), indent=1)
print(len(jobs))
