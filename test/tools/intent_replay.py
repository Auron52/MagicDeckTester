#!/usr/bin/env python3
"""Replay a viewer game by INTENT when its reference JSON is gone (or was never saved): run
--claude-play step by step with the viewer's own arguments, answering each decision from an ordered
list of matchers -- a regex on the plan summary / option name, 'keep', 'mull', 'pass', or a literal
int (bottom index, colour ordinal, lifegain_counters count...). Prints every decision it answers;
stops and dumps the menu when a matcher does not match, so the list can be extended one step at a
time from the user's viewer log.

  python3 test/tools/intent_replay.py decks/SelesnyaLifegain/SelesnyaLifegain.cod 9 \
      mull mull keep 'Accomplished Alchemist' "Verdant Sun's Avatar" \
      '^land=Brushland; cast: Elvish Mystic' pass pass ...

Env: MTG_BIN (binary), DRIVE_LOGDIR (--log-dir for the final replay), DRIVE_MAXTURNS (default 8),
DRIVE_STDERR=1 (print the final replay's stderr -- combine with MTG_TAPDBG=1 to read the payment
trace; MTG_NEEDS_TAP_ORDER=1 adds the per-candidate [nddbg] ranks and the reserved-source line).
Built for the 2026-10-05 viewer batch (docs/design/viewer-mana-batch-2026-10-05.md)."""
import json, os, re, subprocess, sys
def run(args, choices):
    p = subprocess.run(args + ['--choices', ','.join(map(str, choices))], capture_output=True, text=True)
    out = p.stdout
    if '<<<CLAUDE_RESULT>>>' in out:
        return 'result', json.loads(out.split('<<<CLAUDE_RESULT>>>')[1].split('<<<END_RESULT>>>')[0]), p
    if '<<<CLAUDE_DECISION>>>' in out:
        return 'decision', json.loads(out.split('<<<CLAUDE_DECISION>>>')[1].split('<<<END_DECISION>>>')[0]), p
    return 'error', {'rc': p.returncode, 'stdout': out[-2000:], 'stderr': p.stderr[-2000:]}, p
def options_of(d):
    if d['type'] == 'main_phase': return [(pl['index'], pl['summary']) for pl in d.get('plans', [])]
    if d['type'] == 'bottom': return [(i, c.get('name', c) if isinstance(c, dict) else c) for i, c in enumerate(d.get('hand', []))]
    opts = d.get('options') or d.get('candidates') or []
    return [(o.get('index', i), o.get('name') or o.get('label') or o.get('summary') or json.dumps(o)) if isinstance(o, dict) else (i, str(o)) for i, o in enumerate(opts)]
def main():
    deck, seed, matchers = sys.argv[1], sys.argv[2], sys.argv[3:]
    stem = os.path.basename(deck).rsplit('.', 1)[0]
    prof = os.path.join(os.path.dirname(deck), stem + '.profile.json')
    logdir = os.environ.get('DRIVE_LOGDIR')
    args = [os.environ.get('MTG_BIN', 'build/Release/mtg'), deck, '--profile', prof, '--cards-json', 'src/cards/data/cards.json',
            '--claude-play', '--seed', seed, '--game-index', '0', '--max-turns', os.environ.get('DRIVE_MAXTURNS', '8'), '--depth', '0']
    if logdir: args += ['--log-dir', logdir]
    choices = []
    def auto_nothing(d):
        # The viewer answers a main phase whose ONLY option is "cast: (nothing)" by itself and records
        # nothing, so a user's log / a reference never lists it. Mirror that: pass without consuming a
        # matcher (it still rides the positional --choices stream).
        if d['type'] != 'main_phase': return False
        pl = d.get('plans', [])
        return len(pl) == 1 and '(nothing)' in pl[0].get('summary', '')
    for m in matchers:
        while True:
            kind, d, p = run(args, choices)
            if kind == 'decision' and auto_nothing(d):
                print(f'T{d.get("turn")} main_phase   ->  -1  (auto: only "(nothing)" offered)')
                choices.append(-1); continue
            break
        if kind != 'decision':
            print('STOP:', kind, json.dumps(d)[:1500])
            if os.environ.get('DRIVE_STDERR'): print(p.stderr[-6000:])
            return
        t = d['type']; opts = options_of(d)
        if m == 'keep' and t == 'mulligan': ans = 1
        elif m == 'mull' and t == 'mulligan': ans = 0
        elif m == 'pass' and t == 'main_phase': ans = -1
        elif re.fullmatch(r'-?\d+', m): ans = int(m)
        else:
            hits = [i for i, s in opts if re.search(m, s)]
            if not hits:
                print(f'NO MATCH for {m!r} at T{d.get("turn")} type={t} (step {len(choices)}). Menu:')
                for i, s in opts: print(f'   [{i}] {s[:160]}')
                print('note:', str(d.get('note', ''))[:300]); return
            ans = hits[0]
        label = dict(opts).get(ans, '') if t != 'mulligan' else ('keep' if ans else 'mulligan')
        print(f'T{d.get("turn")} {t:<12} -> {ans:>3}  {str(label)[:120]}')
        choices.append(ans)
    while True:
        kind, d, p = run(args, choices)
        if kind == 'decision' and auto_nothing(d):
            print(f'T{d.get("turn")} main_phase   ->  -1  (auto: only "(nothing)" offered)')
            choices.append(-1); continue
        break
    print('NEXT:', kind, (f"T{d.get('turn')} {d.get('type')}" if kind == 'decision' else json.dumps(d)[:600]))
    if kind == 'decision':
        for i, s in options_of(d): print(f'   [{i}] {s[:160]}')
    print('CHOICES:', ','.join(map(str, choices)))
    if os.environ.get('DRIVE_STDERR'): print(p.stderr[-6000:])
main()
