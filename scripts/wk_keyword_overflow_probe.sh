#!/usr/bin/env bash
# Measures the LIVE impact of the Keyword-mask overflow.
#
#   Card::m_keyword_mask is uint32_t and Bit(k) = 1u << int(k), under a header comment asserting
#   "every enum above has < 32 values" -- but the Keyword enum now has 35. So Bit(Persist)==0x1,
#   bit-identical to Bit(Haste) (proved separately by logs/wk_probe/bitprobe.cpp).
#
#   cards.json tags Kitchen Finks and Murderous Redcap with "Persist", both are in Melira Pod,
#   keywords ARE loaded into the mask (CardDatabase.cpp:604 AddKeyword(KeywordFromString(k))),
#   and Permanent::CanAttack is `!entered_this_turn || card.HasKeyword(Keyword::Haste)`.
#   => those two creatures should be able to attack the turn they land.
#
# Method: A/B the shipped cards.json against one with "Persist" STRIPPED from those two entries
# (no other change), comparing the PER-GAME PLAY DIGEST. Win turn alone is far too coarse for a
# 1-of card -- it misses every play change that does not move the win turn.
#
# NOTE ON SHAPE: --cards-json is process-GLOBAL, so the two arms cannot share one batch. Each arm
# is therefore its own pooled `--batch` run (which is also the only route that writes per-game
# .wins digests -- WriteGameLog is on the batch path only). Two processes here is a genuine data
# dependency, not the forbidden "waves" split.
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=logs/wk_probe; mkdir -p "$OUT"

python3 - <<'PY'
import json
db = json.load(open('src/cards/data/cards.json'))
cards = db['cards'] if isinstance(db, dict) else db
for c in cards:
    if 'Persist' in c.get('keywords', []):
        c['keywords'] = [k for k in c['keywords'] if k != 'Persist']
        print('stripped Persist from', c['name'])
json.dump(db, open('logs/wk_probe/cards.nopersist.json','w'), indent=1)

man = {"jobs": [
    {"name": "melira_d0", "deck": "decks/Melira Pod/Melira Pod.cod",
     "profile": "decks/Melira Pod/Melira Pod.profile.json",
     "games": 1000, "seed": 1001, "depth": 0, "budget_ms": 0},
    {"name": "melira_d3", "deck": "decks/Melira Pod/Melira Pod.cod",
     "profile": "decks/Melira Pod/Melira Pod.profile.json",
     "games": 200, "seed": 1001, "depth": 3, "budget_ms": 10},
]}
json.dump(man, open('logs/wk_probe/manifest.json','w'), indent=1)
PY

arm () {   # arm <label> <cards-json>
  local label="$1" cj="$2"
  rm -rf "$OUT/gl_$label"; mkdir -p "$OUT/gl_$label"
  ./build/Release/mtg --batch "$OUT/manifest.json" --cards-json "$cj" \
      --game-log-dir "$OUT/gl_$label" > "$OUT/$label.out" 2> "$OUT/$label.err"
}

echo "-- arm A: shipped cards.json (Persist tagged -> aliases Haste) --"
arm ship src/cards/data/cards.json
echo "-- arm B: Persist stripped --"
arm nop  "$OUT/cards.nopersist.json"

echo
echo "=== per-game PLAY DIGEST diff (games whose play differs) ==="
for case in melira_d0 melira_d3; do
  a="$OUT/gl_ship/$case.wins"; b="$OUT/gl_nop/$case.wins"
  if [ ! -s "$a" ] || [ ! -s "$b" ]; then echo "$case : MISSING (.wins not written)"; continue; fi
  tot=$(wc -l < "$a")
  dif=$(diff <(sort -n "$a") <(sort -n "$b") | grep -c '^<' || true)
  wt=$(diff <(awk '{print $1,$2}' "$a" | sort -n) <(awk '{print $1,$2}' "$b" | sort -n) | grep -c '^<' || true)
  printf '%-10s : %4s / %4s games differ in PLAY DIGEST ; %s differ in WIN TURN\n' \
         "$case" "$dif" "$tot" "$wt"
done
