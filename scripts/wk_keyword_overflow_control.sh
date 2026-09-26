#!/usr/bin/env bash
# CONTROL for wk_keyword_overflow_probe.sh.
#
# That probe found ZERO digest differences between shipped cards.json (Kitchen Finks / Murderous
# Redcap tagged "Persist", which aliases onto Haste's bit) and a variant with Persist stripped.
# Equality can mean BROKEN -- maybe --cards-json is not honoured on the batch path, in which case
# both arms ran the same data and the result is meaningless.
#
# Decisive control: a THIRD arm that tags those two cards with an EXPLICIT "Haste".
#   * If Persist really aliases Haste:  arm A (Persist) == arm C (explicit Haste).
#   * If haste changes this deck's play: arm B (neither) != arm C.
#   * If B == C too, then haste on those two 1-ofs genuinely does not move Melira Pod's play,
#     and the zero in the main probe is a real result rather than dead plumbing.
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=logs/wk_probe; mkdir -p "$OUT"

python3 - <<'PY'
import json
TARGETS = {'Kitchen Finks', 'Murderous Redcap'}

def variant(path, kws):
    db = json.load(open('src/cards/data/cards.json'))
    cards = db['cards'] if isinstance(db, dict) else db
    for c in cards:
        if c['name'] in TARGETS:
            c['keywords'] = list(kws)
    json.dump(db, open(path, 'w'), indent=1)

variant('logs/wk_probe/cards.haste.json', ['Haste'])      # arm C
# sanity: a deliberately play-changing edit, to prove --cards-json is honoured at all
db = json.load(open('src/cards/data/cards.json'))
cards = db['cards'] if isinstance(db, dict) else db
for c in cards:
    if c['name'] == 'Ignoble Hierarch':      # a mana dork Melira Pod actually casts early
        c['keywords'] = []                   # strip its keywords outright
json.dump(db, open('logs/wk_probe/cards.sanity.json', 'w'), indent=1)
print('wrote arm C (explicit Haste) + sanity arm')
PY

arm () {   # arm <label> <cards-json>
  local label="$1" cj="$2"
  rm -rf "$OUT/gl_$label"; mkdir -p "$OUT/gl_$label"
  ./build/Release/mtg --batch "$OUT/manifest.json" --cards-json "$cj" \
      --game-log-dir "$OUT/gl_$label" > "$OUT/$label.out" 2> "$OUT/$label.err"
}

arm haste  "$OUT/cards.haste.json"
arm sanity "$OUT/cards.sanity.json"

cmp2 () {  # cmp2 <case> <labelA> <labelB>
  local case="$1" A="$2" B="$3"
  local a="$OUT/gl_$A/$case.wins" b="$OUT/gl_$B/$case.wins"
  [ -s "$a" ] && [ -s "$b" ] || { echo "  $case $A vs $B : MISSING"; return; }
  local tot dif
  tot=$(wc -l < "$a"); dif=$(diff <(sort -n "$a") <(sort -n "$b") | grep -c '^<' || true)
  printf '  %-10s %-6s vs %-6s : %4s / %4s games differ\n' "$case" "$A" "$B" "$dif" "$tot"
}

echo
echo "=== A(Persist) vs C(explicit Haste) -- should be IDENTICAL if the alias is real ==="
for c in melira_d0 melira_d3; do cmp2 "$c" ship haste; done
echo "=== B(neither) vs C(explicit Haste) -- does haste move this deck at all? ==="
for c in melira_d0 melira_d3; do cmp2 "$c" nop haste; done
echo "=== SANITY: B(neither) vs sanity(Hierarch keywords stripped) -- proves --cards-json is live ==="
for c in melira_d0 melira_d3; do cmp2 "$c" nop sanity; done
