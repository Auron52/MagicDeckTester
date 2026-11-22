import json, re, sys
# usage: mkid.py out.json <games per deck> <jumbo games> <gi0>
# Cross-deck identity slice for an exact cut: every suite deck (test/regression_cases.sh DECK_FILE/DECK_PROF) at
# play settings (the profile's value_play block), seed = 30000000+gi, game_index = gi. No per-job flags: the arm is
# the process environment, so the two arms run as two processes over this same manifest.
out, n, nj, gi0 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
src = open('test/regression_cases.sh').read()
def amap(name):
    body = re.search(r'declare -A %s=\((.*?)\n\)' % name, src, re.S).group(1)
    m = {}
    for line in body.splitlines():
        mm = re.match(r'\s*\[(\w+)\]=("([^"]*)"|(\S+))', line)
        if mm: m[mm.group(1)] = mm.group(3) or mm.group(4)
    return m
files, profs = amap('DECK_FILE'), amap('DECK_PROF')
step = 25
jobs = []
for key in sorted(files):
    games = nj if key == 'jumbo' else n
    for g0 in range(gi0, gi0 + games, step):
        jobs.append({'name': f'{key}__g{g0}', 'deck': files[key], 'profile': profs[key],
                     'games': min(step, gi0 + games - g0), 'seed': 30000000 + g0, 'game_index': g0})
json.dump({'jobs': jobs}, open(out, 'w'), indent=1)
print(len(files), len(jobs), sum(j['games'] for j in jobs))
