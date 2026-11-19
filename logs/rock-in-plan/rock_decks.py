"""List the regression-suite decks (test/regression_cases.sh DECK_FILE/DECK_PROF) whose list holds a mana rock that
enters untapped (the cards MTG_LINE_KEEP_ROCK binds on). Writes build/scratch/claude_rock_plan/rock_decks.json and
suite_decks.json (every suite deck)."""
import json, re
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7/'
cards = {c['name']: c for c in json.load(open(W + 'src/cards/data/cards.json'))['cards']}
def rock(name):
    c = cards.get(name)
    if not c: return False
    p = c.get('parameters', {})
    return bool(p.get('mana_rock')) and 'Creature' not in c.get('types', []) and not p.get('enters_tapped')
txt = open(W + 'test/regression_cases.sh').read()
def amap(var):
    m = re.search(r'declare -A ' + var + r'=\((.*?)\n\)', txt, re.S)
    out = {}
    for k, v in re.findall(r'\[(\w+)\]=("[^"]*"|\S+)', m.group(1)):
        out[k] = v.strip('"')
    return out
files, profs = amap('DECK_FILE'), amap('DECK_PROF')
def names(path):
    t = open(W + path, errors='replace').read()
    if path.endswith('.cod'):
        return re.findall(r'name="([^"]+)"', t)
    return [m.group(2) for m in (re.match(r'\s*(\d+)\s+(.+?)\s*$', ln) for ln in t.splitlines()) if m]
suite, res = {}, {}
for key in sorted(files):
    suite[key] = (files[key], profs.get(key))
    rs = sorted({n for n in names(files[key]) if rock(n)})
    if rs: res[key] = (files[key], profs.get(key), rs)
for k, v in res.items(): print(k, v[2])
print(len(suite), 'suite decks;', len(res), 'with rocks')
json.dump(res, open('/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/rock_decks.json', 'w'), indent=1)
json.dump(suite, open('/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/suite_decks.json', 'w'), indent=1)
