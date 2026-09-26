#!/usr/bin/env bash
# A/B the analyze_deck.py "unbracketed triggered ability" gap check across EVERY shipped deck,
# old (4 hand-written param names) vs new (naming-convention match).
#
# The new version is strictly MORE permissive, so the only risk is that it silences a gap that was
# a real finding. This prints, per deck, every gap that the old code reported and the new code does
# not -- each one has to be inspected by hand, because "fewer findings" is exactly what a broken
# check looks like too.
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=logs/wk_covab; mkdir -p "$OUT"

# MUST live in scripts/ -- analyze_deck.py resolves cards.json RELATIVE TO ITS OWN PATH, so a copy
# run from logs/ silently reports "coverage": [] for every deck. The first version of this harness
# did exactly that and its "no gaps silenced" verdict was vacuous.
OLD=scripts/.wk_analyze_deck_old.py
trap 'rm -f "$OLD"' EXIT
git show HEAD:scripts/analyze_deck.py > "$OLD"

for d in decks/*/; do
  name=$(basename "$d")
  # `|| true`: ls returns nonzero when EITHER glob misses, even if the other matched, and under
  # `set -e` that killed the whole run on the first deck before printing anything.
  f=$( { ls "$d"*.cod "$d"*.txt 2>/dev/null || true; } | head -1)
  [ -n "$f" ] || { echo "NO DECKLIST: $name"; continue; }
  python3 "$OLD"                  "$f" --coverage-only > "$OUT/$name.old.json" 2>"$OUT/$name.old.err" \
    || echo "OLD FAILED: $name  ($(tail -1 "$OUT/$name.old.err"))"
  python3 scripts/analyze_deck.py "$f" --coverage-only > "$OUT/$name.new.json" 2>"$OUT/$name.new.err" \
    || echo "NEW FAILED: $name  ($(tail -1 "$OUT/$name.new.err"))"
done

python3 - "$OUT" <<'PY'
import json, sys, glob, os
out = sys.argv[1]
def gaps(path):
    try: d = json.load(open(path))
    except Exception: return None
    return {c['card']: c.get('gaps', []) for c in d.get('coverage', [])}
silenced = {}
added    = {}
# LIVENESS GUARD: an arm that reports zero cards is a broken arm, and "no gaps silenced" from two
# empty arms is the digest-equality-can-mean-BROKEN trap. Refuse to render a verdict without it.
empty = []
for old_path in sorted(glob.glob(f'{out}/*.old.json')):
    deck = os.path.basename(old_path)[:-len('.old.json')]
    o, n = gaps(old_path), gaps(f'{out}/{deck}.new.json')
    if o is None or n is None:
        print(f'  !! {deck}: could not parse both arms'); continue
    if not o or not n:
        empty.append(f'{deck} (old={len(o)} cards, new={len(n)} cards)')
    for card, og in o.items():
        ng = n.get(card, [])
        for g in og:
            if g not in ng: silenced.setdefault(g, set()).add(f'{deck}/{card}')
        for g in ng:
            if g not in og: added.setdefault(g, set()).add(f'{deck}/{card}')
print(f'decks compared: {len(glob.glob(f"{out}/*.old.json"))}')
if empty:
    print()
    print('!!! DEAD ARM -- these decks reported no cards at all, so their comparison proves nothing:')
    for e in empty: print(f'      {e}')
    sys.exit(1)
print()
print('=== gaps the NEW check no longer reports (each needs a human ruling) ===')
if not silenced: print('  (none)')
for g, where in sorted(silenced.items()):
    print(f'  {g}')
    for w in sorted(where): print(f'      {w}')
print()
print('=== gaps only the NEW check reports ===')
if not added: print('  (none)')
for g, where in sorted(added.items()):
    print(f'  {g}')
    for w in sorted(where): print(f'      {w}')
PY
